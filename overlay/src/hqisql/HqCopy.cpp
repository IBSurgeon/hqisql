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
#include "../hqisql/HqCopy.h"

#include <string.h>

namespace
{

int hexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';

	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return -1;
}

}	// anonymous namespace


namespace Hq {
namespace Copy {

Reader::Reader(const Rows::Source& aSource, char aDelimiter, const std::string& aNullText)
	: source(aSource),
	  delimiter(aDelimiter),
	  nullText(aNullText),
	  buffer(64 * 1024)
{
}

// The next line into rawLine, without the line end. False at the end of
// the input or on an error (errorText is set then).
bool Reader::readLine()
{
	rawLine.clear();
	bool any = false;

	for (;;)
	{
		if (pos == end)
		{
			if (eof)
				break;

			pos = 0;
			end = source(buffer.data(), buffer.size());

			if (end == 0)
			{
				eof = true;
				break;
			}

			if (!started)
			{
				started = true;

				// The signature is 11 bytes: read at least that much.
				static const char binary[] = "PGCOPY\n\377\r\n";

				while (end < sizeof(binary) - 1)
				{
					const size_t n = source(buffer.data() + end, buffer.size() - end);

					if (n == 0)
					{
						eof = true;
						break;
					}

					end += n;
				}

				if (end >= sizeof(binary) - 1 && memcmp(buffer.data(), binary, sizeof(binary) - 1) == 0)
				{
					errorText = "the binary COPY format is not supported, use the text format";
					return false;
				}

				if (end >= 3 && memcmp(buffer.data(), "\xEF\xBB\xBF", 3) == 0)
				{
					pos = 3;
					bom = true;
				}
			}
		}

		any = true;
		const char* const from = buffer.data() + pos;
		const char* const nl = (const char*) memchr(from, '\n', end - pos);

		if (nl)
		{
			rawLine.append(from, nl - from);
			pos += nl - from + 1;
			break;
		}

		rawLine.append(from, end - pos);
		pos = end;

		if (rawLine.size() > MAX_LINE)
		{
			errorText = "the line is too long";
			return false;
		}
	}

	if (!rawLine.empty() && rawLine.back() == '\r')
		rawLine.pop_back();

	return any;
}

bool Reader::unescape(const char* p, const char* const fieldEnd, std::string& value)
{
	value.clear();

	while (p < fieldEnd)
	{
		const char c = *p++;

		if (c != '\\')
		{
			value += c;
			continue;
		}

		if (p == fieldEnd)
		{
			errorText = "a backslash at the end of the line";
			return false;
		}

		const char e = *p++;

		switch (e)
		{
			case 'b':
				value += '\b';
				break;

			case 'f':
				value += '\f';
				break;

			case 'n':
				value += '\n';
				break;

			case 'r':
				value += '\r';
				break;

			case 't':
				value += '\t';
				break;

			case 'v':
				value += '\v';
				break;

			case 'x':
			{
				int code = -1;

				for (int n = 0; n < 2 && p < fieldEnd && hexValue(*p) >= 0; ++n)
					code = (code < 0 ? 0 : code * 16) + hexValue(*p++);

				value += code < 0 ? 'x' : char(code);
				break;
			}

			default:
				if (e >= '0' && e <= '7')
				{
					int code = e - '0';

					for (int n = 1; n < 3 && p < fieldEnd && *p >= '0' && *p <= '7'; ++n)
						code = code * 8 + (*p++ - '0');

					value += char(code);
				}
				else
					value += e;
		}
	}

	return true;
}

Reader::Result Reader::next(Rows::Record& record)
{
	record.clear();
	errorText.clear();

	if (finished)
		return END;

	++lineNumber;

	if (!readLine())
		return errorText.empty() ? END : FAILED;

	if (rawLine == "\\.")
	{
		finished = true;
		return END;
	}

	// The fields: a delimiter after a backslash belongs to the field.
	const char* p = rawLine.data();
	const char* const lineEnd = p + rawLine.size();

	for (;;)
	{
		const char* fieldEnd = p;

		while (fieldEnd < lineEnd && *fieldEnd != delimiter)
			fieldEnd += *fieldEnd == '\\' && fieldEnd + 1 < lineEnd ? 2 : 1;

		Rows::Field field;

		if (std::string_view(p, fieldEnd - p) == nullText)
			field.null = true;
		else if (!unescape(p, fieldEnd, field.value))
			return FAILED;

		record.push_back(field);

		if (fieldEnd == lineEnd)
			break;

		p = fieldEnd + 1;
	}

	return RECORD;
}

}	// namespace Copy
}	// namespace Hq
