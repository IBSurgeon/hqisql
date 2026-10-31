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

#ifndef FB_HQISQL_HQ_AVRO_H
#define FB_HQISQL_HQ_AVRO_H

#include "../hqisql/HqRows.h"

#include <memory>

namespace Hq {

// The reader of FROM AVRO: an Avro object container file (Avro 1.11).
//
// The columns are the fields of the top-level record (a schema of another
// type is one column, VALUE). A union with null is a nullable column. The
// values as text: boolean TRUE / FALSE, numbers, string, enum symbol;
// date, time-*, timestamp-*, local-timestamp-* as dates and times
// (timestamp-* in UTC); decimal as a number; bytes and fixed as bytes
// (OCTETS), uuid on fixed as a UUID; record, array and map as JSON. The
// codecs: null, deflate, snappy, zstandard (see HqCodec.h). The file is read
// block by block.
namespace Avro {

struct Schema;

class Reader : public Rows::Reader
{
public:
	explicit Reader(const Rows::Source& source);
	~Reader();

	// Reads the header and the schema. False with error().
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

private:
	class Impl;
	std::unique_ptr<Impl> impl;
	unsigned row = 0;
};

}	// namespace Avro
}	// namespace Hq

#endif	// FB_HQISQL_HQ_AVRO_H
