# The files of the integration tests of FROM COPY / PARQUET / AVRO.
import datetime, decimal, json, os, struct, uuid, zlib
import pyarrow as pa
import pyarrow.parquet as pq

os.chdir(os.path.dirname(os.path.abspath(__file__)))

U1 = uuid.UUID('12345678-9abc-def0-1234-56789abcdef0')
U2 = uuid.UUID('00000000-0000-0000-0000-000000000001')

t = pa.table({
	'id': pa.array([1, 2, 3], pa.int32()),
	'name': pa.array(['abc', 'Привет', None]),
	'amount': pa.array([decimal.Decimal('12.50'), decimal.Decimal('-0.05'), None], pa.decimal128(10, 2)),
	'd': pa.array([datetime.date(2024, 2, 29), datetime.date(1899, 12, 31), None]),
	'ts': pa.array([1709210096789012, 0, None], pa.timestamp('us')),
	'tsu': pa.array([1709210096789012, 0, None], pa.timestamp('us', tz='UTC')),
	'flag': pa.array([True, False, None]),
	'data': pa.array([b'\x00\x01', b'', None], pa.binary()),
	'u': pa.array([U1.bytes, U2.bytes, None], pa.uuid()),
	'f': pa.array([0.1, -1e300, None]),
	't': pa.array([45296789012, 0, None], pa.time64('us')),
})
pq.write_table(t, 't.parquet', compression='snappy')
pq.write_table(t, 't_zstd.parquet', compression='zstd', data_page_version='2.0')
pq.write_table(t, 't_gzip.parquet', compression='gzip', use_dictionary=False)

pq.write_table(pa.table({'id': pa.array([1, 2, 2, 3, 4], pa.int32()),
	'name': pa.array(['a', 'b', 'dup', 'c', 'x' * 30])}), 'bad.parquet')
pq.write_table(pa.table({'id': pa.array([1], pa.int32()), 'lst': pa.array([[1, 2]])}), 'list.parquet')

N = 200000
pq.write_table(pa.table({'id': pa.array(range(N), pa.int32()),
	'name': pa.array(['row %d' % i for i in range(N)]),
	'amount': pa.array([decimal.Decimal(i) / 100 for i in range(N)], pa.decimal128(12, 2))}),
	'big.parquet', compression='zstd', row_group_size=50000)


def zigzag(n):
	n = (n << 1) ^ (n >> 63)
	out = bytearray()
	while True:
		b = n & 0x7F
		n >>= 7
		if not n:
			out.append(b)
			return bytes(out)
		out.append(b | 0x80)


def blob(b):
	return zigzag(len(b)) + b


def avro(name, schema, rows, codec='null'):
	sync = bytes(range(16))
	meta = {'avro.schema': json.dumps(schema).encode(), 'avro.codec': codec.encode()}
	out = bytearray(b'Obj\x01') + zigzag(len(meta))
	for k, v in meta.items():
		out += blob(k.encode()) + blob(v)
	out += zigzag(0) + sync
	data = b''.join(rows)
	if codec == 'deflate':
		c = zlib.compressobj(9, zlib.DEFLATED, -15)
		data = c.compress(data) + c.flush()
	out += zigzag(len(rows)) + zigzag(len(data)) + data + sync
	open(name, 'wb').write(out)


def opt(t):
	return ['null', t]


schema = {'type': 'record', 'name': 'R', 'fields': [
	{'name': 'id', 'type': 'int'},
	{'name': 'name', 'type': opt('string')},
	{'name': 'amount', 'type': opt({'type': 'bytes', 'logicalType': 'decimal', 'precision': 10, 'scale': 2})},
	{'name': 'd', 'type': opt({'type': 'int', 'logicalType': 'date'})},
	{'name': 'ts', 'type': opt({'type': 'long', 'logicalType': 'local-timestamp-micros'})},
	{'name': 'tsu', 'type': opt({'type': 'long', 'logicalType': 'timestamp-micros'})},
	{'name': 'flag', 'type': opt('boolean')},
	{'name': 'data', 'type': opt('bytes')},
	{'name': 'u', 'type': opt({'type': 'fixed', 'name': 'U', 'size': 16, 'logicalType': 'uuid'})},
	{'name': 'f', 'type': opt('double')},
	{'name': 't', 'type': opt({'type': 'long', 'logicalType': 'time-micros'})},
]}


def some(b):
	return zigzag(1) + b


rows = [
	zigzag(1) + some(blob('abc'.encode())) + some(blob((1250).to_bytes(2, 'big', signed=True))) +
	some(zigzag(19782)) + some(zigzag(1709210096789012)) + some(zigzag(1709210096789012)) +
	some(b'\x01') + some(blob(b'\x00\x01')) + some(U1.bytes) + some(struct.pack('<d', 0.1)) +
	some(zigzag(45296789012)),
	zigzag(2) + some(blob('Привет'.encode())) + some(blob((-5).to_bytes(1, 'big', signed=True))) +
	some(zigzag(-25568)) + some(zigzag(0)) + some(zigzag(0)) + some(b'\x00') + some(blob(b'')) +
	some(U2.bytes) + some(struct.pack('<d', -1e300)) + some(zigzag(0)),
	zigzag(3) + zigzag(0) * 10,
]
avro('t.avro', schema, rows)
avro('t_deflate.avro', schema, rows, 'deflate')
pq.write_table(t.select(['id', 'u']), 'u.parquet')
avro('u.avro', {'type': 'record', 'name': 'R', 'fields': [schema['fields'][0], schema['fields'][8]]},
	[zigzag(5) + some(U1.bytes), zigzag(6) + zigzag(0)])

with open('t.txt', 'w', encoding='utf-8', newline='\n') as f:
	f.write('1\tabc\\tdef\t12.50\t2024-02-29\t2024-02-29 12:34:56.789012\t2024-02-29 12:34:56.789012+03\tt\n')
	f.write('2\t\\N\t-0.05\t1899-12-31\t1970-01-01 00:00:00\t1970-01-01 00:00:00+00\tf\n')
	f.write('3\tline\\nbreak \\\\ \\x41\t\\N\t\\N\t\\N\t\\N\t\\N\n')
	f.write('\\.\n')
	f.write('4\tafter the end\n')

with open('h.txt', 'w', encoding='utf-8', newline='\r\n') as f:
	f.write('id|name\n1|Ж\n2|NULL\n')

print('ok')
