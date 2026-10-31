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
 *  Tests of the highlighter color scheme (Hq::ColorScheme).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqColors.h"
#include <string>
#include <vector>

using Hq::ColorScheme;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqColorsSuite)


BOOST_AUTO_TEST_SUITE(HqColorsTests)

// The code of a color text, or ~0u when it is not a color.
static unsigned codeOf(const char* spec)
{
	unsigned code;
	return ColorScheme::parseColor(spec, &code) ? code : ~0u;
}

BOOST_AUTO_TEST_CASE(DefaultSchemeTest)
{
	const ColorScheme s;
	BOOST_TEST(s.color(ColorScheme::COMMAND) == 11u);		// yellow
	BOOST_TEST(s.color(ColorScheme::RESERVED) == 12u);		// brightblue
	BOOST_TEST(s.color(ColorScheme::KEYWORD) == 4u);		// blue
	BOOST_TEST(s.color(ColorScheme::STRING) == 10u);		// brightgreen
	BOOST_TEST(s.color(ColorScheme::COMMENT) == 8u);		// gray
	BOOST_TEST(s.color(ColorScheme::TERMINATOR) == 9u);		// brightred
	BOOST_TEST(s.color(ColorScheme::HINT) == 8u);
	BOOST_TEST(s.color(ColorScheme::COMPLETION) == 12u);
}

BOOST_AUTO_TEST_CASE(ParseColorTest)
{
	BOOST_TEST(codeOf("yellow") == 11u);
	BOOST_TEST(codeOf("BrightBlue") == 12u);
	BOOST_TEST(codeOf("  white ") == 15u);
	BOOST_TEST(codeOf("default") == ColorScheme::DEFAULT_COLOR);
	BOOST_TEST(codeOf("red bold") == (1u | ColorScheme::BOLD));
	BOOST_TEST(codeOf("underline red") == (1u | ColorScheme::UNDERLINE));
	BOOST_TEST(codeOf("bold") == (ColorScheme::DEFAULT_COLOR | ColorScheme::BOLD));
	BOOST_TEST(codeOf("rgb(0,5,0)") == 16u + 30u);
	BOOST_TEST(codeOf("rgb(5, 5, 5)") == 16u + 215u);
	BOOST_TEST(codeOf("gray(0)") == 232u);
	BOOST_TEST(codeOf("grey(23)") == 255u);
	BOOST_TEST(codeOf("213") == 213u);
	BOOST_TEST(codeOf("gray on black") == (8u | ColorScheme::BACKGROUND_SET));
	BOOST_TEST(codeOf("white on rgb(0,0,5)") == (15u | ColorScheme::BACKGROUND_SET | (21u << 8)));

	// Not colors.
	BOOST_TEST(codeOf("") == ~0u);
	BOOST_TEST(codeOf("pink") == ~0u);
	BOOST_TEST(codeOf("red green") == ~0u);
	BOOST_TEST(codeOf("rgb(6,0,0)") == ~0u);
	BOOST_TEST(codeOf("rgb(1,2)") == ~0u);
	BOOST_TEST(codeOf("gray(24)") == ~0u);
	BOOST_TEST(codeOf("256") == ~0u);
	BOOST_TEST(codeOf("red on") == ~0u);
	BOOST_TEST(codeOf("red on default") == ~0u);
	BOOST_TEST(codeOf("red italic") == ~0u);
}

BOOST_AUTO_TEST_CASE(ParseSchemeTest)
{
	ColorScheme s;
	std::vector<std::string> errors;

	const unsigned bad = s.parse(
		"# a scheme\r\n"
		"command = brightred bold   # trailing comment\n"
		"Reserved: rgb(5,0,5)\n"
		"\n"
		"string=default\n"
		"hint = gray(12)\n", &errors);

	BOOST_TEST(bad == 0u);
	BOOST_TEST(errors.empty());
	BOOST_TEST(s.color(ColorScheme::COMMAND) == (9u | ColorScheme::BOLD));
	BOOST_TEST(s.color(ColorScheme::RESERVED) == 16u + 180u + 5u);
	BOOST_TEST(s.color(ColorScheme::STRING) == ColorScheme::DEFAULT_COLOR);
	BOOST_TEST(s.color(ColorScheme::HINT) == 244u);
	// Untouched elements keep the built-in color.
	BOOST_TEST(s.color(ColorScheme::NUMBER) == 5u);
}

BOOST_AUTO_TEST_CASE(ParseErrorsTest)
{
	ColorScheme s;
	std::vector<std::string> errors;

	const unsigned bad = s.parse(
		"command = yellow\n"
		"strings = green\n"			// line 2: unknown element
		"number = pink\n"			// line 3: unknown color
		"just words\n"				// line 4: no separator
		"comment = white\n", &errors);

	BOOST_TEST(bad == 3u);
	BOOST_TEST(errors.size() == 3u);
	BOOST_TEST(errors[0] == "line 2: unknown element 'strings'");
	BOOST_TEST(errors[1] == "line 3: unknown color 'pink'");
	BOOST_TEST(errors[2] == "line 4: expected <element> = <color>");

	// The valid lines took effect, the invalid ones changed nothing.
	BOOST_TEST(s.color(ColorScheme::COMMAND) == 11u);
	BOOST_TEST(s.color(ColorScheme::COMMENT) == 15u);
	BOOST_TEST(s.color(ColorScheme::NUMBER) == 5u);
	BOOST_TEST(s.color(ColorScheme::STRING) == 10u);
}

BOOST_AUTO_TEST_CASE(ElementNamesTest)
{
	// Every element has a name, and the name finds the element back.
	for (unsigned i = 0; i < ColorScheme::ELEMENT_COUNT; ++i)
	{
		const ColorScheme::Element e = (ColorScheme::Element) i;
		ColorScheme::Element found;
		BOOST_TEST(ColorScheme::findElement(ColorScheme::elementName(e), &found));
		BOOST_TEST(found == e);
	}

	ColorScheme::Element e;
	BOOST_TEST(ColorScheme::findElement("COMMAND", &e));
	BOOST_TEST(e == ColorScheme::COMMAND);
	BOOST_TEST(!ColorScheme::findElement("colour", &e));
}

BOOST_AUTO_TEST_SUITE_END()	// HqColorsTests


BOOST_AUTO_TEST_SUITE_END()	// HqColorsSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
