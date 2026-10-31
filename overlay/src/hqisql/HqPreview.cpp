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
 *  hqisql: SET PREVIEW - the glue between the isql fetch loop and the
 *  compact table (HqTable.cpp).
 */

#include "firebird.h"
#include <stdio.h>
#include "../isql/isql.h"
#include "../hqisql/HqIsql.h"
#include "../hqisql/HqTable.h"
#include "../intl/charsets.h"
#include "../common/classes/auto.h"
#include "../common/classes/ImplementHelper.h"
#include <errno.h>
#include <limits.h>
#include <memory>
#include <stdlib.h>
#include <string.h>

#ifdef WIN_NT
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace Hq {

namespace {

const unsigned DEFAULT_ROWS = 20;

bool g_enabled = false;
unsigned g_rows = DEFAULT_ROWS;
bool g_limited = false;			// the row limit stopped the fetch loop
std::unique_ptr<CompactTable> g_table;	// set while the rows are collected

// Columns of the screen the output goes to; 0 when it is not a terminal.
unsigned screenWidth()
{
	const int fd = fileno(isqlGlob.Out);

#ifdef WIN_NT
	if (!_isatty(fd))
		return 0;

	CONSOLE_SCREEN_BUFFER_INFO info;

	if (!GetConsoleScreenBufferInfo((HANDLE) _get_osfhandle(fd), &info))
		return 0;

	return info.srWindow.Right - info.srWindow.Left + 1;
#else
	if (!isatty(fd))
		return 0;

	winsize ws;

	if (ioctl(fd, TIOCGWINSZ, &ws) != 0)
		return 0;

	return ws.ws_col;
#endif
}

}	// anonymous namespace

bool Preview::set(const char* arg)
{
	if (!*arg)
		g_enabled = !g_enabled;
	else if (strcmp(arg, "ON") == 0)
		g_enabled = true;
	else if (strcmp(arg, "OFF") == 0)
		g_enabled = false;
	else
	{
		char* end = NULL;
		errno = 0;
		const long n = strtol(arg, &end, 10);

		if (end == arg || *end || errno || n <= 0 || n > INT_MAX)
			return false;

		g_rows = (unsigned) n;
		g_enabled = true;
	}

	return true;
}

bool Preview::enabled()
{
	return g_enabled;
}

unsigned Preview::rows()
{
	return g_rows;
}

void Preview::begin(Firebird::IMessageMetadata* message, bool list, bool heading)
{
	g_table.reset();
	g_limited = false;

	if (!g_enabled || list)
		return;

	g_table.reset(new CompactTable(isqlGlob.att_charset == CS_UTF8));

	if (heading)
	{
		Firebird::AutoPtr<Firebird::IStatus, Firebird::SimpleDispose> statusMaster(
			Firebird::MasterInterfacePtr()->getStatus());
		Firebird::CheckStatusWrapper status(statusMaster);
		std::vector<std::string> titles;
		const unsigned count = message->getCount(&status);

		for (unsigned i = 0; i < count && !(status.getState() & Firebird::IStatus::STATE_ERRORS); ++i)
		{
			const char* alias = message->getAlias(&status, i);
			titles.push_back(alias ? alias : "");
		}

		g_table->setTitles(titles);
	}
}

bool Preview::collecting()
{
	return g_table != NULL;
}

bool Preview::full(unsigned lines)
{
	if (!g_table || lines < g_rows)
		return false;

	g_limited = true;
	return true;
}

void Preview::addCell(const char* from, const char* to)
{
	if (g_table)
		g_table->addCell(from, to);
}

bool Preview::endRow()
{
	if (!g_table)
		return false;

	g_table->endRow();
	return true;
}

void Preview::end()
{
	if (!g_table)
		return;

	std::unique_ptr<CompactTable> table(g_table.release());

	if (!table->rowCount())
		return;

	const unsigned width = screenWidth();
	std::vector<std::string> dropped;

	for (const std::string& line : table->format(width, &dropped))
		isqlGlob.printf("%s%s", line.c_str(), NEWLINE);

	// What the preview left out: the rows after the first g_rows, the
	// columns that did not fit the screen.
	if (g_limited || !dropped.empty())
	{
		std::string note("-- preview:");

		if (g_limited)
			note += " first " + std::to_string(g_rows) + " rows";

		if (!dropped.empty())
		{
			note += g_limited ? ";" : "";
			note += " " + std::to_string(dropped.size()) + " more column" + (dropped.size() > 1 ? "s" : "") + ":";

			for (const std::string& d : dropped)
				note += " " + d;
		}

		if (width > 1 && note.size() > width - 1)
			note.replace(width - 4, std::string::npos, "...");

		isqlGlob.printf("%s%s", note.c_str(), NEWLINE);
	}

	isqlGlob.printf(NEWLINE);
}

}	// namespace Hq
