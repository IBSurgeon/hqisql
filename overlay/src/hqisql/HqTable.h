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
 *  hqisql: the compact table of SET PREVIEW.
 */

#ifndef FB_HQISQL_HQ_TABLE_H
#define FB_HQISQL_HQ_TABLE_H

#include <stddef.h>
#include <string>
#include <vector>

namespace Hq {

// A result set as a compact table: every column as wide as its widest value
// or title, the columns that do not fit the screen cut off on the right.
// Built from the cells as isql prints them (padded to the column width, one
// blank after); a value keeps the alignment isql gave it (numbers to the
// right), so nothing is parsed. No dependency on isql: covered by tests.
class CompactTable
{
public:
	// utf8: the cells are UTF-8, widths are counted in code points.
	explicit CompactTable(bool utf8);

	// The column titles; without a call the table has no title lines.
	void setTitles(const std::vector<std::string>& titles);

	// A cell of the current row, the bytes [from, to) as isql printed them.
	void addCell(const char* from, const char* to);
	void endRow();

	size_t rowCount() const;

	// The lines of the table: the titles, the "===" line and the rows. With
	// width 0 the lines have no limit; otherwise they are at most width - 1
	// characters long: the columns after the last one that fits are left out
	// (their titles go to dropped, if not NULL) and the last shown one may be
	// cut. Trailing blanks are removed.
	std::vector<std::string> format(unsigned width, std::vector<std::string>* dropped = NULL) const;

private:
	size_t charLength(const std::string& s) const;
	std::string firstChars(const std::string& s, size_t n) const;
	std::string cell(const std::string& value, size_t width, bool right) const;

	const bool utf8;
	bool hasTitles = false;
	std::vector<std::string> titles;
	std::vector<std::string> row;		// the row being added
	std::vector<std::vector<std::string>> rows;	// the cells as printed
};

}	// namespace Hq

#endif	// FB_HQISQL_HQ_TABLE_H
