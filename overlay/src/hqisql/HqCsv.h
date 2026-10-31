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

#ifndef FB_HQISQL_HQ_CSV_H
#define FB_HQISQL_HQ_CSV_H

#include <functional>
#include <string>
#include <vector>

namespace Hq {

// The CSV reader behind EXEC SQL ... FROM CSV and IMPORT INTO (see
// HqImport.h). No isql dependencies: it is unit tested.
//
// RFC 4180: fields in quotes, a doubled quote inside them, line breaks
// inside quotes, CRLF, LF or CR line ends. The input is read in chunks, the
// file is never read as a whole. A UTF-8 BOM at the start is skipped.
// Empty lines are skipped. An unquoted field equal to the NULL text (empty
// by default) is NULL, a quoted one never is: with the defaults an empty
// field is NULL and "" is an empty string.
namespace Csv {

struct Options
{
	char delimiter = ',';
	char quote = '"';
	std::string nullText;
};

struct Field
{
	std::string value;
	bool null = false;
};

typedef std::vector<Field> Record;

// Reads up to size bytes into buffer; returns the number of bytes read,
// 0 at the end of the input.
typedef std::function<size_t (char* buffer, size_t size)> Source;

class Reader
{
public:
	enum Result
	{
		RECORD,		// a record is in the argument
		END,		// no more records
		FAILED		// a format error: error() and line() tell where
	};

	// A record longer than this is a format error: it is almost surely an
	// unclosed quote that would otherwise read the rest of the file.
	static const size_t MAX_RECORD = 64 * 1024 * 1024;

	Reader(const Source& source, const Options& options);

	Result next(Record& record);

	// The first physical line of the last record (or of the error).
	unsigned line() const
	{
		return recordLine;
	}

	// The last record as it is in the file, without the line end.
	const std::string& raw() const
	{
		return rawRecord;
	}

	const std::string& error() const
	{
		return errorText;
	}

	// The input started with a UTF-8 BOM. Known after the first next().
	bool hasBom() const
	{
		return bom;
	}

private:
	bool ensure(size_t n);
	int get();				// the next byte or -1
	int peek();

	Source source;
	Options options;
	std::vector<char> buffer;
	size_t pos = 0, end = 0;
	bool eof = false;
	bool started = false;
	bool bom = false;
	unsigned physicalLine = 1;
	unsigned recordLine = 0;
	std::string rawRecord;
	std::string errorText;
};

}	// namespace Csv
}	// namespace Hq

#endif	// FB_HQISQL_HQ_CSV_H
