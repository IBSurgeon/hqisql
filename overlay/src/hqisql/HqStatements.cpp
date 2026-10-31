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
 */

#include "firebird.h"
#include <stdarg.h>
#include <stdio.h>
#include "../isql/isql.h"
#include "../hqisql/HqStatements.h"
#include "../hqisql/HqParams.h"
#include "../hqisql/HqImport.h"

#include <map>
#include <string>
#include <vector>

namespace
{

typedef std::map<std::string, std::string> PreparedMap;
PreparedMap g_prepared;

bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isIdentChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		   c == '_' || c == '$' || (c & 0x80);
}

std::string upper(std::string s)
{
	for (auto& c : s)
		c = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;

	return s;
}

std::string trim(const std::string& s)
{
	size_t b = 0, e = s.size();

	while (b < e && isSpace(s[b]))
		++b;

	while (e > b && isSpace(s[e - 1]))
		--e;

	return s.substr(b, e - b);
}

const char* skipBlanks(const char* p)
{
	while (*p && isSpace(*p))
		++p;

	return p;
}

// Read an identifier word; false (p untouched) when there is none.
bool readWord(const char*& p, std::string& word)
{
	p = skipBlanks(p);

	if (!isIdentChar(*p))
		return false;

	const char* start = p;

	while (isIdentChar(*p))
		++p;

	word.assign(start, p - start);
	return true;
}

// Case-insensitive word match; p is advanced only on a match.
bool matchWord(const char*& p, const char* word)
{
	const char* save = p;
	std::string w;

	if (!readWord(p, w))
		return false;

	if (upper(w) == word)
		return true;

	p = save;
	return false;
}

processing_state error(const char* format, ...)
{
	TEXT buf[BUFFER_LENGTH512];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	STDERROUT(buf, false);
	Hq::Isql::printErrorLocation();
	return ps_ERR;
}

}	// anonymous namespace


namespace Hq {

std::vector<std::string> preparedNames()
{
	std::vector<std::string> names;

	for (const PreparedMap::value_type& p : g_prepared)
		names.push_back(p.first);

	return names;
}

processing_state statementCommand(Firebird::string& statement, ExecuteFunc execute)
{
	// EXEC SQL ... FROM <format> and IMPORT INTO.
	Import::Command importCommand;
	std::string importError;

	switch (Import::parse(statement.c_str(), importCommand, importError))
	{
		case Import::PARSE_ERROR:
			return error("%s: %s%s", importCommand.kind == Import::Command::EXEC ? "EXEC SQL" : "IMPORT",
				importError.c_str(), NEWLINE);

		case Import::PARSED:
			if (importCommand.kind == Import::Command::EXEC)
			{
				const PreparedMap::iterator it = g_prepared.find(importCommand.name);

				if (it == g_prepared.end())
				{
					return error("EXEC SQL: statement \"%s\" is not prepared%s",
						importCommand.name.c_str(), NEWLINE);
				}

				return runImport(importCommand, it->second);
			}

			return runImport(importCommand, std::string());

		case Import::NOT_IMPORT:
			break;
	}

	const char* p = statement.c_str();

	if (matchWord(p, "PREPARE"))
	{
		std::string name;

		if (!readWord(p, name))
			return error("PREPARE: statement name expected%s", NEWLINE);

		if (!matchWord(p, "AS"))
			return error("PREPARE: AS expected%s", NEWLINE);

		const std::string body = trim(p);

		if (body.empty())
			return error("PREPARE: statement text expected%s", NEWLINE);

		g_prepared[upper(name)] = body;
		return SKIP;
	}

	if (matchWord(p, "DEALLOCATE"))
	{
		std::string name;

		if (!readWord(p, name))
			return error("DEALLOCATE: statement name expected%s", NEWLINE);

		if (!Params::onlyBlanks(p, isqlGlob.global_Term))
			return error("DEALLOCATE: unexpected text after the statement name%s", NEWLINE);

		if (g_prepared.erase(upper(name)) == 0)
			return error("DEALLOCATE: statement \"%s\" is not prepared%s", name.c_str(), NEWLINE);

		return SKIP;
	}

	if (matchWord(p, "EXEC") && matchWord(p, "SQL"))
	{
		std::string name;

		if (!readWord(p, name))
			return error("EXEC SQL: statement name expected%s", NEWLINE);

		const PreparedMap::iterator it = g_prepared.find(upper(name));

		if (it == g_prepared.end())
			return error("EXEC SQL: statement \"%s\" is not prepared%s", name.c_str(), NEWLINE);

		if (!matchWord(p, "USING"))
			return error("EXEC SQL: USING or FROM CSV | COPY | PARQUET | AVRO expected%s", NEWLINE);

		const bool batch = matchWord(p, "BATCH");
		const std::string_view term(isqlGlob.global_Term);
		std::vector<Params::Tuple> tuples;
		std::string message;

		if (!Params::parseTuples(p, term, batch, tuples, message))
			return error("EXEC SQL: %s%s", message.c_str(), NEWLINE);

		// All tuples are checked before the first one is executed.
		const unsigned expected = Params::countMarks(it->second, term);

		for (size_t i = 0; i < tuples.size(); ++i)
		{
			if (tuples[i].size() != expected)
			{
				if (batch)
				{
					return error("EXEC SQL: tuple %u: expected %u value(s), got %u%s",
						unsigned(i + 1), expected, unsigned(tuples[i].size()), NEWLINE);
				}

				return error("EXEC SQL: expected %u value(s), got %u%s",
					expected, unsigned(tuples[i].size()), NEWLINE);
			}
		}

		if (batch)
		{
			// Each statement goes the same way as a statement typed in isql.
			for (const Params::Tuple& values : tuples)
			{
				const std::string sql(Params::substitute(it->second, term, values));

				if (execute(sql.c_str()) == ps_ERR)
					return ps_ERR;
			}

			return SKIP;
		}

		// Rewrite and let the regular isql pipeline execute it.
		statement = Params::substitute(it->second, term, tuples[0]).c_str();
		return CONT;
	}

	return CONT;
}

}	// namespace Hq
