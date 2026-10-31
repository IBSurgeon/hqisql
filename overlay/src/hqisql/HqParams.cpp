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
#include "../hqisql/HqParams.h"
#include "../hqisql/HqLexer.h"

#include <algorithm>

namespace
{

enum ByteKind
{
	PLAIN,
	QUOTED,		// a string or a quoted name
	COMMENT
};

// Marks the bytes of strings, quoted names and comments.
std::vector<ByteKind> classify(std::string_view text, std::string_view term)
{
	std::vector<ByteKind> kinds(text.size(), PLAIN);

	for (const Hq::Lexer::HighlightRange& r : Hq::Lexer::highlight(text, term))
	{
		ByteKind kind;

		switch (r.type)
		{
			case Hq::Lexer::Token::TYPE_STRING:
			case Hq::Lexer::Token::TYPE_META_STRING:
				kind = QUOTED;
				break;

			case Hq::Lexer::Token::TYPE_COMMENT:
				kind = COMMENT;
				break;

			default:
				continue;
		}

		std::fill(kinds.begin() + r.start, kinds.begin() + r.start + r.length, kind);
	}

	return kinds;
}

bool isBlank(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string trimmed(const std::string& s)
{
	size_t b = 0, e = s.size();

	while (b < e && isBlank(s[b]))
		++b;

	while (e > b && isBlank(s[e - 1]))
		--e;

	return s.substr(b, e - b);
}

// Skips blanks and comments.
size_t skipBlanks(std::string_view text, const std::vector<ByteKind>& kinds, size_t pos)
{
	while (pos < text.size() && (kinds[pos] == COMMENT || (kinds[pos] == PLAIN && isBlank(text[pos]))))
		++pos;

	return pos;
}

bool isPlain(std::string_view text, const std::vector<ByteKind>& kinds, size_t pos, char c)
{
	return pos < text.size() && kinds[pos] == PLAIN && text[pos] == c;
}

// The start of the text at pos for an error message: up to the line end,
// at most 30 bytes, not cut inside a UTF-8 character.
std::string snippet(std::string_view text, size_t pos)
{
	size_t end = std::min(text.size(), pos + 30);
	const size_t lineEnd = text.find_first_of("\r\n", pos);

	if (lineEnd != std::string_view::npos && lineEnd < end)
		end = lineEnd;
	else if (end < text.size())
	{
		while (end > pos && (text[end] & 0xC0) == 0x80)
			--end;
	}

	return std::string(text.substr(pos, end - pos));
}

bool addValue(Hq::Params::Tuple& values, const std::string& value, std::string& error)
{
	const std::string v(trimmed(value));

	if (v.empty())
	{
		error = "empty value in the parameter list";
		return false;
	}

	values.push_back(v);
	return true;
}

std::vector<size_t> markPositions(std::string_view statement, std::string_view term)
{
	std::vector<size_t> marks;

	for (const Hq::Lexer::HighlightRange& r : Hq::Lexer::highlight(statement, term))
	{
		if (r.type == Hq::Lexer::Token::TYPE_PARAM && r.length == 1 && statement[r.start] == '?')
			marks.push_back(r.start);
	}

	return marks;
}

}	// anonymous namespace


namespace Hq {
namespace Params {

bool parseTuples(std::string_view text, std::string_view term, bool batch,
	std::vector<Tuple>& tuples, std::string& error)
{
	const std::vector<ByteKind> kinds(classify(text, term));
	size_t pos = skipBlanks(text, kinds, 0);

	for (;;)
	{
		if (!isPlain(text, kinds, pos, '('))
		{
			error = batch ? "parameter tuple expected" : "parameter list expected";

			if (pos < text.size())
				error += " at \"" + snippet(text, pos) + "\"";

			return false;
		}

		Tuple values;
		std::string value;
		int depth = 1;
		bool closed = false;

		for (++pos; pos < text.size(); ++pos)
		{
			const char c = text[pos];

			if (kinds[pos] == COMMENT)
			{
				// A comment separates words like a blank. A line comment
				// must not stay in the value: it would comment out the rest
				// of the line in the statement.
				if (kinds[pos - 1] != COMMENT)
					value += ' ';

				continue;
			}

			if (kinds[pos] == PLAIN)
			{
				if (c == '(')
					++depth;
				else if (c == ')' && --depth == 0)
				{
					closed = true;
					++pos;
					break;
				}
				else if (c == ',' && depth == 1)
				{
					if (!addValue(values, value, error))
						return false;

					value.clear();
					continue;
				}
			}

			value += c;
		}

		if (!closed)
		{
			error = "\")\" expected at the end of the parameter list";
			return false;
		}

		// "()" is the list of a statement without marks.
		if (!values.empty() || !trimmed(value).empty())
		{
			if (!addValue(values, value, error))
				return false;
		}

		tuples.push_back(values);
		pos = skipBlanks(text, kinds, pos);

		if (!batch || !isPlain(text, kinds, pos, ','))
			break;

		pos = skipBlanks(text, kinds, pos + 1);
	}

	if (pos < text.size())
	{
		error = "unexpected text after the parameter list: \"" + snippet(text, pos) + "\"";
		return false;
	}

	return true;
}

bool onlyBlanks(std::string_view text, std::string_view term)
{
	return skipBlanks(text, classify(text, term), 0) == text.size();
}

unsigned countMarks(std::string_view statement, std::string_view term)
{
	return (unsigned) markPositions(statement, term).size();
}

std::string substitute(std::string_view statement, std::string_view term, const Tuple& values)
{
	const std::vector<size_t> marks(markPositions(statement, term));
	std::string result;
	size_t pos = 0;

	for (size_t i = 0; i < marks.size() && i < values.size(); ++i)
	{
		result.append(statement.substr(pos, marks[i] - pos));
		result += values[i];
		pos = marks[i] + 1;
	}

	result.append(statement.substr(pos));
	return result;
}

}	// namespace Params
}	// namespace Hq
