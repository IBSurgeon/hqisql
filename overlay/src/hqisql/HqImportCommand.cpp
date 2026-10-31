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
#include "../hqisql/HqImportCommand.h"

#include <algorithm>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

namespace
{

using namespace Hq::Import;

struct Token
{
	enum Type
	{
		END,
		WORD,		// a regular identifier or a keyword
		NAME,		// a quoted name: "..."
		STRING,		// '...'
		NUMBER,
		CHAR		// any other character
	};

	Type type = END;
	std::string text;		// WORD: upper case; NAME, STRING: without quotes
	std::string spelled;	// as written
};

bool isBlank(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isWordChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		c == '_' || c == '$' || (c & 0x80);
}

bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

std::string upper(std::string_view s)
{
	std::string result(s);

	for (auto& c : result)
		c = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;

	return result;
}

class Lexer
{
public:
	explicit Lexer(std::string_view aText)
		: text(aText)
	{
	}

	// The next token; false with the message in error on a bad token.
	bool next(Token& token, std::string& error)
	{
		skipBlanks();
		token = Token();

		if (pos >= text.size())
			return true;

		const size_t start = pos;
		const char c = text[pos];

		if (c == '\'' || c == '"')
		{
			std::string value;

			for (++pos;; ++pos)
			{
				if (pos >= text.size())
				{
					error = c == '\'' ? "the string is not closed" : "the quoted name is not closed";
					return false;
				}

				if (text[pos] == c)
				{
					if (pos + 1 < text.size() && text[pos + 1] == c)
						++pos;
					else
						break;
				}

				value += text[pos];
			}

			++pos;
			token.type = c == '\'' ? Token::STRING : Token::NAME;
			token.text = value;
		}
		else if (isDigit(c) || (c == '-' && pos + 1 < text.size() && isDigit(text[pos + 1])))
		{
			for (++pos; pos < text.size() && isWordChar(text[pos]); ++pos)
				;

			token.type = Token::NUMBER;
			token.text = text.substr(start, pos - start);
		}
		else if (isWordChar(c))
		{
			while (pos < text.size() && isWordChar(text[pos]))
				++pos;

			token.type = Token::WORD;
			token.text = upper(text.substr(start, pos - start));
		}
		else
		{
			++pos;
			token.type = Token::CHAR;
			token.text = std::string(1, c);
		}

		token.spelled = text.substr(start, pos - start);
		return true;
	}

private:
	void skipBlanks()
	{
		for (;;)
		{
			while (pos < text.size() && isBlank(text[pos]))
				++pos;

			if (text.substr(pos, 2) == "--")
			{
				while (pos < text.size() && text[pos] != '\n')
					++pos;
			}
			else if (text.substr(pos, 2) == "/*")
			{
				const size_t end = text.find("*/", pos + 2);
				pos = end == std::string_view::npos ? text.size() : end + 2;
			}
			else
				break;
		}
	}

	std::string_view text;
	size_t pos = 0;
};

// A parser over the tokens with one token of look-ahead.
class Parser
{
public:
	explicit Parser(std::string_view text)
		: lexer(text)
	{
	}

	bool start(std::string& error)
	{
		return lexer.next(current, error);
	}

	const Token& peek() const
	{
		return current;
	}

	bool advance(std::string& error)
	{
		return lexer.next(current, error);
	}

	bool isWord(const char* word) const
	{
		return current.type == Token::WORD && current.text == word;
	}

	bool isChar(char c) const
	{
		return current.type == Token::CHAR && current.text[0] == c;
	}

	// The current token as written, for an error message.
	std::string near() const
	{
		return current.type == Token::END ? "the end of the statement" : "\"" + current.spelled + "\"";
	}

private:
	Lexer lexer;
	Token current;
};

bool fail(std::string& error, const std::string& message)
{
	if (error.empty())
		error = message;

	return false;
}

bool expectWord(Parser& p, const char* word, std::string& error)
{
	if (!p.isWord(word))
		return fail(error, std::string(word) + " expected instead of " + p.near());

	return p.advance(error);
}

// A table or column name: a word or a quoted name. spelled is the name as
// it goes into SQL, stored the name in the metadata.
bool readName(Parser& p, const char* what, std::string& spelled, std::string* stored, std::string& error)
{
	const Token& t = p.peek();

	if (t.type != Token::WORD && t.type != Token::NAME)
		return fail(error, std::string(what) + " expected instead of " + p.near());

	spelled = t.spelled;

	if (stored)
		*stored = t.text;

	return p.advance(error);
}

bool readUnsigned(const Token& t, unsigned& value)
{
	if (t.type != Token::NUMBER || t.text[0] == '-')
		return false;

	char* end = NULL;
	errno = 0;
	const unsigned long v = strtoul(t.text.c_str(), &end, 10);

	if (*end || errno || v > UINT_MAX)
		return false;

	value = (unsigned) v;
	return true;
}

// A one-character option: a string of one byte; '\t' is a tab.
bool readChar(const Token& t, char& value)
{
	if (t.type != Token::STRING)
		return false;

	if (t.text == "\\t")
		value = '\t';
	else if (t.text.size() == 1)
		value = t.text[0];
	else
		return false;

	return true;
}

// The options of a format: all for CSV, no QUOTE for COPY, only the
// options of the import itself for PARQUET and AVRO.
bool optionAllowed(Command::Format format, const std::string& name)
{
	switch (format)
	{
		case Command::CSV:
			return true;

		case Command::COPY:
			return name != "QUOTE";

		default:
			return name == "SKIP" || name == "BATCH" || name == "COMMIT" || name == "ERRORS";
	}
}

bool readOption(Parser& p, Command::Format format, Options& options, std::vector<std::string>& seen,
	std::string& error)
{
	const Token name = p.peek();

	if (name.type != Token::WORD)
		return fail(error, "option name expected instead of " + p.near());

	const std::vector<std::string>& known = Hq::Import::optionNames();
	bool isKnown = false;

	for (const std::string& k : known)
		isKnown = isKnown || name.text == k;

	if (!isKnown)
	{
		std::string list;

		for (const std::string& k : known)
			list += std::string(list.empty() ? "" : ", ") + k;

		return fail(error, "unknown option \"" + name.spelled + "\" (known: " + list + ")");
	}

	if (!optionAllowed(format, name.text))
	{
		return fail(error, "option " + name.text + " is not for " +
			Hq::Import::formatNames()[format] + " files");
	}

	for (const std::string& s : seen)
	{
		if (s == name.text)
			return fail(error, "option " + name.text + " is given twice");
	}

	seen.push_back(name.text);

	if (!p.advance(error))
		return false;

	// HEADER alone is HEADER = TRUE.
	if (name.text == "HEADER" && (p.isChar(',') || p.isChar(')')))
	{
		options.header = true;
		return true;
	}

	if (!p.isChar('='))
		return fail(error, "\"=\" expected after " + name.text + " instead of " + p.near());

	if (!p.advance(error))
		return false;

	const Token value = p.peek();
	const std::string bad = "bad value " + p.near() + " of " + name.text + ": ";

	if (name.text == "DELIMITER" || name.text == "QUOTE")
	{
		char c;

		if (!readChar(value, c))
			return fail(error, bad + "one character in quotes expected, '\\t' is a tab");

		if (c == '\r' || c == '\n')
			return fail(error, bad + "a line break is not allowed");

		(name.text == "DELIMITER" ? options.csv.delimiter : options.csv.quote) = c;
	}
	else if (name.text == "HEADER")
	{
		if (value.type == Token::WORD && (value.text == "TRUE" || value.text == "FALSE"))
			options.header = value.text == "TRUE";
		else
			return fail(error, bad + "TRUE or FALSE expected");
	}
	else if (name.text == "NULL")
	{
		if (value.type != Token::STRING)
			return fail(error, bad + "a string expected");

		options.csv.nullText = value.text;
	}
	else if (name.text == "ENCODING")
	{
		if (value.type != Token::STRING && value.type != Token::WORD)
			return fail(error, bad + "a character set name expected");

		options.encoding = upper(value.text);
	}
	else if (name.text == "DECIMAL")
	{
		char c;

		if (!readChar(value, c) || (c != '.' && c != ','))
			return fail(error, bad + "'.' or ',' expected");

		options.decimal = c;
	}
	else
	{
		unsigned n;

		if (!readUnsigned(value, n))
			return fail(error, bad + "a number (0 or more) expected");

		if (name.text == "SKIP")
			options.skip = n;
		else if (name.text == "BATCH")
			options.batch = n;
		else if (name.text == "COMMIT")
			options.commit = n;
		else
			options.errors = n;
	}

	return p.advance(error);
}

// FROM format 'file' [OPTIONS (...)] up to the end of the statement.
bool readSource(Parser& p, Command& command, std::string& error)
{
	if (!expectWord(p, "FROM", error))
		return false;

	const std::vector<std::string>& formats = Hq::Import::formatNames();
	size_t format = 0;

	while (format < formats.size() && !p.isWord(formats[format].c_str()))
		++format;

	if (format == formats.size())
		return fail(error, "CSV, COPY, PARQUET or AVRO expected instead of " + p.near());

	command.format = Command::Format(format);

	if (!p.advance(error))
		return false;

	const Token file = p.peek();

	if ((file.type != Token::STRING && file.type != Token::NAME) || file.text.empty())
		return fail(error, "file name in quotes expected instead of " + p.near());

	command.file = file.text;

	if (!p.advance(error))
		return false;

	std::vector<std::string> seen;

	if (p.isWord("OPTIONS"))
	{
		if (!p.advance(error))
			return false;

		if (!p.isChar('('))
			return fail(error, "\"(\" expected after OPTIONS instead of " + p.near());

		if (!p.advance(error))
			return false;

		for (;;)
		{
			if (!readOption(p, command.format, command.options, seen, error))
				return false;

			if (p.isChar(')'))
				break;

			if (!p.isChar(','))
				return fail(error, "\",\" or \")\" expected instead of " + p.near());

			if (!p.advance(error))
				return false;
		}

		if (!p.advance(error))
			return false;
	}

	if (p.peek().type != Token::END)
		return fail(error, "unexpected " + p.near());

	Options& o = command.options;

	if (command.format == Command::COPY)
	{
		// The COPY defaults: a tab between the fields, \N is NULL.
		if (std::find(seen.begin(), seen.end(), "DELIMITER") == seen.end())
			o.csv.delimiter = '\t';

		if (std::find(seen.begin(), seen.end(), "NULL") == seen.end())
			o.csv.nullText = "\\N";

		if (o.csv.delimiter == '\\')
			return fail(error, "DELIMITER cannot be a backslash");
	}
	else if (command.format == Command::CSV && o.csv.delimiter == o.csv.quote)
		return fail(error, "DELIMITER and QUOTE must differ");

	return true;
}

}	// anonymous namespace


namespace Hq {
namespace Import {

ParseResult parse(std::string_view statement, Command& command, std::string& error)
{
	command = Command();
	error.clear();

	Parser p(statement);

	// A bad token before the command words: not ours.
	if (!p.start(error))
	{
		error.clear();
		return NOT_IMPORT;
	}

	if (p.isWord("EXEC"))
	{
		std::string ignored;

		if (!p.advance(ignored) || !p.isWord("SQL") || !p.advance(ignored) ||
			p.peek().type != Token::WORD)
		{
			return NOT_IMPORT;
		}

		command.kind = Command::EXEC;
		command.name = p.peek().text;

		if (!p.advance(ignored) || !p.isWord("FROM"))
			return NOT_IMPORT;

		return readSource(p, command, error) ? PARSED : PARSE_ERROR;
	}

	if (!p.isWord("IMPORT"))
		return NOT_IMPORT;

	command.kind = Command::IMPORT;

	if (!p.advance(error) || !expectWord(p, "INTO", error) ||
		!readName(p, "table name", command.table, &command.tableName, error))
	{
		return PARSE_ERROR;
	}

	if (p.isChar('('))
	{
		do
		{
			std::string column;

			if (!p.advance(error) || !readName(p, "column name", column, nullptr, error))
				return PARSE_ERROR;

			command.columns.push_back(column);
		} while (p.isChar(','));

		if (!p.isChar(')'))
		{
			fail(error, "\",\" or \")\" expected in the column list instead of " + p.near());
			return PARSE_ERROR;
		}

		if (!p.advance(error))
			return PARSE_ERROR;
	}

	return readSource(p, command, error) ? PARSED : PARSE_ERROR;
}

const std::vector<std::string>& optionNames()
{
	static const std::vector<std::string> names = {"DELIMITER", "QUOTE", "HEADER", "SKIP", "NULL",
		"ENCODING", "DECIMAL", "BATCH", "COMMIT", "ERRORS"};
	return names;
}

const std::vector<std::string>& formatNames()
{
	static const std::vector<std::string> names = {"CSV", "COPY", "PARQUET", "AVRO"};
	return names;
}

std::string columnName(std::string_view title)
{
	bool regular = !title.empty() && title.size() <= 63 &&
		((title[0] >= 'A' && title[0] <= 'Z') || (title[0] >= 'a' && title[0] <= 'z'));

	for (const char c : title)
		regular = regular && isWordChar(c) && !(c & 0x80);

	// A regular identifier is stored in upper case. It is quoted anyway, so
	// a title such as "date" or "user" is not taken for a keyword.
	const std::string name(regular ? upper(title) : std::string(title));
	std::string result("\"");

	for (const char c : name)
	{
		result += c;

		if (c == '"')
			result += '"';
	}

	return result + '"';
}

}	// namespace Import
}	// namespace Hq
