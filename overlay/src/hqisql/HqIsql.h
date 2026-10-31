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
 *  hqisql hooks: the only interface between isql.epp and the hqisql layer
 *  (replxx line editor, syntax highlighting, completion, history and the
 *  PREPARE / EXEC SQL / DEALLOCATE commands). isql.epp includes this file and
 *  calls the hooks only when HQISQL is defined, so the regular isql build
 *  does not see any of it. Include after isql.h.
 */

#ifndef FB_HQISQL_HQ_ISQL_H
#define FB_HQISQL_HQ_ISQL_H

#include "../common/classes/fb_string.h"
#include "HqVersion.h"

namespace Firebird {
class IMessageMetadata;
class ITransaction;
}

namespace Hq {

// The isql state for the hqisql commands. Defined in the HQISQL block at the
// end of isql.epp, so the isql variables stay static.
namespace Isql {

// The DML transaction, as for a statement typed in isql (started when there
// is none). NULL after an error, which is printed.
Firebird::ITransaction* userTransaction();

// Commits the DML transaction, if any. False after an error, which is
// printed; the transaction is rolled back then.
bool commitUserTransaction();

// The script being read: INPUT or -i. Empty for the console.
Firebird::PathName inputFileName();

bool interactive();		// reading the console, not a script
bool interrupted();		// Ctrl-C was pressed
bool quiet();			// -q

// After the error of an hqisql command read from a script: "After line N in
// file F", as isql prints after an error of a statement.
void printErrorLocation();

}	// namespace Isql

// Remove hqisql-only command line switches from argv and configure the
// editor with history, highlighter and completion callbacks. The console
// itself is not changed: the editor switches it to UTF-8 only while a line
// is edited. -noedit or HQISQL_NOEDIT=1 disable the editor. Called once at
// startup, before the regular command line parsing.
void init(int& argc, char** argv);

// Save the history file and release the editor.
void shutdown();

// Hook of readNextInputLine(). Returns false when the editor is not used for
// this read (not interactive, no console, -noedit) - the caller continues
// with its regular input. Otherwise *line receives the next line allocated
// with malloc() (the lastInputLine contract), or NULL on EOF. A multi-line
// input (pasted text) is split into lines returned by the subsequent calls.
// The line is in the console input code page, as isql reads it. Ctrl-C
// drops the line being edited and the statement started on previous lines.
bool readLine(const char* prompt, bool interactive, char** line);

typedef processing_state (*ExecuteFunc)(const TEXT* statement);

// Lives in get_statement() while the statement is assembled: the text
// accumulated so far is the context of the completion and hint callbacks
// (aliases and qualifiers declared on the previous CON> lines).
class StatementScope
{
public:
	explicit StatementScope(Firebird::string& aStatement);
	~StatementScope();

	StatementScope(const StatementScope&) = delete;
	StatementScope& operator=(const StatementScope&) = delete;

	// The statement is complete: put it into the editor history (unless it
	// contains a password) and handle PREPARE / EXEC SQL / DEALLOCATE.
	// A statement dropped with Ctrl-C returns SKIP. Returns the new state;
	// EXEC SQL without BATCH rewrites the statement and returns CONT, so the
	// caller executes it through its regular path. execute() runs a single
	// statement of EXEC SQL ... USING BATCH the same way: isql commands
	// first, then SQL.
	processing_state complete(processing_state ret, ExecuteFunc execute);

private:
	friend bool readLine(const char*, bool, char**);

	Firebird::string& statement;
	StatementScope* const outer;
	bool fromEditor;		// at least one line was read through the editor
	bool cancelled;			// dropped with Ctrl-C
};

// SET PREVIEW [ON | OFF | <n>]: the first n rows (20 by default) of a
// result set as a compact table - every column as wide as its widest value
// or title, the columns that do not fit the screen cut off on the right.
// While a statement is previewed, print_line() hands its cells over
// instead of printing them and the table is printed once after the fetch
// loop (the implementation is in HqPreview.cpp, the layout in HqTable.cpp).
class Preview
{
public:
	// The SET PREVIEW argument: empty toggles, ON / OFF, a number of rows
	// also turns the preview on. False when the argument is none of these.
	static bool set(const char* arg);
	static bool enabled();
	static unsigned rows();

	// Before the fetch loop of a statement with a cursor: starts collecting
	// when the preview is on and the output is not SET LIST. heading: the
	// column titles are wanted (SET HEADING).
	static void begin(Firebird::IMessageMetadata* message, bool list, bool heading);

	// True while the rows of the statement are collected: the caller skips
	// its own header output.
	static bool collecting();

	// True when lines rows are collected already: the fetch loop stops.
	static bool full(unsigned lines);

	// From print_line(): the bytes [from, to) of one cell as printed, then
	// the end of the row. endRow() returns true when the row was collected
	// and must not be printed.
	static void addCell(const char* from, const char* to);
	static bool endRow();

	// After the fetch loop: prints the table and stops collecting.
	static void end();
};

}	// namespace Hq

#endif	// FB_HQISQL_HQ_ISQL_H
