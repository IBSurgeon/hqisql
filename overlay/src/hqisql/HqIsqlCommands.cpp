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

#include "firebird.h"
#include "HqIsqlCommands.h"
#include "HqImportCommand.h"
#include <string.h>

namespace Hq {

namespace {

// A word isql accepts in full or shortened to at least abbr letters
// (abbr 0: in full only) - the rule of OptionsBase::getCommand().
struct Word
{
	const char* text;
	size_t abbr;
};

// What may follow a SET option.
enum SetArg
{
	ARG_FREE,		// a column, a charset, a number, a terminator: no switch
	ARG_ON_OFF,		// a toggle: ON, OFF or nothing
	ARG_BLOB,		// SET BLOB [ALL | OFF | <n>]
	ARG_DIALECT,	// SET SQL DIALECT <n>
	ARG_EXEC_PATH	// SET EXEC_PATH_DISPLAY OFF | BLR
};

struct SetOption
{
	Word word;
	SetArg arg;
};

// The first word of a statement (FrontOptions in isql.epp). CREATE, DROP and
// CONNECT are frontend options there too, but SQL for the highlighter.
const Word COMMANDS[] =
{
	{"SHOW", 0}, {"ADD", 0}, {"COPY", 0}, {"BLOBVIEW", 0}, {"BLOBDUMP", 0},
	{"OUTPUT", 3}, {"SHELL", 0}, {"SET", 0}, {"EDIT", 0}, {"INPUT", 2},
	{"QUIT", 0}, {"EXIT", 0}, {"HELP", 0}
};

// The SET options isql runs itself (SetOptions in isql.epp). TRANSACTION,
// GENERATOR, STATISTICS, ROLE, TRUSTED and DECFLOAT are left out: isql
// passes them to the server, like any SET statement it does not know.
const SetOption SET_OPTIONS[] =
{
	{{"STATS", 4}, ARG_ON_OFF},
	{{"COUNT", 0}, ARG_ON_OFF},
	{{"LIST", 0}, ARG_ON_OFF},
	{{"PLAN", 0}, ARG_ON_OFF},
	{{"PLANONLY", 0}, ARG_ON_OFF},
	{{"EXPLAIN", 0}, ARG_ON_OFF},
	{{"BLOBDISPLAY", 4}, ARG_BLOB},
	{{"ECHO", 0}, ARG_ON_OFF},
	{{"AUTODDL", 4}, ARG_ON_OFF},
	{{"WIDTH", 0}, ARG_FREE},
	{{"TERMINATOR", 4}, ARG_FREE},
	{{"NAMES", 0}, ARG_FREE},
	{{"TIME", 0}, ARG_ON_OFF},
	{{"SQLDA_DISPLAY", 0}, ARG_ON_OFF},
	{{"EXEC_PATH_DISPLAY", 0}, ARG_EXEC_PATH},
	{{"SQL", 0}, ARG_DIALECT},
	{{"WARNINGS", 7}, ARG_ON_OFF},
	{{"WNG", 0}, ARG_ON_OFF},
	{{"HEADING", 0}, ARG_ON_OFF},
	{{"BAIL", 0}, ARG_ON_OFF},
	{{"BULK_INSERT", 0}, ARG_FREE},
	{{"ROWCOUNT", 0}, ARG_FREE},
	{{"MAXROWS", 0}, ARG_FREE},
	{{"LOCAL_TIMEOUT", 0}, ARG_FREE},
	{{"KEEP_TRAN_PARAMS", 9}, ARG_ON_OFF},
	{{"PER_TABLE_STATS", 7}, ARG_ON_OFF},
	{{"WIRE_STATS", 4}, ARG_ON_OFF},
	{{"PAGELENGTH", 3}, ARG_FREE},		// hqisql only
	{{"PREVIEW", 4}, ARG_ON_OFF}		// hqisql only
};

// The SHOW objects (ShowOptions in show.epp), VERSION and SQL (DIALECT).
const Word SHOW_OBJECTS[] =
{
	{"ROLES", 4}, {"TABLES", 5}, {"VIEWS", 4}, {"SYSTEM", 3}, {"INDEXES", 3},
	{"INDICES", 0}, {"DOMAINS", 6}, {"EXCEPTIONS", 5}, {"FILTERS", 6},
	{"FUNCTIONS", 4}, {"GENERATORS", 3}, {"SEQUENCES", 3}, {"GRANTS", 5},
	{"PROCEDURES", 4}, {"TRIGGERS", 4}, {"CHECKS", 5}, {"DB", 0},
	{"DATABASE", 0}, {"COMMENTS", 7}, {"DEPENDENCY", 5}, {"DEPENDENCIES", 5},
	{"COLLATES", 7}, {"COLLATIONS", 9}, {"SECCLASSES", 6}, {"USERS", 0},
	{"PACKAGES", 4}, {"SCHEMAS", 4}, {"MAPPING", 3}, {"PUBLICATIONS", 3},
	{"WIRE_STATISTICS", 9}, {"WIRE_STATS", 10}, {"VERSION", 0}, {"VER", 0},
	{"SQL", 0}
};

bool matches(const std::string& word, const Word& w)
{
	if (w.abbr == 0)
		return word == w.text;

	return word.size() >= w.abbr && word.size() <= strlen(w.text) &&
		strncmp(word.c_str(), w.text, word.size()) == 0;
}

template <size_t N>
bool matchesAny(const std::string& word, const Word (&list)[N])
{
	for (size_t i = 0; i < N; ++i)
	{
		if (matches(word, list[i]))
			return true;
	}

	return false;
}

const SetOption* findSetOption(const std::string& word)
{
	for (const SetOption& o : SET_OPTIONS)
	{
		if (matches(word, o.word))
			return &o;
	}

	return NULL;
}

bool isSetSwitch(const SetOption& option, const std::string& word)
{
	switch (option.arg)
	{
		case ARG_ON_OFF:
			return word == "ON" || word == "OFF";

		case ARG_BLOB:
			return word == "ALL" || word == "OFF";

		case ARG_DIALECT:
			return word == "DIALECT";

		case ARG_EXEC_PATH:
			return word == "OFF" || word == "BLR";

		default:
			return false;
	}
}

bool isOptionName(const std::string& word)
{
	for (const std::string& name : Import::optionNames())
	{
		if (word == name)
			return true;
	}

	return false;
}

bool isFormatName(const std::string& word)
{
	for (const std::string& name : Import::formatNames())
	{
		if (word == name)
			return true;
	}

	return false;
}

// The words from FROM on: FROM <format> 'file' OPTIONS (name = value, ...).
void markCsvTail(const std::vector<std::string>& words, size_t from, std::vector<bool>& flags)
{
	if (from >= words.size() || words[from] != "FROM")
		return;

	flags[from] = true;

	if (from + 1 >= words.size() || !isFormatName(words[from + 1]))
		return;

	flags[from + 1] = true;

	if (from + 2 >= words.size() || words[from + 2] != "OPTIONS")
		return;

	flags[from + 2] = true;

	for (size_t i = from + 3; i < words.size(); ++i)
		flags[i] = isOptionName(words[i]);
}

// PREPARE name AS <statement>, DEALLOCATE name, EXEC SQL name USING [BATCH]
// ... | FROM <format> ..., IMPORT INTO table [(columns)] FROM <format> ... False when
// the statement is none of them.
bool markHqCommand(const std::vector<std::string>& words, std::vector<bool>& flags)
{
	const std::string& first = words[0];

	if (first == "PREPARE")
	{
		flags[0] = true;

		if (words.size() > 2)
			flags[2] = words[2] == "AS";
	}
	else if (first == "DEALLOCATE")
		flags[0] = true;
	else if (first == "EXEC" && (words.size() == 1 || words[1] == "SQL"))
	{
		flags[0] = true;

		if (words.size() > 1)
			flags[1] = true;

		if (words.size() > 3 && words[3] == "USING")
		{
			flags[3] = true;

			if (words.size() > 4)
				flags[4] = words[4] == "BATCH";
		}
		else
			markCsvTail(words, 3, flags);
	}
	else if (first == "IMPORT")
	{
		flags[0] = true;

		if (words.size() > 1 && words[1] == "INTO")
		{
			flags[1] = true;

			for (size_t i = 3; i < words.size(); ++i)
			{
				if (words[i] == "FROM")
				{
					markCsvTail(words, i, flags);
					break;
				}
			}
		}
	}
	else
		return false;

	return true;
}

}	// anonymous namespace

IsqlCommands::Slot IsqlCommands::completionSlot(const std::vector<std::string>& tokens)
{
	const size_t n = tokens.size();

	if (n == 0)
		return SLOT_NONE;

	const bool exec = n >= 2 && tokens[0] == "EXEC" && tokens[1] == "SQL";
	const bool import = n >= 2 && tokens[0] == "IMPORT" && tokens[1] == "INTO";

	if ((n == 2 && exec) || (n == 1 && tokens[0] == "DEALLOCATE"))
		return SLOT_PREPARED;

	if (n == 2 && import)
		return SLOT_TABLE;

	if (!exec && !import)
		return SLOT_NONE;

	if (tokens[n - 1] == "FROM" && (import || n == 4))
		return SLOT_CSV;

	// An option name: right after "OPTIONS (" or after a comma there.
	if (tokens[n - 1] == "(" || tokens[n - 1] == ",")
	{
		for (size_t i = n - 1; i >= 2; --i)
		{
			if (tokens[i] == "(" && tokens[i - 1] == "OPTIONS")
				return isFormatName(tokens[i - 2]) ? SLOT_OPTION : SLOT_NONE;

			if (tokens[i] == ")")
				break;
		}
	}

	return SLOT_NONE;
}

std::vector<bool> IsqlCommands::commandWords(const std::vector<std::string>& words)
{
	std::vector<bool> flags(words.size(), false);

	if (words.empty() || markHqCommand(words, flags) || !matchesAny(words[0], COMMANDS))
		return flags;

	if (words[0] == "SET")
	{
		// SET alone displays the settings and SET <isql option> is run by
		// isql; any other SET statement goes to the server, SET TIME ZONE
		// included.
		if (words.size() == 1)
		{
			flags[0] = true;
			return flags;
		}

		const SetOption* option = findSetOption(words[1]);

		if (!option || (words[1] == "TIME" && words.size() > 2 && words[2] == "ZONE"))
			return flags;

		flags[0] = flags[1] = true;

		if (words.size() > 2)
			flags[2] = isSetSwitch(*option, words[2]);

		return flags;
	}

	flags[0] = true;

	if (words[0] == "SHOW")
	{
		if (words.size() > 1 && matchesAny(words[1], SHOW_OBJECTS))
		{
			flags[1] = true;

			// SHOW SYSTEM <objects>, SHOW SQL DIALECT.
			if (words.size() > 2)
			{
				flags[2] = (matches(words[1], {"SYSTEM", 3}) && matchesAny(words[2], SHOW_OBJECTS)) ||
					(words[1] == "SQL" && words[2] == "DIALECT");
			}
		}
	}
	else if (words[0] == "HELP")
	{
		if (words.size() > 1)
			flags[1] = words[1] == "SET" || words[1] == "SHOW";
	}

	return flags;
}

}	// namespace Hq
