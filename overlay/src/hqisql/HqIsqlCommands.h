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
 *  hqisql: the isql frontend commands, for the syntax highlighter.
 */

#ifndef FB_HQISQL_HQ_ISQL_COMMANDS_H
#define FB_HQISQL_HQ_ISQL_COMMANDS_H

#include <string>
#include <vector>

namespace Hq {

// The commands isql runs itself instead of sending them to the server:
// SHOW, HELP, INPUT, OUTPUT, EDIT, SHELL, EXIT, ... and SET with one of the
// isql options (SET STATS ON, SET TERM ^, SET BLOB ALL). The words and the
// abbreviations are those of isql (FrontOptions and SetOptions in isql.epp,
// ShowOptions in show.epp): "IN", "OUT", "SET STAT", "SHOW TABLE". The
// hqisql commands PREPARE, EXEC SQL, DEALLOCATE and IMPORT INTO count too.
class IsqlCommands
{
public:
	// What an hqisql command expects at the word being typed.
	enum Slot
	{
		SLOT_NONE,		// nothing special: SQL completion
		SLOT_TABLE,		// IMPORT INTO |
		SLOT_CSV,		// IMPORT INTO t FROM |, EXEC SQL name FROM |
		SLOT_OPTION,	// ... FROM CSV 'f' OPTIONS (|, ... OPTIONS (a = 1, |
		SLOT_PREPARED	// EXEC SQL |, DEALLOCATE |
	};

	// tokens: the statement before the word being typed - the words in upper
	// case, the quoted names and the operators as written, without the
	// strings and numbers.
	static Slot completionSlot(const std::vector<std::string>& tokens);

	// One flag per word of a statement: true when the word belongs to the
	// isql command - the command itself, its SET option or SHOW object, a
	// switch after them (ON, OFF, ALL, DIALECT). words are the words of
	// the statement in order and in upper case, without the strings,
	// numbers and operators between them. An SQL statement gets all flags
	// false; so do SET TRANSACTION, SET GENERATOR, SET ROLE, SET TIME ZONE
	// and the other SET statements isql passes to the server.
	static std::vector<bool> commandWords(const std::vector<std::string>& words);
};

}	// namespace Hq

#endif	// FB_HQISQL_HQ_ISQL_COMMANDS_H
