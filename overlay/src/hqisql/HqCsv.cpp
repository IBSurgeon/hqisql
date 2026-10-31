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
#include "../hqisql/HqCsv.h"

#include <string.h>

namespace Hq {
namespace Csv {

Reader::Reader(const Source& aSource, const Options& aOptions)
	: source(aSource),
	  options(aOptions),
	  buffer(64 * 1024)
{
}

// Makes at least n bytes available, unless the input ends first.
bool Reader::ensure(size_t n)
{
	while (end - pos < n && !eof)
	{
		if (pos > 0)
		{
			memmove(buffer.data(), buffer.data() + pos, end - pos);
			end -= pos;
			pos = 0;
		}

		const size_t got = source(buffer.data() + end, buffer.size() - end);

		if (got == 0)
			eof = true;
		else
			end += got;
	}

	return end - pos >= n;
}

int Reader::get()
{
	return ensure(1) ? (unsigned char) buffer[pos++] : -1;
}

int Reader::peek()
{
	return ensure(1) ? (unsigned char) buffer[pos] : -1;
}

Reader::Result Reader::next(Record& record)
{
	record.clear();
	rawRecord.clear();
	errorText.clear();

	if (!started)
	{
		started = true;

		if (ensure(3) && memcmp(buffer.data() + pos, "\xEF\xBB\xBF", 3) == 0)
		{
			pos += 3;
			bom = true;
		}
	}

	// Empty lines.
	for (;;)
	{
		const int c = peek();

		if (c == '\n')
			get();
		else if (c == '\r')
		{
			get();

			if (peek() == '\n')
				get();
		}
		else if (c == -1)
			return END;
		else
			break;

		++physicalLine;
	}

	recordLine = physicalLine;

	const int quote = (unsigned char) options.quote;
	const int delimiter = (unsigned char) options.delimiter;
	Field field;
	bool quoted = false;		// the field started with a quote
	bool inQuotes = false;
	bool afterQuote = false;	// the closing quote was read

	const auto addField = [&]() {
		field.null = !quoted && field.value == options.nullText;
		record.push_back(field);
		field.value.clear();
		quoted = inQuotes = afterQuote = false;
	};

	for (;;)
	{
		if (rawRecord.size() > MAX_RECORD)
		{
			errorText = "the record is longer than 64 MB (a quote is not closed?)";
			return FAILED;
		}

		const int c = get();

		if (c == -1)
		{
			if (inQuotes)
			{
				errorText = "the quote is not closed at the end of the file";
				return FAILED;
			}

			addField();
			return RECORD;
		}

		if (inQuotes)
		{
			rawRecord += char(c);

			if (c == quote)
			{
				if (peek() == quote)
				{
					rawRecord += char(get());
					field.value += char(c);
				}
				else
				{
					inQuotes = false;
					afterQuote = true;
				}

				continue;
			}

			if (c == '\n' || (c == '\r' && peek() != '\n'))
				++physicalLine;

			field.value += char(c);
			continue;
		}

		if (c == '\n' || c == '\r')
		{
			if (c == '\r' && peek() == '\n')
				get();

			++physicalLine;
			addField();
			return RECORD;
		}

		rawRecord += char(c);

		if (c == delimiter)
		{
			addField();
			continue;
		}

		if (afterQuote)
		{
			// Blanks between the closing quote and the delimiter are ignored.
			if (c == ' ' || c == '\t')
				continue;

			errorText = "a character after the closing quote of field " +
				std::to_string(record.size() + 1) + " (a quote inside a quoted field must be doubled)";
			return FAILED;
		}

		if (c == quote && field.value.empty() && !quoted)
		{
			quoted = inQuotes = true;
			continue;
		}

		field.value += char(c);
	}
}

}	// namespace Csv
}	// namespace Hq
