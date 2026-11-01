#!/usr/bin/env python3
r"""Integration tests of hqisql: IMPORT and EXEC SQL ... FROM CSV, COPY,
PARQUET, AVRO on an embedded Firebird 5 database or on a server.

    python3 run.py <firebird-dir> [--hqisql=<file>] [--server=<srv>:<dir>]
                   [--tag=<tag>] [--update] [case ...]

<firebird-dir> is the built Firebird with hqisql in it:
  Linux    gen/Release/firebird      (bin/hqisql, lib/, plugins/)
  Windows  output_x64                (hqisql.exe, plugins\)

Each case is a directory with test.sql and its data files; formats/gen.py
makes the Parquet and Avro files (needs pyarrow). The case runs in a
temporary copy of its directory. The output - with the directory replaced by
<dir>, then the rejected rows files (*.bad) and the exit code - must be the
same as expected/<case>.out. --update writes the output there instead.
--hqisql gives the hqisql executable when it is not in <firebird-dir>.

--server runs the cases on a server over TCP instead of embedded: CREATE
DATABASE 'x.fdb' becomes '<srv>:<dir>/x.fdb' (<dir> is on the server). The
password is in ISC_PASSWORD. The output must be the same as for embedded:
'<srv>:<dir>/' is removed from it. With --tag, the output must be the same
as expected/<case>.<tag>.out when this file is there (an older server does
not know some statements of the case); --update writes this file.
"""

import difflib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = ['csv', 'formats']


def find_hqisql(fb):
	for p in (os.path.join(fb, 'bin', 'hqisql'), os.path.join(fb, 'hqisql.exe'), os.path.join(fb, 'hqisql')):
		if os.path.isfile(p):
			return p
	sys.exit('run.py: no hqisql in %s' % fb)


def run_case(case, fb, hqisql, server=None):
	work = tempfile.mkdtemp(prefix='hqisql-' + case + '-')
	try:
		src = os.path.join(HERE, case)
		for name in os.listdir(src):
			shutil.copy(os.path.join(src, name), work)
		if os.path.exists(os.path.join(work, 'gen.py')):
			subprocess.run([sys.executable, 'gen.py'], cwd=work, check=True)

		prefix = server.rstrip('/') + '/' if server else ''
		if server:
			with open(os.path.join(work, 'test.sql'), encoding='utf-8') as f:
				sql = f.read()
			sql = sql.replace("CREATE DATABASE '", "CREATE DATABASE '" + prefix)
			with open(os.path.join(work, 'test.sql'), 'w', encoding='utf-8', newline='') as f:
				f.write(sql)

		env = dict(os.environ, FIREBIRD=fb, ISC_USER='SYSDBA', NO_COLOR='1')
		if not server:
			env.pop('ISC_PASSWORD', None)
		if os.name == 'nt':
			env['PATH'] = fb + os.pathsep + env.get('PATH', '')
		else:
			env['LD_LIBRARY_PATH'] = os.path.join(fb, 'lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')

		r = subprocess.run([hqisql, '-q', '-noedit', '-ch', 'UTF8', '-i', 'test.sql'], cwd=work, env=env,
			stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		out = r.stdout.decode('utf-8', 'replace')
		for bad in sorted(n for n in os.listdir(work) if n.endswith('.bad')):
			with open(os.path.join(work, bad), 'rb') as f:
				out += '\n=== %s\n%s' % (bad, f.read().decode('utf-8', 'replace'))
		out += '\n=== exit code %d\n' % r.returncode
		if prefix:
			out = out.replace(prefix, '')

		# The temporary directory, both as given and as the real path.
		for d in {work, os.path.realpath(work)}:
			out = out.replace(d + os.sep, '<dir>/').replace(d, '<dir>')
		return out.replace('\r\n', '\n')
	finally:
		shutil.rmtree(work, ignore_errors=True)


def main():
	args = sys.argv[1:]
	update = '--update' in args
	opt = lambda name: next((a.split('=', 1)[1] for a in args if a.startswith(name + '=')), None)
	hqisql, server, tag = opt('--hqisql'), opt('--server'), opt('--tag')
	args = [a for a in args if a != '--update' and not a.startswith(('--hqisql=', '--server=', '--tag='))]
	if not args:
		sys.exit(__doc__)
	fb = os.path.abspath(args[0])
	hqisql = os.path.abspath(hqisql) if hqisql else find_hqisql(fb)
	failed = 0
	for case in args[1:] or CASES:
		out = run_case(case, fb, hqisql, server)
		exp_file = os.path.join(HERE, 'expected', case + '.out')
		if tag:
			tag_file = os.path.join(HERE, 'expected', case + '.' + tag + '.out')
			if update or os.path.exists(tag_file):
				exp_file = tag_file
		if update:
			with open(exp_file, 'w', encoding='utf-8', newline='\n') as f:
				f.write(out)
			print('%s: expected output written' % case)
			continue
		with open(exp_file, encoding='utf-8') as f:
			exp = f.read()
		if out == exp:
			print('%s: passed' % case)
		else:
			failed += 1
			print('%s: FAILED' % case)
			sys.stdout.writelines(difflib.unified_diff(exp.splitlines(True), out.splitlines(True),
				os.path.relpath(exp_file, HERE).replace(os.sep, '/'), 'actual'))
	sys.exit(1 if failed else 0)


if __name__ == '__main__':
	main()
