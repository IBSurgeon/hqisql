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

#ifndef FB_HQISQL_HQ_IMPORT_H
#define FB_HQISQL_HQ_IMPORT_H

#include "../hqisql/HqImportCommand.h"

namespace Hq {

// Executes EXEC SQL name FROM CSV (sql: the text of the PREPARE) or IMPORT
// INTO table (sql is not used) - see HqImportCommand.h.
//
// The statement is prepared on the server once, in the DML transaction of
// isql. Every parameter is sent as VARCHAR in the connection charset (or the
// ENCODING of the file) and the server converts it to the parameter type.
// The rows go in batches (IBatch); each batch runs under a savepoint. When a
// batch fails, it is rolled back to the savepoint and its rows run again one
// by one, so an error has the exact line of the file. Without ERRORS the
// import stops at the first bad row (the rows before it stay in the
// transaction, as with SET BULK_INSERT), with ERRORS = n up to n bad rows
// are written to <file>.bad and skipped. Returns SKIP, or ps_ERR after
// printing the error.
processing_state runImport(const Import::Command& command, const std::string& sql);

}	// namespace Hq

#endif	// FB_HQISQL_HQ_IMPORT_H
