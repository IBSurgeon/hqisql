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
 *  Tests of the PREPARE / EXEC SQL parameter parsing and substitution
 *  (Hq::Params).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqParams.h"
#include <string>

using namespace Hq::Params;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqParamsSuite)


BOOST_AUTO_TEST_SUITE(HqParamsTests)

static std::vector<Tuple> parseOk(const char* text, bool batch, const char* term = ";")
{
	std::vector<Tuple> tuples;
	std::string error;
	BOOST_CHECK_MESSAGE(parseTuples(text, term, batch, tuples, error), text << ": " << error);
	return tuples;
}

static std::string parseError(const char* text, bool batch)
{
	std::vector<Tuple> tuples;
	std::string error;
	BOOST_CHECK_MESSAGE(!parseTuples(text, ";", batch, tuples, error), text);
	return error;
}

BOOST_AUTO_TEST_CASE(SingleTupleTest)
{
	std::vector<Tuple> t = parseOk(" (1, 'a,b', (2 + 3) * 4) ", false);
	BOOST_REQUIRE_EQUAL(t.size(), 1u);
	BOOST_REQUIRE_EQUAL(t[0].size(), 3u);
	BOOST_CHECK_EQUAL(t[0][0], "1");
	BOOST_CHECK_EQUAL(t[0][1], "'a,b'");
	BOOST_CHECK_EQUAL(t[0][2], "(2 + 3) * 4");

	// Quotes, parentheses and commas inside strings and quoted names.
	t = parseOk("('it''s (x)', q'{a ) , b}', \"Col,)\")", false);
	BOOST_REQUIRE_EQUAL(t[0].size(), 3u);
	BOOST_CHECK_EQUAL(t[0][0], "'it''s (x)'");
	BOOST_CHECK_EQUAL(t[0][1], "q'{a ) , b}'");
	BOOST_CHECK_EQUAL(t[0][2], "\"Col,)\"");

	// "()" is the list of a statement without marks.
	t = parseOk("()", false);
	BOOST_REQUIRE_EQUAL(t.size(), 1u);
	BOOST_CHECK(t[0].empty());
}

BOOST_AUTO_TEST_CASE(CommentsTest)
{
	// Comments are removed from the values: a line comment would comment
	// out the rest of the line in the statement.
	std::vector<Tuple> t = parseOk("(1 -- one\n, 2 /* two ) */) -- end", false);
	BOOST_REQUIRE_EQUAL(t[0].size(), 2u);
	BOOST_CHECK_EQUAL(t[0][0], "1");
	BOOST_CHECK_EQUAL(t[0][1], "2");

	BOOST_CHECK(onlyBlanks("  -- c\n /* d */ ", ";"));
	BOOST_CHECK(!onlyBlanks(" x ", ";"));
}

BOOST_AUTO_TEST_CASE(BatchTest)
{
	const std::vector<Tuple> t = parseOk("(1, 'a') ,\n /* next */ (2, 'b'),(3,'c')", true);
	BOOST_REQUIRE_EQUAL(t.size(), 3u);
	BOOST_CHECK_EQUAL(t[1][0], "2");
	BOOST_CHECK_EQUAL(t[2][1], "'c'");
}

BOOST_AUTO_TEST_CASE(ErrorsTest)
{
	BOOST_CHECK_EQUAL(parseError("", false), "parameter list expected");
	BOOST_CHECK_EQUAL(parseError("1, 2", false), "parameter list expected at \"1, 2\"");
	BOOST_CHECK_EQUAL(parseError("(1, 2", false), "\")\" expected at the end of the parameter list");
	BOOST_CHECK_EQUAL(parseError("(1, , 2)", false), "empty value in the parameter list");
	BOOST_CHECK_EQUAL(parseError("(1) x", false), "unexpected text after the parameter list: \"x\"");

	// A second tuple without BATCH, a missing tuple after a comma.
	BOOST_CHECK_EQUAL(parseError("(1), (2)", false), "unexpected text after the parameter list: \", (2)\"");
	BOOST_CHECK_EQUAL(parseError("(1), ", true), "parameter tuple expected");
	BOOST_CHECK_EQUAL(parseError("(1) (2)", true), "unexpected text after the parameter list: \"(2)\"");
}

BOOST_AUTO_TEST_CASE(SubstituteTest)
{
	const char* const sql =
		"insert into t (a, b, c) values (?, '?', ?) -- ?\n /* ? */ returning \"?\", ?";

	BOOST_CHECK_EQUAL(countMarks(sql, ";"), 3u);
	BOOST_CHECK_EQUAL(substitute(sql, ";", {"1", "'x'", "3"}),
		"insert into t (a, b, c) values (1, '?', 'x') -- ?\n /* ? */ returning \"?\", 3");

	// Named parameters are not marks; a PSQL body with another terminator.
	BOOST_CHECK_EQUAL(countMarks("select :a, ? from rdb$database", ";"), 1u);
	BOOST_CHECK_EQUAL(countMarks("execute block as declare x int = ?; begin end", "^"), 1u);
}

BOOST_AUTO_TEST_SUITE_END()	// HqParamsTests

BOOST_AUTO_TEST_SUITE_END()	// HqParamsSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
