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
 *  The keyword dictionary is generated at compile time from
 *  src/common/ParserTokens.h, so it stays in sync with the engine grammar.
 */

#include "firebird.h"
#include "../hqisql/HqKeywords.h"

#include <algorithm>
#include <string.h>
#include <vector>

namespace
{

// Words only - operator-shaped tokens ("(", "!<", ":=", ...) are skipped,
// they are highlighted and completed by other means.
struct KeywordEntry
{
	const char* word;
	bool reserved;
};

const KeywordEntry keywordTable[] = {
#define PARSER_TOKEN(ident, str, nonReserved) {str, !(nonReserved)},
#include "../common/ParserTokens.h"
#undef PARSER_TOKEN
};

struct KeywordSet
{
	std::vector<KeywordEntry> sorted;

	KeywordSet()
	{
		for (size_t i = 0; i < sizeof(keywordTable) / sizeof(keywordTable[0]); ++i)
		{
			const KeywordEntry& e = keywordTable[i];

			if (*e.word >= 'A' && *e.word <= 'Z')
				sorted.push_back(e);
		}

		std::sort(sorted.begin(), sorted.end(),
			[](const KeywordEntry& a, const KeywordEntry& b) { return strcmp(a.word, b.word) < 0; });
	}
};

const KeywordSet& keywordSet()
{
	static KeywordSet set;
	return set;
}

const KeywordEntry* findKeyword(const char* word, size_t length)
{
	if (!word || length == 0 || length > 128)
		return NULL;

	char buf[129];

	for (size_t i = 0; i < length; ++i)
		buf[i] = (word[i] >= 'a' && word[i] <= 'z') ? char(word[i] - 'a' + 'A') : word[i];

	buf[length] = '\0';

	const KeywordSet& set = keywordSet();
	const auto it = std::lower_bound(set.sorted.begin(), set.sorted.end(), buf,
		[](const KeywordEntry& e, const char* w) { return strcmp(e.word, w) < 0; });

	return it != set.sorted.end() && strcmp(it->word, buf) == 0 ? &*it : NULL;
}

}	// anonymous namespace


namespace Hq {

bool isKeyword(const char* word, size_t length)
{
	return findKeyword(word, length) != NULL;
}

bool isReservedWord(const char* word, size_t length)
{
	const KeywordEntry* e = findKeyword(word, length);
	return e && e->reserved;
}

unsigned keywordCount()
{
	return unsigned(keywordSet().sorted.size());
}

const char* keyword(unsigned index)
{
	const KeywordSet& set = keywordSet();
	return index < set.sorted.size() ? set.sorted[index].word : NULL;
}

}	// namespace Hq
