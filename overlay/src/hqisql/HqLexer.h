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
 *  hqisql: tolerant, stateless SQL token classifier used by the syntax
 *  highlighter, the completion and the PREPARE / EXEC SQL substitution.
 */

#ifndef FB_HQISQL_HQ_LEXER_H
#define FB_HQISQL_HQ_LEXER_H

#include <string_view>
#include <vector>

namespace Hq {

class Lexer
{
public:
	struct Token
	{
		enum Type
		{
			TYPE_STRING,		// '...' or Q-string
			TYPE_META_STRING,	// "quoted identifier"
			TYPE_WORD,			// identifier or keyword candidate
			TYPE_NUMBER,
			TYPE_COMMENT,
			TYPE_OPERATOR,
			TYPE_PARAM,			// ':' input parameter or '?'
			TYPE_TERMINATOR		// statement terminator (current SET TERM or ';')
		};
	};

	// Byte range classified by highlight().
	struct HighlightRange
	{
		unsigned start;
		unsigned length;
		Token::Type type;
		bool firstInStatement;	// first significant token of its statement
	};

	// Classifies the whole buffer in one pass and never throws; unterminated
	// strings and comments are colored up to the end of the buffer. Changes
	// of the terminator made by "SET TERM <x>" inside the buffer are followed
	// when matching subsequent terminators. If incompleteOut is not NULL,
	// *incompleteOut receives true when the buffer ends inside an
	// unterminated string or block comment.
	static std::vector<HighlightRange> highlight(std::string_view buffer,
		std::string_view term, bool* incompleteOut = NULL);
};

}	// namespace Hq

#endif	// FB_HQISQL_HQ_LEXER_H
