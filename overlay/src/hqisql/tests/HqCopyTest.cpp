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
 *  Tests of the PostgreSQL COPY text reader (Hq::Copy).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqCopy.h"
#include <string.h>
#include <algorithm>
#include <memory>
#include <string>

using namespace Hq;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqCopySuite)


BOOST_AUTO_TEST_SUITE(HqCopyTests)

// A source that returns the text in chunks of the given size.
static Rows::Source stringSource(const std::string& text, size_t chunk)
{
	auto offset = std::make_shared<size_t>(0);

	return [text, chunk, offset](char* buffer, size_t size) {
		const size_t n = std::min(std::min(size, chunk), text.size() - *offset);
		memcpy(buffer, text.data() + *offset, n);
		*offset += n;
		return n;
	};
}

// The records as "v1|v2|..." with NULL as <null>; each record is followed by
// "@line". A format error ends the list with "!line: error".
static std::string readAll(const std::string& text, size_t chunk, char delimiter, const std::string& nullText)
{
	Copy::Reader reader(stringSource(text, chunk), delimiter, nullText);
	Rows::Record record;
	std::string result;
	Rows::Reader::Result r;

	while ((r = reader.next(record)) == Rows::Reader::RECORD)
	{
		for (size_t i = 0; i < record.size(); ++i)
		{
			if (i)
				result += '|';

			result += record[i].null ? "<null>" : record[i].value;
		}

		result += "@" + std::to_string(reader.line()) + "\n";
	}

	if (r == Rows::Reader::FAILED)
		result += "!" + std::to_string(reader.line()) + ": " + reader.error() + "\n";

	return result;
}

// The same result with every chunk size: 1, 2, 3 and the whole text.
static void check(const std::string& text, const std::string& expected, char delimiter = '\t',
	const std::string& nullText = "\\N")
{
	for (size_t chunk : {size_t(1), size_t(2), size_t(3), size_t(1 << 20)})
		BOOST_CHECK_EQUAL(readAll(text, chunk, delimiter, nullText), expected);
}

BOOST_AUTO_TEST_CASE(PlainTest)
{
	check("1\tabc\t2.5\n2\tdef\t3\n", "1|abc|2.5@1\n2|def|3@2\n");

	// CRLF, the last line without a line end.
	check("a\tb\r\nc\td", "a|b@1\nc|d@2\n");

	// \N is NULL, an empty field is an empty string.
	check("\\N\t\tx\n", "<null>||x@1\n");

	// An empty line is one empty field.
	check("a\n\nb\n", "a@1\n@2\nb@3\n");

	// Another delimiter and NULL text.
	check("1|NULL|\n", "1|<null>|@1\n", '|', "NULL");
	check("1,,x\n", "1|<null>|x@1\n", ',', "");

	// A BOM is skipped; the reader tells it.
	check("\xEF\xBB\xBFid\tname\n", "id|name@1\n");
}

BOOST_AUTO_TEST_CASE(EscapeTest)
{
	check("a\\tb\\nc\\\\d\n", "a\tb\nc\\d@1\n");
	check("\\b\\f\\r\\v\n", "\b\f\r\v@1\n");

	// Octal and hex codes.
	check("\\101\\0623\\x41\\x4a\\x4G\\xz\n", "A23AJ\x04Gxz@1\n");

	// An escaped delimiter is in the value; \N is NULL only as a whole field.
	check("a\\\tb\tc\n", "a\tb|c@1\n");
	check("\\\\N\tx\\N\n", "\\N|xN@1\n");

	// Any other character after a backslash is itself.
	check("\\q\\.\n", "q.@1\n");

	check("abc\\\n", "!1: a backslash at the end of the line\n");
}

BOOST_AUTO_TEST_CASE(EndTest)
{
	// \. ends the data.
	check("1\n\\.\n2\n", "1@1\n");
	check("1\r\n\\.\r\n", "1@1\n");

	// Not the binary format.
	const std::string binary("PGCOPY\n\377\r\n\0\0\0\0\0\0\0\0\0", 19);
	check(binary, "!1: the binary COPY format is not supported, use the text format\n");

	check("", "");
	check("\n", "@1\n");
}

BOOST_AUTO_TEST_CASE(BomTest)
{
	Copy::Reader reader(stringSource("\xEF\xBB\xBF" "a\n", 1), '\t', "\\N");
	Rows::Record record;
	BOOST_CHECK(reader.next(record) == Rows::Reader::RECORD);
	BOOST_CHECK(reader.hasBom());
	BOOST_CHECK_EQUAL(reader.raw(record), "a");
	BOOST_CHECK_EQUAL(reader.unit(), "line");
	BOOST_CHECK(reader.next(record) == Rows::Reader::END);
}

BOOST_AUTO_TEST_SUITE_END()	// HqCopyTests

BOOST_AUTO_TEST_SUITE_END()	// HqCopySuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
