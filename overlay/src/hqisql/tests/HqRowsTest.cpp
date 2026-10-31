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
 *  Tests of the value formatting of the import readers (Hq::Rows).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqRows.h"
#include <string>

using namespace Hq::Rows;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqRowsSuite)


BOOST_AUTO_TEST_SUITE(HqRowsTests)

static Field field(const char* value)
{
	Field f;

	if (value)
		f.value = value;
	else
		f.null = true;

	return f;
}

BOOST_AUTO_TEST_CASE(CsvLineTest)
{
	BOOST_CHECK_EQUAL(csvLine({field("a"), field("1")}), "a,1");
	BOOST_CHECK_EQUAL(csvLine({field(nullptr), field(""), field(nullptr)}), ",\"\",");
	BOOST_CHECK_EQUAL(csvLine({field("a,b"), field("say \"hi\""), field("x\ny")}),
		"\"a,b\",\"say \"\"hi\"\"\",\"x\ny\"");
	BOOST_CHECK_EQUAL(csvLine({field(" a"), field("b ")}), "\" a\",\"b \"");
	BOOST_CHECK_EQUAL(csvLine({}), "");
}

BOOST_AUTO_TEST_CASE(DateTimeTest)
{
	BOOST_CHECK_EQUAL(formatDate(0), "1970-01-01");
	BOOST_CHECK_EQUAL(formatDate(-1), "1969-12-31");
	BOOST_CHECK_EQUAL(formatDate(19782), "2024-02-29");
	BOOST_CHECK_EQUAL(formatDate(-719162), "0001-01-01");
	BOOST_CHECK_EQUAL(formatDate(2932896), "9999-12-31");

	BOOST_CHECK_EQUAL(formatTime(0), "00:00:00");
	BOOST_CHECK_EQUAL(formatTime(45296789012345LL), "12:34:56.7890");
	BOOST_CHECK_EQUAL(formatTime(86399999999999LL), "23:59:59.9999");
	BOOST_CHECK_EQUAL(formatTime(99999), "00:00:00");		// less than 1/10000 s
	BOOST_CHECK_EQUAL(formatTime(100000), "00:00:00.0001");

	BOOST_CHECK_EQUAL(formatTimestamp(0, 0), "1970-01-01 00:00:00");
	BOOST_CHECK_EQUAL(formatTimestamp(1709210096, 789012000), "2024-02-29 12:34:56.7890");
	BOOST_CHECK_EQUAL(formatTimestamp(-1, 0), "1969-12-31 23:59:59");
	BOOST_CHECK_EQUAL(formatTimestamp(0, -1000000), "1969-12-31 23:59:59.9990");
	BOOST_CHECK_EQUAL(formatTimestamp(0, 1500000000), "1970-01-01 00:00:01.5000");
}

BOOST_AUTO_TEST_CASE(BytesTest)
{
	const std::string uuid("\x12\x34\x56\x78\x9a\xbc\xde\xf0\x12\x34\x56\x78\x9a\xbc\xde\xf0", 16);
	BOOST_CHECK_EQUAL(formatUuid(uuid), "12345678-9abc-def0-1234-56789abcdef0");
	BOOST_CHECK_EQUAL(formatHex(std::string("\x00\xff\x1a", 3)), "00FF1A");
	BOOST_CHECK_EQUAL(formatHex(""), "");
}

BOOST_AUTO_TEST_CASE(NumberTest)
{
	BOOST_CHECK_EQUAL(formatDouble(0.1), "0.1");
	BOOST_CHECK_EQUAL(formatDouble(0.1 + 0.2), "0.30000000000000004");
	BOOST_CHECK_EQUAL(formatDouble(1.5), "1.5");
	BOOST_CHECK_EQUAL(formatDouble(-1e300), "-1e+300");
	BOOST_CHECK_EQUAL(formatDouble(3), "3");
	BOOST_CHECK_EQUAL(formatFloat(0.1f), "0.1");
	BOOST_CHECK_EQUAL(formatFloat(-0.25f), "-0.25");
	BOOST_CHECK_EQUAL(formatFloat(16777216.0f), "16777216");

	BOOST_CHECK_EQUAL(formatDecimal(12345, 2), "123.45");
	BOOST_CHECK_EQUAL(formatDecimal(-5, 2), "-0.05");
	BOOST_CHECK_EQUAL(formatDecimal(0, 3), "0.000");
	BOOST_CHECK_EQUAL(formatDecimal(7, 0), "7");
	BOOST_CHECK_EQUAL(formatDecimal(7, -2), "700");
	BOOST_CHECK_EQUAL(formatDecimal(INT64_MIN, 0), "-9223372036854775808");
	BOOST_CHECK_EQUAL(formatDecimal(INT64_MIN, 19), "-0.9223372036854775808");

	BOOST_CHECK_EQUAL(formatBigDecimal(std::string("\x30\x39", 2), 2), "123.45");
	BOOST_CHECK_EQUAL(formatBigDecimal(std::string("\xff\xfb", 2), 2), "-0.05");
	BOOST_CHECK_EQUAL(formatBigDecimal(std::string("\xff", 1), 0), "-1");
	BOOST_CHECK_EQUAL(formatBigDecimal(std::string("\x80", 1), 1), "-12.8");
	BOOST_CHECK_EQUAL(formatBigDecimal(std::string("\x00", 1), 2), "0.00");
	BOOST_CHECK_EQUAL(formatBigDecimal("", 0), "0");
	// 2^127 - 1 and -2^127: DECIMAL(38).
	BOOST_CHECK_EQUAL(formatBigDecimal(std::string("\x7f") + std::string(15, '\xff'), 0),
		"170141183460469231731687303715884105727");
	BOOST_CHECK_EQUAL(formatBigDecimal(std::string("\x80") + std::string(15, '\x00'), 38),
		"-1.70141183460469231731687303715884105728");
}

BOOST_AUTO_TEST_SUITE_END()	// HqRowsTests

BOOST_AUTO_TEST_SUITE_END()	// HqRowsSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
