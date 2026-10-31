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
 *
 *  Tests of the Parquet reader (Hq::Parquet) on the files made by
 *  HqFixtures.py with pyarrow.
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqCodec.h"
#include "../hqisql/HqParquet.h"
#include "HqParquetFixtures.h"
#include <string.h>
#include <string>
#include <vector>

using namespace Hq;
using namespace HqFixtures;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqParquetSuite)


BOOST_AUTO_TEST_SUITE(HqParquetTests)

static Rows::RandomSource memorySource(const std::string& data)
{
	Rows::RandomSource source;
	source.size = data.size();
	source.read = [data](uint64_t offset, char* buffer, size_t size) {
		if (offset > data.size() || size > data.size() - offset)
			return false;

		memcpy(buffer, data.data() + offset, size);
		return true;
	};

	return source;
}

// The values of a column: "v1|v2|..." with NULL as <null>. The binary
// values as hex.
static std::vector<std::string> readColumns(const std::string& file, std::string& error,
	const std::vector<unsigned>* select = nullptr)
{
	Parquet::Reader reader(memorySource(file));
	std::vector<std::string> columns;
	error.clear();

	if (!reader.open() || (select && !reader.select(*select)))
	{
		error = reader.error();
		return columns;
	}

	std::vector<bool> binary;

	if (select)
	{
		for (const unsigned i : *select)
		{
			const Rows::Column::Kind kind = reader.columns()[i].kind;
			binary.push_back(kind == Rows::Column::BINARY || kind == Rows::Column::UUID);
		}
	}
	else
	{
		for (const Rows::Column& c : reader.columns())
			binary.push_back(c.kind == Rows::Column::BINARY || c.kind == Rows::Column::UUID);
	}

	Rows::Record record;
	Rows::Reader::Result r;
	unsigned rows = 0;

	while ((r = reader.next(record)) == Rows::Reader::RECORD)
	{
		++rows;
		BOOST_CHECK_EQUAL(reader.line(), rows);
		columns.resize(record.size());

		for (size_t i = 0; i < record.size(); ++i)
		{
			std::string& c = columns[i];

			if (rows > 1)
				c += '|';

			if (record[i].null)
				c += "<null>";
			else
				c += binary[i] ? Rows::formatHex(record[i].value) : record[i].value;
		}
	}

	if (r == Rows::Reader::FAILED)
		error = reader.error();

	return columns;
}

// The file has a list column: all the other columns are read.
static std::vector<std::string> readTypes(const std::string& file)
{
	Parquet::Reader reader(memorySource(file));
	BOOST_REQUIRE_MESSAGE(reader.open(), reader.error());

	std::vector<unsigned> all;

	for (unsigned i = 0; i < reader.columns().size(); ++i)
	{
		if (reader.columns()[i].supported)
			all.push_back(i);
	}

	std::string error;
	std::vector<std::string> columns = readColumns(file, error, &all);
	BOOST_CHECK_EQUAL(error, "");
	return columns;
}

static void checkTypes(const std::string& file)
{
	const std::vector<std::string> c = readTypes(file);
	const char* const expected[] = {
		"TRUE|FALSE|<null>",										// b
		"-128|0|127",												// i8
		"-32768|<null>|32767",										// i16
		"-2147483648|0|2147483647",									// i32
		"-9223372036854775808|1|9223372036854775807",				// i64
		"0|1|255",													// u8
		"0|1|4294967295",											// u32
		"0|1|18446744073709551615",									// u64
		"1.5|-0.1|<null>",											// f
		"0.1|-1e+300|3",											// d
		"abc|<null>|\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",	// s
		"0001||<null>",												// bin
		"616263|FF0001|<null>",										// fx
		"123.45|-0.05|<null>",										// dec
		"1234567890123.45678|<null>|-1.00000",						// dec18
		"-12345678901234567890.1234567890|0.0000000000|<null>",		// dec30
		"1970-01-01|2024-02-29|1899-12-31",							// dt
		"00:00:00|12:34:56.7890|<null>",							// t_ms
		"00:00:00|12:34:56.7890|23:59:59.9999",						// t_us
		"00:00:00.1234|00:00:00|<null>",							// t_ns
		"1970-01-01 00:00:00|2024-02-29 12:34:56.7890|1969-12-31 23:59:59.9990",	// ts_ms
		"2024-02-29 12:34:56.7890|<null>|1970-01-01 00:00:00",		// ts_us_utc
		"2024-02-29 12:34:56.1234|1970-01-01 00:00:00|<null>",		// ts_ns
		"1|<null>|<null>",											// st.x
		"a|<null>|c",												// st.y
		"123456789ABCDEF0123456789ABCDEF0|<null>|00000000000000000000000000000001",	// id
		"1.5|-2|<null>"												// h
	};

	BOOST_REQUIRE_EQUAL(c.size(), sizeof(expected) / sizeof(expected[0]));

	for (size_t i = 0; i < c.size(); ++i)
		BOOST_CHECK_EQUAL(c[i], expected[i]);
}

BOOST_AUTO_TEST_CASE(TypesTest)
{
	checkTypes(PARQUET_TYPES);
}

BOOST_AUTO_TEST_CASE(DictionaryTest)
{
	checkTypes(PARQUET_TYPES_DICT);
}

BOOST_AUTO_TEST_CASE(PageV2Test)
{
	std::string out, error;

	if (!Codec::zstd("", 0, 0, out, error) && error.find("needs") != std::string::npos)
	{
		BOOST_TEST_MESSAGE("no zstd: " << error);
		return;
	}

	checkTypes(PARQUET_TYPES_V2);
}

BOOST_AUTO_TEST_CASE(ColumnsTest)
{
	Parquet::Reader reader(memorySource(PARQUET_TYPES));
	BOOST_REQUIRE_MESSAGE(reader.open(), reader.error());
	BOOST_CHECK_EQUAL(reader.rowCount(), 3u);
	BOOST_CHECK_EQUAL(reader.unit(), "row");

	const std::vector<Rows::Column>& c = reader.columns();
	BOOST_REQUIRE_EQUAL(c.size(), 28u);

	BOOST_CHECK_EQUAL(c[0].name, "b");
	BOOST_CHECK_EQUAL(c[0].type, "BOOLEAN");
	BOOST_CHECK_EQUAL(c[1].type, "INT32 INT(8)");
	BOOST_CHECK_EQUAL(c[7].type, "INT64 UINT(64)");
	BOOST_CHECK_EQUAL(c[10].type, "BYTE_ARRAY STRING");
	BOOST_CHECK(c[10].kind == Rows::Column::TEXT);
	BOOST_CHECK_EQUAL(c[11].type, "BYTE_ARRAY");
	BOOST_CHECK(c[11].kind == Rows::Column::BINARY);
	BOOST_CHECK(c[12].kind == Rows::Column::BINARY);
	BOOST_CHECK_EQUAL(c[13].type, "FIXED_LEN_BYTE_ARRAY DECIMAL(9,2)");
	BOOST_CHECK(c[13].kind == Rows::Column::TEXT);
	BOOST_CHECK_EQUAL(c[16].type, "INT32 DATE");
	BOOST_CHECK_EQUAL(c[20].type, "INT64 TIMESTAMP(MILLIS)");
	BOOST_CHECK(c[20].kind == Rows::Column::TEXT);
	BOOST_CHECK_EQUAL(c[21].type, "INT64 TIMESTAMP(MICROS, UTC)");
	BOOST_CHECK(c[21].kind == Rows::Column::UTC);
	BOOST_CHECK_EQUAL(c[23].name, "st.x");
	BOOST_CHECK_EQUAL(c[24].name, "st.y");
	BOOST_CHECK_EQUAL(c[25].name, "lst.list.element");
	BOOST_CHECK(!c[25].supported);
	BOOST_CHECK_EQUAL(c[26].type, "FIXED_LEN_BYTE_ARRAY UUID");
	BOOST_CHECK(c[26].kind == Rows::Column::UUID);
	BOOST_CHECK_EQUAL(c[27].type, "FIXED_LEN_BYTE_ARRAY FLOAT16");

	// A list cannot be read.
	BOOST_CHECK(!reader.select({0, 25}));
	BOOST_CHECK_EQUAL(reader.error(),
		"column lst.list.element is repeated (a list or a map): such columns cannot be imported");
}

BOOST_AUTO_TEST_CASE(SelectTest)
{
	// Some columns in another order.
	std::string error;
	const std::vector<unsigned> select = {16, 0, 10};
	const std::vector<std::string> c = readColumns(PARQUET_TYPES, error, &select);
	BOOST_CHECK_EQUAL(error, "");
	BOOST_REQUIRE_EQUAL(c.size(), 3u);
	BOOST_CHECK_EQUAL(c[0], "1970-01-01|2024-02-29|1899-12-31");
	BOOST_CHECK_EQUAL(c[1], "TRUE|FALSE|<null>");
	BOOST_CHECK_EQUAL(c[2], "abc|<null>|\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
}

// 300 rows in row groups of 120 and small pages.
static void checkMany(const std::string& file, const char* name)
{
	std::string error;
	const std::vector<std::string> c = readColumns(file, error);
	BOOST_CHECK_MESSAGE(error.empty(), name << ": " << error);
	BOOST_REQUIRE_EQUAL(c.size(), 7u);

	std::string n, big, s, k, x, fl, ok;

	for (int i = 0; i < 300; ++i)
	{
		const char* const sep = i ? "|" : "";
		n += sep + std::to_string(i);
		big += sep + std::to_string(int64_t(i) * 1000000007 - 500000000000LL);
		s += sep + (i % 7 ? "row " + std::to_string(i) : std::string("<null>"));
		k += sep + ("key" + std::to_string(i % 5));
		x += sep + Rows::formatDouble(i / 4.0);
		fl += sep + Rows::formatFloat(float(i / 2.0));
		ok += sep + std::string(i % 3 == 0 ? "TRUE" : "FALSE");
	}

	BOOST_CHECK_MESSAGE(c[0] == n, name << ": n");
	BOOST_CHECK_MESSAGE(c[1] == big, name << ": big");
	BOOST_CHECK_MESSAGE(c[2] == s, name << ": s");
	BOOST_CHECK_MESSAGE(c[3] == k, name << ": k");
	BOOST_CHECK_MESSAGE(c[4] == x, name << ": x");
	BOOST_CHECK_MESSAGE(c[5] == fl, name << ": fl");
	BOOST_CHECK_MESSAGE(c[6] == ok, name << ": ok");
}

BOOST_AUTO_TEST_CASE(CodecsTest)
{
	checkMany(PARQUET_MANY_NONE, "none");
	checkMany(PARQUET_MANY_SNAPPY, "snappy");
	checkMany(PARQUET_MANY_LZ4, "lz4");
	checkMany(PARQUET_MANY_DELTA, "delta");
	checkMany(PARQUET_MANY_DELTA_V2, "delta v2");

	std::string out, error;

	if (Codec::inflate("", 0, 0, true, out, error) || error.find("needs") == std::string::npos)
		checkMany(PARQUET_MANY_GZIP, "gzip");

	if (Codec::zstd("", 0, 0, out, error) || error.find("needs") == std::string::npos)
		checkMany(PARQUET_MANY_ZSTD, "zstd");

	readColumns(PARQUET_BROTLI, error);
	BOOST_CHECK_EQUAL(error, "column n: the BROTLI compression is not supported");
}

BOOST_AUTO_TEST_CASE(Int96Test)
{
	std::string error;
	const std::vector<std::string> c = readColumns(PARQUET_INT96, error);
	BOOST_CHECK_EQUAL(error, "");
	BOOST_REQUIRE_EQUAL(c.size(), 1u);
	BOOST_CHECK_EQUAL(c[0], "2024-02-29 12:34:56.1234|1970-01-01 00:00:00|1969-12-30 23:59:59.9999");
}

BOOST_AUTO_TEST_CASE(DamagedTest)
{
	std::string error;

	readColumns("hello", error);
	BOOST_CHECK_EQUAL(error, "not a Parquet file");

	readColumns(std::string("PAR1\x10\0\0\0PAR1", 12), error);
	BOOST_CHECK(!error.empty());

	// The footer without the first bytes.
	std::string cut = PARQUET_TYPES;
	cut.erase(4, cut.size() / 2);
	readColumns(cut, error);
	BOOST_CHECK(!error.empty());

	// Every byte of the footer changed: no crash, a row or an error.
	const uint32_t footer = uint32_t((unsigned char) PARQUET_INT96[PARQUET_INT96.size() - 8]) |
		uint32_t((unsigned char) PARQUET_INT96[PARQUET_INT96.size() - 7]) << 8;

	for (size_t i = PARQUET_INT96.size() - 8 - footer; i < PARQUET_INT96.size() - 8; ++i)
	{
		std::string bad = PARQUET_INT96;
		bad[i] = char(bad[i] ^ 0x5A);
		readColumns(bad, error);
	}

	// Every byte of the data changed.
	for (size_t i = 4; i < PARQUET_TYPES_DICT.size() - 8; i += 7)
	{
		std::string bad = PARQUET_TYPES_DICT;
		bad[i] = char(bad[i] ^ 0xA5);
		readColumns(bad, error);
	}
}

BOOST_AUTO_TEST_SUITE_END()	// HqParquetTests

BOOST_AUTO_TEST_SUITE_END()	// HqParquetSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
