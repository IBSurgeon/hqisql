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

#include "firebird.h"
#include "../hqisql/HqMetadata.h"
#include "../hqisql/HqKeywords.h"

#include "../include/ibase.h"
#include "../common/classes/auto.h"
#include "../common/classes/ImplementHelper.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string.h>
#include <thread>
#include <vector>

namespace
{

const unsigned CS_UTF8_ID = 4;			// UTF8 character set id
const unsigned NAME_BUFFER = 1024;		// bytes for one name in UTF-8

struct Snapshot
{
	// Flat name list (relations, all fields, procedures, functions,
	// generators, packages, package entries) for non-qualified completion.
	std::vector<std::string> words;
	std::vector<Hq::TableInfo> tables;
	std::vector<Hq::PackageInfo> packages;
};

// g_current is used by the editor thread only. The loader thread puts its
// result into g_ready; the editor thread takes it in syncMetadata().
Snapshot g_current;
std::unique_ptr<Snapshot> g_ready;
std::mutex g_readyMutex;
// Never destroyed: a joinable std::thread destructor would terminate the
// process if isql exits without waitMetadata().
std::thread* g_loader = NULL;

// A VARCHAR column of the message, trailing blanks removed.
std::string extractName(const unsigned char* msg, unsigned off, unsigned len)
{
	const short rawLen = *(const short*) (msg + off);
	const unsigned n = rawLen < 0 || unsigned(rawLen) > len - sizeof(short) ?
		len - sizeof(short) : unsigned(rawLen);
	std::string s((const char*) msg + off + sizeof(short), n);

	while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
		s.pop_back();

	return s;
}

// Run a query and pass every row (up to 4 name columns) to onRow. The
// columns are fetched as UTF-8 whatever the connection character set is:
// the editor works with UTF-8. All errors are swallowed - a failed system
// table just contributes nothing.
void loadQuery(Firebird::CheckStatusWrapper* status, Firebird::IAttachment* attachment,
	Firebird::ITransaction* transaction, const char* sql, unsigned colCount,
	const std::function<void(const std::vector<std::string>&)>& onRow)
{
	Firebird::IStatement* statement = attachment->prepare(status, transaction, strlen(sql), sql,
		3, Firebird::IStatement::PREPARE_PREFETCH_NONE);

	if (status->getState() & Firebird::IStatus::STATE_ERRORS)
	{
		status->init();	// discard errors of this query only
		return;
	}

	Firebird::IMessageMetadata* meta = NULL;
	Firebird::IMetadataBuilder* builder = NULL;
	Firebird::IMessageMetadata* described = statement->getOutputMetadata(status);

	if (!(status->getState() & Firebird::IStatus::STATE_ERRORS) &&
		described->getCount(status) >= colCount)
	{
		builder = described->getBuilder(status);

		for (unsigned i = 0; i < colCount && !(status->getState() & Firebird::IStatus::STATE_ERRORS); ++i)
		{
			builder->setType(status, i, SQL_VARYING + 1);	// nullable
			builder->setCharSet(status, i, CS_UTF8_ID);
			builder->setLength(status, i, NAME_BUFFER);
		}

		if (!(status->getState() & Firebird::IStatus::STATE_ERRORS))
			meta = builder->getMetadata(status);
	}

	if (builder)
		builder->release();

	if (described)
		described->release();

	if (!meta || (status->getState() & Firebird::IStatus::STATE_ERRORS))
	{
		status->init();

		if (meta)
			meta->release();

		statement->release();
		return;
	}

	const unsigned msgLen = meta->getMessageLength(status);
	unsigned offsets[4], nullOffsets[4];

	for (unsigned i = 0; i < colCount; ++i)
	{
		offsets[i] = meta->getOffset(status, i);
		nullOffsets[i] = meta->getNullOffset(status, i);
	}

	Firebird::IResultSet* rs = NULL;

	if (!(status->getState() & Firebird::IStatus::STATE_ERRORS))
	{
		rs = statement->openCursor(status, transaction, NULL, NULL, meta, 0);

		if (!(status->getState() & Firebird::IStatus::STATE_ERRORS))
		{
			std::vector<unsigned char> buffer(msgLen ? msgLen : 1);

			while (rs->fetchNext(status, &buffer[0]) == Firebird::IStatus::RESULT_OK)
			{
				std::vector<std::string> row(colCount);

				for (unsigned i = 0; i < colCount; ++i)
				{
					if (!*(const short*) &buffer[nullOffsets[i]])
						row[i] = extractName(&buffer[0], offsets[i], NAME_BUFFER + sizeof(short));
				}

				onRow(row);
			}

			status->init();	// end of fetch is not an error of its own
		}
	}

	if (rs)
		rs->close(status);

	meta->release();
	statement->release();
	status->init();	// discard errors of this query only
}

void addPackageEntry(std::vector<Hq::PackageInfo>& packages, const std::string& pkg,
	const std::string& entry)
{
	for (auto& p : packages)
	{
		if (p.name == pkg)
		{
			p.entries.push_back(entry);
			return;
		}
	}

	Hq::PackageInfo p;
	p.name = pkg;
	p.entries.push_back(entry);
	packages.push_back(p);
}

// Loader thread body: NULL when the transaction can not be started.
Snapshot* load(Firebird::IAttachment* attachment)
{
	Firebird::AutoPtr<Firebird::IStatus, Firebird::SimpleDispose> statusMaster(
		Firebird::MasterInterfacePtr()->getStatus());
	Firebird::CheckStatusWrapper status(statusMaster);

	// Read-only transaction with a short lifetime: it does not touch any
	// user transaction of the session.
	static const UCHAR tpb[] = {isc_tpb_version3, isc_tpb_read, isc_tpb_read_committed, isc_tpb_wait};
	Firebird::ITransaction* transaction = attachment->startTransaction(&status, sizeof(tpb), tpb);

	if (status.getState() & Firebird::IStatus::STATE_ERRORS)
		return NULL;

	std::unique_ptr<Snapshot> snapshot(new Snapshot);
	std::vector<std::string>& words = snapshot->words;
	std::vector<Hq::TableInfo>& tables = snapshot->tables;
	std::vector<Hq::PackageInfo>& packages = snapshot->packages;
	std::map<std::string, size_t> tableIndex;

	const auto addWord = [&words](const std::vector<std::string>& row)
	{
		if (!row[0].empty())
			words.push_back(row[0]);
	};

	// Relations.
	loadQuery(&status, attachment, transaction,
		"SELECT RDB$RELATION_NAME FROM RDB$RELATIONS", 1,
		[&](const std::vector<std::string>& row)
		{
			if (row[0].empty())
				return;

			tableIndex[row[0]] = tables.size();
			Hq::TableInfo t;
			t.name = row[0];
			tables.push_back(t);
			words.push_back(row[0]);
		});

	// Fields together with their relation - the flat list gets the field
	// names, the tables get the relation -> columns structure.
	loadQuery(&status, attachment, transaction,
		"SELECT RDB$RELATION_NAME, RDB$FIELD_NAME FROM RDB$RELATION_FIELDS", 2,
		[&](const std::vector<std::string>& row)
		{
			if (row[1].empty())
				return;

			words.push_back(row[1]);
			const auto it = tableIndex.find(row[0]);

			if (it != tableIndex.end())
				tables[it->second].columns.push_back(row[1]);
		});

	loadQuery(&status, attachment, transaction,
		"SELECT RDB$PROCEDURE_NAME FROM RDB$PROCEDURES WHERE RDB$PACKAGE_NAME IS NULL", 1, addWord);
	loadQuery(&status, attachment, transaction,
		"SELECT RDB$FUNCTION_NAME FROM RDB$FUNCTIONS WHERE RDB$PACKAGE_NAME IS NULL", 1, addWord);
	loadQuery(&status, attachment, transaction,
		"SELECT RDB$GENERATOR_NAME FROM RDB$GENERATORS", 1, addWord);
	loadQuery(&status, attachment, transaction,
		"SELECT RDB$PACKAGE_NAME FROM RDB$PACKAGES", 1, addWord);

	// Package members (second qualifier level: PACKAGE.ENTRY).
	const auto addEntry = [&](const std::vector<std::string>& row)
	{
		if (!row[0].empty() && !row[1].empty())
		{
			words.push_back(row[1]);
			addPackageEntry(packages, row[0], row[1]);
		}
	};

	loadQuery(&status, attachment, transaction,
		"SELECT RDB$PACKAGE_NAME, RDB$PROCEDURE_NAME FROM RDB$PROCEDURES"
		" WHERE RDB$PACKAGE_NAME IS NOT NULL", 2, addEntry);
	loadQuery(&status, attachment, transaction,
		"SELECT RDB$PACKAGE_NAME, RDB$FUNCTION_NAME FROM RDB$FUNCTIONS"
		" WHERE RDB$PACKAGE_NAME IS NOT NULL", 2, addEntry);

	std::sort(words.begin(), words.end());
	words.erase(std::unique(words.begin(), words.end()), words.end());

	for (auto& t : tables)
	{
		std::sort(t.columns.begin(), t.columns.end());
		t.columns.erase(std::unique(t.columns.begin(), t.columns.end()), t.columns.end());
	}

	std::sort(tables.begin(), tables.end(),
		[](const Hq::TableInfo& a, const Hq::TableInfo& b) { return a.name < b.name; });

	for (auto& p : packages)
	{
		std::sort(p.entries.begin(), p.entries.end());
		p.entries.erase(std::unique(p.entries.begin(), p.entries.end()), p.entries.end());
	}

	std::sort(packages.begin(), packages.end(),
		[](const Hq::PackageInfo& a, const Hq::PackageInfo& b) { return a.name < b.name; });

	transaction->commit(&status);
	transaction->release();

	return snapshot.release();
}

}	// anonymous namespace


namespace Hq {

void refreshMetadata(Firebird::IAttachment* attachment)
{
	waitMetadata();

	if (!attachment)
	{
		std::lock_guard<std::mutex> guard(g_readyMutex);
		g_ready.reset(new Snapshot);
		return;
	}

	// The thread holds its own reference: the attachment object stays valid
	// even if isql releases it while the load runs.
	attachment->addRef();

	g_loader = new std::thread([attachment]
	{
		Snapshot* snapshot = load(attachment);

		if (snapshot)
		{
			std::lock_guard<std::mutex> guard(g_readyMutex);
			g_ready.reset(snapshot);
		}

		attachment->release();
	});
}

void waitMetadata()
{
	if (g_loader)
	{
		g_loader->join();
		delete g_loader;
		g_loader = NULL;
	}
}

void syncMetadata()
{
	std::lock_guard<std::mutex> guard(g_readyMutex);

	if (g_ready)
	{
		g_current = std::move(*g_ready);
		g_ready.reset();
	}
}

const std::vector<std::string>& metadataWords()
{
	return g_current.words;
}

const std::vector<Hq::TableInfo>& tables()
{
	return g_current.tables;
}

const std::vector<Hq::PackageInfo>& packages()
{
	return g_current.packages;
}

const Hq::TableInfo* findTable(const std::string& name)
{
	const std::vector<Hq::TableInfo>& list = tables();
	const auto it = std::lower_bound(list.begin(), list.end(), name,
		[](const Hq::TableInfo& t, const std::string& n) { return t.name < n; });

	return it != list.end() && it->name == name ? &*it : NULL;
}

const Hq::PackageInfo* findPackage(const std::string& name)
{
	const std::vector<Hq::PackageInfo>& list = packages();
	const auto it = std::lower_bound(list.begin(), list.end(), name,
		[](const Hq::PackageInfo& p, const std::string& n) { return p.name < n; });

	return it != list.end() && it->name == name ? &*it : NULL;
}

std::string sqlName(const std::string& name)
{
	bool regular = !name.empty() && name[0] >= 'A' && name[0] <= 'Z' &&
		!isReservedWord(name.data(), name.size());

	for (size_t i = 1; regular && i < name.size(); ++i)
	{
		const char c = name[i];
		regular = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
	}

	if (regular)
		return name;

	std::string quoted("\"");

	for (const char c : name)
	{
		quoted += c;

		if (c == '"')
			quoted += '"';
	}

	return quoted + '"';
}

}	// namespace Hq
