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

#ifndef FB_HQISQL_HQ_ROWS_H
#define FB_HQISQL_HQ_ROWS_H

#include "../hqisql/HqCsv.h"

#include <stdint.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Hq {

// The readers of the import formats other than CSV (COPY, PARQUET, AVRO)
// and the helpers they share. Every reader gives the rows as text fields,
// as the CSV reader does: the import sends each field as a VARCHAR and the
// server converts it to the parameter type (see HqImport.h). No isql
// dependencies: the readers are unit tested.
namespace Rows {

typedef Csv::Field Field;
typedef Csv::Record Record;

// Sequential input, as for CSV: up to size bytes, 0 at the end.
typedef Csv::Source Source;

// Random access input (PARQUET): exactly size bytes at offset; false when
// they cannot be read.
struct RandomSource
{
	std::function<bool (uint64_t offset, char* buffer, size_t size)> read;
	uint64_t size = 0;
};

// A column of a binary format (PARQUET, AVRO).
struct Column
{
	enum Kind
	{
		TEXT,		// UTF-8: a string, a number, a date as the server reads a literal
		BINARY,		// bytes: the parameter is sent as OCTETS
		UUID,		// 16 bytes: OCTETS for an OCTETS parameter, else the text form
		UTC			// a timestamp or a time in UTC, the text has no zone
	};

	std::string name;
	Kind kind = TEXT;
	std::string type;		// the type in the file, for the messages
	bool supported = true;	// false: a nested type that cannot be read
};

class Reader
{
public:
	enum Result
	{
		RECORD,		// a record is in the argument
		END,		// no more records
		FAILED		// error() and line() tell what and where
	};

	virtual ~Reader()
	{
	}

	virtual Result next(Record& record) = 0;

	// The text formats: the first line of the last record (or of the
	// error); the binary ones: the number of the row, from 1.
	virtual unsigned line() const = 0;

	// What line() counts: "line" or "row".
	virtual const char* unit() const
	{
		return "line";
	}

	// The last record (given back by the caller) as it goes to the .bad
	// file: the line of the file, or a CSV line for a binary format.
	virtual std::string raw(const Record& record) const = 0;

	// The input started with a UTF-8 BOM. Known after the first next().
	virtual bool hasBom() const
	{
		return false;
	}

	const std::string& error() const
	{
		return errorText;
	}

	// The columns of a binary format; empty for a text format. Known after
	// open().
	const std::vector<Column>& columns() const
	{
		return allColumns;
	}

	// A binary format: only these columns (indexes in columns()), in this
	// order, are read. All of them by default. False with error() when a
	// column cannot be read.
	virtual bool select(const std::vector<unsigned>& indexes);

protected:
	std::string errorText;
	std::vector<Column> allColumns;
};

// The fields as one CSV line (RFC 4180, NULL is an empty field, an empty
// string is ""), for the .bad file of a binary format.
std::string csvLine(const Record& record);

// Text forms of the values of the binary formats, as the server reads
// them. days: since 1970-01-01; the fraction of a second has at most 4
// digits (the precision of Firebird), the rest is cut off.
std::string formatDate(int64_t days);
std::string formatTime(int64_t nanosOfDay);
std::string formatTimestamp(int64_t seconds, int64_t nanos);	// seconds since 1970-01-01 00:00
std::string formatUuid(std::string_view bytes);					// 16 bytes
std::string formatHex(std::string_view bytes);
std::string formatDouble(double value);
std::string formatFloat(float value);

// An integer in big-endian two's complement (a DECIMAL of PARQUET and
// AVRO) as a decimal number with scale digits after the point.
std::string formatBigDecimal(std::string_view bytes, int scale);
std::string formatDecimal(int64_t value, int scale);

}	// namespace Rows
}	// namespace Hq

#endif	// FB_HQISQL_HQ_ROWS_H
