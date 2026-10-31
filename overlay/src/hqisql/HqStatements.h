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

#ifndef FB_HQISQL_HQ_STATEMENTS_H
#define FB_HQISQL_HQ_STATEMENTS_H

#include "../hqisql/HqIsql.h"
#include <string>
#include <vector>

namespace Hq {

// PREPARE name AS <statement>                 - store a statement (with ? inputs)
// EXEC SQL name USING (v, ...)                - execute with the given values
// EXEC SQL name USING BATCH (v, ...), (...)   - execute for every tuple
// EXEC SQL name FROM CSV 'file' [OPTIONS (...)] - execute for every row (HqImport.h)
// IMPORT INTO table [(cols)] FROM CSV 'file' [OPTIONS (...)]
// DEALLOCATE name                             - drop the stored statement
//
// This is text substitution in the client, not a server-side prepared
// statement: EXEC SQL replaces each ? mark (outside strings, quoted names
// and comments) with the value as it is spelled, and the result is a new
// statement that the server prepares and executes. So a value can be any
// SQL expression, and strings must be quoted. See HqParams.h.
//
// Returns CONT when the statement is none of these commands. EXEC SQL without
// BATCH rewrites the statement in place and returns CONT, so the caller
// executes it exactly as if it was entered directly. The other commands
// return SKIP, or ps_ERR after printing an error. BATCH checks all tuples
// first, then runs every rewritten statement through execute() and stops at
// the first error.
processing_state statementCommand(Firebird::string& statement, ExecuteFunc execute);

// The names of the stored statements in upper case, sorted (for the
// completion after EXEC SQL and DEALLOCATE).
std::vector<std::string> preparedNames();

}	// namespace Hq

#endif	// FB_HQISQL_HQ_STATEMENTS_H
