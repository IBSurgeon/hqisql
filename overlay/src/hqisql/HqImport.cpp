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
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../isql/isql.h"
#include "../isql/isql_proto.h"
#include "../hqisql/HqIsql.h"
#include "../hqisql/HqImport.h"
#include "../hqisql/HqAvro.h"
#include "../hqisql/HqCopy.h"
#include "../hqisql/HqParquet.h"
#include "../common/os/os_utils.h"
#include "../common/os/path_utils.h"
#include "../jrd/ods.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

extern Firebird::IAttachment* DB;

namespace
{

using namespace Firebird;
using Hq::Import::Command;

const unsigned MAX_VARCHAR = 32765;
const unsigned BATCH_BUFFER = 16 * 1024 * 1024;		// the server default batch buffer
const unsigned CS_DYNAMIC = 127;					// the connection charset
const unsigned CS_OCTETS = 1;

std::string format(const char* fmt, ...)
{
	char buf[BUFFER_LENGTH512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	return buf;
}

bool isNumeric(unsigned type)
{
	switch (type)
	{
		case SQL_SHORT:
		case SQL_LONG:
		case SQL_INT64:
		case SQL_INT128:
		case SQL_FLOAT:
		case SQL_DOUBLE:
		case SQL_D_FLOAT:
		case SQL_DEC16:
		case SQL_DEC34:
			return true;
	}

	return false;
}

bool isDateTime(unsigned type)
{
	switch (type)
	{
		case SQL_TIMESTAMP:
		case SQL_TYPE_TIME:
		case SQL_TIMESTAMP_TZ:
		case SQL_TIME_TZ:
		case SQL_TIMESTAMP_TZ_EX:
		case SQL_TIME_TZ_EX:
			return true;
	}

	return false;
}

bool hasTimeZone(unsigned type)
{
	return type == SQL_TIMESTAMP_TZ || type == SQL_TIME_TZ || type == SQL_TIMESTAMP_TZ_EX ||
		type == SQL_TIME_TZ_EX;
}

// Firebird keeps 4 digits of a fraction of a second and rejects more
// (PostgreSQL gives 6): the rest is cut off. The fraction is the digits
// after the point that follows the first colon (the time).
void cutFraction(std::string& value)
{
	const size_t colon = value.find(':');

	if (colon == std::string::npos)
		return;

	const size_t point = value.find('.', colon);

	if (point == std::string::npos)
		return;

	size_t end = point + 1;

	while (end < value.size() && value[end] >= '0' && value[end] <= '9')
		++end;

	if (end - point - 1 > 4)
		value.erase(point + 5, end - point - 5);
}

// The CSV reader as a Rows::Reader.
class CsvRows : public Hq::Rows::Reader
{
public:
	CsvRows(const Hq::Rows::Source& source, const Hq::Csv::Options& options)
		: reader(source, options)
	{
	}

	Result next(Hq::Rows::Record& record) override
	{
		const Hq::Csv::Reader::Result r = reader.next(record);
		errorText = reader.error();
		return r == Hq::Csv::Reader::RECORD ? RECORD : r == Hq::Csv::Reader::END ? END : FAILED;
	}

	unsigned line() const override
	{
		return reader.line();
	}

	std::string raw(const Hq::Rows::Record&) const override
	{
		return reader.raw();
	}

	bool hasBom() const override
	{
		return reader.hasBom();
	}

private:
	Hq::Csv::Reader reader;
};

std::string upper(const std::string& s)
{
	std::string result(s);

	for (char& c : result)
		c = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;

	return result;
}

bool seekTo(FILE* f, uint64_t offset, int whence)
{
#ifdef WIN_NT
	return _fseeki64(f, (__int64) offset, whence) == 0;
#else
	return fseeko(f, (off_t) offset, whence) == 0;
#endif
}

uint64_t tell(FILE* f)
{
#ifdef WIN_NT
	return uint64_t(_ftelli64(f));
#else
	return uint64_t(ftello(f));
#endif
}

std::string quoteName(const std::string& name)
{
	std::string result("\"");

	for (const char c : name)
	{
		result += c;

		if (c == '"')
			result += '"';
	}

	return result + '"';
}

class Importer
{
public:
	Importer(const Command& aCommand, const std::string& aSql)
		: command(aCommand),
		  options(aCommand.options),
		  sql(aSql),
		  master(fb_get_master_interface()),
		  statusMaster(master->getStatus()),
		  status(statusMaster),
		  name(aCommand.kind == Command::EXEC ? "EXEC SQL" : "IMPORT")
	{
	}

	~Importer()
	{
		if (DB)
		{
			CheckStatusWrapper st(statusMaster);
			DB->cancelOperation(&st, fb_cancel_disable);
		}

		if (batch)
			batch->release();

		freeStatement(single);
		freeStatement(stmt);

		if (inMeta)
			inMeta->release();

		if (msgMeta)
			msgMeta->release();

		if (outMeta)
			outMeta->release();

		if (file)
			fclose(file);

		if (badFile)
			fclose(badFile);

		statusMaster->dispose();
	}

	processing_state run();

private:
	bool failed()
	{
		return status.getState() & IStatus::STATE_ERRORS;
	}

	bool cancelled()
	{
		const ISC_STATUS* v = status.getErrors();
		return failed() && v[0] == isc_arg_gds && v[1] == isc_cancelled;
	}

	// An error of the command itself: "IMPORT: message".
	bool fail(const std::string& message)
	{
		STDERROUT((name + ": " + message).c_str());
		Hq::Isql::printErrorLocation();
		return false;
	}

	// "data.csv, line 5" or "data.parquet, row 5".
	std::string where(unsigned line)
	{
		return format("%s, %s %u", command.file.c_str(), reader ? reader->unit() : "line", line);
	}

	// An error at a line of the file: "data.csv, line 5: message".
	bool fileError(unsigned line, const std::string& message)
	{
		STDERROUT((where(line) + ": " + message).c_str());
		Hq::Isql::printErrorLocation();
		return false;
	}

	// An error of the file as a whole: "data.parquet: message".
	bool formatError(const std::string& message)
	{
		return fail(command.file + ": " + message);
	}

	// A server error.
	bool serverError()
	{
		ISQL_errmsg(&status);
		return false;
	}

	struct Param;

	bool openFile();
	bool openBinary();
	bool readHeader();
	bool query(const std::string& text, std::vector<std::string>& values);
	bool buildInsert();
	bool prepare();
	bool buildMessage();
	void freeStatement(IStatement*& statement);
	void createBatch();
	bool fillRow(const Hq::Rows::Record& record, unsigned char* msg, std::string& error);
	void convert(const Param& param, std::string& value);
	bool badRow(unsigned line, const std::string& raw, const std::string& message, bool server);
	bool savepoint(const char* text);
	bool flush();
	bool commitIfDue();
	void progress(bool done);
	void summary();

	unsigned char* message(unsigned i)
	{
		return messages.data() + size_t(i) * alignedLength;
	}

	const Command& command;
	const Hq::Import::Options& options;
	std::string sql;
	IMaster* master;
	IStatus* statusMaster;
	CheckStatusWrapper status;
	const std::string name;

	FILE* file = nullptr;
	Firebird::PathName path;
	bool bom = false;
	std::unique_ptr<Hq::Rows::Reader> reader;
	Hq::Rows::Record header;			// the names of the columns, if known
	std::string headerRaw;				// the first line of the .bad file
	std::vector<unsigned> selected;		// a binary format: the columns read

	ITransaction* tra = nullptr;
	IStatement* stmt = nullptr;
	IStatement* single = nullptr;		// replays the rows of a failed batch
	IMessageMetadata* inMeta = nullptr;
	IMessageMetadata* msgMeta = nullptr;
	IMessageMetadata* outMeta = nullptr;
	std::vector<unsigned char> outBuffer;
	IBatch* batch = nullptr;

	struct Param
	{
		enum Convert
		{
			NONE,
			UUID_TEXT,		// 16 bytes as the text of a UUID
			ADD_UTC			// a UTC time without a zone to a parameter with a zone
		};

		unsigned offset, nullOffset, length;
		bool numeric = false;
		bool boolean = false;
		bool dateTime = false;
		bool timeZone = false;
		Convert convert = NONE;
	};

	std::vector<Param> params;
	std::string converted;			// a value after convert()
	unsigned charset = CS_DYNAMIC;
	unsigned alignedLength = 0;
	unsigned rowsPerBatch = 1;

	// The rows waiting to be sent: their messages, lines and, for the .bad
	// file, the records as they are in the file.
	std::vector<unsigned char> messages;
	std::vector<unsigned> lines;
	std::vector<std::string> raws;
	unsigned pending = 0;

	unsigned done = 0;			// rows executed
	unsigned rejected = 0;		// rows written to the .bad file
	unsigned sinceCommit = 0;
	FILE* badFile = nullptr;
	Firebird::PathName badPath;

	std::chrono::steady_clock::time_point start, lastProgress;
	bool progressShown = false;
};

bool Importer::openFile()
{
	// A relative path is relative to the script being read, as for INPUT.
	const Firebird::PathName given(command.file.c_str());

	if (PathUtils::isRelative(given))
	{
		Firebird::PathName dir, last;
		PathUtils::splitLastComponent(dir, last, Hq::Isql::inputFileName());
		PathUtils::concatPath(path, dir, given);
	}
	else
		path = given;

	file = os_utils::fopen(path.c_str(), "rb");

	if (!file)
		return fail(format("cannot open file %s: %s", path.c_str(), strerror(errno)));

	if (Hq::Import::isBinary(command.format))
		return openBinary();

	unsigned char head[3];
	bom = fread(head, 1, 3, file) == 3 && memcmp(head, "\xEF\xBB\xBF", 3) == 0;
	rewind(file);

	FILE* const f = file;
	const Hq::Rows::Source source = [f](char* buffer, size_t size) {
		return fread(buffer, 1, size, f);
	};

	if (command.format == Command::COPY)
		reader.reset(FB_NEW Hq::Copy::Reader(source, options.csv.delimiter, options.csv.nullText));
	else
		reader.reset(FB_NEW CsvRows(source, options.csv));

	return true;
}

// PARQUET, AVRO: the reader with the schema of the file.
bool Importer::openBinary()
{
	FILE* const f = file;

	if (command.format == Command::PARQUET)
	{
		Hq::Rows::RandomSource source;

		if (!seekTo(f, 0, SEEK_END))
			return fail(format("cannot read file %s: %s", path.c_str(), strerror(errno)));

		source.size = tell(f);
		source.read = [f](uint64_t offset, char* buffer, size_t size) {
			return seekTo(f, offset, SEEK_SET) && fread(buffer, 1, size, f) == size;
		};

		Hq::Parquet::Reader* parquet = FB_NEW Hq::Parquet::Reader(source);
		reader.reset(parquet);

		if (!parquet->open())
			return formatError(parquet->error());
	}
	else
	{
		Hq::Avro::Reader* avro = FB_NEW Hq::Avro::Reader([f](char* buffer, size_t size) {
			return fread(buffer, 1, size, f);
		});
		reader.reset(avro);

		if (!avro->open())
			return formatError(avro->error());
	}

	return true;
}

// The text formats: the header line (HEADER). The binary formats: the
// columns to read - those of the IMPORT column list, else all - and their
// names as the header.
bool Importer::readHeader()
{
	if (Hq::Import::isBinary(command.format))
	{
		const std::vector<Hq::Rows::Column>& columns = reader->columns();

		if (columns.empty())
			return formatError("the file has no columns");

		if (command.columns.empty())
		{
			for (unsigned i = 0; i < columns.size(); ++i)
				selected.push_back(i);
		}

		for (const std::string& spelled : command.columns)
		{
			// A quoted name as it is, else in any case.
			const bool quoted = spelled.size() > 1 && spelled[0] == '"';
			std::string wanted;

			if (quoted)
			{
				for (size_t i = 1; i + 1 < spelled.size(); ++i)
				{
					wanted += spelled[i];

					if (spelled[i] == '"')
						++i;
				}
			}
			else
				wanted = upper(spelled);

			unsigned found = ~0u;

			for (unsigned i = 0; i < columns.size() && found == ~0u; ++i)
			{
				if (columns[i].name == wanted || (!quoted && upper(columns[i].name) == wanted))
					found = i;
			}

			if (found == ~0u)
			{
				std::string list;

				for (const Hq::Rows::Column& c : columns)
					list += (list.empty() ? "" : ", ") + c.name;

				return formatError("column " + spelled + " is not in the file (the file has: " + list + ")");
			}

			selected.push_back(found);
		}

		if (!reader->select(selected))
			return formatError(reader->error());

		for (const unsigned i : selected)
		{
			Hq::Rows::Field name;
			name.value = columns[i].name;
			header.push_back(name);
		}

		headerRaw = Hq::Rows::csvLine(header);

		// With a column list the INSERT takes the names as they are spelled.
		if (!command.columns.empty())
			header.clear();

		return true;
	}

	if (!options.header)
		return true;

	switch (reader->next(header))
	{
		case Hq::Rows::Reader::END:
			return fail(format("%s: the file is empty, the header is expected", command.file.c_str()));

		case Hq::Rows::Reader::FAILED:
			return fileError(reader->line(), reader->error());

		default:
			break;
	}

	headerRaw = reader->raw(header);
	return true;
}

// The first column of the rows of a query in the DML transaction; the
// column must be a VARCHAR.
bool Importer::query(const std::string& text, std::vector<std::string>& values)
{
	IResultSet* rs = DB->openCursor(&status, tra, 0, text.c_str(), isqlGlob.SQL_dialect,
		nullptr, nullptr, nullptr, nullptr, 0);

	if (failed())
		return serverError();

	IMessageMetadata* meta = rs->getMetadata(&status);
	bool ok = !failed() && (meta->getType(&status, 0) & ~1) == SQL_VARYING;

	if (ok)
	{
		std::vector<unsigned char> buffer(meta->getMessageLength(&status));
		const unsigned offset = meta->getOffset(&status, 0);
		const unsigned nullOffset = meta->getNullOffset(&status, 0);

		while (rs->fetchNext(&status, buffer.data()) == IStatus::RESULT_OK)
		{
			if (*(const short*) &buffer[nullOffset])
				continue;

			const unsigned char* p = &buffer[offset];
			values.push_back(std::string((const char*) p + 2, *(const unsigned short*) p));
		}

		ok = !failed();
	}

	if (!ok)
		serverError();

	if (meta)
		meta->release();

	rs->close(&status);

	if (failed())
		rs->release();

	return ok;
}

// IMPORT INTO: the INSERT for the given columns, the header or all the
// columns of the table that take a value.
bool Importer::buildInsert()
{
	std::vector<std::string> columns(command.columns);

	if (columns.empty() && !header.empty())
	{
		for (unsigned i = 0; i < header.size(); ++i)
		{
			if (header[i].null || header[i].value.empty())
				return fileError(1, format("column %u of the header has no name", i + 1));

			columns.push_back(Hq::Import::columnName(header[i].value));
		}
	}

	if (columns.empty())
	{
		std::string tableName;

		for (const char c : command.tableName)
		{
			tableName += c;

			if (c == '\'')
				tableName += '\'';
		}

		// Not computed and not GENERATED ALWAYS AS IDENTITY.
		std::string text =
			"SELECT CAST(TRIM(RF.RDB$FIELD_NAME) AS VARCHAR(63)) FROM RDB$RELATION_FIELDS RF"
			" JOIN RDB$FIELDS F ON F.RDB$FIELD_NAME = RF.RDB$FIELD_SOURCE"
			" WHERE RF.RDB$RELATION_NAME = '" + tableName + "' AND F.RDB$COMPUTED_BLR IS NULL";

		if (isqlGlob.major_ods >= ODS_VERSION12)
			text += " AND COALESCE(RF.RDB$IDENTITY_TYPE, 1) <> 0";

		text += " ORDER BY RF.RDB$FIELD_POSITION";

		std::vector<std::string> names;

		if (!query(text, names))
			return false;

		if (names.empty())
			return fail(format("table %s does not exist or has no columns to insert into", command.table.c_str()));

		for (const std::string& n : names)
			columns.push_back(quoteName(n));
	}

	sql = "INSERT INTO " + command.table + " (";

	for (size_t i = 0; i < columns.size(); ++i)
		sql += (i ? ", " : "") + columns[i];

	sql += ") VALUES (";

	for (size_t i = 0; i < columns.size(); ++i)
		sql += i ? ", ?" : "?";

	sql += ")";
	return true;
}

bool Importer::prepare()
{
	stmt = DB->prepare(&status, tra, 0, sql.c_str(), isqlGlob.SQL_dialect,
		IStatement::PREPARE_PREFETCH_METADATA);

	if (failed())
	{
		stmt = nullptr;
		return serverError();
	}

	switch (stmt->getType(&status))
	{
		case isc_info_sql_stmt_insert:
		case isc_info_sql_stmt_update:
		case isc_info_sql_stmt_delete:
		case isc_info_sql_stmt_exec_procedure:
			break;

		default:
			return fail("only INSERT, UPDATE, DELETE, MERGE, EXECUTE PROCEDURE and EXECUTE BLOCK can take the rows of a file");
	}

	outMeta = stmt->getOutputMetadata(&status);

	if (failed())
	{
		outMeta = nullptr;
		return serverError();
	}

	if (outMeta->getCount(&status) == 0)
	{
		outMeta->release();
		outMeta = nullptr;
	}
	else
		outBuffer.resize(outMeta->getMessageLength(&status));

	return true;
}

// Every parameter becomes a VARCHAR in the file charset (UTF8 for the
// binary formats; OCTETS for their bytes).
bool Importer::buildMessage()
{
	const bool binary = Hq::Import::isBinary(command.format);
	std::string encoding(options.encoding);

	if (binary || (encoding.empty() && bom))
		encoding = "UTF8";

	if (!encoding.empty())
	{
		if (encoding.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
			return fail("unknown character set " + encoding);

		std::vector<std::string> ids;

		if (!query("SELECT CAST(RDB$CHARACTER_SET_ID AS VARCHAR(10)) FROM RDB$CHARACTER_SETS"
				" WHERE RDB$CHARACTER_SET_NAME = '" + encoding + "'"
				" UNION ALL SELECT CAST(RDB$TYPE AS VARCHAR(10)) FROM RDB$TYPES"
				" WHERE RDB$FIELD_NAME = 'RDB$CHARACTER_SET_NAME' AND RDB$TYPE_NAME = '" + encoding + "'", ids))
		{
			return false;
		}

		if (ids.empty())
			return fail("unknown character set " + encoding);

		charset = (unsigned) atoi(ids[0].c_str());
	}

	inMeta = stmt->getInputMetadata(&status);

	if (failed())
	{
		inMeta = nullptr;
		return serverError();
	}

	const unsigned count = inMeta->getCount(&status);

	if (count == 0)
		return fail("the statement has no parameters");

	if (binary && selected.size() != count)
	{
		return fail(format("the file has %u columns, the statement has %u parameters",
			unsigned(selected.size()), count));
	}

	IMetadataBuilder* builder = inMeta->getBuilder(&status);

	if (failed())
		return serverError();

	params.resize(count);

	for (unsigned i = 0; i < count && !failed(); ++i)
	{
		const unsigned type = inMeta->getType(&status, i) & ~1;
		unsigned length = 64;

		switch (type)
		{
			case SQL_TEXT:
			case SQL_VARYING:
				// Bytes in the connection charset; up to 4 bytes a character
				// in another one.
				length = inMeta->getLength(&status, i) * (encoding.empty() ? 1 : 4);
				break;

			case SQL_BLOB:
				length = MAX_VARCHAR;
				break;

			case SQL_ARRAY:
				builder->release();
				return fail(format("parameter %u is an array, arrays are not supported", i + 1));

			case SQL_TIMESTAMP_TZ:
			case SQL_TIME_TZ:
			case SQL_TIMESTAMP_TZ_EX:
			case SQL_TIME_TZ_EX:
				length = 128;		// with a long time zone name
				break;
		}

		Param& param = params[i];
		param.length = std::min(std::max(length, 1u), MAX_VARCHAR);
		param.numeric = isNumeric(type);
		param.boolean = type == SQL_BOOLEAN;
		param.dateTime = isDateTime(type);
		param.timeZone = hasTimeZone(type);
		unsigned paramCharset = charset;

		if (binary)
		{
			// Bytes go as they are. A UUID is bytes for a parameter of
			// bytes (CHAR(16) CHARACTER SET OCTETS, a binary BLOB), else text.
			const bool octets = ((type == SQL_TEXT || type == SQL_VARYING) && inMeta->getCharSet(&status, i) == CS_OCTETS) ||
				(type == SQL_BLOB && inMeta->getSubType(&status, i) == 0);

			switch (reader->columns()[selected[i]].kind)
			{
				case Hq::Rows::Column::BINARY:
					paramCharset = CS_OCTETS;
					break;

				case Hq::Rows::Column::UUID:
					if (octets)
						paramCharset = CS_OCTETS;
					else
						param.convert = Param::UUID_TEXT;

					break;

				case Hq::Rows::Column::UTC:
					if (param.timeZone)
						param.convert = Param::ADD_UTC;

					break;

				default:
					break;
			}
		}

		builder->setType(&status, i, SQL_VARYING + 1);
		builder->setSubType(&status, i, 0);
		builder->setScale(&status, i, 0);
		builder->setLength(&status, i, param.length);
		builder->setCharSet(&status, i, paramCharset);
	}

	if (!failed())
		msgMeta = builder->getMetadata(&status);

	builder->release();

	if (failed())
	{
		msgMeta = nullptr;
		return serverError();
	}

	for (unsigned i = 0; i < count; ++i)
	{
		params[i].offset = msgMeta->getOffset(&status, i);
		params[i].nullOffset = msgMeta->getNullOffset(&status, i);
	}

	alignedLength = msgMeta->getAlignedLength(&status);

	if (failed())
		return serverError();

	return true;
}

void Importer::freeStatement(IStatement*& statement)
{
	if (!statement)
		return;

	status.init();
	statement->free(&status);

	if (failed())
		statement->release();

	statement = nullptr;
	status.init();
}

// IBatch, unless BATCH = 0 or the server does not support it (protocol
// below 16, Firebird 3): then a row at a time.
void Importer::createBatch()
{
	if (options.batch)
	{
		IXpbBuilder* bpb = master->getUtilInterface()->getXpbBuilder(&status, IXpbBuilder::BATCH, nullptr, 0);

		if (!failed())
		{
			bpb->insertInt(&status, IBatch::TAG_RECORD_COUNTS, 1);
			batch = stmt->createBatch(&status, msgMeta, bpb->getBufferLength(&status), bpb->getBuffer(&status));
			bpb->dispose();
		}

		if (failed())
			batch = nullptr;

		status.init();
	}

	rowsPerBatch = batch ? std::min(options.batch, std::max(1u, BATCH_BUFFER / alignedLength)) : 1;

	if (options.commit)
		rowsPerBatch = std::min(rowsPerBatch, options.commit);

	messages.resize(size_t(rowsPerBatch) * alignedLength);
	lines.resize(rowsPerBatch);

	if (options.errors)
		raws.resize(rowsPerBatch);
}

bool Importer::fillRow(const Hq::Rows::Record& record, unsigned char* msg, std::string& error)
{
	if (record.size() != params.size())
	{
		error = format("%u field(s) expected, %u found", unsigned(params.size()), unsigned(record.size()));
		return false;
	}

	for (size_t i = 0; i < params.size(); ++i)
	{
		const Param& param = params[i];
		short* const nullFlag = (short*) (msg + param.nullOffset);

		if (record[i].null)
		{
			*nullFlag = -1;
			continue;
		}

		const std::string* value = &record[i].value;

		if (param.convert != Param::NONE || param.boolean || param.dateTime)
		{
			converted = *value;
			convert(param, converted);
			value = &converted;
		}

		if (value->size() > param.length)
		{
			error = format("field %u is too long: %u bytes, at most %u", unsigned(i + 1),
				unsigned(value->size()), param.length);
			return false;
		}

		*nullFlag = 0;
		unsigned char* const p = msg + param.offset;
		*(unsigned short*) p = (unsigned short) value->size();
		memcpy(p + 2, value->data(), value->size());

		if (param.numeric && options.decimal == ',')
			std::replace((char*) p + 2, (char*) p + 2 + value->size(), ',', '.');
	}

	return true;
}

// A value as the server reads it: the text formats get what PostgreSQL
// writes - t / f for a BOOLEAN, 6 digits of a second, a zone offset of
// hours (+03); the binary formats a UUID as text, a zone for a UTC time.
void Importer::convert(const Param& param, std::string& value)
{
	switch (param.convert)
	{
		case Param::UUID_TEXT:
			if (value.size() == 16)
				value = Hq::Rows::formatUuid(value);

			return;

		case Param::ADD_UTC:
			value += " +00:00";
			return;

		default:
			break;
	}

	if (Hq::Import::isBinary(command.format))
		return;

	if (param.boolean && value.size() == 1)
	{
		switch (value[0])
		{
			case 't': case 'T': case 'y': case 'Y': case '1':
				value = "TRUE";
				break;

			case 'f': case 'F': case 'n': case 'N': case '0':
				value = "FALSE";
				break;
		}
	}

	if (param.dateTime)
	{
		cutFraction(value);

		// +03 is +03:00.
		const size_t n = value.size();

		if (param.timeZone && n > 3 && (value[n - 3] == '+' || value[n - 3] == '-') &&
			isdigit((unsigned char) value[n - 2]) && isdigit((unsigned char) value[n - 1]) &&
			value.find(':') < n - 3)
		{
			value += ":00";
		}
	}
}

// A row is bad: it is written to the .bad file when ERRORS allows, else
// the error is printed and the import stops (false).
bool Importer::badRow(unsigned line, const std::string& raw, const std::string& message, bool server)
{
	if (rejected < options.errors)
	{
		if (!badFile)
		{
			badPath = path + ".bad";
			badFile = os_utils::fopen(badPath.c_str(), "wb");

			if (!badFile)
				return fail(format("cannot create file %s: %s", badPath.c_str(), strerror(errno)));

			if (!headerRaw.empty())
				fprintf(badFile, "%s\n", headerRaw.c_str());
		}

		++rejected;
		fwrite(raw.data(), 1, raw.size(), badFile);
		fputc('\n', badFile);

		std::string text(message);

		if (server)
		{
			char buf[BUFFER_LENGTH512];
			master->getUtilInterface()->formatStatus(buf, sizeof(buf), &status);
			text = buf;
			std::replace(text.begin(), text.end(), '\n', ' ');
		}

		STDERROUT((where(line) + ": rejected: " + text).c_str());
		status.init();
		return true;
	}

	if (options.errors)
	{
		STDERROUT(format("%s: more than %u bad rows, the import stops", command.file.c_str(),
			options.errors).c_str());
	}

	if (server)
	{
		STDERROUT((where(line) + ":").c_str());
		serverError();
	}
	else
		fileError(line, message);

	return false;
}

bool Importer::savepoint(const char* text)
{
	DB->execute(&status, tra, 0, text, isqlGlob.SQL_dialect, nullptr, nullptr, nullptr, nullptr);
	return failed() ? serverError() : true;
}

// Sends the pending rows. False when the import stops; the error is printed.
bool Importer::flush()
{
	if (!pending)
		return true;

	const unsigned count = pending;
	pending = 0;
	sinceCommit += count;
	IStatement* statement = stmt;

	if (batch)
	{
		if (!savepoint("SAVEPOINT HQ$IMPORT"))
			return false;

		bool ok = false;
		batch->add(&status, count, messages.data());

		if (!failed())
		{
			IBatchCompletionState* cs = batch->execute(&status, tra);

			if (!failed())
			{
				ok = cs->findError(&status, 0) == IBatchCompletionState::NO_MORE_ERRORS;
				cs->dispose();
			}
		}

		if (ok)
		{
			done += count;
			return savepoint("RELEASE SAVEPOINT HQ$IMPORT ONLY");
		}

		const bool wasCancelled = cancelled();

		// The rows that were not sent stay in the batch after an exception.
		status.init();
		batch->cancel(&status);
		status.init();

		if (!savepoint("ROLLBACK TO SAVEPOINT HQ$IMPORT"))
			return false;

		if (wasCancelled)
		{
			fail("cancelled at " + where(lines[0]));
			return false;
		}

		// The remote server does not execute a statement that has a batch
		// ("Attempt to reopen an open cursor"): the rows go through a copy.
		if (!single)
		{
			single = DB->prepare(&status, tra, 0, sql.c_str(), isqlGlob.SQL_dialect, 0);

			if (failed())
			{
				single = nullptr;
				return serverError();
			}
		}

		statement = single;
	}

	// A row at a time: without IBatch, or again after a batch failed, to
	// find the bad rows.
	for (unsigned i = 0; i < count; ++i)
	{
		statement->execute(&status, tra, msgMeta, message(i), outMeta, outBuffer.data());

		if (!failed())
		{
			++done;
			continue;
		}

		if (cancelled())
		{
			fail("cancelled at " + where(lines[i]));
			return false;
		}

		if (!badRow(lines[i], options.errors ? raws[i] : std::string(), "", true))
			return false;
	}

	return batch ? savepoint("RELEASE SAVEPOINT HQ$IMPORT ONLY") : true;
}

bool Importer::commitIfDue()
{
	if (!options.commit || sinceCommit < options.commit)
		return true;

	sinceCommit = 0;

	if (!Hq::Isql::commitUserTransaction())
		return false;

	tra = Hq::Isql::userTransaction();
	return tra != nullptr;
}

// Interactive: the number of rows once a second, on stderr.
void Importer::progress(bool finished)
{
	if (!Hq::Isql::interactive())
		return;

	if (finished)
	{
		if (progressShown)
			fprintf(stderr, "\r%40s\r", "");

		return;
	}

	const auto now = std::chrono::steady_clock::now();

	if (now - lastProgress < std::chrono::seconds(1))
		return;

	lastProgress = now;
	progressShown = true;
	fprintf(stderr, "\r%u rows...", done + rejected + pending);
	fflush(stderr);
}

void Importer::summary()
{
	progress(true);

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::string text = format("%u rows %s", done, command.kind == Command::IMPORT ? "imported" : "executed");

	if (rejected)
		text += format(", %u rejected (%s)", rejected, badPath.c_str());

	if (rejected && Hq::Isql::quiet())
		STDERROUT(text.c_str());
	else if (!Hq::Isql::quiet())
		isqlGlob.printf("%s, %.3f s%s", text.c_str(), seconds, NEWLINE);
}

processing_state Importer::run()
{
	if (!DB)
	{
		fail("no database is connected");
		return ps_ERR;
	}

	start = lastProgress = std::chrono::steady_clock::now();

	tra = Hq::Isql::userTransaction();

	if (!tra || !openFile() || !readHeader() ||
		(command.kind == Command::IMPORT && !buildInsert()) || !prepare() || !buildMessage())
	{
		return ps_ERR;
	}

	createBatch();

	// Ctrl-C cancels the batch being executed, as for a statement.
	DB->cancelOperation(&status, fb_cancel_enable);
	status.init();

	Hq::Rows::Record record;
	unsigned skip = options.skip;
	bool ok = true;

	while (ok)
	{
		if (Hq::Isql::interrupted())
		{
			fail("cancelled at " + where(reader->line()));
			ok = false;
			break;
		}

		const Hq::Rows::Reader::Result r = reader->next(record);

		if (r == Hq::Rows::Reader::END)
			break;

		if (r == Hq::Rows::Reader::FAILED)
		{
			// The rest of the file cannot be read: the rows before go first.
			ok = flush() && fileError(reader->line(), reader->error());
			break;
		}

		if (skip)
		{
			--skip;
			continue;
		}

		std::string error;

		if (!fillRow(record, message(pending), error))
		{
			// The rows before go first.
			ok = flush() && commitIfDue() && badRow(reader->line(), reader->raw(record), error, false);
			continue;
		}

		lines[pending] = reader->line();

		if (options.errors)
			raws[pending] = reader->raw(record);

		if (++pending == rowsPerBatch || (options.commit && sinceCommit + pending >= options.commit))
			ok = flush() && commitIfDue();

		progress(false);
	}

	ok = ok && flush();

	// COMMIT = n commits the rest too.
	if (ok && options.commit && sinceCommit)
	{
		sinceCommit = options.commit;
		ok = commitIfDue();
	}

	summary();
	return ok ? SKIP : ps_ERR;
}

}	// anonymous namespace


namespace Hq {

processing_state runImport(const Import::Command& command, const std::string& sql)
{
	Importer importer(command, sql);
	return importer.run();
}

}	// namespace Hq
