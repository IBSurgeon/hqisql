# hqisql

hqisql is an interactive console for Firebird. It is built from the same
source code as isql and accepts the same commands and command line options.
hqisql adds a line editor based on replxx, syntax highlighting, Tab
completion and new commands to load data from files. Scripts that do not use
the new commands give the same output as in isql.

## Features

- Multi-line line editor with statement history.
- Syntax highlighting of SQL and isql commands, with color schemes.
- Tab completion of keywords and of database object names.
- `PREPARE`, `EXEC SQL` and `DEALLOCATE`: run one statement with different
  values.
- `IMPORT INTO` and `EXEC SQL ... FROM`: load data from CSV, PostgreSQL
  `COPY` text, Apache Parquet and Avro files.
- `SET PAGELENGTH`: set the number of rows between column headers.
- `SET PREVIEW`: show the first rows of a result as a compact table.

## Supported Firebird versions

- Build: Firebird 5.0 source tree. Tested with `v5.0.4` and `v5.0-release`.
- Client: the fbclient of Firebird 5.
- Server: Firebird 3.0 or newer.

## Building

hqisql is installed into a Firebird 5.0 source tree and built with Firebird.
You need git (`install.sh` uses `git apply`).

Linux:

```
git clone -b v5.0.4 https://github.com/FirebirdSQL/firebird
git clone https://github.com/IBSurgeon/hqisql
hqisql/install.sh firebird && cd firebird && ./autogen.sh && make -j8 && make run_tests
```

The result is `gen/Release/firebird/bin/hqisql`.

Windows (needs Git for Windows and Visual Studio 2022; Visual Studio 2026
needs Firebird `v5.0-release`):

1. Run `hqisql\install.bat C:\src\firebird`.
2. In `C:\src\firebird\builds\win32`, run `make_icu.bat` and `make_boot.bat`.
3. Run `make_all.bat` (or `run_all.bat`).

`make_all.bat` builds hqisql, runs the unit tests and copies `hqisql.exe` to
`output_<platform>`.

See [Building](docs/hqisql.md#building) for the install options and the
runtime requirements.

## Documentation

- [User guide (English)](docs/hqisql.md)
- [Руководство (Russian)](docs/ru/hqisql.md)
- [Guia (Portuguese, Brazil)](docs/pt-BR/hqisql.md)
- [Handbuch (German)](docs/de/hqisql.md)
- [Color scheme sample](docs/hqisql-colors.txt)

## License

- hqisql code: Initial Developer's Public License (IDPL) 1.0. See
  [LICENSE](LICENSE).
- The patch to `isql.epp`: InterBase Public License (IPL), the same license
  as `isql.epp` itself.
- replxx: BSD license. See [NOTICE](NOTICE).

## Author

IBSurgeon
