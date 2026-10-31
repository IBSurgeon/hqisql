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

#ifndef FB_HQISQL_HQ_COPY_H
#define FB_HQISQL_HQ_COPY_H

#include "../hqisql/HqRows.h"

namespace Hq {

// The reader of FROM COPY: the text format of the PostgreSQL COPY command
// (COPY ... TO, \copy, the data of pg_dump).
//
// A line is a row; the fields are separated by the delimiter (a tab by
// default). A field equal to the NULL text (\N by default) is NULL. A
// backslash starts an escape: \b \f \n \r \t \v, \ and 1 to 3 octal
// digits, \x and 1 or 2 hex digits; before any other character it means the
// character itself (\\, the delimiter). A line \. ends the data. LF or CRLF
// line ends; a UTF-8 BOM at the start is skipped. Empty lines are rows with
// one empty field, as in PostgreSQL. The binary COPY format is recognized
// and rejected.
namespace Copy {

class Reader : public Rows::Reader
{
public:
	// A line longer than this is a format error.
	static const size_t MAX_LINE = 64 * 1024 * 1024;

	Reader(const Rows::Source& source, char delimiter, const std::string& nullText);

	Result next(Rows::Record& record) override;

	unsigned line() const override
	{
		return lineNumber;
	}

	std::string raw(const Rows::Record&) const override
	{
		return rawLine;
	}

	bool hasBom() const override
	{
		return bom;
	}

private:
	bool readLine();
	bool unescape(const char* p, const char* end, std::string& value);

	Rows::Source source;
	char delimiter;
	std::string nullText;
	std::vector<char> buffer;
	size_t pos = 0, end = 0;
	bool eof = false;
	bool started = false;
	bool finished = false;		// \. was read
	bool bom = false;
	unsigned lineNumber = 0;
	std::string rawLine;
};

}	// namespace Copy
}	// namespace Hq

#endif	// FB_HQISQL_HQ_COPY_H
