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

#ifndef FB_HQISQL_HQ_CODEC_H
#define FB_HQISQL_HQ_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace Hq {

// The decompression of the PARQUET pages and the AVRO blocks. Snappy and
// LZ4 are decoded here; deflate and gzip need zlib (zlib1.dll, libz.so.1,
// as for the wire compression), zstd needs libzstd (libzstd.dll or
// zstd.dll, libzstd.so.1). The libraries are loaded on first use.
//
// Every function decompresses size bytes at data into out (replaced). When
// the size of the result is known (PARQUET), expected is it, otherwise 0.
// False with the reason in error.
namespace Codec {

bool snappy(const char* data, size_t size, size_t expected, std::string& out, std::string& error);

// A raw LZ4 block (PARQUET LZ4_RAW).
bool lz4Raw(const char* data, size_t size, size_t expected, std::string& out, std::string& error);

// PARQUET LZ4 (deprecated): the Hadoop framing, the LZ4 frame format or a
// raw block, as Arrow reads it.
bool lz4Hadoop(const char* data, size_t size, size_t expected, std::string& out, std::string& error);

// raw: RFC 1951 deflate (AVRO deflate), else gzip or zlib (PARQUET GZIP).
bool inflate(const char* data, size_t size, size_t expected, bool raw, std::string& out, std::string& error);

bool zstd(const char* data, size_t size, size_t expected, std::string& out, std::string& error);

// CRC-32 (ISO 3309), the checksum of the AVRO snappy blocks.
uint32_t crc32(const char* data, size_t size);

}	// namespace Codec
}	// namespace Hq

#endif	// FB_HQISQL_HQ_CODEC_H
