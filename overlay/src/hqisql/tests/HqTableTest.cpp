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
 *  Tests of the compact table of SET PREVIEW (Hq::CompactTable).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqTable.h"
#include <initializer_list>
#include <string>
#include <vector>

using Hq::CompactTable;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqTableSuite)


BOOST_AUTO_TEST_SUITE(HqTableTests)

// Adds a row of cells as isql prints them (padded, a blank after each).
static void addRow(CompactTable& t, std::initializer_list<const char*> cells)
{
	for (const char* c : cells)
		t.addCell(c, c + strlen(c));

	t.endRow();
}

// The lines joined with '|' - easier to compare.
static std::string joined(const std::vector<std::string>& lines)
{
	std::string s;

	for (const auto& l : lines)
		s += l + "|";

	return s;
}

BOOST_AUTO_TEST_CASE(CompactLayoutTest)
{
	// ID is numeric (padded on the left), NAME is text (padded on the right).
	CompactTable t(false);
	t.setTitles({"ID", "NAME"});
	addRow(t, {"           1 ", "a                    "});
	addRow(t, {"          22 ", "bcd                  "});

	BOOST_TEST(t.rowCount() == 2u);
	BOOST_TEST(joined(t.format(0)) == "ID NAME|== ====| 1 a|22 bcd|");
}

BOOST_AUTO_TEST_CASE(TitleWiderThanValuesTest)
{
	CompactTable t(false);
	t.setTitles({"LONG_TITLE", "X"});
	addRow(t, {"  7 ", "abc "});

	// The title sets the width of the first column, the value of the second;
	// a numeric title is aligned to the right.
	BOOST_TEST(joined(t.format(0)) == "LONG_TITLE X|========== ===|         7 abc|");
}

BOOST_AUTO_TEST_CASE(NoTitlesAndNullTest)
{
	CompactTable t(false);
	addRow(t, {"      <null> ", "<null>     "});
	addRow(t, {"           5 ", "text       "});

	BOOST_TEST(joined(t.format(0)) == "<null> <null>|     5 text|");
}

BOOST_AUTO_TEST_CASE(WidthCutTest)
{
	CompactTable t(false);
	t.setTitles({"A", "B", "C", "D"});
	addRow(t, {"aaaaaaaaaa ", "bbbbbbbbbb ", "cccccccccc ", "dddddddddd "});

	// Width 30: lines of at most 29 characters. A and B fit (10 + 11),
	// C is cut to 29 - 21 - 1 = 7 characters, D is left out.
	std::vector<std::string> dropped;
	const auto lines = t.format(30, &dropped);
	BOOST_TEST(joined(lines) == "A          B          C|========== ========== =======|aaaaaaaaaa bbbbbbbbbb ccccccc|");
	BOOST_TEST(dropped.size() == 1u);
	BOOST_TEST(dropped[0] == "D");

	// Width 26: 4 characters would remain for C - too few, C is left out too.
	dropped.clear();
	BOOST_TEST(joined(t.format(26, &dropped)) == "A          B|========== ==========|aaaaaaaaaa bbbbbbbbbb|");
	BOOST_TEST(dropped.size() == 2u);

	// A single column wider than the screen is cut, never left out.
	CompactTable one(false);
	one.setTitles({"T"});
	addRow(one, {"0123456789 "});
	BOOST_TEST(joined(one.format(6)) == "T|=====|01234|");
}

BOOST_AUTO_TEST_CASE(Utf8WidthTest)
{
	// Cyrillic: 2 bytes per character, widths in characters.
	CompactTable t(true);
	t.setTitles({"\xD0\x98\xD0\x9C\xD0\xAF", "N"});	// "ИМЯ"
	addRow(t, {"\xD0\xB0\xD0\xB1    ", " 1 "});				// "аб" padded to 6

	const auto lines = t.format(0);
	BOOST_TEST(lines[0] == "\xD0\x98\xD0\x9C\xD0\xAF N");
	BOOST_TEST(lines[1] == "=== =");
	BOOST_TEST(lines[2] == "\xD0\xB0\xD0\xB1  1");

	// A cut falls between characters.
	CompactTable c(true);
	c.setTitles({"\xD0\x90\xD0\x91\xD0\x92\xD0\x93\xD0\x94\xD0\x95"});	// "АБВГДЕ"
	addRow(c, {"\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3\xD0\xB4\xD0\xB5 "});
	BOOST_TEST(joined(c.format(5)) == "\xD0\x90\xD0\x91\xD0\x92\xD0\x93|====|\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3|");
}

BOOST_AUTO_TEST_SUITE_END()	// HqTableTests


BOOST_AUTO_TEST_SUITE_END()	// HqTableSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
