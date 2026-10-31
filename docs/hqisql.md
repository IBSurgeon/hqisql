# hqisql User Guide

hqisql is an interactive console for Firebird. It is built from the same
source code as isql and accepts the same commands and command line options.
hqisql adds a line editor based on replxx (the ClickHouse fork), syntax
highlighting, Tab completion and several new commands. The behavior of isql
itself does not change.

## Features

- Syntax highlighting of SQL and isql commands. You can change the colors
  with a color scheme file.
- Multi-line editor: Enter continues the statement until the terminator.
- Tab completion and hints: keywords, and tables, columns, procedures,
  functions, generators and packages of the connected database.
- Statement history: a multi-line statement is one history entry.
- `PREPARE`, `EXEC SQL` and `DEALLOCATE`: run one statement with different
  values.
- `IMPORT INTO` and `EXEC SQL ... FROM`: load data from CSV, PostgreSQL
  `COPY` text, Apache Parquet and Avro files.
- `SET PAGELENGTH`: set the number of rows between column headers.
- `SET PREVIEW`: show the first rows of a result as a compact table.
- Non-interactive modes (`-i`, pipe, `-x`, `-a`, `INPUT`) use the isql code.
  On the same scripts (without `PREPARE`, `EXEC SQL`, `DEALLOCATE` and
  `IMPORT`), hqisql and isql give the same output, byte for byte.
- Works on Windows 10 and newer (Windows Terminal, conhost) and on Linux.

## Usage

The commands and the command line options are the same as in isql. hqisql
adds these:

- `-noedit` or the environment variable `HQISQL_NOEDIT=1`: no editor; hqisql
  works as isql. hqisql does not change the console and does not use the
  history file.
- `NO_COLOR`: no highlighting. `HQISQL_COLORS=<file>`: your own color scheme
  (see "Color scheme").
- `-z` prints `hqisql 1.0.0`, then the isql version line.

Keys:

- Tab: completion. One candidate is inserted in full. For several
  candidates, hqisql inserts the common prefix and shows the list; Ctrl-N and
  Ctrl-P select a candidate.
- Right or End at the end of the line: accept the gray hint.
- Ctrl-Up and Ctrl-Down: go through the hints.
- Ctrl-R: search the history.
- Ctrl-C: clear the line or the statement you started.
- Ctrl-D on an empty line: exit (EOF).

## Line editor

Multi-line input:

- Enter continues the statement until the terminator. The prompt changes to
  `CON>`.
- `SET TERM` works. The highlighting uses the new terminator.

Paste from the clipboard:

- Windows: the console has no bracketed paste. hqisql takes the lines that
  are already in the console input buffer and runs them at once, as isql
  does. Each line is shown with the `CON>` prompt. A paste of a statement
  with 2000 lines takes about 0.2 s (isql: about 0.7 s).
- Linux and macOS: bracketed paste is on. The pasted text goes into the
  editor as a whole, and Enter runs it. If the paste has several statements,
  hqisql runs them one after another. A paste of a statement with 2000 lines
  takes about 0.3 s on Linux. Without bracketed paste, the lines that come in
  one block run as in isql.

Ctrl-C:

- at the `SQL>` prompt, Ctrl-C clears the line;
- at the `CON>` prompt and inside an open string, Ctrl-C clears the whole
  statement;
- while a query runs, Ctrl-C cancels the query, as in isql.

The session does not end in all of these cases.

History:

- hqisql keeps history by statement. A multi-line statement is one entry. The
  entry keeps the terminator that you typed with the statement.
- The history file is `%LOCALAPPDATA%\hqisql\history.txt` on Windows and
  `~/.hqisql_history` on POSIX (access rights 0600).
- replxx stores the line breaks inside an entry as the byte 0x17. `cat` does
  not show this byte.
- hqisql saves the file after each statement. If the process is killed, the
  history is not lost. After a paste, hqisql saves the file before the next
  prompt.
- Statements with the word `PASSWORD` (`CONNECT ... PASSWORD`,
  `CREATE USER ...`) do not go into the history.

## Syntax highlighting

The built-in colors:

- reserved words: bright blue;
- other keywords: blue, so that `NAME`, `TYPE`, `VALUE` as column names do
  not stand out;
- isql commands: yellow. This covers the command word (`SHOW`, `HELP`,
  `INPUT`/`IN`, `OUTPUT`/`OUT`, `EDIT`, `SHELL`, `EXIT`, ...), the `SET`
  option (`STATS`, `AUTODDL`, `TERM`, `BLOB`, ...), the `SHOW` object
  (`TABLES`, `DB`, `VERSION`, ...) and the switch after them (`ON`/`OFF`,
  `ALL`, `DIALECT`). The word lists and abbreviations are the same as isql
  accepts (`SET STAT ON`, `SHOW TABLE`);
- `SET` without an isql option (`SET TRANSACTION`, `SET GENERATOR`,
  `SET ROLE`, `SET TIME ZONE`, `SET BIND`) is SQL and gets the usual SQL
  colors;
- strings: green (Cyrillic text too);
- numbers: magenta;
- comments: gray;
- the terminator: red;
- parameters `:` and `?`: cyan.

The `CON>` continuation lines are colored with the previous lines of the
statement in mind. For example, hqisql knows that a line is inside an open
string or comment.

You can change the colors with a color scheme file (see "Color scheme").

## Tab completion

- Tab completion and hints while you type: keywords, and tables, columns,
  procedures, functions, generators and packages of the connected database.
  hqisql loads the names in the background.
- **Names with case and quotes.** hqisql stores the names as they are:
  `from ord` + Tab gives `"Orders"`, `o."I` + Tab gives `"Id"`. hqisql puts a
  name in quotes when it cannot be read without them (lower case, spaces, a
  reserved word). Without quotes, the comparison ignores case. In quotes, the
  comparison is exact.
- **Refresh after DDL.** After `CREATE`, `ALTER`, `DROP`, `RECREATE`,
  `DECLARE` and `COMMIT`, hqisql reads the metadata again in the background
  before the next prompt. Example: after `create table "Abc"`, `"Ab` + Tab
  gives `"Abc"` without a new connection.
- **Qualified completion.** After `Table.`, hqisql shows only the columns of
  this table. After `Package.`, it shows the procedures and functions of the
  package. hqisql finds `Alias.` in the query text (`FROM`, `JOIN`, `UPDATE`,
  lists with commas, optional `AS`, an alias in quotes). The context includes
  all `CON>` continuation lines and the text after the cursor up to the end
  of the statement. So `SELECT E.` + Tab works also when `FROM EMPLOYEE E` is
  already written to the right of the cursor. Other statements on the same
  line are not used.
- **Table position.** After `FROM`, `JOIN` and `UPDATE`, hqisql shows only
  tables and views. System tables (`RDB$`, `MON$`, `SEC$`, `TMP$`) are
  hidden.

## Character sets

hqisql reads and prints data in the console code page, as isql does. replxx
switches the Windows console to UTF-8 and VT mode only while you edit a line.
It sets the old values back before isql continues. The typed line is
converted to the input code page of the console. So the rules are the same
as for isql: `-ch` must match the console code page (`chcp 1251` +
`-ch WIN1251`, `chcp 65001` + `-ch UTF8`).

A WIN1251 database in console code page 1251, and `-ch UTF8` in console code
page 65001, give the same bytes as isql. The same is true on Linux (UTF-8,
`-ch UTF8`).

## PREPARE / EXEC SQL / DEALLOCATE

These commands are only in hqisql. They work interactively and in scripts
(`-i`, `INPUT`, pipe).

```
PREPARE name AS <statement with ? >;
EXEC SQL name USING (v1, v2, ...);
EXEC SQL name USING BATCH (v1, ...), (v1, ...), ...;
DEALLOCATE name;
```

**This is a text substitution in the client, not a prepare on the server.**
`EXEC SQL` replaces each `?` (outside strings, quoted names and comments)
with the value as it is written. The result is a new statement. The server
prepares and runs it again. So:

- a value is any SQL expression (`1`, `'text'`, `current_date`,
  `(2 + 3) * 4`); put strings in quotes;
- `BATCH` is N separate statements, not `IBatch`;
- the name is valid until the end of the session. A new `PREPARE` with the
  same name replaces the statement.

How hqisql reads the values:

- strings `'...'` (with `''`), Q-strings `q'{...}'`, names `"..."` and nested
  parentheses inside a value stay as they are;
- hqisql removes comments from the values (a line comment would comment out
  the rest of the statement line);
- after the list, only spaces and comments are allowed. Extra text, an empty
  value or a missing `)` is an error;
- `()` is the list for a statement without `?`.

How hqisql runs the statement:

- a single `EXEC SQL` rewrites the statement and gives it to the normal isql
  processing (isql commands, output, transactions work as for direct input);
- `BATCH` first checks all tuples (the number of values). Then it runs each
  statement the same way: first the isql commands (`SHOW`, `SET` ...), then
  SQL. It stops at the first error;
- an error of the command itself (`EXEC SQL: ...`) sets the error exit code,
  respects `SET BAIL` and in a script prints "After line N in file F", as
  isql does after a statement error.

## Loading files: EXEC SQL ... FROM and IMPORT INTO

These commands are only in hqisql. They work interactively and in scripts.
The `FROM CSV` syntax comes from
[FirebirdSQL/firebird#9046](https://github.com/FirebirdSQL/firebird/issues/9046).

```
PREPARE ins AS INSERT INTO ORDERS (ID, CUSTOMER, AMOUNT) VALUES (?, UPPER(?), ?);
EXEC SQL ins FROM CSV 'orders.csv' OPTIONS (HEADER, ERRORS = 10);

IMPORT INTO EMPLOYEE FROM CSV 'emp.csv' OPTIONS (HEADER, DELIMITER = ';');
IMPORT INTO "Orders" ("Id", AMOUNT) FROM CSV 'C:\data\o.csv' OPTIONS (ENCODING = WIN1251, DECIMAL = ',');
```

- **EXEC SQL name FROM CSV**: hqisql runs the statement from `PREPARE` for
  each record of the file. Field N is the value of the N-th `?`. Allowed
  statements: `INSERT`, `UPDATE`, `DELETE`, `MERGE`, `EXECUTE PROCEDURE`,
  `EXECUTE BLOCK`. `SELECT` is an error.
- **IMPORT INTO table**: hqisql makes the `INSERT` itself. The columns come
  from the list in the command, or from the file header (`HEADER`), or are
  all columns of the table in order, except computed columns and
  `GENERATED ALWAYS AS IDENTITY` columns. A name from the header: a regular
  identifier is converted to upper case; any other name is put in quotes as
  it is. A view also works.
- A relative file name is relative to the script directory (`-i`, `INPUT`).
  In the console, it is relative to the current directory.

How it works:

- the server prepares the statement **once**, in the DML transaction of isql
  (with `USING`, the values are put into the text instead);
- each field goes as `VARCHAR` in the connection character set (or in
  `ENCODING`; after a UTF-8 BOM, in UTF8). The server converts the value to
  the parameter type (date, number, BOOLEAN, BLOB), as for a literal;
- the records go in `IBatch` batches (`BATCH`, 16 MB buffer). Each batch runs
  under `SAVEPOINT HQ$IMPORT`. If a batch fails, hqisql rolls it back to the
  savepoint and runs its records again one by one. So the error gets the
  exact line number in the file;
- a server without `IBatch` (Firebird 3, protocol < 16): one record at a
  time, as with `BATCH = 0`. Tested with Firebird 3.0 servers over TCP: the
  same test script as for Firebird 5 gives the same result.

### Options

| Option | Default | Meaning |
|---|---|---|
| `DELIMITER` | `','` | field delimiter; `'\t'` is a tab |
| `QUOTE` | `'"'` | quote character; `""` inside a field is the quote itself |
| `HEADER` | `FALSE` | the first record has the column names; `HEADER` without a value = `TRUE` |
| `SKIP` | `0` | skip n records after the header |
| `NULL` | `''` | a field without quotes with this text is NULL; by default an empty field is NULL and `""` is an empty string |
| `ENCODING` | connection character set | character set of the file (`WIN1251`, `UTF8`, ...) |
| `DECIMAL` | `'.'` | `','` means a comma in numbers (`1,5`) |
| `BATCH` | `1000` | records in a batch; `0` means no `IBatch`, one at a time |
| `COMMIT` | `0` | commit every n records and at the end; `0` means the user ends the transaction |
| `ERRORS` | `0` | how many bad records can be skipped |

### Errors

- Without `ERRORS`, the load stops at the first bad record:
  `file, line N:`, the server error, `After line M in file script.sql`.
  The records before it stay in the transaction (as with `SET BULK_INSERT`).
  The exit code is an error; `SET BAIL` is respected.
- With `ERRORS = n`: `file, line N: rejected: message`. hqisql writes the
  record to `<file>.bad` (first the header line, if there is one). At the
  n+1-th bad record, the load stops.
- A record with a wrong number of fields, a field longer than the column, or
  a quote that is not closed gives an error with the line number. hqisql does
  not send such a record to the server.
- Ctrl-C cancels the load (`cancelOperation`). The records of the current
  batch are rolled back.

### Summary and speed

At the end, hqisql prints `N rows imported, K rejected (path .bad), T s`
(`executed` for `EXEC SQL`). With `-q`, hqisql does not print it; if there
are rejected records, it prints the summary to stderr. In the console,
hqisql prints a record counter once a second.

Speed: 1 million records into a table `(INT, VARCHAR(30), NUMERIC(12,2), DATE)`,
Firebird 5.0, hqisql run time without the database creation:

| Method | Windows 11, embedded | Windows 11, TCP localhost (SuperServer) |
|---|---|---|
| `IMPORT INTO` (BATCH 1000) | 1.3 s | 2.2 s |
| `IMPORT INTO`, `BATCH = 0` | 2.5 s | 45.9 s |
| `SET BULK_INSERT` | 3.4 s | 70.9 s |
| `EXEC SQL ... USING BATCH` (100,000 records) | 2.6 s | 21.8 s |

On Linux (Ubuntu 22.04, embedded): `IMPORT INTO` takes 11.1 s,
`SET BULK_INSERT` takes 22.3 s.

`EXEC SQL ... USING BATCH` prepares each statement again. For 1 million
records it takes about 26 s and 218 s. Over the network, `IBatch` gives the
largest gain, because there is no round trip to the server for each record.

### COPY, PARQUET and AVRO formats

The syntax is the same. Only the format word changes:

```
IMPORT INTO T FROM COPY 'dump.txt';
IMPORT INTO T (ID, NAME, AMOUNT) FROM PARQUET 'orders.parquet' OPTIONS (BATCH = 5000);
EXEC SQL ins FROM AVRO 'events.avro' OPTIONS (ERRORS = 10);
```

| Format | What it is | Options |
|---|---|---|
| `CSV` | RFC 4180 (see above) | all |
| `COPY` | PostgreSQL text format of `COPY ... TO` / `\copy` / pg_dump data | all except `QUOTE`; the default `DELIMITER` is a tab, the default `NULL` is `\N` |
| `PARQUET` | Apache Parquet | `SKIP`, `BATCH`, `COMMIT`, `ERRORS` |
| `AVRO` | Avro object container (`.avro`) | `SKIP`, `BATCH`, `COMMIT`, `ERRORS` |

Any other option for a format is a parse error (`option HEADER is not for
PARQUET files`).

**COPY**: a line is a record, `\N` is NULL. Escapes: `\t \n \r \b \f \v`,
`\\`, octal `\101`, `\x41`. The line `\.` is the end of the data. hqisql
detects the binary format `PGCOPY` and rejects it. `HEADER`, `ENCODING` and
`DECIMAL` work as for CSV. hqisql converts the booleans `t` / `f` and the
offset `+03` in `timestamptz` to a form that Firebird understands.

**PARQUET and AVRO**: the file has a schema, so the columns are matched **by
name**:

- `IMPORT INTO t (a, b)`: the columns `a`, `b` of the file go to the columns
  `A`, `B` of the table. Without a list: all columns of the file; the name of
  a file column is the name of the table column (`id` → `ID`). A name without
  quotes is found without regard to case. If there is no such column, the
  error shows the list of the file columns;
- `EXEC SQL name`: all columns of the file in order. Their number must be the
  same as the number of `?`;
- Parquet: a column of a nested group has the name `group.column`. Lists and
  maps (repeated columns) cannot be loaded; selecting one is an error;
- Avro: the fields of the top-level record. A union with null is a column
  with NULL. record, array and map become JSON text. If the schema is not a
  record, the file has one column `VALUE`.

The values go as text, as for CSV, with these exceptions:

- bytes (Parquet `BYTE_ARRAY` without a logical type, `FIXED_LEN_BYTE_ARRAY`;
  Avro `bytes`, `fixed`) go as `OCTETS` into `VARBINARY` /
  `CHAR(n) CHARACTER SET OCTETS` / `BLOB` without conversion;
- a UUID goes as 16 bytes into an `OCTETS` column, otherwise as the text
  `12345678-9abc-def0-1234-56789abcdef0`;
- a time in UTC (Parquet `TIMESTAMP(isAdjustedToUTC)`, not INT96; Avro
  `timestamp-*`) gets ` +00:00` for a `WITH TIME ZONE` column. For a
  `TIMESTAMP` column, the value is UTC without a zone;
- the fraction of a second is cut to 4 digits (the Firebird precision).
  `DECIMAL` is an exact number; `FLOAT` / `DOUBLE` use the shortest exact
  form.

Parquet: pages v1 and v2, dictionaries, the encodings PLAIN, RLE, BIT_PACKED,
DELTA_BINARY_PACKED, DELTA_LENGTH_BYTE_ARRAY, DELTA_BYTE_ARRAY,
BYTE_STREAM_SPLIT; the compressions SNAPPY, GZIP, LZ4, LZ4_RAW, ZSTD (BROTLI
and LZO give an error). Avro: the codecs null, deflate, snappy, zstandard
(bzip2 and xz give an error).

hqisql has its own reader for these formats and needs no extra libraries,
except for compressed data. It loads zlib and zstd at the first compressed
block: `zlib1.dll` (the hqisql directory, then `PATH`) / `libz.so.1`, and
`libzstd.dll` or `zstd.dll` / `libzstd.so.1`. If the library is missing, the
error is `the zstd compression needs ...`. hqisql reads the file by pages
(Parquet) and blocks (Avro). It does not load the whole file into memory.

In the messages and in `.bad`, the record number is `file, row N`. In `.bad`,
the record is a CSV line; the first line has the column names.

Tested with Firebird 5 (embedded and TCP) and with Firebird 3.0 servers over
TCP (one record at a time): Parquet (snappy, zstd v2, gzip), Avro (null,
deflate), COPY. 200,000 Parquet rows (zstd) load in about 1 s into embedded
Firebird 5, and in about 40 s into Firebird 3.0 over TCP.

### Editor support and HELP

The editor colors `PREPARE`, `EXEC SQL`, `DEALLOCATE`, `IMPORT INTO`,
`FROM CSV` (and `COPY`, `PARQUET`, `AVRO`), `OPTIONS` and the option names
with the color of isql commands. Tab completion: after `IMPORT INTO`, tables
and views; after `FROM`, the formats; inside `OPTIONS (`, the option names;
after `EXEC SQL` and `DEALLOCATE`, the names from `PREPARE`. `HELP` shows the
hqisql commands and options.

## SET PAGELENGTH

This command is only in hqisql. In isql, you can set the number of rows
after which the column headers and `====` are printed again only with the
command line option `-pag <n>`.

```
SET PAGELENGTH <n>;   -- header every n rows (short form: SET PAG n)
SET PAGELENGTH 0;     -- header once per query
SET PAGELENGTH;       -- default, 20
```

A negative value or a value that is not a number is an error. `SET` shows the
current value (`Page length:`). The command is listed in `HELP SET`. Unlike
`-pag 0`, `SET PAGELENGTH 0` does not switch off `SET HEADING`.

## SET PREVIEW

This command is only in hqisql. It switches on the preview mode. hqisql
prints the query result as a compact table, without wrapping the headers and
rows:

```
SET PREVIEW ON;       -- switch on (SET PREVIEW without an argument toggles)
SET PREVIEW 50;       -- switch on and show the first 50 rows (default 20)
SET PREVIEW OFF;
```

What it does:

- hqisql fetches only the first N rows. The fetch stops, and the server does
  not read further. If the limit was reached, hqisql prints
  `-- preview: first 20 rows` after the table;
- the width of each column is the width of the longest value or header, not
  the declared length. Numbers stay right-aligned; `<null>` is shown as in
  isql;
- columns that do not fit into the screen width are dropped on the right. The
  last shown column can be cut (to no less than 4 characters). The list of
  dropped columns is on the same line:
  `-- preview: ... 2 more columns: C3 C4`. hqisql takes the screen width from
  the console. If the output does not go to a terminal (`OUTPUT file`, pipe),
  the width has no limit, but the columns are still made narrow;
- `SET LIST ON` switches the mode off for the query. `SET HEADING OFF`
  removes the header lines. hqisql prints only the identifier of a BLOB field
  (it does not print the BLOB contents in preview mode). `SET COUNT ON`
  counts the fetched rows.

Widths are counted in characters (for a UTF8 connection, in code points;
otherwise in bytes). `SET` shows the current state (`Preview: ON, 20 rows`).

## Color scheme

An optional text file sets the highlighting colors. hqisql reads it once at
startup (only when the editor is active and `NO_COLOR` is not set):

- `%LOCALAPPDATA%\hqisql\colors.txt` on Windows, `~/.hqisql_colors` on POSIX
  (next to the history file);
- the variable `HQISQL_COLORS=<path>` gives another file. If this file does
  not exist, hqisql prints a warning to stderr and uses the built-in colors.

Without a file, hqisql uses the built-in colors. The sample file
[hqisql-colors.txt](hqisql-colors.txt) has the built-in colors and describes
the format. You can copy it and change it. The format is one line for each
element; `#` starts a comment:

```
# <element> = <color> [bold] [underline] [on <color>]
command    = yellow            # isql commands and their options: SET STATS ON, SHOW TABLE
reserved   = brightblue bold   # reserved words
keyword    = blue              # other keywords (NAME, TYPE)
string     = rgb(0,5,0)        # 'string'
quoted     = brightmagenta     # "quoted name"
number     = 213               # terminal palette index 0..255
comment    = gray(10)          # gray level 0..23
param      = default underline # :name and ?
terminator = brightred
hint       = gray              # the hint after the cursor
completion = brightblue        # keywords in the Tab completion list
```

Colors:

- `default`: the terminal text color. `default` without attributes means "do
  not color";
- the 16 replxx names: `black red green brown blue magenta cyan lightgray
  gray brightred brightgreen yellow brightblue brightmagenta brightcyan
  white`;
- `rgb(r,g,b)` with components 0..5 (the 216-color cube);
- `gray(n)` with n from 0 to 23;
- a number from 0 to 255 (the terminal palette index).

Names are not case-sensitive. The separator is `=` or `:`. An element that
is not in the file keeps its built-in color. hqisql skips a line that it
does not understand and prints a message, for example
`hqisql: color scheme <file>: line N: unknown color 'pink'`. Colors 16..255
need a terminal with 256 colors (Windows Terminal, conhost on Windows 10 and
newer, xterm-256color).

## Building

hqisql is not built alone. You install it into a Firebird 5.0 source tree and
build it with Firebird. hqisql is tested with Firebird `v5.0.4` and
`v5.0-release` (the coming 5.0.5).

You need:

- git. `install.sh` uses `git apply`. The Firebird tree does not have to be a
  git work tree.
- Windows: Git for Windows (`install.bat` runs `install.sh` with its bash),
  and Visual Studio 2022. You can also use Visual Studio 2026 with Firebird
  `v5.0-release`; Firebird 5.0.4 does not know Visual Studio 2026.

### Linux

```
git clone -b v5.0.4 https://github.com/FirebirdSQL/firebird
git clone https://github.com/IBSurgeon/hqisql
hqisql/install.sh firebird && cd firebird && ./autogen.sh && make -j8 && make run_tests
```

The result is `gen/Release/firebird/bin/hqisql`. `make tests` and
`make run_tests` build and run the unit tests `hqisql_test` together with the
Firebird tests.

### Windows

```
hqisql\install.bat C:\src\firebird
```

Then, in `builds\win32`, run `make_icu.bat`, `make_boot.bat` and `make_all.bat` (or
`run_all.bat`). `make_all.bat` builds hqisql (the solution `HQisql.sln`),
runs `hqisql_test` and copies `hqisql.exe` and `replxx-LICENSE.md` to
`output_<platform>`.

### Install options

`install.sh` and `install.bat` have these options:

- `--check`: only check that the patches apply. Nothing changes.
- `--remove`: reverse the patches and delete the files.
- `--no-hook`: do not change the Firebird build files. Then build hqisql
  after Firebird with
  `make -C gen -f ../builds/posix/Makefile.hqisql` (Linux) or
  `builds\win32\make_hqisql.bat` (Windows).

If you run the install again with the same version, it does nothing. A new
version replaces the old one. The install record is in `src/hqisql/.install`.

What the install changes in the Firebird tree:

- it copies `src/hqisql`, `extern/replxx` and the hqisql build files;
- it patches `src/isql/isql.epp`. All changes are under `#ifdef HQISQL`, so
  the regular isql does not change;
- unless you use `--no-hook`, it patches `builds/posix/Makefile.in` and
  `builds/win32/make_all.bat`.

### Runtime

- hqisql needs the fbclient of Firebird 5. The server can be Firebird 3.0 or
  newer.
- Parquet and Avro files compressed with zstd need libzstd. Linux: the
  system package (for example `libzstd1`). Windows: `libzstd.dll` next to
  `hqisql.exe`; the release archive includes it.
- zlib comes with Firebird.
- `hqisql -z` prints `hqisql 1.0.0`, then the isql version line.

## Integration tests

```
python3 tests/integration/run.py <firebird-dir>
```

The script runs the `IMPORT` and `EXEC SQL` tests on an embedded database
and compares the output with `tests/integration/expected/*.out`. It needs
pyarrow to make the Parquet and Avro data.

## Known limitations

- macOS is not built and not tested.
- Loading files: BLOBs larger than 32 KB, `FROM STDIN`, export to CSV and
  creating a table from the file header are not supported.
- There is no external editor (Alt+E), no highlighting of the error position
  from the server and no vi mode.

## How it works

This section is for contributors.

hqisql is built from the isql sources as a second target. The build compiles
`isql.epp` and the other isql sources again with `-DHQISQL`, and adds the
files from `src/hqisql`. `isql.epp` has only thin hooks under
`#ifdef HQISQL`. The interface of the hooks is `src/hqisql/HqIsql.h`; it is
the only hqisql header that `isql.epp` includes. Without `HQISQL`, the
preprocessed `isql.cpp` is the same as in Firebird, so isql does not change.

The input splits in one place: `readNextInputLine()` in `isql.epp`. The hook
is before the `HAVE_EDITLINE_H` branch. If `Hq::readLine()` returns false
(`-noedit`, not a terminal, `-i`), the normal isql code runs, including
editline. In interactive hqisql, replxx reads the line. hqisql splits a
multi-line input into lines, and the existing isql statement reader
(`get_statement`) works without changes. ECHO, the line numbers of errors,
`INPUT`, `SET TERM` and the temp file work as in isql.

The modules in `src/hqisql`:

- `HqLineReader.cpp`: the replxx wrapper (highlighting, completion, hints,
  history, Ctrl-C, paste) and the hook code;
- `HqLexer`, `HqKeywords`, `HqIsqlCommands`, `HqColors`: highlighting and
  the color scheme;
- `HqMetadata`: loads the database names for completion in a background
  thread, in its own read-only transaction;
- `HqStatements`, `HqParams`: `PREPARE`, `EXEC SQL`, `DEALLOCATE`;
- `HqImportCommand`, `HqImport`, `HqRows`, `HqCsv`, `HqCopy`, `HqParquet`,
  `HqAvro`, `HqCodec`: file loading;
- `HqTable`, `HqPreview`: `SET PREVIEW`;
- `tests/`: the Boost.Test unit tests (`hqisql_test`).

replxx is in `extern/replxx`. It is a copy of ClickHouse/replxx; the local
changes are listed in `extern/replxx/REVISION`.
