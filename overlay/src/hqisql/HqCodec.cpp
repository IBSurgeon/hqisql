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
#include "../hqisql/HqCodec.h"

#ifdef WIN_NT
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#include <string.h>
#include <algorithm>
#include <memory>

namespace
{

// The largest result accepted when the size is not known: a damaged
// length must not take all the memory.
const size_t MAX_OUTPUT = size_t(1) << 31;

uint32_t le32(const unsigned char* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}

uint32_t be32(const unsigned char* p)
{
	return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

bool damaged(std::string& error, const char* codec)
{
	error = std::string("the ") + codec + " data is damaged";
	return false;
}

// Copies length bytes from offset bytes back; the ranges may overlap.
bool copyBack(std::string& out, size_t offset, size_t length)
{
	if (offset == 0 || offset > out.size())
		return false;

	const size_t from = out.size() - offset;

	for (size_t i = 0; i < length; ++i)
		out += out[from + i];

	return true;
}

// One LZ4 block appended to out; false when it is damaged.
bool lz4Block(const unsigned char* p, const unsigned char* const end, std::string& out, size_t limit)
{
	while (p < end)
	{
		const unsigned token = *p++;
		size_t literals = token >> 4;

		if (literals == 15)
		{
			unsigned char b;

			do
			{
				if (p >= end)
					return false;

				b = *p++;
				literals += b;
			} while (b == 255);
		}

		if (size_t(end - p) < literals || out.size() + literals > limit)
			return false;

		out.append((const char*) p, literals);
		p += literals;

		// The last sequence has only the literals.
		if (p == end)
			break;

		if (end - p < 2)
			return false;

		const size_t offset = p[0] | (p[1] << 8);
		p += 2;
		size_t length = token & 15;

		if (length == 15)
		{
			unsigned char b;

			do
			{
				if (p >= end)
					return false;

				b = *p++;
				length += b;
			} while (b == 255);
		}

		length += 4;

		if (out.size() + length > limit || !copyBack(out, offset, length))
			return false;
	}

	return true;
}

// The LZ4 frame format (lz4 command line tool, LZ4F).
bool lz4Frame(const unsigned char* p, const unsigned char* const end, std::string& out, size_t limit)
{
	if (end - p < 7 || le32(p) != 0x184D2204)
		return false;

	p += 4;
	const unsigned flags = *p++;
	p++;	// BD

	if ((flags >> 6) != 1)
		return false;

	if (flags & 8)
		p += 8;		// content size

	if (flags & 1)
		p += 4;		// dictionary id

	p++;	// header checksum

	for (;;)
	{
		if (end - p < 4)
			return false;

		const uint32_t blockSize = le32(p);
		p += 4;

		if (blockSize == 0)
			break;

		const size_t size = blockSize & 0x7FFFFFFF;

		if (size_t(end - p) < size)
			return false;

		if (blockSize & 0x80000000)
		{
			if (out.size() + size > limit)
				return false;

			out.append((const char*) p, size);
		}
		else if (!lz4Block(p, p + size, out, limit))
			return false;

		p += size;

		if (flags & 0x10)
			p += 4;		// block checksum
	}

	return true;
}

// A shared library loaded once and never unloaded. Windows: the directory
// of the program first (zlib1.dll is in the Firebird directory, with
// hqisql.exe), then the usual search; POSIX: the usual dlopen() search.
class Library
{
public:
	explicit Library(const char* name)
	{
#ifdef WIN_NT
		char path[MAX_PATH];
		const DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));

		if (n > 0 && n < sizeof(path))
		{
			std::string full(path, n);
			const size_t slash = full.find_last_of("\\/");

			if (slash != std::string::npos)
			{
				full.erase(slash + 1);
				full += name;
				handle = LoadLibraryExA(full.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
			}
		}

		if (!handle)
			handle = LoadLibraryA(name);
#else
		handle = dlopen(name, RTLD_NOW);
#endif
	}

	bool loaded() const
	{
		return handle != NULL;
	}

	template <typename T>
	bool find(const char* symbol, T& function)
	{
#ifdef WIN_NT
		function = handle ? (T) GetProcAddress(handle, symbol) : nullptr;
#else
		function = handle ? (T) dlsym(handle, symbol) : nullptr;
#endif
		return function != nullptr;
	}

private:
#ifdef WIN_NT
	HMODULE handle = NULL;
#else
	void* handle = NULL;
#endif
};

#ifdef HAVE_ZLIB_H
// zlib with inflateInit2_ (gzip and raw deflate) that Firebird::ZLib does
// not have.
class ZLibInflate
{
public:
	ZLibInflate()
#ifdef WIN_NT
		: library("zlib1.dll")
#else
		: library("libz." SHRLIB_EXT ".1")
#endif
	{
		ok = library.find("inflateInit2_", inflateInit2_) &&
			library.find("inflate", inflate) &&
			library.find("inflateEnd", inflateEnd);
	}

	int (ZEXPORT* inflateInit2_)(z_stream* strm, int windowBits, const char* version, int streamSize) = nullptr;
	int (ZEXPORT* inflate)(z_stream* strm, int flush) = nullptr;
	int (ZEXPORT* inflateEnd)(z_stream* strm) = nullptr;
	Library library;
	bool ok = false;
};
#endif

// The stable zstd streaming API, declared here: no zstd headers are needed.
struct ZstdInBuffer
{
	const void* src;
	size_t size;
	size_t pos;
};

struct ZstdOutBuffer
{
	void* dst;
	size_t size;
	size_t pos;
};

class ZStdLib
{
public:
	ZStdLib()
	{
#ifdef WIN_NT
		const char* const names[] = {"libzstd.dll", "zstd.dll"};
#else
		const char* const names[] = {"libzstd." SHRLIB_EXT ".1", "libzstd." SHRLIB_EXT};
#endif

		for (const char* name : names)
		{
			library.reset(new Library(name));

			if (library->find("ZSTD_createDStream", createDStream) &&
				library->find("ZSTD_freeDStream", freeDStream) &&
				library->find("ZSTD_decompressStream", decompressStream) &&
				library->find("ZSTD_isError", isError) &&
				library->find("ZSTD_getErrorName", getErrorName))
			{
				ok = true;
				break;
			}
		}
	}

	void* (*createDStream)() = nullptr;
	size_t (*freeDStream)(void* stream) = nullptr;
	size_t (*decompressStream)(void* stream, ZstdOutBuffer* output, ZstdInBuffer* input) = nullptr;
	unsigned (*isError)(size_t code) = nullptr;
	const char* (*getErrorName)(size_t code) = nullptr;
	std::unique_ptr<Library> library;
	bool ok = false;
};

}	// anonymous namespace


namespace Hq {
namespace Codec {

bool snappy(const char* data, size_t size, size_t expected, std::string& out, std::string& error)
{
	const unsigned char* p = (const unsigned char*) data;
	const unsigned char* const end = p + size;
	out.clear();

	// The length of the result: a varint.
	uint64_t length = 0;

	for (int shift = 0;; shift += 7)
	{
		if (p >= end || shift > 35)
			return damaged(error, "snappy");

		length |= uint64_t(*p & 0x7F) << shift;

		if (!(*p++ & 0x80))
			break;
	}

	if ((expected && length != expected) || length > MAX_OUTPUT)
		return damaged(error, "snappy");

	out.reserve(length);

	while (p < end)
	{
		const unsigned tag = *p++;
		size_t len, offset;

		switch (tag & 3)
		{
			case 0:		// literal
				len = tag >> 2;

				if (len >= 60)
				{
					const unsigned bytes = unsigned(len) - 59;

					if (size_t(end - p) < bytes)
						return damaged(error, "snappy");

					len = 0;

					for (unsigned i = 0; i < bytes; ++i)
						len |= size_t(p[i]) << (8 * i);

					p += bytes;
				}

				++len;

				if (size_t(end - p) < len || out.size() + len > length)
					return damaged(error, "snappy");

				out.append((const char*) p, len);
				p += len;
				continue;

			case 1:
				if (p >= end)
					return damaged(error, "snappy");

				len = ((tag >> 2) & 7) + 4;
				offset = ((tag >> 5) << 8) | *p++;
				break;

			case 2:
				if (end - p < 2)
					return damaged(error, "snappy");

				len = (tag >> 2) + 1;
				offset = p[0] | (p[1] << 8);
				p += 2;
				break;

			default:
				if (end - p < 4)
					return damaged(error, "snappy");

				len = (tag >> 2) + 1;
				offset = le32(p);
				p += 4;
				break;
		}

		if (out.size() + len > length || !copyBack(out, offset, len))
			return damaged(error, "snappy");
	}

	if (out.size() != length)
		return damaged(error, "snappy");

	return true;
}

bool lz4Raw(const char* data, size_t size, size_t expected, std::string& out, std::string& error)
{
	out.clear();
	const size_t limit = expected ? expected : MAX_OUTPUT;
	out.reserve(expected);

	const unsigned char* p = (const unsigned char*) data;

	if (!lz4Block(p, p + size, out, limit) || (expected && out.size() != expected))
		return damaged(error, "LZ4");

	return true;
}

bool lz4Hadoop(const char* data, size_t size, size_t expected, std::string& out, std::string& error)
{
	const unsigned char* const start = (const unsigned char*) data;
	const unsigned char* const end = start + size;
	const size_t limit = expected ? expected : MAX_OUTPUT;

	// The Hadoop framing: blocks of (uncompressed size, compressed size,
	// LZ4 block), the sizes big-endian.
	out.clear();
	const unsigned char* p = start;
	bool ok = true;

	while (ok && p < end)
	{
		if (end - p < 8)
		{
			ok = false;
			break;
		}

		const uint32_t rawSize = be32(p);
		const uint32_t blockSize = be32(p + 4);
		p += 8;

		if (size_t(end - p) < blockSize)
		{
			ok = false;
			break;
		}

		const size_t before = out.size();
		ok = lz4Block(p, p + blockSize, out, limit) && out.size() - before == rawSize;
		p += blockSize;
	}

	if (ok && (!expected || out.size() == expected))
		return true;

	out.clear();

	if (lz4Frame(start, end, out, limit) && (!expected || out.size() == expected))
		return true;

	return lz4Raw(data, size, expected, out, error);
}

bool inflate(const char* data, size_t size, size_t expected, bool raw, std::string& out, std::string& error)
{
#ifdef HAVE_ZLIB_H
	static ZLibInflate zlib;

	if (!zlib.ok)
	{
#ifdef WIN_NT
		error = "the deflate and gzip compression needs zlib1.dll";
#else
		error = "the deflate and gzip compression needs libz.so.1";
#endif
		return false;
	}

	z_stream stream;
	memset(&stream, 0, sizeof(stream));

	// 15: the largest window; + 32: gzip or zlib, by the header; negative:
	// raw deflate.
	if (zlib.inflateInit2_(&stream, raw ? -15 : 15 + 32, ZLIB_VERSION, int(sizeof(z_stream))) != Z_OK)
		return damaged(error, "deflate");

	// One byte more than expected: the end of the stream is seen then.
	out.resize(expected ? expected + 1 : std::max(size * 4, size_t(64 * 1024)));
	stream.next_in = (Bytef*) data;
	stream.avail_in = (uInt) size;
	size_t produced = 0;
	int rc = Z_OK;

	for (;;)
	{
		stream.next_out = (Bytef*) &out[produced];
		stream.avail_out = (uInt) (out.size() - produced);
		rc = zlib.inflate(&stream, Z_NO_FLUSH);
		produced = out.size() - stream.avail_out;

		if (rc == Z_STREAM_END)
		{
			// gzip: the next member, if any (a file of several members).
			if (!raw && stream.avail_in > 0)
			{
				const Bytef* next = stream.next_in;
				const uInt left = stream.avail_in;
				zlib.inflateEnd(&stream);
				memset(&stream, 0, sizeof(stream));

				if (zlib.inflateInit2_(&stream, 15 + 32, ZLIB_VERSION, int(sizeof(z_stream))) != Z_OK)
					break;

				stream.next_in = (Bytef*) next;
				stream.avail_in = left;
				continue;
			}

			break;
		}

		if (rc != Z_OK && rc != Z_BUF_ERROR)
			break;

		if (stream.avail_out == 0)
		{
			if (out.size() >= MAX_OUTPUT || expected)
			{
				rc = Z_DATA_ERROR;
				break;
			}

			out.resize(out.size() * 2);
		}
		else if (stream.avail_in == 0)
			break;		// the input ends before the stream does
	}

	zlib.inflateEnd(&stream);
	out.resize(produced);

	if (rc != Z_STREAM_END || (expected && produced != expected))
		return damaged(error, raw ? "deflate" : "gzip");

	return true;
#else
	error = "the deflate and gzip compression is not supported in this build";
	return false;
#endif
}

bool zstd(const char* data, size_t size, size_t expected, std::string& out, std::string& error)
{
	static ZStdLib lib;

	if (!lib.ok)
	{
#ifdef WIN_NT
		error = "the zstd compression needs libzstd.dll or zstd.dll";
#else
		error = "the zstd compression needs libzstd.so.1";
#endif
		return false;
	}

	void* const stream = lib.createDStream();

	if (!stream)
	{
		error = "zstd: out of memory";
		return false;
	}

	// One byte more than expected: the end of the stream is seen then.
	out.resize(expected ? expected + 1 : std::max(size * 4, size_t(64 * 1024)));
	ZstdInBuffer in = {data, size, 0};
	size_t produced = 0;
	size_t rc = 0;
	bool ok = true;

	for (;;)
	{
		ZstdOutBuffer o = {&out[0], out.size(), produced};
		const size_t before = produced;
		rc = lib.decompressStream(stream, &o, &in);
		produced = o.pos;

		if (lib.isError(rc))
		{
			error = std::string("zstd: ") + lib.getErrorName(rc);
			ok = false;
			break;
		}

		// rc == 0: a frame is complete; more frames may follow.
		if (rc == 0 && in.pos == in.size)
			break;

		if (o.pos == o.size)
		{
			if (out.size() >= MAX_OUTPUT || expected)
			{
				ok = damaged(error, "zstd");
				break;
			}

			// The result is full; zstd may still have to say that the frame
			// ends, so one more call with some room.
			out.resize(out.size() * 2);
		}
		else if (in.pos == in.size && produced == before)
		{
			ok = damaged(error, "zstd");	// the input ends inside a frame
			break;
		}
	}

	lib.freeDStream(stream);
	out.resize(produced);

	if (ok && expected && produced != expected)
		ok = damaged(error, "zstd");

	return ok;
}

uint32_t crc32(const char* data, size_t size)
{
	static uint32_t table[256];
	static bool ready = false;

	if (!ready)
	{
		for (uint32_t i = 0; i < 256; ++i)
		{
			uint32_t c = i;

			for (int k = 0; k < 8; ++k)
				c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;

			table[i] = c;
		}

		ready = true;
	}

	uint32_t crc = 0xFFFFFFFF;

	for (size_t i = 0; i < size; ++i)
		crc = table[(crc ^ (unsigned char) data[i]) & 0xFF] ^ (crc >> 8);

	return crc ^ 0xFFFFFFFF;
}

}	// namespace Codec
}	// namespace Hq
