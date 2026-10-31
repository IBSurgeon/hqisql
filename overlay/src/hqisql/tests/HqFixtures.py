#  The contents of this file are subject to the Initial
#  Developer's Public License Version 1.0 (the "License");
#  you may not use this file except in compliance with the
#  License. You may obtain a copy of the License at
#  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
#
#  Software distributed under the License is distributed AS IS,
#  WITHOUT WARRANTY OF ANY KIND, either express or implied.
#  See the License for the specific language governing rights
#  and limitations under the License.
#
#  The Original Code was created by IBSurgeon
#  for the Firebird Open Source RDBMS project.
#
#  Copyright (c) 2026 IBSurgeon
#  and all contributors signed below.
#
#  All Rights Reserved.
#  Contributor(s): ______________________________________.
#
# The test files of HqParquetTest.cpp, HqAvroTest.cpp and HqCodecTest.cpp.
#
# Writes HqCodecFixtures.h, HqParquetFixtures.h and HqAvroFixtures.h: every
# file as a byte array. Needs pyarrow (the
# Parquet files and the compressed data); the Avro files are written here.
#
#   python HqFixtures.py
#
# The tests compare the values with fixed text: change the data here and
# the tests together.

import datetime
import decimal
import io
import json
import os
import struct
import uuid
import zlib

import pyarrow as pa
import pyarrow.parquet as pq

fixtures = []


def add(name, data):
	fixtures.append((name, bytes(data)))


# ---------------------------------------------------------------- Codecs

TEXT = b'Firebird HQbird hqisql ' * 20 + bytes(range(256))

add('CODEC_TEXT', TEXT)
add('CODEC_SNAPPY', pa.compress(TEXT, codec='snappy', asbytes=True))
add('CODEC_LZ4_RAW', pa.compress(TEXT, codec='lz4_raw', asbytes=True))
add('CODEC_LZ4_FRAME', pa.compress(TEXT, codec='lz4', asbytes=True))
# The Hadoop LZ4 framing: big-endian sizes of the result and of the block.
raw = pa.compress(TEXT, codec='lz4_raw', asbytes=True)
add('CODEC_LZ4_HADOOP', struct.pack('>II', len(TEXT), len(raw)) + raw)
add('CODEC_ZSTD', pa.compress(TEXT, codec='zstd', asbytes=True))
add('CODEC_GZIP', pa.compress(TEXT, codec='gzip', asbytes=True))
d = zlib.compressobj(9, zlib.DEFLATED, -15)
add('CODEC_DEFLATE', d.compress(TEXT) + d.flush())


# ---------------------------------------------------------------- Parquet

def parquet(name, table, **options):
	out = io.BytesIO()
	pq.write_table(table, out, **options)
	add(name, out.getvalue())


utc = datetime.timezone.utc
types = pa.table({
	'b': pa.array([True, False, None]),
	'i8': pa.array([-128, 0, 127], pa.int8()),
	'i16': pa.array([-32768, None, 32767], pa.int16()),
	'i32': pa.array([-2147483648, 0, 2147483647], pa.int32()),
	'i64': pa.array([-9223372036854775808, 1, 9223372036854775807], pa.int64()),
	'u8': pa.array([0, 1, 255], pa.uint8()),
	'u32': pa.array([0, 1, 4294967295], pa.uint32()),
	'u64': pa.array([0, 1, 18446744073709551615], pa.uint64()),
	'f': pa.array([1.5, -0.1, None], pa.float32()),
	'd': pa.array([0.1, -1e300, 3.0], pa.float64()),
	's': pa.array(['abc', None, 'Привет'], pa.string()),
	'bin': pa.array([b'\x00\x01', b'', None], pa.binary()),
	'fx': pa.array([b'abc', b'\xff\x00\x01', None], pa.binary(3)),
	'dec': pa.array([decimal.Decimal('123.45'), decimal.Decimal('-0.05'), None], pa.decimal128(9, 2)),
	'dec18': pa.array([decimal.Decimal('1234567890123.45678'), None, decimal.Decimal('-1')], pa.decimal128(18, 5)),
	'dec30': pa.array([decimal.Decimal('-12345678901234567890.1234567890'), decimal.Decimal('0'), None],
		pa.decimal128(30, 10)),
	'dt': pa.array([datetime.date(1970, 1, 1), datetime.date(2024, 2, 29), datetime.date(1899, 12, 31)], pa.date32()),
	't_ms': pa.array([0, 45296789, None], pa.time32('ms')),
	't_us': pa.array([1, 45296789012, 86399999999], pa.time64('us')),
	't_ns': pa.array([123456789, 0, None], pa.time64('ns')),
	'ts_ms': pa.array([0, 1709210096789, -1], pa.timestamp('ms')),
	'ts_us_utc': pa.array([1709210096789012, None, 0], pa.timestamp('us', tz='UTC')),
	'ts_ns': pa.array([1709210096123456789, 0, None], pa.timestamp('ns')),
	'st': pa.array([{'x': 1, 'y': 'a'}, None, {'x': None, 'y': 'c'}],
		pa.struct([('x', pa.int32()), ('y', pa.string())])),
	'lst': pa.array([[1, 2], [], None], pa.list_(pa.int32())),
	'id': pa.array([uuid.UUID('12345678-9abc-def0-1234-56789abcdef0').bytes, None,
		uuid.UUID('00000000-0000-0000-0000-000000000001').bytes], pa.uuid()),
	'h': pa.array([1.5, -2.0, None], pa.float16()),
})

parquet('PARQUET_TYPES', types, compression='none', use_dictionary=False, store_schema=False)
parquet('PARQUET_TYPES_DICT', types, compression='snappy', use_dictionary=True, store_schema=False)
parquet('PARQUET_TYPES_V2', types, compression='zstd', use_dictionary=False, data_page_version='2.0',
	store_schema=False)

# Many rows: some row groups, some pages, every codec and encoding.
N = 300
many = pa.table({
	'n': pa.array(range(N), pa.int32()),
	'big': pa.array([i * 1000000007 - 500000000000 for i in range(N)], pa.int64()),
	's': pa.array(['row %d' % i if i % 7 else None for i in range(N)], pa.string()),
	'k': pa.array(['key%d' % (i % 5) for i in range(N)], pa.string()),
	'x': pa.array([i / 4.0 for i in range(N)], pa.float64()),
	'fl': pa.array([i / 2.0 for i in range(N)], pa.float32()),
	'ok': pa.array([i % 3 == 0 for i in range(N)], pa.bool_()),
})

for codec in ['none', 'snappy', 'gzip', 'zstd', 'lz4']:
	parquet('PARQUET_MANY_' + codec.upper(), many, compression=codec, row_group_size=120,
		data_page_size=256, write_statistics=False, store_schema=False)

parquet('PARQUET_MANY_DELTA', many, compression='none', use_dictionary=False, row_group_size=120,
	data_page_size=256, write_statistics=False, store_schema=False,
	column_encoding={'n': 'DELTA_BINARY_PACKED', 'big': 'DELTA_BINARY_PACKED',
		's': 'DELTA_LENGTH_BYTE_ARRAY', 'k': 'DELTA_BYTE_ARRAY', 'x': 'BYTE_STREAM_SPLIT',
		'fl': 'BYTE_STREAM_SPLIT', 'ok': 'RLE'})
parquet('PARQUET_MANY_DELTA_V2', many, compression='snappy', use_dictionary=False, row_group_size=120,
	data_page_size=256, data_page_version='2.0', write_statistics=False, store_schema=False,
	column_encoding={'n': 'DELTA_BINARY_PACKED', 'big': 'DELTA_BINARY_PACKED',
		's': 'DELTA_LENGTH_BYTE_ARRAY', 'k': 'DELTA_BYTE_ARRAY', 'x': 'BYTE_STREAM_SPLIT',
		'fl': 'BYTE_STREAM_SPLIT', 'ok': 'RLE'})
parquet('PARQUET_BROTLI', many.slice(0, 10), compression='brotli', store_schema=False)

# INT96 timestamps (Impala, old Spark).
parquet('PARQUET_INT96', pa.table({
	'ts': pa.array([1709210096123456789, 0, -86400000000000 - 1000], pa.timestamp('ns'))}),
	compression='none', use_deprecated_int96_timestamps=True, store_schema=False)


# ---------------------------------------------------------------- Avro

def zigzag(n):
	n = (n << 1) ^ (n >> 63)
	out = bytearray()
	while True:
		b = n & 0x7F
		n >>= 7
		if n:
			out.append(b | 0x80)
		else:
			out.append(b)
			return bytes(out)


def blob(b):
	return zigzag(len(b)) + b


def text(s):
	return blob(s.encode('utf-8'))


def big(value, size=None):
	n = size or max(1, (value.bit_length() + 8) // 8)
	return value.to_bytes(n, 'big', signed=True)


def avro(name, schema, rows, codec='null', block=100, meta_extra=None):
	sync = bytes(range(16, 32))
	meta = {'avro.schema': json.dumps(schema).encode(), 'avro.codec': codec.encode()}
	meta.update(meta_extra or {})
	out = bytearray(b'Obj\x01')
	out += zigzag(len(meta))
	for k, v in meta.items():
		out += text(k) + blob(v)
	out += zigzag(0) + sync
	for i in range(0, len(rows), block):
		part = rows[i:i + block]
		data = b''.join(part)
		if codec == 'deflate':
			c = zlib.compressobj(9, zlib.DEFLATED, -15)
			data = c.compress(data) + c.flush()
		elif codec == 'snappy':
			data = pa.compress(data, codec='snappy', asbytes=True) + struct.pack('>I', zlib.crc32(b''.join(part)))
		elif codec == 'zstandard':
			data = pa.compress(data, codec='zstd', asbytes=True)
		out += zigzag(len(part)) + zigzag(len(data)) + data + sync
	add(name, out)


all_schema = {
	'type': 'record', 'name': 'Row', 'namespace': 'test',
	'fields': [
		{'name': 'b', 'type': 'boolean'},
		{'name': 'i', 'type': 'int'},
		{'name': 'l', 'type': ['null', 'long']},
		{'name': 'f', 'type': 'float'},
		{'name': 'd', 'type': 'double'},
		{'name': 's', 'type': ['string', 'null']},
		{'name': 'by', 'type': 'bytes'},
		{'name': 'fx', 'type': {'type': 'fixed', 'name': 'F3', 'size': 3}},
		{'name': 'e', 'type': {'type': 'enum', 'name': 'Color', 'symbols': ['RED', 'GREEN']}},
		{'name': 'a', 'type': {'type': 'array', 'items': 'int'}},
		{'name': 'm', 'type': {'type': 'map', 'values': 'string'}},
		{'name': 'r', 'type': {'type': 'record', 'name': 'Inner',
			'fields': [{'name': 'x', 'type': 'int'}, {'name': 'c', 'type': 'Color'}]}},
		{'name': 'dt', 'type': {'type': 'int', 'logicalType': 'date'}},
		{'name': 'tms', 'type': {'type': 'int', 'logicalType': 'time-millis'}},
		{'name': 'tus', 'type': {'type': 'long', 'logicalType': 'time-micros'}},
		{'name': 'tsms', 'type': {'type': 'long', 'logicalType': 'timestamp-millis'}},
		{'name': 'tsus', 'type': {'type': 'long', 'logicalType': 'timestamp-micros'}},
		{'name': 'ltsus', 'type': {'type': 'long', 'logicalType': 'local-timestamp-micros'}},
		{'name': 'dec', 'type': {'type': 'bytes', 'logicalType': 'decimal', 'precision': 10, 'scale': 2}},
		{'name': 'decf', 'type': {'type': 'fixed', 'name': 'D8', 'size': 8, 'logicalType': 'decimal',
			'precision': 18, 'scale': 4}},
		{'name': 'u', 'type': {'type': 'string', 'logicalType': 'uuid'}},
		{'name': 'uf', 'type': {'type': 'fixed', 'name': 'U16', 'size': 16, 'logicalType': 'uuid'}},
		{'name': 'inner2', 'type': ['null', 'Inner']},
	]}


def all_row(i):
	r = bytearray()
	r += bytes([i % 2])												# b
	r += zigzag([0, -1, 2147483647][i])								# i
	r += zigzag(0) if i == 1 else zigzag(1) + zigzag([-9223372036854775808, 0, 42][i])	# l
	r += struct.pack('<f', [1.5, -0.25, 3.0][i])						# f
	r += struct.pack('<d', [0.1, 1e-300, -2.5][i])					# d
	r += zigzag(1) if i == 2 else zigzag(0) + text(['a"b\\', 'Ж'][i])	# s
	r += blob([b'\x00\xff', b'', b'x'][i])							# by
	r += [b'abc', b'\x01\x02\x03', b'zzz'][i]							# fx
	r += zigzag(i % 2)												# e
	if i == 0:
		r += zigzag(2) + zigzag(1) + zigzag(2) + zigzag(0)			# a
	elif i == 1:
		r += zigzag(-1) + zigzag(1) + zigzag(7) + zigzag(0)			# a: a block with its size
	else:
		r += zigzag(0)
	if i == 0:
		r += zigzag(1) + text('k') + text('v') + zigzag(0)			# m
	else:
		r += zigzag(0)
	r += zigzag(i) + zigzag(1)										# r
	r += zigzag([0, 19782, -1][i])									# dt
	r += zigzag([0, 45296789, 86399999][i])							# tms
	r += zigzag([0, 45296789012, 1][i])								# tus
	r += zigzag([0, 1709210096789, -1][i])							# tsms
	r += zigzag([0, 1709210096789012, 1][i])						# tsus
	r += zigzag([0, 1709210096789012, 1][i])						# ltsus
	r += blob(big([12345, -5, 0][i]))									# dec
	r += big([123456789, -1, 0][i], 8)								# decf
	r += text(['12345678-9abc-def0-1234-56789abcdef0', '00000000-0000-0000-0000-000000000000', 'x'][i])
	r += uuid.UUID(int=i + 1).bytes									# uf
	r += zigzag(0) if i != 1 else zigzag(1) + zigzag(5) + zigzag(0)	# inner2
	return bytes(r)


rows = [all_row(i) for i in range(3)]
avro('AVRO_TYPES', all_schema, rows)

many_schema = {'type': 'record', 'name': 'M', 'fields': [
	{'name': 'n', 'type': 'long'}, {'name': 's', 'type': ['null', 'string']}]}
many_rows = [zigzag(i) + (zigzag(0) if i % 7 == 0 else zigzag(1) + text('row %d' % i)) for i in range(N)]
for codec in ['null', 'deflate', 'snappy', 'zstandard']:
	avro('AVRO_MANY_' + codec.upper(), many_schema, many_rows, codec=codec, block=64)

avro('AVRO_LONG', 'long', [zigzag(5), zigzag(-6)])
avro('AVRO_BZIP2', many_schema, many_rows[:1], codec='null', meta_extra={'avro.codec': b'bzip2'})


# ---------------------------------------------------------------- Output

here = os.path.dirname(os.path.abspath(__file__))

for prefix, header in [('CODEC_', 'HqCodecFixtures.h'), ('PARQUET_', 'HqParquetFixtures.h'),
		('AVRO_', 'HqAvroFixtures.h')]:
	guard = 'FB_HQISQL_' + header[:-2].upper() + '_H'
	with open(os.path.join(here, header), 'w', newline='\n') as f:
		f.write('// Generated by HqFixtures.py: the test files. Do not edit.\n'
			'//\n'
			'// The Original Code was created by IBSurgeon for the Firebird Open Source\n'
			'// RDBMS project. Copyright (c) 2026 IBSurgeon. Subject to the Initial\n'
			"// Developer's Public License Version 1.0, see HqFixtures.py.\n\n")
		f.write('#ifndef %s\n#define %s\n\n#include <string>\n\n' % (guard, guard))
		f.write('namespace HqFixtures {\n\n')
		for name, data in fixtures:
			if not name.startswith(prefix):
				continue
			f.write('static const unsigned char %s_DATA[%d] = {\n' % (name, len(data)))
			for i in range(0, len(data), 32):
				f.write('\t' + ','.join('%d' % b for b in data[i:i + 32]) + ',\n')
			f.write('};\n')
			f.write('static const std::string %s((const char*) %s_DATA, sizeof(%s_DATA));\n\n' %
				(name, name, name))
		f.write('}\t// namespace HqFixtures\n\n#endif\t// %s\n' % guard)

print('\n'.join('%-28s %6d' % (n, len(d)) for n, d in fixtures))
