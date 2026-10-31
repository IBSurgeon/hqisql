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
#include "../hqisql/HqLexer.h"
#include <string>
#include <string.h>

// Keep in sync with MAXTERM_SIZE in isql.h; a local constant is used here to
// keep this file free of isql-wide dependencies.
static const size_t HL_MAX_TERM_SIZE = 32;

static inline bool fbIsspace(const char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

namespace
{

inline bool hlIsAlpha(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c & 0x80);
}

inline bool hlIsDigit(char c)
{
	return c >= '0' && c <= '9';
}

inline bool hlIsWordStart(char c)
{
	return hlIsAlpha(c) || c == '_' || c == '$';
}

inline bool hlIsWordChar(char c)
{
	return hlIsWordStart(c) || hlIsDigit(c);
}

inline bool hlIsSpace(char c)
{
	return fbIsspace(c);
}

inline char hlUpper(char c)
{
	return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
}

// Case-insensitive (ASCII) comparison of a buffer slice with a keyword.
bool hlWordIs(const std::string_view buffer, const size_t start, const size_t length, const char* word)
{
	if (strlen(word) != length)
		return false;

	for (size_t i = 0; i < length; ++i)
	{
		if (hlUpper(buffer[start + i]) != word[i])
			return false;
	}

	return true;
}

// Scan a quoted string starting at pos (which points at the opening quote).
// Advances pos past the closing quote, or to the end of the buffer.
// Returns false when the closing quote was not found.
bool hlScanQuoted(const std::string_view buffer, size_t& pos)
{
	const size_t start = pos;
	const char quote = buffer[pos++];

	while (pos < buffer.size())
	{
		if (buffer[pos] == quote)
		{
			if (pos + 1 < buffer.size() && buffer[pos + 1] == quote)
				pos += 2;
			else
			{
				++pos;
				return true;
			}
		}
		else
			++pos;
	}

	pos = start;
	return false;
}

// Scan a Q-string (q'{...}' and friends) starting at pos (which points at
// Q or q). Advances pos past the closing quote, or to the end of the buffer.
// Returns false when the string is not closed.
bool hlScanQString(const std::string_view buffer, size_t& pos)
{
	const size_t start = pos;

	if (pos + 3 >= buffer.size())
	{
		pos = buffer.size();
		return false;
	}

	char endChar;
	switch (buffer[pos + 2])
	{
		case '{':
			endChar = '}';
			break;
		case '[':
			endChar = ']';
			break;
		case '(':
			endChar = ')';
			break;
		case '<':
			endChar = '>';
			break;
		default:
			endChar = buffer[pos + 2];
	}

	pos += 3;

	while (pos + 1 < buffer.size())
	{
		if (buffer[pos] == endChar && buffer[pos + 1] == '\'')
		{
			pos += 2;
			return true;
		}

		++pos;
	}

	pos = start;
	return false;
}

// Skip whitespace and comments; returns false when an unterminated block
// comment is found (pos is advanced to the end in that case).
bool hlSkipSpacesAndComments(const std::string_view buffer, size_t& pos)
{
	while (pos < buffer.size())
	{
		if (hlIsSpace(buffer[pos]))
			++pos;
		else if (buffer[pos] == '-' && pos + 1 < buffer.size() && buffer[pos + 1] == '-')
		{
			while (pos < buffer.size() && buffer[pos] != '\n')
				++pos;
		}
		else if (buffer[pos] == '/' && pos + 1 < buffer.size() && buffer[pos + 1] == '*')
		{
			pos += 2;

			while (pos + 1 < buffer.size() &&
				   !(buffer[pos] == '*' && buffer[pos + 1] == '/'))
			{
				++pos;
			}

			if (pos + 1 >= buffer.size())
			{
				pos = buffer.size();
				return false;
			}

			pos += 2;
		}
		else
			break;
	}

	return true;
}

// Read the next word at pos (assumed to be a word start) and return its
// uppercased form.
std::string hlReadWord(const std::string_view buffer, size_t& pos)
{
	const size_t start = pos;

	while (pos < buffer.size() && hlIsWordChar(buffer[pos]))
		++pos;

	std::string word(buffer.substr(start, pos - start));
	for (auto& c : word)
		c = hlUpper(c);

	return word;
}

}	// anonymous namespace


namespace Hq {

std::vector<Lexer::HighlightRange> Lexer::highlight(std::string_view buffer,
	std::string_view term, bool* incompleteOut)
{
	std::vector<HighlightRange> ranges;
	bool incomplete = false;
	size_t pos = 0;
	const size_t size = buffer.size();
	std::string curTerm(term);
	bool firstInStatement = true;

	while (pos < size)
	{
		const char c = buffer[pos];

		if (hlIsSpace(c))
		{
			++pos;
			continue;
		}

		// Line comment.
		if (c == '-' && pos + 1 < size && buffer[pos + 1] == '-')
		{
			const size_t start = pos;

			while (pos < size && buffer[pos] != '\n')
				++pos;

			ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_COMMENT, firstInStatement});
			continue;
		}

		// Block comment.
		if (c == '/' && pos + 1 < size && buffer[pos + 1] == '*')
		{
			const size_t start = pos;
			pos += 2;

			while (pos + 1 < size && !(buffer[pos] == '*' && buffer[pos + 1] == '/'))
				++pos;

			if (pos + 1 >= size)
			{
				pos = size;
				incomplete = true;
			}
			else
				pos += 2;

			ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_COMMENT, firstInStatement});
			continue;
		}

		// Single/double quoted string or Q-string. An unterminated string is
		// common while the user is typing, so it is colored up to the end of
		// the buffer.
		if (c == '\'' || c == '"')
		{
			const size_t start = pos;
			const char quote = c;
			const bool closed = hlScanQuoted(buffer, pos);

			if (!closed)
			{
				pos = size;
				incomplete = true;
			}

			ranges.push_back({unsigned(start), unsigned(pos - start),
				quote == '\'' ? Token::TYPE_STRING : Token::TYPE_META_STRING, false});
			continue;
		}

		if ((c == 'Q' || c == 'q') && pos + 1 < size && buffer[pos + 1] == '\'')
		{
			const size_t start = pos;

			if (hlScanQString(buffer, pos))
			{
				ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_STRING, false});
				continue;
			}

			pos = size;
			incomplete = true;
			ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_STRING, false});
			continue;
		}

		// Statement terminator - the current term, including one set by
		// "SET TERM <x>" earlier in the buffer (the default ';' then loses
		// its terminator meaning, as in isql itself). A word-shaped
		// terminator must not match in the middle of a word.
		if (!curTerm.empty() && size - pos >= curTerm.size() &&
			memcmp(buffer.data() + pos, curTerm.data(), curTerm.size()) == 0 &&
			(!hlIsWordStart(curTerm[0]) || pos + curTerm.size() >= size ||
				!hlIsWordChar(buffer[pos + curTerm.size()])))
		{
			ranges.push_back({unsigned(pos), unsigned(curTerm.size()), Token::TYPE_TERMINATOR, false});
			pos += curTerm.size();
			firstInStatement = true;
			continue;
		}

		// Follow "SET TERM <new-term>" inside the buffer.
		if (firstInStatement && hlIsWordStart(c))
		{
			size_t scan = pos;
			std::string word = hlReadWord(buffer, scan);

			if (word == "SET")
			{
				if (hlSkipSpacesAndComments(buffer, scan))
				{
					if (scan < size && hlIsWordStart(buffer[scan]) && hlReadWord(buffer, scan) == "TERM")
					{
						size_t termScan = scan;

						if (hlSkipSpacesAndComments(buffer, termScan) && termScan < size)
						{
							std::string newTerm;

							if (buffer[termScan] == '\'' || buffer[termScan] == '"')
							{
								const size_t qStart = termScan;

								if (hlScanQuoted(buffer, termScan))
									newTerm.assign(buffer.substr(qStart, termScan - qStart));
							}
							else
							{
								const size_t termStart = termScan;

								while (termScan < size && !hlIsSpace(buffer[termScan]) && buffer[termScan] != ';')
									++termScan;

								newTerm.assign(buffer.substr(termStart, termScan - termStart));
							}

							if (!newTerm.empty() && newTerm.size() < HL_MAX_TERM_SIZE)
								curTerm = newTerm;
						}
					}
				}
			}
		}

		// Input parameter: ':' followed by an identifier character, or '?'.
		if (c == ':' && pos + 1 < size && (hlIsWordStart(buffer[pos + 1]) || hlIsDigit(buffer[pos + 1])))
		{
			const size_t start = pos++;
			const size_t wordStart = pos;

			while (pos < size && hlIsWordChar(buffer[pos]))
				++pos;

			ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_PARAM, false});
			continue;
		}

		if (c == '?')
		{
			ranges.push_back({unsigned(pos), 1, Token::TYPE_PARAM, firstInStatement});
			++pos;
			firstInStatement = false;
			continue;
		}

		// Word (identifier or keyword candidate).
		if (hlIsWordStart(c))
		{
			const size_t start = pos;

			while (pos < size && hlIsWordChar(buffer[pos]))
				++pos;

			ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_WORD, firstInStatement});
			firstInStatement = false;
			continue;
		}

		// Number: decimal, hexadecimal or a leading-dot decimal.
		if (hlIsDigit(c) || (c == '.' && pos + 1 < size && hlIsDigit(buffer[pos + 1])))
		{
			const size_t start = pos;

			if (c == '0' && pos + 1 < size && (buffer[pos + 1] == 'x' || buffer[pos + 1] == 'X'))
			{
				pos += 2;

				while (pos < size && (hlIsDigit(buffer[pos]) ||
					(buffer[pos] >= 'a' && buffer[pos] <= 'f') ||
					(buffer[pos] >= 'A' && buffer[pos] <= 'F')))
				{
					++pos;
				}
			}
			else
			{
				while (pos < size && (hlIsDigit(buffer[pos]) || buffer[pos] == '.'))
					++pos;

				// Exponent (including its sign).
				if (pos < size && (buffer[pos] == 'e' || buffer[pos] == 'E'))
				{
					size_t save = pos;
					++pos;

					if (pos < size && (buffer[pos] == '+' || buffer[pos] == '-'))
						++pos;

					if (pos < size && hlIsDigit(buffer[pos]))
					{
						while (pos < size && hlIsDigit(buffer[pos]))
							++pos;
					}
					else
						pos = save;
				}
			}

			ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_NUMBER, false});
			firstInStatement = false;
			continue;
		}

		// Everything else is an operator/punctuation run. The run stops
		// before characters that have their own branches ('?', ':' and ';'
		// must not be swallowed - e.g. the "?" of "VALUES (?, ?)").
		{
			const size_t start = pos;

			while (pos < size && !hlIsSpace(buffer[pos]) && !hlIsWordChar(buffer[pos]) &&
				   buffer[pos] != '\'' && buffer[pos] != '"' &&
				   buffer[pos] != '?' && buffer[pos] != ':' && buffer[pos] != ';')
			{
				++pos;
			}

			if (pos == start)
				++pos;

			ranges.push_back({unsigned(start), unsigned(pos - start), Token::TYPE_OPERATOR, firstInStatement});
			firstInStatement = false;
		}
	}

	if (incompleteOut)
		*incompleteOut = incomplete;

	return ranges;
}

}	// namespace Hq
