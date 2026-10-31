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

#ifndef FB_HQISQL_HQ_KEYWORDS_H
#define FB_HQISQL_HQ_KEYWORDS_H

#include <stddef.h>

namespace Hq {

// Lookup in the keyword dictionary generated from src/common/ParserTokens.h.
// The comparison is ASCII case-insensitive; returns true when the word is a
// keyword.
bool isKeyword(const char* word, size_t length);

// True for a reserved word: it can not be used as an unquoted name.
bool isReservedWord(const char* word, size_t length);

// Number of keywords in the dictionary (for completion lists).
unsigned keywordCount();

// Keyword by index, upper case, NULL-terminated.
const char* keyword(unsigned index);

}	// namespace Hq

#endif	// FB_HQISQL_HQ_KEYWORDS_H
