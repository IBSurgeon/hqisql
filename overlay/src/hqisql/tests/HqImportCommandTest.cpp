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
 *  Tests of the EXEC SQL ... FROM <format> and IMPORT INTO parsing
 *  (Hq::Import).
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqImportCommand.h"
#include <string>

using namespace Hq::Import;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqImportCommandSuite)


BOOST_AUTO_TEST_SUITE(HqImportCommandTests)

static Command parseOk(const char* text)
{
	Command command;
	std::string error;
	BOOST_CHECK_MESSAGE(parse(text, command, error) == PARSED, text << ": " << error);
	return command;
}

static std::string parseError(const char* text)
{
	Command command;
	std::string error;
	BOOST_CHECK_MESSAGE(parse(text, command, error) == PARSE_ERROR, text);
	return error;
}

static bool notImport(const char* text)
{
	Command command;
	std::string error;
	return parse(text, command, error) == NOT_IMPORT && error.empty();
}

BOOST_AUTO_TEST_CASE(ExecTest)
{
	Command c = parseOk("exec sql ins from csv 'data.csv'");
	BOOST_CHECK(c.kind == Command::EXEC);
	BOOST_CHECK_EQUAL(c.name, "INS");
	BOOST_CHECK_EQUAL(c.file, "data.csv");

	// Defaults.
	BOOST_CHECK_EQUAL(c.options.csv.delimiter, ',');
	BOOST_CHECK_EQUAL(c.options.csv.quote, '"');
	BOOST_CHECK_EQUAL(c.options.csv.nullText, "");
	BOOST_CHECK(!c.options.header);
	BOOST_CHECK_EQUAL(c.options.batch, 1000u);
	BOOST_CHECK_EQUAL(c.options.commit, 0u);
	BOOST_CHECK_EQUAL(c.options.errors, 0u);
	BOOST_CHECK_EQUAL(c.options.decimal, '.');
	BOOST_CHECK_EQUAL(c.options.encoding, "");

	// Quotes in the file name, comments, line breaks.
	c = parseOk("EXEC SQL\n  ins -- the statement\n FROM /* x */ CSV 'C:\\data\\it''s.csv'");
	BOOST_CHECK_EQUAL(c.file, "C:\\data\\it's.csv");

	c = parseOk("EXEC SQL ins FROM CSV \"my file.csv\"");
	BOOST_CHECK_EQUAL(c.file, "my file.csv");
}

BOOST_AUTO_TEST_CASE(NotImportTest)
{
	BOOST_CHECK(notImport("exec sql ins using (1, 2)"));
	BOOST_CHECK(notImport("exec sql ins using batch (1), (2)"));
	BOOST_CHECK(notImport("exec sql ins"));
	BOOST_CHECK(notImport("execute procedure p"));
	BOOST_CHECK(notImport("select * from t"));
	BOOST_CHECK(notImport("input 'x.sql'"));
	BOOST_CHECK(notImport("importance"));
	BOOST_CHECK(notImport("'unclosed"));
	BOOST_CHECK(notImport(""));
}

BOOST_AUTO_TEST_CASE(ImportTest)
{
	Command c = parseOk("import into employee from csv 'emp.csv'");
	BOOST_CHECK(c.kind == Command::IMPORT);
	BOOST_CHECK_EQUAL(c.table, "employee");
	BOOST_CHECK_EQUAL(c.tableName, "EMPLOYEE");
	BOOST_CHECK(c.columns.empty());

	c = parseOk("IMPORT INTO \"Orders\" (\"Id\", amount,\"Order \"\"Date\"\"\") FROM CSV 'o.csv'");
	BOOST_CHECK_EQUAL(c.table, "\"Orders\"");
	BOOST_CHECK_EQUAL(c.tableName, "Orders");
	BOOST_REQUIRE_EQUAL(c.columns.size(), 3u);
	BOOST_CHECK_EQUAL(c.columns[0], "\"Id\"");
	BOOST_CHECK_EQUAL(c.columns[1], "amount");
	BOOST_CHECK_EQUAL(c.columns[2], "\"Order \"\"Date\"\"\"");
}

BOOST_AUTO_TEST_CASE(OptionsTest)
{
	Command c = parseOk("IMPORT INTO T FROM CSV 'f' OPTIONS (DELIMITER = ';', QUOTE = '''', HEADER = TRUE,"
		" SKIP = 2, NULL = '\\N', ENCODING = 'win1251', DECIMAL = ',', BATCH = 0, COMMIT = 100000, ERRORS = 5)");
	const Options& o = c.options;
	BOOST_CHECK_EQUAL(o.csv.delimiter, ';');
	BOOST_CHECK_EQUAL(o.csv.quote, '\'');
	BOOST_CHECK(o.header);
	BOOST_CHECK_EQUAL(o.skip, 2u);
	BOOST_CHECK_EQUAL(o.csv.nullText, "\\N");
	BOOST_CHECK_EQUAL(o.encoding, "WIN1251");
	BOOST_CHECK_EQUAL(o.decimal, ',');
	BOOST_CHECK_EQUAL(o.batch, 0u);
	BOOST_CHECK_EQUAL(o.commit, 100000u);
	BOOST_CHECK_EQUAL(o.errors, 5u);

	c = parseOk("exec sql x from csv 'f' options (delimiter = '\\t', header, encoding = utf8)");
	BOOST_CHECK_EQUAL(c.options.csv.delimiter, '\t');
	BOOST_CHECK(c.options.header);
	BOOST_CHECK_EQUAL(c.options.encoding, "UTF8");

	c = parseOk("exec sql x from csv 'f' options (header = false)");
	BOOST_CHECK(!c.options.header);
}

BOOST_AUTO_TEST_CASE(FormatTest)
{
	Command c = parseOk("exec sql ins from csv 'a.csv'");
	BOOST_CHECK(c.format == Command::CSV);
	BOOST_CHECK(!isBinary(c.format));

	// COPY: a tab and \N by default.
	c = parseOk("import into t from copy 'a.txt'");
	BOOST_CHECK(c.format == Command::COPY);
	BOOST_CHECK_EQUAL(c.options.csv.delimiter, '\t');
	BOOST_CHECK_EQUAL(c.options.csv.nullText, "\\N");

	c = parseOk("import into t from copy 'a.txt' options (delimiter = '|', null = '', header, encoding = win1251)");
	BOOST_CHECK_EQUAL(c.options.csv.delimiter, '|');
	BOOST_CHECK_EQUAL(c.options.csv.nullText, "");
	BOOST_CHECK(c.options.header);
	BOOST_CHECK_EQUAL(c.options.encoding, "WIN1251");

	c = parseOk("IMPORT INTO T (A, B) FROM PARQUET 'a.parquet' OPTIONS (SKIP = 10, BATCH = 500, COMMIT = 1000, ERRORS = 3)");
	BOOST_CHECK(c.format == Command::PARQUET);
	BOOST_CHECK(isBinary(c.format));
	BOOST_CHECK_EQUAL(c.options.skip, 10u);
	BOOST_CHECK_EQUAL(c.options.batch, 500u);
	BOOST_CHECK_EQUAL(c.options.commit, 1000u);
	BOOST_CHECK_EQUAL(c.options.errors, 3u);

	c = parseOk("exec sql ins from Avro 'a.avro'");
	BOOST_CHECK(c.format == Command::AVRO);
	BOOST_CHECK(isBinary(c.format));

	BOOST_REQUIRE_EQUAL(formatNames().size(), 4u);
	BOOST_CHECK_EQUAL(formatNames()[Command::PARQUET], "PARQUET");

	BOOST_CHECK_EQUAL(parseError("import into t from json 'a'"),
		"CSV, COPY, PARQUET or AVRO expected instead of \"json\"");
	BOOST_CHECK_EQUAL(parseError("import into t from copy 'a' options (quote = '\"')"),
		"option QUOTE is not for COPY files");
	BOOST_CHECK_EQUAL(parseError("import into t from parquet 'a' options (header)"),
		"option HEADER is not for PARQUET files");
	BOOST_CHECK_EQUAL(parseError("import into t from avro 'a' options (encoding = utf8)"),
		"option ENCODING is not for AVRO files");
	BOOST_CHECK_EQUAL(parseError("import into t from copy 'a' options (delimiter = '\\')"),
		"DELIMITER cannot be a backslash");
}

BOOST_AUTO_TEST_CASE(ErrorTest)
{
	BOOST_CHECK_EQUAL(parseError("import"), "INTO expected instead of the end of the statement");
	BOOST_CHECK_EQUAL(parseError("import t from csv 'f'"), "INTO expected instead of \"t\"");
	BOOST_CHECK_EQUAL(parseError("import into from csv 'f'"), "FROM expected instead of \"csv\"");
	BOOST_CHECK_EQUAL(parseError("import into t (a, ) from csv 'f'"),
		"column name expected instead of \")\"");
	BOOST_CHECK_EQUAL(parseError("import into t (a b) from csv 'f'"),
		"\",\" or \")\" expected in the column list instead of \"b\"");
	BOOST_CHECK_EQUAL(parseError("import into t from 'f'"), "CSV, COPY, PARQUET or AVRO expected instead of \"'f'\"");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv"),
		"file name in quotes expected instead of the end of the statement");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv ''"), "file name in quotes expected instead of \"''\"");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' extra"), "unexpected \"extra\"");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options delimiter = ';'"),
		"\"(\" expected after OPTIONS instead of \"delimiter\"");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (sep = ';')"),
		"unknown option \"sep\" (known: DELIMITER, QUOTE, HEADER, SKIP, NULL, ENCODING, DECIMAL, BATCH, COMMIT, ERRORS)");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (batch = 1, batch = 2)"),
		"option BATCH is given twice");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (delimiter = ';;')"),
		"bad value \"';;'\" of DELIMITER: one character in quotes expected, '\\t' is a tab");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (batch = -1)"),
		"bad value \"-1\" of BATCH: a number (0 or more) expected");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (decimal = ';')"),
		"bad value \"';'\" of DECIMAL: '.' or ',' expected");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (header = 1)"),
		"bad value \"1\" of HEADER: TRUE or FALSE expected");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (quote = ',')"),
		"DELIMITER and QUOTE must differ");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (batch 5)"),
		"\"=\" expected after BATCH instead of \"5\"");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f' options (batch = 5"),
		"\",\" or \")\" expected instead of the end of the statement");
	BOOST_CHECK_EQUAL(parseError("exec sql x from csv 'f"), "the string is not closed");
}

BOOST_AUTO_TEST_CASE(ColumnNameTest)
{
	BOOST_CHECK_EQUAL(columnName("id"), "\"ID\"");
	BOOST_CHECK_EQUAL(columnName("First_Name$2"), "\"FIRST_NAME$2\"");
	BOOST_CHECK_EQUAL(columnName("date"), "\"DATE\"");
	BOOST_CHECK_EQUAL(columnName("Order Date"), "\"Order Date\"");
	BOOST_CHECK_EQUAL(columnName("2nd"), "\"2nd\"");
	BOOST_CHECK_EQUAL(columnName("a\"b"), "\"a\"\"b\"");
	BOOST_CHECK_EQUAL(columnName("\xD0\x98\xD0\xBC\xD1\x8F"), "\"\xD0\x98\xD0\xBC\xD1\x8F\"");
}

BOOST_AUTO_TEST_SUITE_END()	// HqImportCommandTests

BOOST_AUTO_TEST_SUITE_END()	// HqImportCommandSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
