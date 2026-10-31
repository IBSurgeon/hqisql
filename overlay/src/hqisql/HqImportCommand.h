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

#ifndef FB_HQISQL_HQ_IMPORT_COMMAND_H
#define FB_HQISQL_HQ_IMPORT_COMMAND_H

#include "../hqisql/HqCsv.h"

#include <string>
#include <string_view>
#include <vector>

namespace Hq {

// Parsing of the import commands (executed by HqImport.cpp):
//
//   EXEC SQL name FROM format 'file' [OPTIONS (option = value, ...)]
//   IMPORT INTO table [(column, ...)] FROM format 'file' [OPTIONS (...)]
//
// format: CSV, COPY (the PostgreSQL COPY text format), PARQUET or AVRO.
//
// No isql dependencies: the functions are unit tested.
namespace Import {

struct Options
{
	Csv::Options csv;
	bool header = false;		// the first record has the column names
	unsigned skip = 0;			// records to skip after the header
	std::string encoding;		// the file charset; empty: the connection charset
	char decimal = '.';			// ',' : a comma in numbers is the decimal point
	unsigned batch = 1000;		// rows in a batch; 0: no IBatch, a row at a time
	unsigned commit = 0;		// commit every n rows; 0: never
	unsigned errors = 0;		// rows that may fail and are skipped
};

struct Command
{
	enum Format
	{
		CSV,
		COPY,
		PARQUET,
		AVRO
	};

	enum Kind
	{
		EXEC,		// EXEC SQL name FROM CSV
		IMPORT		// IMPORT INTO table
	};

	Kind kind = EXEC;
	std::string name;					// EXEC: the statement name in upper case
	std::string table;					// IMPORT: the table as spelled
	std::string tableName;				// IMPORT: the table as stored in the metadata
	std::vector<std::string> columns;	// IMPORT: the columns as spelled
	Format format = CSV;
	std::string file;					// the file name without quotes
	Options options;
};

enum ParseResult
{
	NOT_IMPORT,		// another statement (also EXEC SQL ... USING)
	PARSED,
	PARSE_ERROR		// error has the message (without the command name)
};

// Parses a complete statement without its terminator.
ParseResult parse(std::string_view statement, Command& command, std::string& error);

// The names of the OPTIONS (...), in upper case.
const std::vector<std::string>& optionNames();

// The formats after FROM, in the order of Command::Format.
const std::vector<std::string>& formatNames();

// The binary formats: PARQUET and AVRO (the file has the column names and
// types; text options such as DELIMITER do not apply).
inline bool isBinary(Command::Format format)
{
	return format == Command::PARQUET || format == Command::AVRO;
}

// The SQL name for a column title of the CSV header: as it is when it is a
// regular identifier, otherwise in double quotes.
std::string columnName(std::string_view title);

}	// namespace Import
}	// namespace Hq

#endif	// FB_HQISQL_HQ_IMPORT_COMMAND_H
