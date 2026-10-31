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
 *  hqisql: the color scheme of the syntax highlighter - the built-in
 *  colors and the parser of an optional scheme file.
 */

#ifndef FB_HQISQL_HQ_COLORS_H
#define FB_HQISQL_HQ_COLORS_H

#include <string>
#include <string_view>
#include <vector>

namespace Hq {

// The colors of the editor, one per kind of text. A scheme file assigns
// them by name:
//
//   # comment
//   command = yellow            # the isql commands and their options
//   reserved = brightblue bold  # reserved words
//   string = rgb(0,5,0)         # a color of the 6x6x6 cube, 0..5 each
//   comment = gray(10)          # a gray level, 0 (black) .. 23 (white)
//   number = 213                # a terminal color index, 0..255
//   param = default underline   # the terminal foreground, underlined
//   hint = gray on black        # "on <color>" sets the background
//
// The colors are the 16 names of Replxx::Color (black, red, green, brown,
// blue, magenta, cyan, lightgray, gray, brightred, brightgreen, yellow,
// brightblue, brightmagenta, brightcyan, white), default, rgb(r,g,b),
// gray(n) or a number, with the optional words bold and underline and an
// optional background. Names are case-insensitive; '=' or ':' separate
// the element from its color; a '#' starts a comment.
//
// A color is stored as the integer value of a Replxx::Color (see the
// constants below), so this class needs no replxx and can be tested apart.
class ColorScheme
{
public:
	enum Element
	{
		COMMAND,		// isql command words: SET, STATS, ON, SHOW, TABLE
		RESERVED,		// reserved words: SELECT, FROM
		KEYWORD,		// non-reserved keywords: NAME, TYPE
		STRING,			// 'string'
		QUOTED,			// "quoted identifier"
		NUMBER,
		COMMENT,
		PARAM,			// :name and ?
		TERMINATOR,		// ; or the SET TERM terminator
		HINT,			// the gray hint after the cursor
		COMPLETION,		// a keyword in the Tab completion list
		ELEMENT_COUNT
	};

	// The encoding of Replxx::Color: the foreground in the low byte (0..15
	// the named colors, 16..255 the xterm palette), the background in the
	// next byte, and flags above.
	static const unsigned DEFAULT_COLOR = 1u << 16;
	static const unsigned BOLD = 1u << 17;
	static const unsigned UNDERLINE = 1u << 18;
	static const unsigned BACKGROUND_SET = 1u << 19;

	// The built-in scheme.
	ColorScheme();

	unsigned color(Element e) const
	{
		return colors[e];
	}

	void setColor(Element e, unsigned c)
	{
		colors[e] = c;
	}

	// Parses a scheme file. Each valid line sets one element; an invalid
	// line is skipped and reported as "line N: <reason>" in *errors (when
	// not NULL). Returns the number of invalid lines.
	unsigned parse(std::string_view text, std::vector<std::string>* errors = NULL);

	// "yellow", "brightblue bold", "rgb(0,5,0) on gray(3)" -> the color
	// code. False when the text is not a color.
	static bool parseColor(std::string_view spec, unsigned* code);

	// "command", "reserved", ... (the scheme file names).
	static const char* elementName(Element e);

	// False when the name is not an element.
	static bool findElement(std::string_view name, Element* e);

private:
	unsigned colors[ELEMENT_COUNT];
};

}	// namespace Hq

#endif	// FB_HQISQL_HQ_COLORS_H
