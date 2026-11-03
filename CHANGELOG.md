# Changelog

## Unreleased

- CI runs the integration tests on Firebird 3 and 4 servers over TCP too
  (`tests/integration/run.py --server`).
- Table and view names have their own color (`table`, cyan by default).
  The names come from `RDB$RELATIONS`, system tables too.
- Completion after `FROM`, `JOIN` and `UPDATE` offers the system tables
  too, after the tables of the user.

## 1.0.0 (2026-10-31)

First public release.

- isql of Firebird 5.0 with the replxx line editor: multi-line editing,
  syntax highlighting, completion of keywords and object names, history.
- `PREPARE`, `EXEC SQL`, `DEALLOCATE`: statements with parameters.
- `IMPORT INTO` and `EXEC SQL ... FROM`: load CSV, PostgreSQL COPY text,
  Parquet and Avro files.
- `SET PAGELENGTH`, `SET PREVIEW`, color schemes (`HQISQL_COLORS`,
  `~/.hqisql_colors`, `NO_COLOR`).
- `install.sh` / `install.bat`: install into a Firebird 5.0 source tree; the
  regular Firebird build builds and tests hqisql.
- Tested with Firebird `v5.0.4` (Linux, gcc) and `v5.0-release`
  (Windows, Visual Studio 2026).
