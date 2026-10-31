/*
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by IBSurgeon
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2026 IBSurgeon
 *  and all contributors signed below.
 *
 *  All Rights Reserved.  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../hqisql/HqParquet.h"
#include "../hqisql/HqCodec.h"

#include <math.h>
#include <string.h>
#include <algorithm>

namespace
{

using Hq::Rows::Column;
using Hq::Rows::Field;
using Hq::Rows::Record;

// parquet.thrift
enum PhysicalType
{
	PT_BOOLEAN,
	PT_INT32,
	PT_INT64,
	PT_INT96,
	PT_FLOAT,
	PT_DOUBLE,
	PT_BYTE_ARRAY,
	PT_FIXED_LEN_BYTE_ARRAY
};

enum Encoding
{
	PLAIN = 0,
	PLAIN_DICTIONARY = 2,
	RLE = 3,
	BIT_PACKED = 4,
	DELTA_BINARY_PACKED = 5,
	DELTA_LENGTH_BYTE_ARRAY = 6,
	DELTA_BYTE_ARRAY = 7,
	RLE_DICTIONARY = 8,
	BYTE_STREAM_SPLIT = 9
};

enum CompressionCodec
{
	UNCOMPRESSED,
	SNAPPY,
	GZIP,
	LZO,
	BROTLI,
	LZ4,
	ZSTD,
	LZ4_RAW
};

enum PageType
{
	DATA_PAGE,
	INDEX_PAGE,
	DICTIONARY_PAGE,
	DATA_PAGE_V2
};

enum Repetition
{
	REQUIRED,
	OPTIONAL,
	REPEATED
};

// The converted types (the legacy logical types).
enum ConvertedType
{
	CT_UTF8 = 0,
	CT_ENUM = 4,
	CT_DECIMAL = 5,
	CT_DATE = 6,
	CT_TIME_MILLIS = 7,
	CT_TIME_MICROS = 8,
	CT_TIMESTAMP_MILLIS = 9,
	CT_TIMESTAMP_MICROS = 10,
	CT_UINT_8 = 11,
	CT_UINT_64 = 14,
	CT_INT_8 = 15,
	CT_INT_64 = 18,
	CT_JSON = 19,
	CT_BSON = 20,
	CT_INTERVAL = 21
};

const char* const TYPE_NAMES[] = {"BOOLEAN", "INT32", "INT64", "INT96", "FLOAT", "DOUBLE", "BYTE_ARRAY",
	"FIXED_LEN_BYTE_ARRAY"};
const char* const CODEC_NAMES[] = {"UNCOMPRESSED", "SNAPPY", "GZIP", "LZO", "BROTLI", "LZ4", "ZSTD", "LZ4_RAW"};

const unsigned MAX_DEPTH = 64;
const int64_t JULIAN_1970 = 2440588;		// the Julian day of 1970-01-01 (INT96)

uint32_t le32(const unsigned char* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}

uint64_t le64(const unsigned char* p)
{
	return le32(p) | (uint64_t(le32(p + 4)) << 32);
}


// A value of the Thrift compact protocol, the encoding of the footer and
// the page headers.
struct TValue
{
	enum Kind
	{
		NONE,
		BOOL,
		INT,
		PT_DOUBLE,
		BINARY,
		LIST,
		STRUCT,
		MAP
	};

	Kind kind = NONE;
	int64_t i = 0;
	double d = 0;
	std::string s;
	std::vector<TValue> items;						// LIST; MAP: key, value, key, ...
	std::vector<std::pair<int, TValue>> fields;	// STRUCT

	const TValue* field(int id) const
	{
		for (const auto& f : fields)
		{
			if (f.first == id)
				return &f.second;
		}

		return nullptr;
	}

	int64_t getInt(int id, int64_t defaultValue) const
	{
		const TValue* f = field(id);
		return f && (f->kind == INT || f->kind == BOOL) ? f->i : defaultValue;
	}

	std::string getString(int id) const
	{
		const TValue* f = field(id);
		return f && f->kind == BINARY ? f->s : std::string();
	}
};

class Thrift
{
public:
	Thrift(const char* data, size_t size)
		: start((const unsigned char*) data), p(start), end(start + size)
	{
	}

	bool readStruct(TValue& v, unsigned depth = 0)
	{
		v.kind = TValue::STRUCT;
		int last = 0;

		if (depth > MAX_DEPTH)
			return false;

		for (;;)
		{
			if (p >= end)
				return outOfData();

			const unsigned char b = *p++;

			if (b == 0)
				return true;	// STOP

			const int type = b & 15;
			int id;

			if (b >> 4)
				id = last + (b >> 4);
			else
			{
				int64_t z;

				if (!zigzag(z))
					return false;

				id = int(z);
			}

			last = id;
			v.fields.emplace_back(id, TValue());
			TValue& f = v.fields.back().second;

			if (type == 1 || type == 2)
			{
				f.kind = TValue::BOOL;
				f.i = type == 1;
			}
			else if (!readValue(type, f, depth + 1))
				return false;
		}
	}

	size_t consumed() const
	{
		return p - start;
	}

	bool truncated = false;		// the data ends inside the value

private:
	bool outOfData()
	{
		truncated = true;
		return false;
	}

	bool varint(uint64_t& v)
	{
		v = 0;

		for (int shift = 0; shift < 64; shift += 7)
		{
			if (p >= end)
				return outOfData();

			const unsigned char b = *p++;
			v |= uint64_t(b & 0x7F) << shift;

			if (!(b & 0x80))
				return true;
		}

		return false;
	}

	bool zigzag(int64_t& v)
	{
		uint64_t u;

		if (!varint(u))
			return false;

		v = int64_t(u >> 1) ^ -int64_t(u & 1);
		return true;
	}

	bool readValue(int type, TValue& v, unsigned depth)
	{
		if (depth > MAX_DEPTH)
			return false;

		switch (type)
		{
			case 1:		// a boolean in a list: a byte
			case 2:
				if (p >= end)
					return outOfData();

				v.kind = TValue::BOOL;
				v.i = *p++ == 1;
				return true;

			case 3:		// i8
				if (p >= end)
					return outOfData();

				v.kind = TValue::INT;
				v.i = (signed char) *p++;
				return true;

			case 4:		// i16, i32, i64
			case 5:
			case 6:
				v.kind = TValue::INT;
				return zigzag(v.i);

			case 7:		// double
				if (end - p < 8)
					return outOfData();

				v.kind = TValue::PT_DOUBLE;
				memcpy(&v.d, p, 8);
				p += 8;
				return true;

			case 8:		// binary
			{
				uint64_t n;

				if (!varint(n))
					return false;

				if (uint64_t(end - p) < n)
					return outOfData();

				v.kind = TValue::BINARY;
				v.s.assign((const char*) p, size_t(n));
				p += n;
				return true;
			}

			case 9:		// list, set
			case 10:
			{
				if (p >= end)
					return outOfData();

				const unsigned char h = *p++;
				uint64_t n = h >> 4;
				const int itemType = h & 15;

				if (n == 15 && !varint(n))
					return false;

				// Every item takes a byte at least.
				if (n > uint64_t(end - p))
					return outOfData();

				v.kind = TValue::LIST;
				v.items.resize(size_t(n));

				for (TValue& item : v.items)
				{
					if (!readValue(itemType, item, depth + 1))
						return false;
				}

				return true;
			}

			case 11:	// map
			{
				uint64_t n;

				if (!varint(n))
					return false;

				v.kind = TValue::MAP;

				if (n == 0)
					return true;

				if (p >= end)
					return outOfData();

				const unsigned char kv = *p++;

				if (n > uint64_t(end - p))
					return outOfData();

				v.items.resize(size_t(n) * 2);

				for (size_t i = 0; i < v.items.size(); ++i)
				{
					if (!readValue(i % 2 ? kv & 15 : kv >> 4, v.items[i], depth + 1))
						return false;
				}

				return true;
			}

			case 12:	// struct
				return readStruct(v, depth + 1);

			default:
				return false;
		}
	}

	const unsigned char* const start;
	const unsigned char* p;
	const unsigned char* const end;
};


// A leaf column of the schema.
struct Leaf
{
	enum Logical
	{
		NONE,
		STRING,		// STRING, ENUM, JSON: text
		BSON,
		DECIMAL,
		DATE,
		TIME,
		TIMESTAMP,
		INTEGER,
		UUID,
		FLOAT16,
		OTHER
	};

	std::string name;
	int type = -1;
	int typeLength = 0;
	int maxDef = 0, maxRep = 0;
	Logical logical = NONE;
	int scale = 0;
	int unit = 0;			// TIME, TIMESTAMP: 0 millis, 1 micros, 2 nanos
	bool utc = false;		// TIME, TIMESTAMP: isAdjustedToUTC
	bool isSigned = true;	// INTEGER
	std::string logicalText;
};

// The decoded values of a page or of a dictionary.
struct Values
{
	bool isBytes = false;
	std::vector<uint64_t> fixed;	// BOOLEAN, INT32 (sign extended), INT64, FLOAT, DOUBLE (the bits)
	std::string bytes;				// INT96, BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY: the values one after another
	std::vector<size_t> offsets;	// the start of each one and the end of the last

	void reset(bool aIsBytes)
	{
		isBytes = aIsBytes;
		fixed.clear();
		bytes.clear();
		offsets.assign(1, 0);
	}

	size_t size() const
	{
		return isBytes ? offsets.size() - 1 : fixed.size();
	}

	std::string_view at(size_t i) const
	{
		return std::string_view(bytes.data() + offsets[i], offsets[i + 1] - offsets[i]);
	}

	void addBytes(const char* data, size_t n)
	{
		bytes.append(data, n);
		offsets.push_back(bytes.size());
	}
};

// LSB-first bits.
uint64_t readBits(const unsigned char* data, uint64_t bit, int width)
{
	uint64_t value = 0;

	for (int done = 0; done < width;)
	{
		const int offset = int(bit & 7);
		const int take = std::min(8 - offset, width - done);
		value |= uint64_t((data[bit >> 3] >> offset) & ((1u << take) - 1)) << done;
		done += take;
		bit += take;
	}

	return value;
}

int bitWidthOf(int maxValue)
{
	int width = 0;

	while (maxValue >> width)
		++width;

	return width;
}

bool readUleb(const unsigned char*& p, const unsigned char* end, uint64_t& v)
{
	v = 0;

	for (int shift = 0; shift < 64; shift += 7)
	{
		if (p >= end)
			return false;

		const unsigned char b = *p++;
		v |= uint64_t(b & 0x7F) << shift;

		if (!(b & 0x80))
			return true;
	}

	return false;
}

bool readZigzag(const unsigned char*& p, const unsigned char* end, int64_t& v)
{
	uint64_t u;

	if (!readUleb(p, end, u))
		return false;

	v = int64_t(u >> 1) ^ -int64_t(u & 1);
	return true;
}

// The RLE / bit-packed hybrid encoding: count values of width bits.
bool decodeHybrid(const unsigned char* p, const unsigned char* end, int width, size_t count,
	std::vector<uint32_t>& out)
{
	out.clear();
	out.reserve(count);

	if (width > 32)
		return false;

	const size_t valueBytes = (width + 7) / 8;

	while (out.size() < count)
	{
		uint64_t header;

		if (!readUleb(p, end, header))
			return false;

		if (header & 1)
		{
			// Bit-packed: groups of 8 values.
			const uint64_t groups = header >> 1;
			const uint64_t bytes = groups * width;

			if (uint64_t(end - p) < bytes)
				return false;

			const uint64_t n = std::min<uint64_t>(groups * 8, count - out.size());

			for (uint64_t i = 0; i < n; ++i)
				out.push_back(uint32_t(readBits(p, i * width, width)));

			p += bytes;
		}
		else
		{
			const uint64_t run = header >> 1;

			if (size_t(end - p) < valueBytes)
				return false;

			uint32_t value = 0;

			for (size_t i = 0; i < valueBytes; ++i)
				value |= uint32_t(p[i]) << (8 * i);

			p += valueBytes;

			if (run == 0)
				return false;

			out.insert(out.end(), size_t(std::min<uint64_t>(run, count - out.size())), value);
		}
	}

	return true;
}

// The deprecated BIT_PACKED levels: MSB first.
bool decodeBitPacked(const unsigned char*& p, const unsigned char* end, int width, size_t count,
	std::vector<uint32_t>& out)
{
	const uint64_t bytes = (uint64_t(count) * width + 7) / 8;

	if (uint64_t(end - p) < bytes)
		return false;

	out.resize(count);

	for (size_t i = 0; i < count; ++i)
	{
		uint32_t value = 0;

		for (int b = 0; b < width; ++b)
		{
			const uint64_t bit = uint64_t(i) * width + b;
			value = (value << 1) | ((p[bit >> 3] >> (7 - (bit & 7))) & 1);
		}

		out[i] = value;
	}

	p += bytes;
	return true;
}

// DELTA_BINARY_PACKED, up to limit values (the values of the page); p moves
// past the encoded values.
bool decodeDelta(const unsigned char*& p, const unsigned char* end, size_t limit, std::vector<int64_t>& out)
{
	out.clear();
	uint64_t blockSize, miniblocks, total;
	int64_t first;

	if (!readUleb(p, end, blockSize) || !readUleb(p, end, miniblocks) || !readUleb(p, end, total) ||
		!readZigzag(p, end, first))
	{
		return false;
	}

	if (total == 0)
		return true;

	// A width of 0 takes no bytes: the number of values is checked against
	// the page, not against the size of the data.
	if (miniblocks == 0 || blockSize % miniblocks || (blockSize / miniblocks) % 8 || total > limit)
		return false;

	const uint64_t perMiniblock = blockSize / miniblocks;
	out.reserve(size_t(total));
	out.push_back(first);
	uint64_t last = uint64_t(first);

	while (out.size() < total)
	{
		int64_t minDelta;

		if (!readZigzag(p, end, minDelta) || uint64_t(end - p) < miniblocks)
			return false;

		const unsigned char* const widths = p;
		p += miniblocks;

		for (uint64_t m = 0; m < miniblocks && out.size() < total; ++m)
		{
			const int width = widths[m];
			const uint64_t bytes = perMiniblock * width / 8;

			if (width > 64 || uint64_t(end - p) < bytes)
				return false;

			for (uint64_t i = 0; i < perMiniblock && out.size() < total; ++i)
			{
				last += uint64_t(minDelta) + readBits(p, i * width, width);
				out.push_back(int64_t(last));
			}

			p += bytes;
		}
	}

	return true;
}

std::string halfToText(uint16_t h)
{
	const int sign = h >> 15;
	const int exponent = (h >> 10) & 0x1F;
	const int mantissa = h & 0x3FF;
	float f;

	if (exponent == 0)
		f = mantissa / 16777216.0f;			// subnormal: m * 2^-24
	else if (exponent == 31)
		f = mantissa ? NAN : INFINITY;
	else
		f = (1 + mantissa / 1024.0f) * float(ldexp(1.0, exponent - 15));

	return Hq::Rows::formatFloat(sign ? -f : f);
}

}	// anonymous namespace


namespace Hq {
namespace Parquet {

class Reader::Impl
{
public:
	Impl(Reader& aOwner, const Rows::RandomSource& aSource)
		: owner(aOwner), source(aSource)
	{
	}

	bool open();
	Result next(Record& record);
	bool select(const std::vector<unsigned>& indexes);

	uint64_t rowCount = 0;

private:
	// A selected column in the current row group.
	struct State
	{
		unsigned leaf = 0;
		int codec = UNCOMPRESSED;
		uint64_t offset = 0, end = 0;		// the next page, the end of the chunk

		Values dictionary;
		bool hasDictionary = false;

		// The current page.
		size_t count = 0, at = 0, valueAt = 0;
		std::vector<uint32_t> levels;		// definition levels; empty: all defined
		Values values;
		std::vector<uint32_t> indexes;		// into the dictionary
		bool useDictionary = false;

		std::string raw, data;				// buffers
	};

	bool fail(const std::string& message)
	{
		owner.errorText = message;
		return false;
	}

	bool damaged(const State& s)
	{
		return fail("column " + leaves[s.leaf].name + ": the data is damaged");
	}

	bool read(uint64_t offset, size_t size, std::string& to);
	bool flatten(const std::vector<TValue>& schema, size_t& i, const std::string& prefix, int def, int rep,
		unsigned depth);
	void describe(Leaf& leaf, Column& column, const TValue& element);
	bool startRowGroup();
	bool loadPage(State& s);
	bool decompress(State& s, const char* data, size_t size, size_t expected, std::string& out);
	bool decodePlain(State& s, const char* data, size_t size, size_t count, Values& out);
	bool decodeValues(State& s, int encoding, const char* data, size_t size, size_t count);
	void toText(const Leaf& leaf, const Values& values, size_t i, Field& field);

	Reader& owner;
	Rows::RandomSource source;
	TValue meta;
	std::vector<Leaf> leaves;
	std::vector<unsigned> chosen;		// selected leaves
	std::vector<State> states;			// for chosen, in the current row group
	const TValue* rowGroups = nullptr;
	size_t nextGroup = 0;
	int64_t rowsLeft = 0;				// in the current row group
};

bool Reader::Impl::read(uint64_t offset, size_t size, std::string& to)
{
	to.resize(size);
	return size == 0 || (offset + size <= source.size && source.read(offset, &to[0], size));
}

bool Reader::Impl::flatten(const std::vector<TValue>& schema, size_t& i, const std::string& prefix, int def,
	int rep, unsigned depth)
{
	if (i >= schema.size() || depth > MAX_DEPTH)
		return false;

	const TValue& element = schema[i++];
	const std::string name = prefix + element.getString(4);
	const int64_t repetition = element.getInt(3, REQUIRED);
	const int64_t children = element.getInt(5, 0);

	if (depth > 0)
	{
		if (repetition != REQUIRED)
			++def;

		if (repetition == REPEATED)
			++rep;
	}

	if (children > 0 || !element.field(1))
	{
		// A group; the root has no name in the paths.
		for (int64_t c = 0; c < children; ++c)
		{
			if (!flatten(schema, i, depth > 0 ? name + "." : std::string(), def, rep, depth + 1))
				return false;
		}

		return true;
	}

	Leaf leaf;
	leaf.name = name;
	leaf.type = int(element.getInt(1, -1));
	leaf.typeLength = int(element.getInt(2, 0));
	leaf.maxDef = def;
	leaf.maxRep = rep;

	if (leaf.type < PT_BOOLEAN || leaf.type > PT_FIXED_LEN_BYTE_ARRAY)
		return false;

	Column column;
	column.name = name;
	describe(leaf, column, element);
	leaves.push_back(leaf);
	owner.allColumns.push_back(column);
	return true;
}

void Reader::Impl::describe(Leaf& leaf, Column& column, const TValue& element)
{
	static const char* const UNITS[] = {"MILLIS", "MICROS", "NANOS"};
	const TValue* logical = element.field(10);

	if (logical && !logical->fields.empty())
	{
		const int id = logical->fields[0].first;
		const TValue& v = logical->fields[0].second;

		switch (id)
		{
			case 1:		// STRING
				leaf.logical = Leaf::STRING;
				leaf.logicalText = "STRING";
				break;

			case 4:		// ENUM
				leaf.logical = Leaf::STRING;
				leaf.logicalText = "ENUM";
				break;

			case 12:	// JSON
				leaf.logical = Leaf::STRING;
				leaf.logicalText = "JSON";
				break;

			case 13:
				leaf.logical = Leaf::BSON;
				leaf.logicalText = "BSON";
				break;

			case 5:		// DECIMAL
				leaf.logical = Leaf::DECIMAL;
				leaf.scale = int(v.getInt(1, 0));
				leaf.logicalText = "DECIMAL(" + std::to_string(v.getInt(2, 0)) + "," + std::to_string(leaf.scale) + ")";
				break;

			case 6:
				leaf.logical = Leaf::DATE;
				leaf.logicalText = "DATE";
				break;

			case 7:		// TIME
			case 8:		// TIMESTAMP
			{
				leaf.logical = id == 7 ? Leaf::TIME : Leaf::TIMESTAMP;
				leaf.utc = v.getInt(1, 0) != 0;
				const TValue* unit = v.field(2);
				leaf.unit = unit && !unit->fields.empty() ? std::min(std::max(unit->fields[0].first - 1, 0), 2) : 0;
				leaf.logicalText = std::string(id == 7 ? "TIME(" : "TIMESTAMP(") + UNITS[leaf.unit] +
					(leaf.utc ? ", UTC)" : ")");
				break;
			}

			case 10:	// INTEGER
				leaf.logical = Leaf::INTEGER;
				leaf.isSigned = v.getInt(2, 1) != 0;
				leaf.logicalText = std::string(leaf.isSigned ? "INT(" : "UINT(") + std::to_string(v.getInt(1, 0)) + ")";
				break;

			case 14:
				leaf.logical = Leaf::UUID;
				leaf.logicalText = "UUID";
				break;

			case 15:
				leaf.logical = Leaf::FLOAT16;
				leaf.logicalText = "FLOAT16";
				break;

			case 11:	// UNKNOWN: always null
				break;

			default:
				leaf.logical = Leaf::OTHER;
				leaf.logicalText = "logical type " + std::to_string(id);
				break;
		}
	}
	else if (element.field(6))
	{
		const int64_t converted = element.getInt(6, -1);

		switch (converted)
		{
			case CT_UTF8:
			case CT_ENUM:
			case CT_JSON:
				leaf.logical = Leaf::STRING;
				leaf.logicalText = converted == CT_UTF8 ? "UTF8" : converted == CT_ENUM ? "ENUM" : "JSON";
				break;

			case CT_BSON:
				leaf.logical = Leaf::BSON;
				leaf.logicalText = "BSON";
				break;

			case CT_DECIMAL:
				leaf.logical = Leaf::DECIMAL;
				leaf.scale = int(element.getInt(7, 0));
				leaf.logicalText = "DECIMAL(" + std::to_string(element.getInt(8, 0)) + "," + std::to_string(leaf.scale) + ")";
				break;

			case CT_DATE:
				leaf.logical = Leaf::DATE;
				leaf.logicalText = "DATE";
				break;

			case CT_TIME_MILLIS:
			case CT_TIME_MICROS:
				leaf.logical = Leaf::TIME;
				leaf.unit = converted == CT_TIME_MILLIS ? 0 : 1;
				leaf.utc = true;
				leaf.logicalText = converted == CT_TIME_MILLIS ? "TIME_MILLIS" : "TIME_MICROS";
				break;

			case CT_TIMESTAMP_MILLIS:
			case CT_TIMESTAMP_MICROS:
				leaf.logical = Leaf::TIMESTAMP;
				leaf.unit = converted == CT_TIMESTAMP_MILLIS ? 0 : 1;
				leaf.utc = true;
				leaf.logicalText = converted == CT_TIMESTAMP_MILLIS ? "TIMESTAMP_MILLIS" : "TIMESTAMP_MICROS";
				break;

			case CT_INTERVAL:
				leaf.logical = Leaf::OTHER;
				leaf.logicalText = "INTERVAL";
				break;

			default:
				if (converted >= CT_UINT_8 && converted <= CT_INT_64)
				{
					leaf.logical = Leaf::INTEGER;
					leaf.isSigned = converted >= CT_INT_8;
					leaf.logicalText = leaf.isSigned ? "INT" : "UINT";
				}

				break;
		}
	}

	column.type = TYPE_NAMES[leaf.type];

	if (!leaf.logicalText.empty())
		column.type += " " + leaf.logicalText;

	column.supported = leaf.maxRep == 0;

	switch (leaf.type)
	{
		case PT_BYTE_ARRAY:
			column.kind = leaf.logical == Leaf::STRING || leaf.logical == Leaf::DECIMAL ?
				Column::TEXT : Column::BINARY;
			break;

		case PT_FIXED_LEN_BYTE_ARRAY:
			if (leaf.logical == Leaf::UUID && leaf.typeLength == 16)
				column.kind = Column::UUID;
			else if (leaf.logical == Leaf::DECIMAL || leaf.logical == Leaf::FLOAT16)
				column.kind = Column::TEXT;
			else
				column.kind = Column::BINARY;

			break;

		case PT_INT32:
		case PT_INT64:
			if ((leaf.logical == Leaf::TIMESTAMP || leaf.logical == Leaf::TIME) && leaf.utc)
				column.kind = Column::UTC;

			break;

		default:
			break;
	}
}

bool Reader::Impl::open()
{
	std::string tail;

	if (source.size < 12 || !read(source.size - 8, 8, tail))
		return fail("not a Parquet file");

	if (memcmp(tail.data() + 4, "PARE", 4) == 0)
		return fail("an encrypted Parquet file is not supported");

	std::string head;

	if (memcmp(tail.data() + 4, "PAR1", 4) != 0 || !read(0, 4, head) || head != "PAR1")
		return fail("not a Parquet file");

	const uint32_t footerSize = le32((const unsigned char*) tail.data());

	if (footerSize > source.size - 12)
		return fail("the footer of the file is damaged");

	std::string footer;

	if (!read(source.size - 8 - footerSize, footerSize, footer))
		return fail("the footer of the file cannot be read");

	Thrift thrift(footer.data(), footer.size());

	if (!thrift.readStruct(meta))
		return fail("the footer of the file is damaged");

	rowCount = uint64_t(meta.getInt(3, 0));
	rowGroups = meta.field(4);
	const TValue* schema = meta.field(2);

	if (!schema || schema->kind != TValue::LIST || schema->items.empty())
		return fail("the file has no schema");

	size_t i = 0;

	if (!flatten(schema->items, i, std::string(), 0, 0, 0))
		return fail("the schema of the file is damaged");

	std::vector<unsigned> all;

	for (unsigned c = 0; c < leaves.size(); ++c)
		all.push_back(c);

	chosen = all;		// select() checks only what is asked
	return true;
}

bool Reader::Impl::select(const std::vector<unsigned>& indexes)
{
	for (const unsigned i : indexes)
	{
		if (i >= leaves.size())
			return fail("no such column");

		if (leaves[i].maxRep > 0)
		{
			return fail("column " + leaves[i].name +
				" is repeated (a list or a map): such columns cannot be imported");
		}
	}

	chosen = indexes;
	return true;
}

bool Reader::Impl::startRowGroup()
{
	for (;;)
	{
		if (!rowGroups || rowGroups->kind != TValue::LIST || nextGroup >= rowGroups->items.size())
			return false;

		const TValue& group = rowGroups->items[nextGroup++];
		rowsLeft = group.getInt(3, 0);

		if (rowsLeft <= 0)
			continue;

		const TValue* chunks = group.field(1);

		if (!chunks || chunks->kind != TValue::LIST || chunks->items.size() != leaves.size())
			return fail("the row group " + std::to_string(nextGroup) + " is damaged");

		states.assign(chosen.size(), State());

		for (size_t k = 0; k < chosen.size(); ++k)
		{
			State& s = states[k];
			s.leaf = chosen[k];
			const TValue& chunk = chunks->items[s.leaf];
			const TValue* cm = chunk.field(3);

			if (!chunk.getString(1).empty())
				return fail("column " + leaves[s.leaf].name + " is in another file: not supported");

			if (!cm)
				return damaged(s);

			s.codec = int(cm->getInt(4, UNCOMPRESSED));
			const int64_t data = cm->getInt(9, 0);
			const int64_t dictionary = cm->getInt(11, 0);
			const int64_t size = cm->getInt(7, 0);
			const int64_t start = dictionary > 0 && dictionary < data ? dictionary : data;

			if (start <= 0 || size < 0 || uint64_t(start) + uint64_t(size) > source.size)
				return damaged(s);

			s.offset = uint64_t(start);
			s.end = s.offset + uint64_t(size);

			if (s.codec < UNCOMPRESSED || s.codec > LZ4_RAW || s.codec == LZO || s.codec == BROTLI)
			{
				return fail("column " + leaves[s.leaf].name + ": the " +
					(s.codec >= 0 && s.codec <= LZ4_RAW ? CODEC_NAMES[s.codec] : "unknown") +
					" compression is not supported");
			}
		}

		return true;
	}
}

bool Reader::Impl::decompress(State& s, const char* data, size_t size, size_t expected, std::string& out)
{
	std::string error;
	bool ok;

	switch (s.codec)
	{
		case UNCOMPRESSED:
			out.assign(data, size);
			return true;

		case SNAPPY:
			ok = Codec::snappy(data, size, expected, out, error);
			break;

		case GZIP:
			ok = Codec::inflate(data, size, expected, false, out, error);
			break;

		case LZ4:
			ok = Codec::lz4Hadoop(data, size, expected, out, error);
			break;

		case LZ4_RAW:
			ok = Codec::lz4Raw(data, size, expected, out, error);
			break;

		case ZSTD:
			ok = Codec::zstd(data, size, expected, out, error);
			break;

		default:
			ok = false;
			break;
	}

	return ok ? true : fail("column " + leaves[s.leaf].name + ": " + error);
}

bool Reader::Impl::decodePlain(State& s, const char* data, size_t size, size_t count, Values& out)
{
	const Leaf& leaf = leaves[s.leaf];
	const unsigned char* p = (const unsigned char*) data;
	const unsigned char* const end = p + size;
	size_t width = 0;

	out.reset(leaf.type == PT_INT96 || leaf.type == PT_BYTE_ARRAY || leaf.type == PT_FIXED_LEN_BYTE_ARRAY);

	switch (leaf.type)
	{
		case PT_BOOLEAN:
			if (size * 8 < count)
				return damaged(s);

			out.fixed.resize(count);

			for (size_t i = 0; i < count; ++i)
				out.fixed[i] = (p[i >> 3] >> (i & 7)) & 1;

			return true;

		case PT_INT32:
		case PT_FLOAT:
			if (size / 4 < count)
				return damaged(s);

			out.fixed.resize(count);

			for (size_t i = 0; i < count; ++i)
			{
				const uint32_t v = le32(p + 4 * i);
				out.fixed[i] = leaf.type == PT_INT32 ? uint64_t(int64_t(int32_t(v))) : v;
			}

			return true;

		case PT_INT64:
		case PT_DOUBLE:
			if (size / 8 < count)
				return damaged(s);

			out.fixed.resize(count);

			for (size_t i = 0; i < count; ++i)
				out.fixed[i] = le64(p + 8 * i);

			return true;

		case PT_INT96:
			width = 12;
			break;

		case PT_FIXED_LEN_BYTE_ARRAY:
			width = size_t(leaf.typeLength);
			break;

		default:	// BYTE_ARRAY: a length, then the bytes
			out.offsets.reserve(count + 1);

			for (size_t i = 0; i < count; ++i)
			{
				if (end - p < 4)
					return damaged(s);

				const uint32_t n = le32(p);
				p += 4;

				if (uint32_t(end - p) < n)
					return damaged(s);

				out.addBytes((const char*) p, n);
				p += n;
			}

			return true;
	}

	if (width && size / width < count)
		return damaged(s);

	out.bytes.assign(data, width * count);
	out.offsets.resize(count + 1);

	for (size_t i = 0; i <= count; ++i)
		out.offsets[i] = i * width;

	return true;
}

bool Reader::Impl::decodeValues(State& s, int encoding, const char* data, size_t size, size_t count)
{
	const Leaf& leaf = leaves[s.leaf];
	const unsigned char* p = (const unsigned char*) data;
	const unsigned char* const end = p + size;
	s.useDictionary = false;

	switch (encoding)
	{
		case PLAIN:
			return decodePlain(s, data, size, count, s.values);

		case PLAIN_DICTIONARY:
		case RLE_DICTIONARY:
		{
			if (!s.hasDictionary)
				return damaged(s);

			s.useDictionary = true;

			if (count == 0)
			{
				s.indexes.clear();
				return true;
			}

			if (p >= end || !decodeHybrid(p + 1, end, *p, count, s.indexes))
				return damaged(s);

			for (const uint32_t i : s.indexes)
			{
				if (i >= s.dictionary.size())
					return damaged(s);
			}

			return true;
		}

		case RLE:
		{
			// BOOLEAN values: a length, then the hybrid encoding of 1 bit.
			std::vector<uint32_t> bits;

			if (leaf.type != PT_BOOLEAN || size < 4 || le32(p) > size - 4 ||
				!decodeHybrid(p + 4, p + 4 + le32(p), 1, count, bits))
			{
				return damaged(s);
			}

			s.values.reset(false);
			s.values.fixed.assign(bits.begin(), bits.end());
			return true;
		}

		case DELTA_BINARY_PACKED:
		{
			std::vector<int64_t> v;

			if ((leaf.type != PT_INT32 && leaf.type != PT_INT64) || !decodeDelta(p, end, count, v) || v.size() < count)
				return damaged(s);

			s.values.reset(false);
			s.values.fixed.resize(v.size());

			for (size_t i = 0; i < v.size(); ++i)
			{
				s.values.fixed[i] = leaf.type == PT_INT32 ?
					uint64_t(int64_t(int32_t(uint32_t(v[i])))) : uint64_t(v[i]);
			}

			return true;
		}

		case DELTA_LENGTH_BYTE_ARRAY:
		case DELTA_BYTE_ARRAY:
		{
			if (leaf.type != PT_BYTE_ARRAY && leaf.type != PT_FIXED_LEN_BYTE_ARRAY)
				return damaged(s);

			std::vector<int64_t> prefixes, lengths;

			if (encoding == DELTA_BYTE_ARRAY && (!decodeDelta(p, end, count, prefixes) || prefixes.size() < count))
				return damaged(s);

			if (!decodeDelta(p, end, count, lengths) || lengths.size() < count)
				return damaged(s);

			s.values.reset(true);
			std::string previous;

			for (size_t i = 0; i < count; ++i)
			{
				const int64_t n = lengths[i];
				const int64_t prefix = encoding == DELTA_BYTE_ARRAY ? prefixes[i] : 0;

				if (n < 0 || n > end - p || prefix < 0 || size_t(prefix) > previous.size())
					return damaged(s);

				if (encoding == DELTA_BYTE_ARRAY)
				{
					previous.resize(size_t(prefix));
					previous.append((const char*) p, size_t(n));
					s.values.addBytes(previous.data(), previous.size());
				}
				else
					s.values.addBytes((const char*) p, size_t(n));

				p += n;
			}

			return true;
		}

		case BYTE_STREAM_SPLIT:
		{
			// Byte k of every value, then byte k + 1 ...
			size_t width;

			switch (leaf.type)
			{
				case PT_INT32:
				case PT_FLOAT:
					width = 4;
					break;

				case PT_INT64:
				case PT_DOUBLE:
					width = 8;
					break;

				case PT_FIXED_LEN_BYTE_ARRAY:
					width = size_t(leaf.typeLength);
					break;

				default:
					return damaged(s);
			}

			if (width == 0 || size / width < count)
				return damaged(s);

			std::string plain(width * count, '\0');

			for (size_t i = 0; i < count; ++i)
			{
				for (size_t k = 0; k < width; ++k)
					plain[i * width + k] = data[k * count + i];
			}

			return decodePlain(s, plain.data(), plain.size(), count, s.values);
		}

		default:
			return fail("column " + leaves[s.leaf].name + ": the encoding " + std::to_string(encoding) +
				" is not supported");
	}
}

bool Reader::Impl::loadPage(State& s)
{
	const Leaf& leaf = leaves[s.leaf];

	for (;;)
	{
		if (s.offset >= s.end)
			return fail("column " + leaf.name + ": the row group has fewer values than rows");

		// The page header: its size is not known, more is read when it
		// does not fit.
		TValue header;
		size_t headerSize = 0;

		for (size_t want = 256;; want *= 4)
		{
			want = size_t(std::min<uint64_t>(want, s.end - s.offset));

			if (!read(s.offset, want, s.raw))
				return damaged(s);

			Thrift thrift(s.raw.data(), s.raw.size());
			header = TValue();

			if (thrift.readStruct(header))
			{
				headerSize = thrift.consumed();
				break;
			}

			if (!thrift.truncated || want == s.end - s.offset)
				return damaged(s);
		}

		const int64_t type = header.getInt(1, -1);
		const int64_t uncompressed = header.getInt(2, -1);
		const int64_t compressed = header.getInt(3, -1);

		if (uncompressed < 0 || compressed < 0 || s.offset + headerSize + uint64_t(compressed) > s.end ||
			!read(s.offset + headerSize, size_t(compressed), s.raw))
		{
			return damaged(s);
		}

		s.offset += headerSize + uint64_t(compressed);

		if (type == DICTIONARY_PAGE)
		{
			const TValue* dh = header.field(7);

			if (!dh || !decompress(s, s.raw.data(), s.raw.size(), size_t(uncompressed), s.data) ||
				!decodePlain(s, s.data.data(), s.data.size(), size_t(dh->getInt(1, 0)), s.dictionary))
			{
				return owner.errorText.empty() ? damaged(s) : false;
			}

			s.hasDictionary = true;
			continue;
		}

		if (type != DATA_PAGE && type != DATA_PAGE_V2)
			continue;	// an index page

		const int defWidth = bitWidthOf(leaf.maxDef);
		int64_t count;
		int encoding;
		const char* values;
		size_t valuesSize;

		if (type == DATA_PAGE)
		{
			const TValue* dh = header.field(5);

			if (!dh || !decompress(s, s.raw.data(), s.raw.size(), size_t(uncompressed), s.data))
				return owner.errorText.empty() ? damaged(s) : false;

			count = dh->getInt(1, -1);
			encoding = int(dh->getInt(2, PLAIN));
			const unsigned char* p = (const unsigned char*) s.data.data();
			const unsigned char* const end = p + s.data.size();

			if (count < 0)
				return damaged(s);

			s.levels.clear();

			if (leaf.maxDef > 0)
			{
				if (dh->getInt(3, RLE) == BIT_PACKED)
				{
					if (!decodeBitPacked(p, end, defWidth, size_t(count), s.levels))
						return damaged(s);
				}
				else
				{
					if (end - p < 4 || le32(p) > uint32_t(end - p - 4) ||
						!decodeHybrid(p + 4, p + 4 + le32(p), defWidth, size_t(count), s.levels))
					{
						return damaged(s);
					}

					p += 4 + le32(p);
				}
			}

			values = (const char*) p;
			valuesSize = end - p;
		}
		else
		{
			const TValue* dh = header.field(8);

			if (!dh)
				return damaged(s);

			count = dh->getInt(1, -1);
			encoding = int(dh->getInt(4, PLAIN));
			const int64_t defSize = dh->getInt(5, 0);
			const int64_t repSize = dh->getInt(6, 0);
			const bool isCompressed = dh->getInt(7, 1) != 0;

			if (count < 0 || defSize < 0 || repSize < 0 || defSize + repSize > compressed ||
				defSize + repSize > uncompressed)
			{
				return damaged(s);
			}

			const unsigned char* const levels = (const unsigned char*) s.raw.data() + repSize;
			s.levels.clear();

			if (leaf.maxDef > 0 && !decodeHybrid(levels, levels + defSize, defWidth, size_t(count), s.levels))
				return damaged(s);

			const char* const rest = s.raw.data() + repSize + defSize;
			const size_t restSize = s.raw.size() - size_t(repSize + defSize);

			if (isCompressed)
			{
				if (!decompress(s, rest, restSize, size_t(uncompressed - repSize - defSize), s.data))
					return owner.errorText.empty() ? damaged(s) : false;
			}
			else
				s.data.assign(rest, restSize);

			values = s.data.data();
			valuesSize = s.data.size();
		}

		size_t defined = size_t(count);

		if (!s.levels.empty())
			defined = size_t(std::count(s.levels.begin(), s.levels.end(), uint32_t(leaf.maxDef)));

		if (!decodeValues(s, encoding, values, valuesSize, defined))
			return false;

		s.count = size_t(count);
		s.at = s.valueAt = 0;

		if (s.count > 0)
			return true;
	}
}

void Reader::Impl::toText(const Leaf& leaf, const Values& values, size_t i, Field& field)
{
	field.null = false;

	switch (leaf.type)
	{
		case PT_BOOLEAN:
			field.value = values.fixed[i] ? "TRUE" : "FALSE";
			break;

		case PT_INT32:
		case PT_INT64:
		{
			const int64_t v = int64_t(values.fixed[i]);
			const int64_t scale[] = {1000, 1000000, 1000000000};	// units in a second

			switch (leaf.logical)
			{
				case Leaf::DATE:
					field.value = Rows::formatDate(v);
					break;

				case Leaf::DECIMAL:
					field.value = Rows::formatDecimal(v, leaf.scale);
					break;

				case Leaf::TIME:
					field.value = Rows::formatTime(v * (1000000000 / scale[leaf.unit]));
					break;

				case Leaf::TIMESTAMP:
					field.value = Rows::formatTimestamp(v / scale[leaf.unit],
						v % scale[leaf.unit] * (1000000000 / scale[leaf.unit]));
					break;

				case Leaf::INTEGER:
					if (!leaf.isSigned)
					{
						field.value = std::to_string(leaf.type == PT_INT32 ?
							uint64_t(uint32_t(values.fixed[i])) : values.fixed[i]);
						break;
					}
					// fall through

				default:
					field.value = std::to_string(v);
					break;
			}

			break;
		}

		case PT_INT96:
		{
			// The nanoseconds of the day, then the Julian day.
			const unsigned char* p = (const unsigned char*) values.at(i).data();
			const int64_t nanos = int64_t(le64(p));
			const int64_t days = int64_t(int32_t(le32(p + 8))) - JULIAN_1970;
			field.value = Rows::formatTimestamp(days * 86400, nanos);
			break;
		}

		case PT_FLOAT:
		{
			const uint32_t bits = uint32_t(values.fixed[i]);
			float f;
			memcpy(&f, &bits, 4);
			field.value = Rows::formatFloat(f);
			break;
		}

		case PT_DOUBLE:
		{
			double d;
			memcpy(&d, &values.fixed[i], 8);
			field.value = Rows::formatDouble(d);
			break;
		}

		default:	// BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY
		{
			const std::string_view b = values.at(i);

			if (leaf.logical == Leaf::DECIMAL)
				field.value = Rows::formatBigDecimal(b, leaf.scale);
			else if (leaf.logical == Leaf::FLOAT16 && b.size() == 2)
				field.value = halfToText(uint16_t((unsigned char) b[0] | ((unsigned char) b[1] << 8)));
			else
				field.value.assign(b.data(), b.size());

			break;
		}
	}
}

Reader::Result Reader::Impl::next(Record& record)
{
	while (rowsLeft <= 0)
	{
		if (!startRowGroup())
			return owner.errorText.empty() ? END : FAILED;
	}

	record.assign(states.size(), Field());

	for (size_t k = 0; k < states.size(); ++k)
	{
		State& s = states[k];
		const Leaf& leaf = leaves[s.leaf];

		if (s.at == s.count && !loadPage(s))
			return FAILED;

		const uint32_t level = s.levels.empty() ? uint32_t(leaf.maxDef) : s.levels[s.at];
		++s.at;

		if (level < uint32_t(leaf.maxDef))
		{
			record[k].null = true;
			continue;
		}

		const size_t v = s.valueAt++;

		if (s.useDictionary && v < s.indexes.size())
			toText(leaf, s.dictionary, s.indexes[v], record[k]);
		else if (!s.useDictionary && v < s.values.size())
			toText(leaf, s.values, v, record[k]);
		else
			return damaged(s), FAILED;
	}

	--rowsLeft;
	return RECORD;
}


Reader::Reader(const Rows::RandomSource& source)
	: impl(new Impl(*this, source))
{
}

Reader::~Reader()
{
}

bool Reader::open()
{
	return impl->open();
}

Reader::Result Reader::next(Rows::Record& record)
{
	errorText.clear();
	++row;
	return impl->next(record);
}

bool Reader::select(const std::vector<unsigned>& indexes)
{
	return impl->select(indexes);
}

uint64_t Reader::rowCount() const
{
	return impl->rowCount;
}

}	// namespace Parquet
}	// namespace Hq
