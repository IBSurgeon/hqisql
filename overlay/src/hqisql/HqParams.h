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

#ifndef FB_HQISQL_HQ_PARAMS_H
#define FB_HQISQL_HQ_PARAMS_H

#include <string>
#include <string_view>
#include <vector>

namespace Hq {

// Text substitution behind PREPARE / EXEC SQL (see HqStatements.h). No isql
// dependencies: the functions are unit tested. "term" is the current
// statement terminator, the lexer needs it to find the end of a statement.
namespace Params {

typedef std::vector<std::string> Tuple;

// Parses the values after USING: "(v1, v2, ...)", or with batch a list of
// such tuples separated by commas. Strings (also Q-strings), quoted names
// and nested parentheses are kept in a value as they are spelled, comments
// are removed. Only blanks and comments may follow the last tuple. Returns
// false and a message in error when the text is not such a list.
bool parseTuples(std::string_view text, std::string_view term, bool batch,
	std::vector<Tuple>& tuples, std::string& error);

// True when the text has only blanks and comments.
bool onlyBlanks(std::string_view text, std::string_view term);

// The number of "?" marks in the statement, outside of strings, quoted
// names and comments.
unsigned countMarks(std::string_view statement, std::string_view term);

// Replaces the "?" marks counted by countMarks() with the values, in order.
// The caller checks that the numbers match; extra marks stay as they are.
std::string substitute(std::string_view statement, std::string_view term,
	const Tuple& values);

}	// namespace Params
}	// namespace Hq

#endif	// FB_HQISQL_HQ_PARAMS_H
