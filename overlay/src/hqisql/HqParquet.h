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

#ifndef FB_HQISQL_HQ_PARQUET_H
#define FB_HQISQL_HQ_PARQUET_H

#include "../hqisql/HqRows.h"

#include <memory>

namespace Hq {

// The reader of FROM PARQUET: an Apache Parquet file.
//
// The columns are the leaf columns of the schema; a column inside a group
// (a struct) is named group.column. A list or a map (a repeated column)
// cannot be read: such a column is reported when it is selected. The
// values as text: BOOLEAN TRUE / FALSE, integers (signed or unsigned),
// FLOAT, DOUBLE, DECIMAL as a number, DATE, TIME, TIMESTAMP and INT96 as
// dates and times (TIMESTAMP with isAdjustedToUTC in UTC), STRING, ENUM,
// JSON as text; a BYTE_ARRAY without a logical type, BSON and
// FIXED_LEN_BYTE_ARRAY as bytes (OCTETS), UUID as a UUID, FLOAT16 as a
// number.
//
// Data pages v1 and v2, dictionary pages; the encodings PLAIN,
// PLAIN_DICTIONARY, RLE_DICTIONARY, RLE, BIT_PACKED, DELTA_BINARY_PACKED,
// DELTA_LENGTH_BYTE_ARRAY, DELTA_BYTE_ARRAY, BYTE_STREAM_SPLIT; the codecs
// UNCOMPRESSED, SNAPPY, GZIP, LZ4, LZ4_RAW, ZSTD (see HqCodec.h). The file
// is read a page of every selected column at a time.
namespace Parquet {

class Reader : public Rows::Reader
{
public:
	explicit Reader(const Rows::RandomSource& source);
	~Reader();

	// Reads the footer. False with error().
	bool open();

	Result next(Rows::Record& record) override;
	bool select(const std::vector<unsigned>& indexes) override;

	unsigned line() const override
	{
		return row;
	}

	const char* unit() const override
	{
		return "row";
	}

	std::string raw(const Rows::Record& record) const override
	{
		return Rows::csvLine(record);
	}

	// The number of rows in the file (the footer).
	uint64_t rowCount() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl;
	unsigned row = 0;
};

}	// namespace Parquet
}	// namespace Hq

#endif	// FB_HQISQL_HQ_PARQUET_H
