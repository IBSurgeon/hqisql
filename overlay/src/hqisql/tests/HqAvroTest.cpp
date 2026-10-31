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
 *  Tests of the Avro reader (Hq::Avro) on the files made by HqFixtures.py.
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqAvro.h"
#include "../hqisql/HqCodec.h"
#include "HqAvroFixtures.h"
#include <string.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace Hq;
using namespace HqFixtures;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqAvroSuite)


BOOST_AUTO_TEST_SUITE(HqAvroTests)

// A source that returns the data in chunks of the given size.
static Rows::Source memorySource(const std::string& data, size_t chunk)
{
	auto offset = std::make_shared<size_t>(0);

	return [data, chunk, offset](char* buffer, size_t size) {
		const size_t n = std::min(std::min(size, chunk), data.size() - *offset);
		memcpy(buffer, data.data() + *offset, n);
		*offset += n;
		return n;
	};
}

// The rows as "v1|v2|..." with NULL as <null>, one per line. The binary
// values as hex.
static std::string readAll(const std::string& file, std::string& error, size_t chunk = 1 << 20,
	const std::vector<unsigned>* select = nullptr)
{
	Avro::Reader reader(memorySource(file, chunk));
	std::string result;
	error.clear();

	if (!reader.open() || (select && !reader.select(*select)))
	{
		error = reader.error();
		return result;
	}

	std::vector<bool> binary;

	for (size_t i = 0; i < reader.columns().size(); ++i)
	{
		const Rows::Column::Kind kind = reader.columns()[select ? (*select)[i] : i].kind;
		binary.push_back(kind == Rows::Column::BINARY || kind == Rows::Column::UUID);

		if (select && i + 1 == select->size())
			break;
	}

	Rows::Record record;
	Rows::Reader::Result r;
	unsigned rows = 0;

	while ((r = reader.next(record)) == Rows::Reader::RECORD)
	{
		BOOST_CHECK_EQUAL(reader.line(), ++rows);

		for (size_t i = 0; i < record.size(); ++i)
		{
			if (i)
				result += '|';

			if (record[i].null)
				result += "<null>";
			else
				result += binary[i] ? Rows::formatHex(record[i].value) : record[i].value;
		}

		result += '\n';
	}

	if (r == Rows::Reader::FAILED)
		error = reader.error();

	return result;
}

BOOST_AUTO_TEST_CASE(TypesTest)
{
	const std::string expected =
		"FALSE|0|-9223372036854775808|1.5|0.1|a\"b\\|00FF|616263|RED|[1, 2]|{\"k\": \"v\"}|"
			"{\"x\": 0, \"c\": \"GREEN\"}|1970-01-01|00:00:00|00:00:00|1970-01-01 00:00:00|1970-01-01 00:00:00|"
			"1970-01-01 00:00:00|123.45|12345.6789|12345678-9abc-def0-1234-56789abcdef0|"
			"00000000000000000000000000000001|<null>\n"
		"TRUE|-1|<null>|-0.25|1e-300|\xD0\x96||010203|GREEN|[7]|{}|{\"x\": 1, \"c\": \"GREEN\"}|2024-02-29|"
			"12:34:56.7890|12:34:56.7890|2024-02-29 12:34:56.7890|2024-02-29 12:34:56.7890|"
			"2024-02-29 12:34:56.7890|-0.05|-0.0001|00000000-0000-0000-0000-000000000000|"
			"00000000000000000000000000000002|{\"x\": 5, \"c\": \"RED\"}\n"
		"FALSE|2147483647|42|3|-2.5|<null>|78|7A7A7A|RED|[]|{}|{\"x\": 2, \"c\": \"GREEN\"}|1969-12-31|"
			"23:59:59.9990|00:00:00|1969-12-31 23:59:59.9990|1970-01-01 00:00:00|1970-01-01 00:00:00|"
			"0.00|0.0000|x|00000000000000000000000000000003|<null>\n";

	std::string error;

	for (size_t chunk : {size_t(1), size_t(7), size_t(1 << 20)})
	{
		BOOST_CHECK_EQUAL(readAll(AVRO_TYPES, error, chunk), expected);
		BOOST_CHECK_EQUAL(error, "");
	}
}

BOOST_AUTO_TEST_CASE(ColumnsTest)
{
	Avro::Reader reader(memorySource(AVRO_TYPES, 1 << 20));
	BOOST_REQUIRE_MESSAGE(reader.open(), reader.error());
	BOOST_CHECK_EQUAL(reader.unit(), "row");

	const std::vector<Rows::Column>& c = reader.columns();
	BOOST_REQUIRE_EQUAL(c.size(), 23u);
	BOOST_CHECK_EQUAL(c[0].name, "b");
	BOOST_CHECK_EQUAL(c[0].type, "boolean");
	BOOST_CHECK_EQUAL(c[2].type, "null | long");
	BOOST_CHECK(c[2].kind == Rows::Column::TEXT);
	BOOST_CHECK(c[6].kind == Rows::Column::BINARY);
	BOOST_CHECK(c[7].kind == Rows::Column::BINARY);
	BOOST_CHECK_EQUAL(c[12].type, "int (date)");
	BOOST_CHECK(c[15].kind == Rows::Column::UTC);
	BOOST_CHECK(c[16].kind == Rows::Column::UTC);
	BOOST_CHECK(c[17].kind == Rows::Column::TEXT);
	BOOST_CHECK(c[18].kind == Rows::Column::TEXT);
	BOOST_CHECK(c[19].kind == Rows::Column::TEXT);
	BOOST_CHECK(c[20].kind == Rows::Column::TEXT);
	BOOST_CHECK(c[21].kind == Rows::Column::UUID);
	BOOST_CHECK_EQUAL(c[22].name, "inner2");

	BOOST_CHECK(!reader.select({0, 0}));
	BOOST_CHECK_EQUAL(reader.error(), "column b is given twice");
	BOOST_CHECK(!reader.select({23}));
}

BOOST_AUTO_TEST_CASE(SelectTest)
{
	std::string error;
	const std::vector<unsigned> select = {22, 1, 5};
	BOOST_CHECK_EQUAL(readAll(AVRO_TYPES, error, 1 << 20, &select),
		"<null>|0|a\"b\\\n"
		"{\"x\": 5, \"c\": \"RED\"}|-1|\xD0\x96\n"
		"<null>|2147483647|<null>\n");
	BOOST_CHECK_EQUAL(error, "");
}

static void checkMany(const std::string& file, const char* name)
{
	std::string expected;

	for (int i = 0; i < 300; ++i)
		expected += std::to_string(i) + "|" + (i % 7 ? "row " + std::to_string(i) : std::string("<null>")) + "\n";

	std::string error;
	BOOST_CHECK_MESSAGE(readAll(file, error) == expected, name);
	BOOST_CHECK_MESSAGE(error.empty(), name << ": " << error);
	BOOST_CHECK_MESSAGE(readAll(file, error, 5) == expected, name);
}

BOOST_AUTO_TEST_CASE(CodecsTest)
{
	checkMany(AVRO_MANY_NULL, "null");
	checkMany(AVRO_MANY_SNAPPY, "snappy");

	std::string out, error;

	if (Codec::inflate("", 0, 0, true, out, error) || error.find("needs") == std::string::npos)
		checkMany(AVRO_MANY_DEFLATE, "deflate");

	if (Codec::zstd("", 0, 0, out, error) || error.find("needs") == std::string::npos)
		checkMany(AVRO_MANY_ZSTANDARD, "zstandard");

	readAll(AVRO_BZIP2, error);
	BOOST_CHECK_EQUAL(error, "the codec bzip2 is not supported (null, deflate, snappy and zstandard are)");
}

BOOST_AUTO_TEST_CASE(NotRecordTest)
{
	Avro::Reader reader(memorySource(AVRO_LONG, 1 << 20));
	BOOST_REQUIRE_MESSAGE(reader.open(), reader.error());
	BOOST_REQUIRE_EQUAL(reader.columns().size(), 1u);
	BOOST_CHECK_EQUAL(reader.columns()[0].name, "VALUE");

	std::string error;
	BOOST_CHECK_EQUAL(readAll(AVRO_LONG, error), "5\n-6\n");
}

BOOST_AUTO_TEST_CASE(DamagedTest)
{
	std::string error;

	readAll("hello", error);
	BOOST_CHECK_EQUAL(error, "not an Avro object container file");

	readAll(std::string("Obj\x01", 4), error);
	BOOST_CHECK_EQUAL(error, "the file header is damaged");

	// The last block cut off.
	readAll(AVRO_MANY_NULL.substr(0, AVRO_MANY_NULL.size() - 20), error);
	BOOST_CHECK_EQUAL(error, "the file ends inside a block");

	// A wrong sync marker.
	std::string bad = AVRO_MANY_NULL;
	bad[bad.size() - 1] ^= 1;
	readAll(bad, error);
	BOOST_CHECK_EQUAL(error, "the sync marker after a block is wrong: the file is damaged");

	// The snappy checksum.
	bad = AVRO_MANY_SNAPPY;
	bad[bad.size() - 17] ^= 1;
	readAll(bad, error);
	BOOST_CHECK_EQUAL(error, "the checksum of a snappy block is wrong");

	// Every byte changed: no crash, rows or an error.
	for (size_t i = 0; i < AVRO_TYPES.size(); ++i)
	{
		bad = AVRO_TYPES;
		bad[i] = char(bad[i] ^ 0x5A);
		readAll(bad, error);
	}
}

BOOST_AUTO_TEST_SUITE_END()	// HqAvroTests

BOOST_AUTO_TEST_SUITE_END()	// HqAvroSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
