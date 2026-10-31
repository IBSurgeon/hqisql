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
 *  Tests of the hqisql highlighting scanner (Hq::Lexer::highlight) and the
 *  keyword dictionary (HqKeywords).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqLexer.h"
#include "../hqisql/HqKeywords.h"
#include <string>

using Hq::Lexer;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqLexerSuite)


BOOST_AUTO_TEST_SUITE(HqLexerTests)

// Returns true and the type of the token covering byte offset "offset".
static bool typeAt(const std::string& buffer, const char* term, std::string::size_type offset,
	Lexer::Token::Type& type)
{
	const auto ranges = Lexer::highlight(buffer, term);

	for (const auto& r : ranges)
	{
		if (r.start <= offset && offset < r.start + r.length)
		{
			type = r.type;
			return true;
		}
	}

	return false;
}

static bool isTypeAt(const std::string& buffer, const char* term, std::string::size_type offset,
	Lexer::Token::Type expected)
{
	Lexer::Token::Type type;
	return typeAt(buffer, term, offset, type) && type == expected;
}

BOOST_AUTO_TEST_CASE(HighlightClassificationTest)
{
	// Keywords vs identifiers vs numbers vs strings vs parameters.
	BOOST_TEST(isTypeAt("select fld from tab", ";", 0, Lexer::Token::TYPE_WORD));
	BOOST_TEST(isTypeAt("select fld from tab", ";", 8, Lexer::Token::TYPE_WORD));
	BOOST_TEST(isTypeAt("select 123.45 x", ";", 7, Lexer::Token::TYPE_NUMBER));
	BOOST_TEST(isTypeAt("select 0x1F x", ";", 7, Lexer::Token::TYPE_NUMBER));
	BOOST_TEST(isTypeAt("select 'abc' x", ";", 7, Lexer::Token::TYPE_STRING));
	BOOST_TEST(isTypeAt("select \"quoted id\"", ";", 7, Lexer::Token::TYPE_META_STRING));
	BOOST_TEST(isTypeAt("select :param x", ";", 7, Lexer::Token::TYPE_PARAM));
	BOOST_TEST(isTypeAt("select ? x", ";", 7, Lexer::Token::TYPE_PARAM));
	BOOST_TEST(isTypeAt("select 1;", ";", 8, Lexer::Token::TYPE_TERMINATOR));
	BOOST_TEST(isTypeAt("select 1; select 2", ";", 10, Lexer::Token::TYPE_WORD));

	// Parameters right after punctuation must not be swallowed by an
	// operator run ("VALUES (?, ?)" - three parameters).
	{
		int params = 0;

		for (const auto& r : Lexer::highlight("values (?, ?, :x)", ";"))
		{
			if (r.type == Lexer::Token::TYPE_PARAM)
				++params;
		}

		BOOST_TEST(params == 3);
	}

	// Comments (line and block).
	BOOST_TEST(isTypeAt("select -- text\n1", ";", 11, Lexer::Token::TYPE_COMMENT));
	BOOST_TEST(isTypeAt("select /* text */ 1", ";", 11, Lexer::Token::TYPE_COMMENT));

	// Q-strings.
	BOOST_TEST(isTypeAt("select q'{abc}' x", ";", 7, Lexer::Token::TYPE_STRING));

	// Custom multi-character terminator.
	BOOST_TEST(isTypeAt("select 1 ++ select 2", "++", 9, Lexer::Token::TYPE_TERMINATOR));
}

BOOST_AUTO_TEST_CASE(HighlightSetTermTest)
{
	// The new terminator is honored in the same buffer, after the SET TERM,
	// and the old one loses its meaning - as in isql itself.
	const std::string s = "select 1; SET TERM ^; select 2^ select 3";
	BOOST_TEST(isTypeAt(s, ";", 8, Lexer::Token::TYPE_TERMINATOR));		// ';' (default term)
	BOOST_TEST(isTypeAt(s, ";", 19, Lexer::Token::TYPE_TERMINATOR));	// '^' (new term)
	BOOST_TEST(isTypeAt(s, ";", 20, Lexer::Token::TYPE_OPERATOR));		// ';' is not a terminator anymore
	BOOST_TEST(isTypeAt(s, ";", 22, Lexer::Token::TYPE_WORD));			// 'select' after '^'
	BOOST_TEST(isTypeAt(s, ";", 30, Lexer::Token::TYPE_TERMINATOR));	// '^'
}

BOOST_AUTO_TEST_CASE(HighlightTolerantTest)
{
	// Unterminated constructs never throw and color the rest of the buffer.
	bool incomplete = false;
	const auto ranges = Lexer::highlight("select 'abc", ";", &incomplete);
	BOOST_TEST(incomplete);
	BOOST_TEST(ranges.back().type == Lexer::Token::TYPE_STRING);

	incomplete = false;
	const auto ranges2 = Lexer::highlight("select /* abc", ";", &incomplete);
	BOOST_TEST(incomplete);
	BOOST_TEST(ranges2.back().type == Lexer::Token::TYPE_COMMENT);

	incomplete = false;
	Lexer::highlight("select q'{abc", ";", &incomplete);
	BOOST_TEST(incomplete);

	incomplete = false;
	Lexer::highlight("select 'closed' -- ok", ";", &incomplete);
	BOOST_TEST(!incomplete);
}

BOOST_AUTO_TEST_CASE(HighlightFirstInStatementTest)
{
	const auto ranges = Lexer::highlight("show tables; select 1", ";");

	// "SHOW" is the first token of its statement, "TABLES" is not.
	BOOST_TEST(ranges[0].type == Lexer::Token::TYPE_WORD);
	BOOST_TEST(ranges[0].firstInStatement);
	BOOST_TEST(ranges[1].type == Lexer::Token::TYPE_WORD);
	BOOST_TEST(!ranges[1].firstInStatement);
}

BOOST_AUTO_TEST_CASE(KeywordsTest)
{
	BOOST_TEST(Hq::isKeyword("SELECT", 6));
	BOOST_TEST(Hq::isKeyword("select", 6));	// lookup is case-insensitive
	BOOST_TEST(Hq::isKeyword("VARCHAR", 7));
	BOOST_TEST(Hq::isKeyword("NOT_A_KEYWORD_XYZ", 17) == false);
	BOOST_TEST(Hq::isKeyword("", 0) == false);
	BOOST_TEST(Hq::keywordCount() > 400u);
	BOOST_TEST(Hq::keyword(0) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()	// HqLexerTests


BOOST_AUTO_TEST_SUITE_END()	// HqLexerSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
