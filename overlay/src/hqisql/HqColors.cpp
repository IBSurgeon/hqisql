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
 *  hqisql: the color scheme of the syntax highlighter (see HqColors.h).
 */

#include "firebird.h"
#include "../hqisql/HqColors.h"
#include <ctype.h>
#include <string.h>

namespace Hq {

namespace {

// The 16 named colors, in the order of Replxx::Color.
const char* const COLOR_NAMES[16] =
{
	"black", "red", "green", "brown", "blue", "magenta", "cyan", "lightgray",
	"gray", "brightred", "brightgreen", "yellow", "brightblue", "brightmagenta",
	"brightcyan", "white"
};

const char* const ELEMENT_NAMES[ColorScheme::ELEMENT_COUNT] =
{
	"command", "reserved", "keyword", "string", "quoted", "number", "comment",
	"param", "terminator", "hint", "completion"
};

// The first xterm color of the 6x6x6 cube and of the gray ramp.
const unsigned RGB_BASE = 16;
const unsigned GRAY_BASE = 232;

bool equalsNoCase(std::string_view a, const char* b)
{
	const size_t n = strlen(b);

	if (a.size() != n)
		return false;

	for (size_t i = 0; i < n; ++i)
	{
		if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i]))
			return false;
	}

	return true;
}

std::string_view trim(std::string_view s)
{
	while (!s.empty() && isspace((unsigned char) s.front()))
		s.remove_prefix(1);

	while (!s.empty() && isspace((unsigned char) s.back()))
		s.remove_suffix(1);

	return s;
}

// A decimal number not above max. False otherwise.
bool parseNumber(std::string_view s, unsigned max, unsigned* out)
{
	s = trim(s);

	if (s.empty() || s.size() > 3)
		return false;

	unsigned n = 0;

	for (const char c : s)
	{
		if (!isdigit((unsigned char) c))
			return false;

		n = n * 10 + (c - '0');
	}

	if (n > max)
		return false;

	*out = n;
	return true;
}

// The words of s separated by blanks.
std::vector<std::string_view> splitWords(std::string_view s)
{
	std::vector<std::string_view> words;
	size_t i = 0;

	while (i < s.size())
	{
		while (i < s.size() && isspace((unsigned char) s[i]))
			++i;

		const size_t start = i;

		while (i < s.size() && !isspace((unsigned char) s[i]))
			++i;

		if (i > start)
			words.push_back(s.substr(start, i - start));
	}

	return words;
}

// One foreground or background color: a name, default, rgb(r,g,b),
// gray(n) or a palette index. *code receives 0..255 or DEFAULT_COLOR.
bool parseOneColor(std::string_view word, unsigned* code)
{
	for (unsigned i = 0; i < 16; ++i)
	{
		if (equalsNoCase(word, COLOR_NAMES[i]))
		{
			*code = i;
			return true;
		}
	}

	if (equalsNoCase(word, "default"))
	{
		*code = ColorScheme::DEFAULT_COLOR;
		return true;
	}

	// rgb(r,g,b) and gray(n): the word up to '(' and the arguments.
	const size_t paren = word.find('(');

	if (paren != std::string_view::npos && word.back() == ')')
	{
		const std::string_view name = word.substr(0, paren);
		const std::string_view args = word.substr(paren + 1, word.size() - paren - 2);

		if (equalsNoCase(name, "rgb"))
		{
			unsigned rgb[3];
			size_t pos = 0;

			for (unsigned i = 0; i < 3; ++i)
			{
				const size_t comma = args.find(',', pos);
				const bool last = i == 2;

				if (last != (comma == std::string_view::npos))
					return false;

				if (!parseNumber(args.substr(pos, last ? std::string_view::npos : comma - pos), 5, &rgb[i]))
					return false;

				pos = comma + 1;
			}

			*code = RGB_BASE + rgb[0] * 36 + rgb[1] * 6 + rgb[2];
			return true;
		}

		if (equalsNoCase(name, "gray") || equalsNoCase(name, "grey"))
		{
			unsigned level;

			if (!parseNumber(args, 23, &level))
				return false;

			*code = GRAY_BASE + level;
			return true;
		}

		return false;
	}

	return parseNumber(word, 255, code);
}

}	// anonymous namespace

ColorScheme::ColorScheme()
{
	colors[COMMAND] = 11;		// yellow
	colors[RESERVED] = 12;		// brightblue
	colors[KEYWORD] = 4;		// blue
	colors[STRING] = 10;		// brightgreen
	colors[QUOTED] = 13;		// brightmagenta
	colors[NUMBER] = 5;			// magenta
	colors[COMMENT] = 8;		// gray
	colors[PARAM] = 14;			// brightcyan
	colors[TERMINATOR] = 9;		// brightred
	colors[HINT] = 8;			// gray
	colors[COMPLETION] = 12;	// brightblue
}

const char* ColorScheme::elementName(Element e)
{
	return ELEMENT_NAMES[e];
}

bool ColorScheme::findElement(std::string_view name, Element* e)
{
	for (unsigned i = 0; i < ELEMENT_COUNT; ++i)
	{
		if (equalsNoCase(name, ELEMENT_NAMES[i]))
		{
			*e = (Element) i;
			return true;
		}
	}

	return false;
}

bool ColorScheme::parseColor(std::string_view spec, unsigned* code)
{
	// Blanks around '(' and ',' are allowed: "rgb(0, 5, 0)" is one color.
	std::string packed;
	int depth = 0;

	for (const char c : spec)
	{
		if (c == '(')
			++depth;
		else if (c == ')')
			--depth;

		if (depth > 0 && isspace((unsigned char) c))
			continue;

		packed += c;
	}

	if (depth != 0)
		return false;

	const std::vector<std::string_view> words = splitWords(packed);

	if (words.empty())
		return false;

	unsigned result = 0;
	bool haveColor = false;

	for (size_t i = 0; i < words.size(); ++i)
	{
		const std::string_view word = words[i];
		unsigned c;

		if (equalsNoCase(word, "bold"))
			result |= BOLD;
		else if (equalsNoCase(word, "underline"))
			result |= UNDERLINE;
		else if (equalsNoCase(word, "on"))
		{
			// The background: a real color, not default.
			if (i + 1 >= words.size() || (result & BACKGROUND_SET) ||
				!parseOneColor(words[++i], &c) || c == DEFAULT_COLOR)
			{
				return false;
			}

			result |= BACKGROUND_SET | (c << 8);
		}
		else if (!haveColor && parseOneColor(word, &c))
		{
			result |= c;
			haveColor = true;
		}
		else
			return false;
	}

	// Attributes alone ("bold") apply to the default foreground.
	if (!haveColor)
		result |= DEFAULT_COLOR;

	*code = result;
	return true;
}

unsigned ColorScheme::parse(std::string_view text, std::vector<std::string>* errors)
{
	unsigned bad = 0;
	unsigned lineNo = 0;

	while (!text.empty())
	{
		++lineNo;
		const size_t nl = text.find('\n');
		std::string_view line = text.substr(0, nl);
		text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);

		const size_t hash = line.find('#');

		if (hash != std::string_view::npos)
			line = line.substr(0, hash);

		line = trim(line);

		if (line.empty())
			continue;

		std::string reason;
		size_t sep = line.find_first_of("=:");

		if (sep == std::string_view::npos)
			reason = "expected <element> = <color>";
		else
		{
			const std::string_view name = trim(line.substr(0, sep));
			const std::string_view spec = trim(line.substr(sep + 1));
			Element e;
			unsigned code;

			if (!findElement(name, &e))
				reason = "unknown element '" + std::string(name) + "'";
			else if (!parseColor(spec, &code))
				reason = "unknown color '" + std::string(spec) + "'";
			else
				colors[e] = code;
		}

		if (!reason.empty())
		{
			++bad;

			if (errors)
				errors->push_back("line " + std::to_string(lineNo) + ": " + reason);
		}
	}

	return bad;
}

}	// namespace Hq
