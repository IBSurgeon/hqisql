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
 *  Tests of the decompression of the import readers (Hq::Codec). The
 *  compressed data is made by HqFixtures.py. zlib and zstd are loaded at
 *  run time: without them the tests check the error.
 */

#include "firebird.h"
#include "boost/test/unit_test.hpp"
#include "../hqisql/HqCodec.h"
#include "HqCodecFixtures.h"
#include <string>

using namespace Hq;
using namespace HqFixtures;

BOOST_AUTO_TEST_SUITE(HqisqlSuite)
BOOST_AUTO_TEST_SUITE(HqCodecSuite)


BOOST_AUTO_TEST_SUITE(HqCodecTests)

typedef bool Decoder(const char* data, size_t size, size_t expected, std::string& out, std::string& error);

// Decodes the data with and without the expected size; a damaged copy
// fails.
static void check(Decoder* decode, const std::string& data, const char* name)
{
	std::string out, error;

	BOOST_CHECK_MESSAGE(decode(data.data(), data.size(), CODEC_TEXT.size(), out, error), name << ": " << error);
	BOOST_CHECK_MESSAGE(out == CODEC_TEXT, name);

	BOOST_CHECK_MESSAGE(decode(data.data(), data.size(), 0, out, error), name << ": " << error);
	BOOST_CHECK_MESSAGE(out == CODEC_TEXT, name);

	// The wrong size.
	BOOST_CHECK_MESSAGE(!decode(data.data(), data.size(), CODEC_TEXT.size() - 1, out, error), name);
	BOOST_CHECK_MESSAGE(!error.empty(), name);

	// Cut off.
	BOOST_CHECK_MESSAGE(!decode(data.data(), data.size() / 2, CODEC_TEXT.size(), out, error), name);
}

BOOST_AUTO_TEST_CASE(SnappyTest)
{
	check(Codec::snappy, CODEC_SNAPPY, "snappy");

	std::string out, error;
	BOOST_CHECK(Codec::snappy("\0", 1, 0, out, error));
	BOOST_CHECK(out.empty());

	// A copy before the start of the data.
	BOOST_CHECK(!Codec::snappy("\x04\x05\x01", 3, 0, out, error));
	BOOST_CHECK_EQUAL(error, "the snappy data is damaged");
}

BOOST_AUTO_TEST_CASE(Lz4Test)
{
	check(Codec::lz4Raw, CODEC_LZ4_RAW, "lz4 raw");
	check(Codec::lz4Hadoop, CODEC_LZ4_HADOOP, "lz4 hadoop");
	check(Codec::lz4Hadoop, CODEC_LZ4_FRAME, "lz4 frame");
	check(Codec::lz4Hadoop, CODEC_LZ4_RAW, "lz4 hadoop raw");
}

static bool inflateRaw(const char* data, size_t size, size_t expected, std::string& out, std::string& error)
{
	return Codec::inflate(data, size, expected, true, out, error);
}

static bool inflateGzip(const char* data, size_t size, size_t expected, std::string& out, std::string& error)
{
	return Codec::inflate(data, size, expected, false, out, error);
}

BOOST_AUTO_TEST_CASE(InflateTest)
{
	std::string out, error;

	if (!inflateRaw(CODEC_DEFLATE.data(), CODEC_DEFLATE.size(), 0, out, error) &&
		error.find("needs") != std::string::npos)
	{
		BOOST_TEST_MESSAGE("no zlib: " << error);
		return;
	}

	check(inflateRaw, CODEC_DEFLATE, "deflate");
	check(inflateGzip, CODEC_GZIP, "gzip");

	// Two gzip members are one stream.
	BOOST_CHECK(inflateGzip((CODEC_GZIP + CODEC_GZIP).data(), CODEC_GZIP.size() * 2, 0, out, error));
	BOOST_CHECK(out == CODEC_TEXT + CODEC_TEXT);
}

BOOST_AUTO_TEST_CASE(ZstdTest)
{
	std::string out, error;

	if (!Codec::zstd(CODEC_ZSTD.data(), CODEC_ZSTD.size(), 0, out, error) &&
		error.find("needs") != std::string::npos)
	{
		BOOST_TEST_MESSAGE("no zstd: " << error);
		return;
	}

	check(Codec::zstd, CODEC_ZSTD, "zstd");
}

BOOST_AUTO_TEST_CASE(Crc32Test)
{
	BOOST_CHECK_EQUAL(Codec::crc32("123456789", 9), 0xCBF43926u);
	BOOST_CHECK_EQUAL(Codec::crc32("", 0), 0u);
}

BOOST_AUTO_TEST_SUITE_END()	// HqCodecTests

BOOST_AUTO_TEST_SUITE_END()	// HqCodecSuite
BOOST_AUTO_TEST_SUITE_END()	// HqisqlSuite
