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
 *  Tests of the isql frontend command words used by the hqisql highlighter
 *  (Hq::IsqlCommands).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqIsqlCommands.h"
#include <initializer_list>
#include <string>
#include <vector>

using Hq::IsqlCommands;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqIsqlCommandsSuite)


BOOST_AUTO_TEST_SUITE(HqIsqlCommandsTests)

// The flags of the statement words as a string: 'c' for a word of an isql
// command, '.' for any other word. {"SET", "WIDTH", "COL"} -> "cc.".
static std::string flagsOf(std::initializer_list<const char*> words)
{
	const std::vector<bool> flags =
		IsqlCommands::commandWords(std::vector<std::string>(words.begin(), words.end()));
	std::string s;

	for (const bool f : flags)
		s += f ? 'c' : '.';

	return s;
}

BOOST_AUTO_TEST_CASE(FrontendCommandsTest)
{
	BOOST_TEST(flagsOf({"SHOW"}) == "c");
	BOOST_TEST(flagsOf({"HELP"}) == "c");
	BOOST_TEST(flagsOf({"EXIT"}) == "c");
	BOOST_TEST(flagsOf({"QUIT"}) == "c");
	BOOST_TEST(flagsOf({"EDIT"}) == "c");
	BOOST_TEST(flagsOf({"BLOBDUMP"}) == "c");
	BOOST_TEST(flagsOf({"SHELL", "DIR"}) == "c.");		// the arguments are not command words
	BOOST_TEST(flagsOf({"INPUT", "FILE"}) == "c.");
	BOOST_TEST(flagsOf({"ADD", "T1"}) == "c.");

	// The abbreviations isql accepts: IN, OUT. Shorter: not a command.
	BOOST_TEST(flagsOf({"IN", "FILE"}) == "c.");
	BOOST_TEST(flagsOf({"OUT"}) == "c");
	BOOST_TEST(flagsOf({"OU"}) == ".");

	// SQL statements; CREATE, DROP and CONNECT count as SQL.
	BOOST_TEST(flagsOf({"SELECT", "ON"}) == "..");
	BOOST_TEST(flagsOf({"CONNECT", "DB"}) == "..");
	BOOST_TEST(flagsOf({}) == "");
}

BOOST_AUTO_TEST_CASE(SetCommandsTest)
{
	BOOST_TEST(flagsOf({"SET"}) == "c");	// displays the settings
	BOOST_TEST(flagsOf({"SET", "STATS", "ON"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "STAT", "OFF"}) == "ccc");	// abbreviation
	BOOST_TEST(flagsOf({"SET", "STA"}) == "..");			// too short: goes to the server
	BOOST_TEST(flagsOf({"SET", "AUTODDL"}) == "cc");
	BOOST_TEST(flagsOf({"SET", "AUTO", "ON"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "BLOB", "ALL"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "BLOB", "OFF"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "SQL", "DIALECT"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "TIME", "ON"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "KEEP_TRAN", "ON"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "PER_TAB", "OFF"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "WIRE_STATS", "ON"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "EXEC_PATH_DISPLAY", "BLR"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "PAGELENGTH"}) == "cc");	// hqisql only
	BOOST_TEST(flagsOf({"SET", "PAG"}) == "cc");
	BOOST_TEST(flagsOf({"SET", "PA"}) == "..");
	BOOST_TEST(flagsOf({"SET", "PREVIEW", "ON"}) == "ccc");
	BOOST_TEST(flagsOf({"SET", "PREV", "OFF"}) == "ccc");

	// A free argument is not a command word, nor is a word that is not a switch.
	BOOST_TEST(flagsOf({"SET", "TERM", "GO"}) == "cc.");
	BOOST_TEST(flagsOf({"SET", "WIDTH", "COL"}) == "cc.");
	BOOST_TEST(flagsOf({"SET", "NAMES", "UTF8"}) == "cc.");
	BOOST_TEST(flagsOf({"SET", "MAXROWS"}) == "cc");
	BOOST_TEST(flagsOf({"SET", "HEADING", "MAYBE"}) == "cc.");
	BOOST_TEST(flagsOf({"SET", "WIDTH", "ON"}) == "cc.");

	// SET statements isql passes to the server are SQL.
	BOOST_TEST(flagsOf({"SET", "TRANSACTION", "READ", "ONLY"}) == "....");
	BOOST_TEST(flagsOf({"SET", "GENERATOR", "G", "TO"}) == "....");
	BOOST_TEST(flagsOf({"SET", "ROLE", "R"}) == "...");
	BOOST_TEST(flagsOf({"SET", "TIME", "ZONE"}) == "...");
	BOOST_TEST(flagsOf({"SET", "BIND", "OF"}) == "...");
}

BOOST_AUTO_TEST_CASE(ShowCommandsTest)
{
	BOOST_TEST(flagsOf({"SHOW", "TABLES"}) == "cc");
	BOOST_TEST(flagsOf({"SHOW", "TABLE", "EMPLOYEE"}) == "cc.");
	BOOST_TEST(flagsOf({"SHOW", "TAB"}) == "c.");		// TABLES needs 5 letters
	BOOST_TEST(flagsOf({"SHOW", "GEN"}) == "cc");
	BOOST_TEST(flagsOf({"SHOW", "DB"}) == "cc");
	BOOST_TEST(flagsOf({"SHOW", "VERSION"}) == "cc");
	BOOST_TEST(flagsOf({"SHOW", "VER"}) == "cc");
	BOOST_TEST(flagsOf({"SHOW", "SQL", "DIALECT"}) == "ccc");
	BOOST_TEST(flagsOf({"SHOW", "SYSTEM", "TABLES"}) == "ccc");
	BOOST_TEST(flagsOf({"SHOW", "SYS"}) == "cc");
	BOOST_TEST(flagsOf({"SHOW", "GRANTS", "EMPLOYEE"}) == "cc.");
	BOOST_TEST(flagsOf({"SHOW", "WIRE_STATS"}) == "cc");
	BOOST_TEST(flagsOf({"SHOW", "NOTHING"}) == "c.");

	BOOST_TEST(flagsOf({"HELP", "SET"}) == "cc");
	BOOST_TEST(flagsOf({"HELP", "SHOW"}) == "cc");
	BOOST_TEST(flagsOf({"HELP", "ME"}) == "c.");
}

BOOST_AUTO_TEST_CASE(HqCommandsTest)
{
	BOOST_TEST(flagsOf({"PREPARE", "INS", "AS", "INSERT", "INTO", "T"}) == "c.c...");
	BOOST_TEST(flagsOf({"PREPARE", "INS", "INSERT"}) == "c..");
	BOOST_TEST(flagsOf({"DEALLOCATE", "INS"}) == "c.");
	BOOST_TEST(flagsOf({"EXEC"}) == "c");
	BOOST_TEST(flagsOf({"EXEC", "SQL", "INS", "USING"}) == "cc.c");
	BOOST_TEST(flagsOf({"EXEC", "SQL", "INS", "USING", "BATCH", "NULL"}) == "cc.cc.");
	BOOST_TEST(flagsOf({"EXEC", "SQL", "INS", "USING", "CURRENT_DATE"}) == "cc.c.");
	BOOST_TEST(flagsOf({"EXEC", "SQL", "INS", "FROM", "CSV", "OPTIONS", "HEADER", "TRUE", "NULL"}) ==
		"cc.cccc.c");
	BOOST_TEST(flagsOf({"EXEC", "PROCEDURE"}) == "..");
	BOOST_TEST(flagsOf({"IMPORT"}) == "c");
	BOOST_TEST(flagsOf({"IMPORT", "INTO", "T", "FROM", "CSV"}) == "cc.cc");
	BOOST_TEST(flagsOf({"IMPORT", "INTO", "T", "ID", "FROM", "CSV", "OPTIONS", "ENCODING", "UTF8"}) ==
		"cc..cccc.");
	BOOST_TEST(flagsOf({"IMPORT", "INTO", "T", "FROM", "AVRO", "OPTIONS", "SKIP"}) == "cc.cccc");
	BOOST_TEST(flagsOf({"EXEC", "SQL", "INS", "FROM", "COPY"}) == "cc.cc");
	BOOST_TEST(flagsOf({"IMPORT", "INTO", "T", "FROM", "FILE"}) == "cc.c.");
	BOOST_TEST(flagsOf({"IMPORT", "T"}) == "c.");
}

// The completion slot after the tokens.
static IsqlCommands::Slot slotOf(std::initializer_list<const char*> tokens)
{
	return IsqlCommands::completionSlot(std::vector<std::string>(tokens.begin(), tokens.end()));
}

BOOST_AUTO_TEST_CASE(CompletionSlotTest)
{
	BOOST_TEST(slotOf({"IMPORT", "INTO"}) == IsqlCommands::SLOT_TABLE);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "FROM"}) == IsqlCommands::SLOT_CSV);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "(", "A", ",", "B", ")", "FROM"}) == IsqlCommands::SLOT_CSV);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "(", "A", ","}) == IsqlCommands::SLOT_NONE);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "FROM", "CSV", "OPTIONS", "("}) == IsqlCommands::SLOT_OPTION);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "FROM", "CSV", "OPTIONS", "(", "HEADER", ","}) ==
		IsqlCommands::SLOT_OPTION);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "FROM", "CSV", "OPTIONS", "(", "SKIP", "="}) ==
		IsqlCommands::SLOT_NONE);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "FROM", "PARQUET", "OPTIONS", "("}) == IsqlCommands::SLOT_OPTION);
	BOOST_TEST(slotOf({"IMPORT", "INTO", "T", "FROM", "JSON", "OPTIONS", "("}) == IsqlCommands::SLOT_NONE);
	BOOST_TEST(slotOf({"EXEC", "SQL"}) == IsqlCommands::SLOT_PREPARED);
	BOOST_TEST(slotOf({"DEALLOCATE"}) == IsqlCommands::SLOT_PREPARED);
	BOOST_TEST(slotOf({"EXEC", "SQL", "INS", "FROM"}) == IsqlCommands::SLOT_CSV);
	BOOST_TEST(slotOf({"EXEC", "SQL", "INS", "FROM", "CSV", "OPTIONS", "(", "BATCH", "=", ","}) ==
		IsqlCommands::SLOT_OPTION);
	BOOST_TEST(slotOf({"EXEC", "SQL", "INS", "USING", "(", "A", "FROM"}) == IsqlCommands::SLOT_NONE);
	BOOST_TEST(slotOf({"SELECT", "*", "FROM"}) == IsqlCommands::SLOT_NONE);
	BOOST_TEST(slotOf({"INSERT", "INTO"}) == IsqlCommands::SLOT_NONE);
	BOOST_TEST(slotOf({}) == IsqlCommands::SLOT_NONE);
}

BOOST_AUTO_TEST_SUITE_END()	// HqIsqlCommandsTests


BOOST_AUTO_TEST_SUITE_END()	// HqIsqlCommandsSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
