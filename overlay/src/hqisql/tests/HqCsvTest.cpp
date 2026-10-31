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
 *  Tests of the CSV reader of EXEC SQL ... FROM CSV and IMPORT INTO
 *  (Hq::Csv).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqCsv.h"
#include <string.h>
#include <algorithm>
#include <memory>
#include <string>

using namespace Hq::Csv;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqCsvSuite)


BOOST_AUTO_TEST_SUITE(HqCsvTests)

// A source that returns the text in chunks of the given size.
static Source stringSource(const std::string& text, size_t chunk)
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
static std::string readAll(const std::string& text, size_t chunk, const Options& options = Options())
{
	Reader reader(stringSource(text, chunk), options);
	Record record;
	std::string result;
	Reader::Result r;

	while ((r = reader.next(record)) == Reader::RECORD)
	{
		for (size_t i = 0; i < record.size(); ++i)
		{
			if (i)
				result += '|';

			result += record[i].null ? "<null>" : record[i].value;
		}

		result += "@" + std::to_string(reader.line()) + "\n";
	}

	if (r == Reader::FAILED)
		result += "!" + std::to_string(reader.line()) + ": " + reader.error() + "\n";

	return result;
}

// The same result with every chunk size: 1, 2, 3 and the whole text.
static void check(const std::string& text, const std::string& expected, const Options& options = Options())
{
	for (size_t chunk : {size_t(1), size_t(2), size_t(3), size_t(1 << 20)})
		BOOST_CHECK_EQUAL(readAll(text, chunk, options), expected);
}

BOOST_AUTO_TEST_CASE(PlainTest)
{
	check("1,abc,2.5\n2,def,3\n", "1|abc|2.5@1\n2|def|3@2\n");

	// The last line without a line end, CRLF and CR line ends.
	check("a,b\r\nc,d\re,f", "a|b@1\nc|d@2\ne|f@3\n");

	// Blanks are part of the value.
	check(" a , b ", " a | b @1\n");

	// One field.
	check("x\ny\n", "x@1\ny@2\n");

	check("", "");
}

BOOST_AUTO_TEST_CASE(QuotesTest)
{
	check("\"a,b\",\"say \"\"hi\"\"\",c\n", "a,b|say \"hi\"|c@1\n");

	// Line breaks inside quotes: the line of the record is its first line.
	check("1,\"two\nlines\",x\n2,\"crlf\r\nhere\",y\n3,z,w\n",
		"1|two\nlines|x@1\n2|crlf\r\nhere|y@3\n3|z|w@5\n");

	// A quote inside an unquoted field is a plain character.
	check("5\" disk,ok\n", "5\" disk|ok@1\n");

	// Blanks after the closing quote are ignored.
	check("\"a\"  ,b\n", "a|b@1\n");

	// Only a quoted delimiter and a quoted quote.
	check("\",\",\"\"\"\"\n", ",|\"@1\n");
}

BOOST_AUTO_TEST_CASE(NullTest)
{
	// Empty unquoted is NULL, "" is an empty string.
	check(",\"\",x,\n", "<null>||x|<null>@1\n");

	Options options;
	options.nullText = "\\N";
	check("\\N,,\"\\N\"\n", "<null>||\\N@1\n", options);

	options.nullText = "NULL";
	check("NULL,null,\"NULL\"\n", "<null>|null|NULL@1\n", options);
}

BOOST_AUTO_TEST_CASE(EmptyLinesTest)
{
	check("\n\na,b\n\r\n\nc,d\n\n", "a|b@3\nc|d@6\n");
}

BOOST_AUTO_TEST_CASE(OptionsTest)
{
	Options options;
	options.delimiter = ';';
	check("1;\"a;b\";2,5\n", "1|a;b|2,5@1\n", options);

	options.delimiter = '\t';
	check("1\t\"a\tb\"\t \n", "1|a\tb| @1\n", options);

	options.delimiter = ',';
	options.quote = '\'';
	check("'it''s',\"x\"\n", "it's|\"x\"@1\n", options);
}

BOOST_AUTO_TEST_CASE(BomTest)
{
	for (size_t chunk : {size_t(1), size_t(2), size_t(1 << 20)})
	{
		Reader reader(stringSource("\xEF\xBB\xBF" "a,b\n", chunk), Options());
		Record record;
		BOOST_REQUIRE(reader.next(record) == Reader::RECORD);
		BOOST_CHECK(reader.hasBom());
		BOOST_REQUIRE_EQUAL(record.size(), 2u);
		BOOST_CHECK_EQUAL(record[0].value, "a");
	}

	Reader reader(stringSource("\xEF\xBB" "a\n", 1), Options());
	Record record;
	BOOST_REQUIRE(reader.next(record) == Reader::RECORD);
	BOOST_CHECK(!reader.hasBom());
	BOOST_CHECK_EQUAL(record[0].value, "\xEF\xBB" "a");
}

BOOST_AUTO_TEST_CASE(RawTest)
{
	Reader reader(stringSource("1,\"a\nb\"\r\n2,x", 2), Options());
	Record record;
	BOOST_REQUIRE(reader.next(record) == Reader::RECORD);
	BOOST_CHECK_EQUAL(reader.raw(), "1,\"a\nb\"");
	BOOST_REQUIRE(reader.next(record) == Reader::RECORD);
	BOOST_CHECK_EQUAL(reader.raw(), "2,x");
	BOOST_CHECK(reader.next(record) == Reader::END);
}

BOOST_AUTO_TEST_CASE(ErrorTest)
{
	check("a,b\n\"open,c\nd,e\n",
		"a|b@1\n!2: the quote is not closed at the end of the file\n");

	check("a\n\"x\"y,z\n",
		"a@1\n!2: a character after the closing quote of field 1 (a quote inside a quoted field must be doubled)\n");
}

BOOST_AUTO_TEST_CASE(LongFieldTest)
{
	const std::string big(200000, 'q');
	const std::string text = "1,\"" + big + "\",2\n3," + big + "\n";
	Reader reader(stringSource(text, 4096), Options());
	Record record;

	BOOST_REQUIRE(reader.next(record) == Reader::RECORD);
	BOOST_REQUIRE_EQUAL(record.size(), 3u);
	BOOST_CHECK(record[1].value == big);

	BOOST_REQUIRE(reader.next(record) == Reader::RECORD);
	BOOST_CHECK(record[1].value == big);
	BOOST_CHECK_EQUAL(reader.line(), 2u);
}

BOOST_AUTO_TEST_SUITE_END()	// HqCsvTests

BOOST_AUTO_TEST_SUITE_END()	// HqCsvSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
