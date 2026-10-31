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
#include "../hqisql/HqAvro.h"
#include "../hqisql/HqCodec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>

namespace
{

using Hq::Rows::Column;
using Hq::Rows::Field;
using Hq::Rows::Record;

const unsigned MAX_DEPTH = 64;						// nested values and schemas
const int64_t MAX_BLOCK = int64_t(1) << 31;		// the size of a block

// A JSON value: the schema of the file is JSON.
struct Json
{
	enum Type
	{
		NUL,
		BOOL,
		NUMBER,
		STRING,
		ARRAY,
		OBJECT
	};

	Type type = NUL;
	bool boolean = false;
	double number = 0;
	std::string text;
	std::vector<Json> items;
	std::vector<std::pair<std::string, Json>> members;

	const Json* get(const char* key) const
	{
		for (const auto& m : members)
		{
			if (m.first == key)
				return &m.second;
		}

		return nullptr;
	}

	std::string getText(const char* key) const
	{
		const Json* v = get(key);
		return v && v->type == STRING ? v->text : std::string();
	}

	int getInt(const char* key, int defaultValue) const
	{
		const Json* v = get(key);
		return v && v->type == NUMBER ? int(v->number) : defaultValue;
	}
};

void appendUtf8(std::string& s, unsigned code)
{
	if (code < 0x80)
		s += char(code);
	else if (code < 0x800)
	{
		s += char(0xC0 | (code >> 6));
		s += char(0x80 | (code & 0x3F));
	}
	else if (code < 0x10000)
	{
		s += char(0xE0 | (code >> 12));
		s += char(0x80 | ((code >> 6) & 0x3F));
		s += char(0x80 | (code & 0x3F));
	}
	else
	{
		s += char(0xF0 | (code >> 18));
		s += char(0x80 | ((code >> 12) & 0x3F));
		s += char(0x80 | ((code >> 6) & 0x3F));
		s += char(0x80 | (code & 0x3F));
	}
}

class JsonParser
{
public:
	JsonParser(const char* text, size_t size)
		: p(text), end(text + size)
	{
	}

	bool parse(Json& value)
	{
		return parseValue(value, 0) && (skipBlanks(), p == end);
	}

private:
	void skipBlanks()
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			++p;
	}

	bool literal(const char* word)
	{
		const size_t n = strlen(word);

		if (size_t(end - p) < n || memcmp(p, word, n) != 0)
			return false;

		p += n;
		return true;
	}

	unsigned hex4()
	{
		unsigned code = 0;

		for (int i = 0; i < 4; ++i)
		{
			if (p >= end)
				return ~0u;

			const char c = *p++;
			code <<= 4;

			if (c >= '0' && c <= '9')
				code |= c - '0';
			else if (c >= 'a' && c <= 'f')
				code |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				code |= c - 'A' + 10;
			else
				return ~0u;
		}

		return code;
	}

	bool parseString(std::string& s)
	{
		++p;	// the opening quote

		while (p < end && *p != '"')
		{
			if (*p != '\\')
			{
				s += *p++;
				continue;
			}

			if (++p >= end)
				return false;

			const char e = *p++;

			switch (e)
			{
				case 'b': s += '\b'; break;
				case 'f': s += '\f'; break;
				case 'n': s += '\n'; break;
				case 'r': s += '\r'; break;
				case 't': s += '\t'; break;

				case 'u':
				{
					unsigned code = hex4();

					if (code == ~0u)
						return false;

					// A surrogate pair.
					if (code >= 0xD800 && code < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
					{
						p += 2;
						const unsigned low = hex4();

						if (low < 0xDC00 || low >= 0xE000)
							return false;

						code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
					}

					appendUtf8(s, code);
					break;
				}

				default:
					s += e;
			}
		}

		if (p >= end)
			return false;

		++p;
		return true;
	}

	bool parseValue(Json& v, unsigned depth)
	{
		skipBlanks();

		if (p >= end || depth > MAX_DEPTH)
			return false;

		switch (*p)
		{
			case '{':
				v.type = Json::OBJECT;
				++p;
				skipBlanks();

				if (p < end && *p == '}')
				{
					++p;
					return true;
				}

				for (;;)
				{
					skipBlanks();
					std::pair<std::string, Json> member;

					if (p >= end || *p != '"' || !parseString(member.first))
						return false;

					skipBlanks();

					if (p >= end || *p++ != ':' || !parseValue(member.second, depth + 1))
						return false;

					v.members.push_back(std::move(member));
					skipBlanks();

					if (p < end && *p == ',')
					{
						++p;
						continue;
					}

					if (p < end && *p == '}')
					{
						++p;
						return true;
					}

					return false;
				}

			case '[':
				v.type = Json::ARRAY;
				++p;
				skipBlanks();

				if (p < end && *p == ']')
				{
					++p;
					return true;
				}

				for (;;)
				{
					v.items.emplace_back();

					if (!parseValue(v.items.back(), depth + 1))
						return false;

					skipBlanks();

					if (p < end && *p == ',')
					{
						++p;
						continue;
					}

					if (p < end && *p == ']')
					{
						++p;
						return true;
					}

					return false;
				}

			case '"':
				v.type = Json::STRING;
				return parseString(v.text);

			case 't':
				v.type = Json::BOOL;
				v.boolean = true;
				return literal("true");

			case 'f':
				v.type = Json::BOOL;
				return literal("false");

			case 'n':
				return literal("null");

			default:
			{
				char* numberEnd = nullptr;
				const std::string rest(p, std::min<size_t>(end - p, 64));
				v.type = Json::NUMBER;
				v.number = strtod(rest.c_str(), &numberEnd);

				if (numberEnd == rest.c_str())
					return false;

				p += numberEnd - rest.c_str();
				return true;
			}
		}
	}

	const char* p;
	const char* const end;
};

void appendJsonString(std::string& out, std::string_view s)
{
	out += '"';

	for (const char c : s)
	{
		switch (c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;

			default:
				if ((unsigned char) c < 0x20)
				{
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char) c);
					out += buf;
				}
				else
					out += c;
		}
	}

	out += '"';
}

}	// anonymous namespace


namespace Hq {
namespace Avro {

struct Schema
{
	enum Type
	{
		NUL,
		BOOLEAN,
		INT,
		LONG,
		FLOAT,
		DOUBLE,
		BYTES,
		STRING,
		RECORD,
		ENUM,
		ARRAY,
		MAP,
		UNION,
		FIXED
	};

	Type type = NUL;
	std::string name;			// the full name of a named type
	std::string logical;		// logicalType
	int scale = 0;				// decimal
	size_t size = 0;			// fixed
	std::vector<std::string> symbols;					// enum
	std::vector<std::pair<std::string, Schema*>> fields;	// record
	Schema* items = nullptr;	// array items, map values
	std::vector<Schema*> branches;						// union
};

namespace
{

const char* const TYPE_NAMES[] = {"null", "boolean", "int", "long", "float", "double", "bytes", "string",
	"record", "enum", "array", "map", "union", "fixed"};

// The decoder of the binary encoding over a block.
struct Cursor
{
	const unsigned char* p;
	const unsigned char* end;
	bool ok = true;

	int64_t readLong()
	{
		uint64_t value = 0;

		for (int shift = 0; shift < 70; shift += 7)
		{
			if (p >= end)
				break;

			const unsigned char b = *p++;
			value |= uint64_t(b & 0x7F) << shift;

			if (!(b & 0x80))
				return int64_t(value >> 1) ^ -int64_t(value & 1);
		}

		ok = false;
		return 0;
	}

	std::string_view readFixed(size_t n)
	{
		if (size_t(end - p) < n)
		{
			ok = false;
			p = end;
			return std::string_view();
		}

		const std::string_view result((const char*) p, n);
		p += n;
		return result;
	}

	std::string_view readBytes()
	{
		const int64_t n = readLong();

		if (n < 0)
		{
			ok = false;
			return std::string_view();
		}

		return readFixed(size_t(n));
	}
};

}	// anonymous namespace


class Reader::Impl
{
public:
	Impl(Reader& aOwner, const Rows::Source& aSource)
		: owner(aOwner), source(aSource), buffer(64 * 1024)
	{
	}

	bool open();
	Result next(Record& record);
	bool select(const std::vector<unsigned>& indexes);

private:
	bool fail(const std::string& message)
	{
		owner.errorText = message;
		return false;
	}

	// The container stream.
	bool ensure(size_t n);
	bool readRaw(char* to, size_t n);
	bool readLong(int64_t& value);
	bool readString(std::string& value);
	bool readBlock(bool& end);

	// The schema.
	Schema* parseSchema(const Json& json, const std::string& space, unsigned depth);
	Schema* newSchema(Schema::Type type);
	std::string describe(const Schema* s) const;
	Column::Kind kindOf(const Schema* s) const;

	// The values.
	void toText(const Schema* s, Cursor& c, Field& field, unsigned depth);
	void toJson(const Schema* s, Cursor& c, std::string& out, unsigned depth);
	void skip(const Schema* s, Cursor& c, unsigned depth);

	Reader& owner;
	Rows::Source source;
	std::vector<char> buffer;
	size_t pos = 0, end = 0;
	bool eof = false;

	std::vector<std::unique_ptr<Schema>> schemas;
	std::map<std::string, Schema*> named;
	Schema* top = nullptr;
	std::vector<const Schema*> fieldSchemas;	// a schema per column
	std::vector<int> target;					// a position in the record per column, -1: skipped
	unsigned selected = 0;

	std::string codec;
	char sync[16];
	std::string block, decoded;
	Cursor cursor = {nullptr, nullptr};
	int64_t left = 0;		// records left in the block
};

bool Reader::Impl::ensure(size_t n)
{
	while (end - pos < n && !eof)
	{
		if (pos > 0)
		{
			memmove(buffer.data(), buffer.data() + pos, end - pos);
			end -= pos;
			pos = 0;
		}

		if (buffer.size() < n)
			buffer.resize(n);

		const size_t got = source(buffer.data() + end, buffer.size() - end);

		if (got == 0)
			eof = true;
		else
			end += got;
	}

	return end - pos >= n;
}

bool Reader::Impl::readRaw(char* to, size_t n)
{
	// What is in the buffer, then directly from the source.
	const size_t first = std::min(n, end - pos);
	memcpy(to, buffer.data() + pos, first);
	pos += first;

	for (size_t done = first; done < n;)
	{
		const size_t got = source(to + done, n - done);

		if (got == 0)
		{
			eof = true;
			return false;
		}

		done += got;
	}

	return true;
}

bool Reader::Impl::readLong(int64_t& value)
{
	uint64_t v = 0;

	for (int shift = 0; shift < 70; shift += 7)
	{
		if (!ensure(1))
			return false;

		const unsigned char b = (unsigned char) buffer[pos++];
		v |= uint64_t(b & 0x7F) << shift;

		if (!(b & 0x80))
		{
			value = int64_t(v >> 1) ^ -int64_t(v & 1);
			return true;
		}
	}

	return false;
}

bool Reader::Impl::readString(std::string& value)
{
	int64_t n;

	if (!readLong(n) || n < 0 || n > MAX_BLOCK)
		return false;

	value.resize(size_t(n));
	return n == 0 || readRaw(&value[0], size_t(n));
}

Schema* Reader::Impl::newSchema(Schema::Type type)
{
	schemas.emplace_back(new Schema);
	schemas.back()->type = type;
	return schemas.back().get();
}

Schema* Reader::Impl::parseSchema(const Json& json, const std::string& space, unsigned depth)
{
	if (depth > MAX_DEPTH)
		return nullptr;

	// A union.
	if (json.type == Json::ARRAY)
	{
		Schema* s = newSchema(Schema::UNION);

		for (const Json& item : json.items)
		{
			Schema* branch = parseSchema(item, space, depth + 1);

			if (!branch)
				return nullptr;

			s->branches.push_back(branch);
		}

		return s;
	}

	std::string type;
	const Json* object = nullptr;

	if (json.type == Json::STRING)
		type = json.text;
	else if (json.type == Json::OBJECT)
	{
		object = &json;
		const Json* t = json.get("type");

		if (!t)
			return nullptr;

		// {"type": {...}} or {"type": [...]}: the inner schema.
		if (t->type != Json::STRING)
			return parseSchema(*t, space, depth + 1);

		type = t->text;
	}
	else
		return nullptr;

	for (unsigned i = 0; i < sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0]); ++i)
	{
		if (type != TYPE_NAMES[i] || i == Schema::UNION)
			continue;

		const Schema::Type t = Schema::Type(i);

		// A primitive type, maybe with a logical type.
		if (t <= Schema::STRING)
		{
			Schema* s = newSchema(t);

			if (object)
			{
				s->logical = object->getText("logicalType");
				s->scale = object->getInt("scale", 0);
			}

			return s;
		}

		if (!object)
			return nullptr;

		Schema* s = newSchema(t);
		std::string childSpace = space;

		if (t == Schema::RECORD || t == Schema::ENUM || t == Schema::FIXED)
		{
			// The full name: a name with dots is full, else the namespace.
			const std::string name = object->getText("name");
			const std::string ns = object->get("namespace") ? object->getText("namespace") : space;

			if (name.empty())
				return nullptr;

			s->name = name.find('.') != std::string::npos || ns.empty() ? name : ns + "." + name;
			childSpace = s->name.substr(0, s->name.rfind('.') == std::string::npos ? 0 : s->name.rfind('.'));
			named[s->name] = s;
		}

		switch (t)
		{
			case Schema::RECORD:
			{
				const Json* fields = object->get("fields");

				if (!fields || fields->type != Json::ARRAY)
					return nullptr;

				for (const Json& f : fields->items)
				{
					const Json* ft = f.get("type");
					const std::string fname = f.getText("name");

					if (!ft || fname.empty())
						return nullptr;

					Schema* fs = parseSchema(*ft, childSpace, depth + 1);

					if (!fs)
						return nullptr;

					s->fields.push_back(std::make_pair(fname, fs));
				}

				break;
			}

			case Schema::ENUM:
			{
				const Json* symbols = object->get("symbols");

				if (!symbols || symbols->type != Json::ARRAY)
					return nullptr;

				for (const Json& sym : symbols->items)
					s->symbols.push_back(sym.text);

				break;
			}

			case Schema::ARRAY:
			case Schema::MAP:
			{
				const Json* inner = object->get(t == Schema::ARRAY ? "items" : "values");

				if (!inner || !(s->items = parseSchema(*inner, childSpace, depth + 1)))
					return nullptr;

				break;
			}

			case Schema::FIXED:
			{
				const int size = object->getInt("size", -1);

				if (size < 0)
					return nullptr;

				s->size = size_t(size);
				s->logical = object->getText("logicalType");
				s->scale = object->getInt("scale", 0);
				break;
			}

			default:
				break;
		}

		return s;
	}

	// A reference to a named type.
	const auto it = named.find(type.find('.') == std::string::npos && !space.empty() ? space + "." + type : type);

	if (it != named.end())
		return it->second;

	const auto plain = named.find(type);
	return plain == named.end() ? nullptr : plain->second;
}

std::string Reader::Impl::describe(const Schema* s) const
{
	if (s->type == Schema::UNION)
	{
		std::string text;

		for (const Schema* b : s->branches)
			text += (text.empty() ? "" : " | ") + describe(b);

		return text;
	}

	std::string text = TYPE_NAMES[s->type];

	if (!s->logical.empty())
		text += " (" + s->logical + ")";

	return text;
}

Column::Kind Reader::Impl::kindOf(const Schema* s) const
{
	if (s->type == Schema::UNION)
	{
		// A union with null: the kind of the other branch.
		const Schema* other = nullptr;

		for (const Schema* b : s->branches)
		{
			if (b->type != Schema::NUL)
			{
				if (other)
					return Column::TEXT;

				other = b;
			}
		}

		return other ? kindOf(other) : Column::TEXT;
	}

	switch (s->type)
	{
		case Schema::BYTES:
			return s->logical == "decimal" ? Column::TEXT : Column::BINARY;

		case Schema::FIXED:
			if (s->logical == "decimal")
				return Column::TEXT;

			return s->logical == "uuid" && s->size == 16 ? Column::UUID : Column::BINARY;

		case Schema::LONG:
			if (s->logical == "timestamp-millis" || s->logical == "timestamp-micros" ||
				s->logical == "timestamp-nanos")
			{
				return Column::UTC;
			}

			return Column::TEXT;

		default:
			return Column::TEXT;
	}
}

bool Reader::Impl::open()
{
	char magic[4];

	if (!ensure(4) || !readRaw(magic, 4) || memcmp(magic, "Obj\x01", 4) != 0)
		return fail("not an Avro object container file");

	// The metadata: a map of bytes.
	std::string schemaText;

	for (;;)
	{
		int64_t count;

		if (!readLong(count))
			return fail("the file header is damaged");

		if (count == 0)
			break;

		if (count < 0)
		{
			int64_t size;
			count = -count;

			if (!readLong(size))
				return fail("the file header is damaged");
		}

		for (int64_t i = 0; i < count; ++i)
		{
			std::string key, value;

			if (!readString(key) || !readString(value))
				return fail("the file header is damaged");

			if (key == "avro.schema")
				schemaText = value;
			else if (key == "avro.codec")
				codec = value;
		}
	}

	if (!readRaw(sync, sizeof(sync)))
		return fail("the file header is damaged");

	if (codec.empty())
		codec = "null";

	if (codec != "null" && codec != "deflate" && codec != "snappy" && codec != "zstandard")
		return fail("the codec " + codec + " is not supported (null, deflate, snappy and zstandard are)");

	Json json;
	JsonParser parser(schemaText.data(), schemaText.size());

	if (schemaText.empty() || !parser.parse(json) || !(top = parseSchema(json, "", 0)))
		return fail("the schema of the file cannot be read");

	if (top->type == Schema::RECORD)
	{
		for (const auto& f : top->fields)
		{
			Column column;
			column.name = f.first;
			column.kind = kindOf(f.second);
			column.type = describe(f.second);
			owner.allColumns.push_back(column);
			fieldSchemas.push_back(f.second);
		}
	}
	else
	{
		Column column;
		column.name = "VALUE";
		column.kind = kindOf(top);
		column.type = describe(top);
		owner.allColumns.push_back(column);
		fieldSchemas.push_back(top);
	}

	std::vector<unsigned> all;

	for (unsigned i = 0; i < fieldSchemas.size(); ++i)
		all.push_back(i);

	return select(all);
}

bool Reader::Impl::select(const std::vector<unsigned>& indexes)
{
	target.assign(fieldSchemas.size(), -1);

	for (unsigned k = 0; k < indexes.size(); ++k)
	{
		const unsigned i = indexes[k];

		if (i >= target.size())
			return fail("no such column");

		if (target[i] >= 0)
			return fail("column " + owner.allColumns[i].name + " is given twice");

		target[i] = int(k);
	}

	selected = unsigned(indexes.size());
	return true;
}

// The next block; end is set at the end of the file.
bool Reader::Impl::readBlock(bool& atEnd)
{
	atEnd = false;

	if (!ensure(1))
	{
		atEnd = true;
		return true;
	}

	int64_t count, size;

	if (!readLong(count) || !readLong(size) || count < 0 || size < 0 || size > MAX_BLOCK)
		return fail("a block header is damaged");

	block.resize(size_t(size));

	if (size && !readRaw(&block[0], size_t(size)))
		return fail("the file ends inside a block");

	char marker[sizeof(sync)];

	if (!readRaw(marker, sizeof(marker)) || memcmp(marker, sync, sizeof(sync)) != 0)
		return fail("the sync marker after a block is wrong: the file is damaged");

	const std::string* data = &block;
	std::string error;

	if (codec == "deflate")
	{
		if (!Codec::inflate(block.data(), block.size(), 0, true, decoded, error))
			return fail(error);

		data = &decoded;
	}
	else if (codec == "snappy")
	{
		// The data, then the CRC-32 of the result, big-endian.
		if (block.size() < 4 ||
			!Codec::snappy(block.data(), block.size() - 4, 0, decoded, error))
		{
			return fail(error.empty() ? "the snappy data is damaged" : error);
		}

		const unsigned char* c = (const unsigned char*) block.data() + block.size() - 4;
		const uint32_t crc = (uint32_t(c[0]) << 24) | (c[1] << 16) | (c[2] << 8) | c[3];

		if (crc != Codec::crc32(decoded.data(), decoded.size()))
			return fail("the checksum of a snappy block is wrong");

		data = &decoded;
	}
	else if (codec == "zstandard")
	{
		if (!Codec::zstd(block.data(), block.size(), 0, decoded, error))
			return fail(error);

		data = &decoded;
	}

	cursor.p = (const unsigned char*) data->data();
	cursor.end = cursor.p + data->size();
	cursor.ok = true;
	left = count;
	return true;
}

void Reader::Impl::toText(const Schema* s, Cursor& c, Field& field, unsigned depth)
{
	field.null = false;
	field.value.clear();

	switch (s->type)
	{
		case Schema::NUL:
			field.null = true;
			break;

		case Schema::BOOLEAN:
		{
			const std::string_view b = c.readFixed(1);
			field.value = !b.empty() && b[0] ? "TRUE" : "FALSE";
			break;
		}

		case Schema::INT:
		case Schema::LONG:
		{
			const int64_t v = c.readLong();
			const std::string& l = s->logical;

			if (l == "date")
				field.value = Rows::formatDate(v);
			else if (l == "time-millis")
				field.value = Rows::formatTime(v * 1000000);
			else if (l == "time-micros")
				field.value = Rows::formatTime(v * 1000);
			else if (l == "timestamp-millis" || l == "local-timestamp-millis")
				field.value = Rows::formatTimestamp(v / 1000, v % 1000 * 1000000);
			else if (l == "timestamp-micros" || l == "local-timestamp-micros")
				field.value = Rows::formatTimestamp(v / 1000000, v % 1000000 * 1000);
			else if (l == "timestamp-nanos" || l == "local-timestamp-nanos")
				field.value = Rows::formatTimestamp(v / 1000000000, v % 1000000000);
			else
				field.value = std::to_string(v);

			break;
		}

		case Schema::FLOAT:
		{
			const std::string_view b = c.readFixed(4);
			float f = 0;

			if (b.size() == 4)
				memcpy(&f, b.data(), 4);	// little-endian, as the platforms of Firebird

			field.value = Rows::formatFloat(f);
			break;
		}

		case Schema::DOUBLE:
		{
			const std::string_view b = c.readFixed(8);
			double d = 0;

			if (b.size() == 8)
				memcpy(&d, b.data(), 8);

			field.value = Rows::formatDouble(d);
			break;
		}

		case Schema::BYTES:
		case Schema::FIXED:
		{
			const std::string_view b = s->type == Schema::BYTES ? c.readBytes() : c.readFixed(s->size);

			if (s->logical == "decimal")
				field.value = Rows::formatBigDecimal(b, s->scale);
			else
				field.value.assign(b.data(), b.size());

			break;
		}

		case Schema::STRING:
		{
			const std::string_view b = c.readBytes();
			field.value.assign(b.data(), b.size());
			break;
		}

		case Schema::ENUM:
		{
			const int64_t i = c.readLong();

			if (i < 0 || size_t(i) >= s->symbols.size())
				c.ok = false;
			else
				field.value = s->symbols[size_t(i)];

			break;
		}

		case Schema::UNION:
		{
			const int64_t i = c.readLong();

			if (i < 0 || size_t(i) >= s->branches.size())
				c.ok = false;
			else
				toText(s->branches[size_t(i)], c, field, depth + 1);

			break;
		}

		default:
			toJson(s, c, field.value, depth + 1);
			break;
	}
}

void Reader::Impl::toJson(const Schema* s, Cursor& c, std::string& out, unsigned depth)
{
	if (depth > MAX_DEPTH || !c.ok)
	{
		c.ok = false;
		return;
	}

	switch (s->type)
	{
		case Schema::RECORD:
			out += '{';

			for (size_t i = 0; i < s->fields.size(); ++i)
			{
				if (i)
					out += ", ";

				appendJsonString(out, s->fields[i].first);
				out += ": ";
				toJson(s->fields[i].second, c, out, depth + 1);
			}

			out += '}';
			break;

		case Schema::ARRAY:
		case Schema::MAP:
		{
			const bool map = s->type == Schema::MAP;
			out += map ? '{' : '[';
			bool first = true;

			for (;;)
			{
				int64_t count = c.readLong();

				if (count == 0 || !c.ok)
					break;

				if (count < 0)
				{
					count = -count;
					c.readLong();	// the size of the block in bytes
				}

				for (int64_t i = 0; i < count && c.ok; ++i)
				{
					if (!first)
						out += ", ";

					first = false;

					if (map)
					{
						appendJsonString(out, c.readBytes());
						out += ": ";
					}

					toJson(s->items, c, out, depth + 1);
				}
			}

			out += map ? '}' : ']';
			break;
		}

		case Schema::UNION:
		{
			const int64_t i = c.readLong();

			if (i < 0 || size_t(i) >= s->branches.size())
				c.ok = false;
			else
				toJson(s->branches[size_t(i)], c, out, depth + 1);

			break;
		}

		default:
		{
			Field field;
			toText(s, c, field, depth + 1);

			if (field.null)
				out += "null";
			else if (s->type == Schema::BOOLEAN)
				out += field.value == "TRUE" ? "true" : "false";
			else if ((s->type == Schema::INT || s->type == Schema::LONG) && s->logical.empty())
				out += field.value;
			else if ((s->type == Schema::FLOAT || s->type == Schema::DOUBLE) &&
				field.value.find_first_of("ni") == std::string::npos)	// not nan, inf
			{
				out += field.value;
			}
			else if (s->logical == "decimal")
				out += field.value;
			else if (s->type == Schema::BYTES || s->type == Schema::FIXED)
				appendJsonString(out, s->logical == "uuid" ? Rows::formatUuid(field.value) : Rows::formatHex(field.value));
			else
				appendJsonString(out, field.value);

			break;
		}
	}
}

void Reader::Impl::skip(const Schema* s, Cursor& c, unsigned depth)
{
	if (depth > MAX_DEPTH || !c.ok)
	{
		c.ok = false;
		return;
	}

	switch (s->type)
	{
		case Schema::NUL:
			break;

		case Schema::BOOLEAN:
			c.readFixed(1);
			break;

		case Schema::INT:
		case Schema::LONG:
		case Schema::ENUM:
			c.readLong();
			break;

		case Schema::FLOAT:
			c.readFixed(4);
			break;

		case Schema::DOUBLE:
			c.readFixed(8);
			break;

		case Schema::BYTES:
		case Schema::STRING:
			c.readBytes();
			break;

		case Schema::FIXED:
			c.readFixed(s->size);
			break;

		case Schema::RECORD:
			for (const auto& f : s->fields)
				skip(f.second, c, depth + 1);

			break;

		case Schema::ARRAY:
		case Schema::MAP:
			for (;;)
			{
				int64_t count = c.readLong();

				if (count == 0 || !c.ok)
					break;

				if (count < 0)
				{
					// The size in bytes is known: the whole block at once.
					const int64_t size = c.readLong();

					if (size < 0)
						c.ok = false;
					else
						c.readFixed(size_t(size));

					continue;
				}

				for (int64_t i = 0; i < count && c.ok; ++i)
				{
					if (s->type == Schema::MAP)
						c.readBytes();

					skip(s->items, c, depth + 1);
				}
			}

			break;

		case Schema::UNION:
		{
			const int64_t i = c.readLong();

			if (i < 0 || size_t(i) >= s->branches.size())
				c.ok = false;
			else
				skip(s->branches[size_t(i)], c, depth + 1);

			break;
		}
	}
}

Reader::Result Reader::Impl::next(Record& record)
{
	while (left == 0)
	{
		bool atEnd;

		if (!readBlock(atEnd))
			return FAILED;

		if (atEnd)
			return END;
	}

	--left;
	record.assign(selected, Field());

	for (size_t i = 0; i < fieldSchemas.size() && cursor.ok; ++i)
	{
		if (target[i] >= 0)
			toText(fieldSchemas[i], cursor, record[target[i]], 0);
		else
			skip(fieldSchemas[i], cursor, 0);
	}

	if (!cursor.ok)
		return fail("the data of the row is damaged"), FAILED;

	return RECORD;
}


Reader::Reader(const Rows::Source& source)
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

}	// namespace Avro
}	// namespace Hq
