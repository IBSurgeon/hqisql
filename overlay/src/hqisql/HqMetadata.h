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

#ifndef FB_HQISQL_HQ_METADATA_H
#define FB_HQISQL_HQ_METADATA_H

#include <firebird/Interface.h>
#include <string>
#include <vector>

namespace Hq {

// Columns of one table or view. Names are the real metadata names in UTF-8
// (case kept, trailing blanks removed), sorted.
struct TableInfo
{
	std::string name;
	std::vector<std::string> columns;
};

// Procedures and functions of one package (real names, sorted).
struct PackageInfo
{
	std::string name;
	std::vector<std::string> entries;
};

// Start (re)loading the completion data from the attached database:
// relations, their fields, procedures, functions, generators and packages.
// The load runs in a background thread with its own short read-only
// transaction, so the prompt is not delayed; the data appears in the
// accessors below when it is ready. Failures leave the previous data in
// place. Passing NULL clears everything (disconnected state).
void refreshMetadata(Firebird::IAttachment* attachment);

// Wait for the background load. Call before isql uses the attachment again:
// executes a statement, disconnects or exits.
void waitMetadata();

// Make the data of a finished background load current. Call at the start
// of a completion or hint callback: the references returned below stay
// valid until the next call.
void syncMetadata();

// Sorted, duplicate-free name list for flat (non-qualified) completion;
// empty before the first successful load.
const std::vector<std::string>& metadataWords();

// Table/view and package structures for qualified completion, sorted by
// name; empty before the first successful load.
const std::vector<TableInfo>& tables();
const std::vector<PackageInfo>& packages();

// Binary search by the exact (resolved) name; NULL when not found.
const TableInfo* findTable(const std::string& name);
const PackageInfo* findPackage(const std::string& name);

// The name as it is written in SQL: unchanged when it is a regular
// identifier (upper case letters, digits, '_' and '$', not a reserved word),
// otherwise in double quotes with inner quotes doubled.
std::string sqlName(const std::string& name);

}	// namespace Hq

#endif	// FB_HQISQL_HQ_METADATA_H
