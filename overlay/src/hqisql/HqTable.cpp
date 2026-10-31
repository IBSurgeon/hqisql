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

#include "firebird.h"
#include "HqTable.h"
#include <algorithm>

namespace Hq {

namespace {

const size_t MIN_CUT_WIDTH = 4;		// a cut column keeps at least this many characters

bool isLead(char c)
{
	return (((unsigned char) c) & 0xC0) != 0x80;
}

std::string trim(const std::string& s)
{
	const size_t first = s.find_first_not_of(' ');

	if (first == std::string::npos)
		return std::string();

	return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

void rtrim(std::string& s)
{
	const size_t last = s.find_last_not_of(' ');
	s.erase(last == std::string::npos ? 0 : last + 1);
}

}	// anonymous namespace

CompactTable::CompactTable(bool aUtf8)
	: utf8(aUtf8)
{
}

void CompactTable::setTitles(const std::vector<std::string>& aTitles)
{
	titles = aTitles;
	hasTitles = true;
}

void CompactTable::addCell(const char* from, const char* to)
{
	row.push_back(std::string(from, to));
}

void CompactTable::endRow()
{
	rows.push_back(row);
	row.clear();
}

size_t CompactTable::rowCount() const
{
	return rows.size();
}

size_t CompactTable::charLength(const std::string& s) const
{
	if (!utf8)
		return s.size();

	return std::count_if(s.begin(), s.end(), isLead);
}

// The first n characters of s.
std::string CompactTable::firstChars(const std::string& s, size_t n) const
{
	if (!utf8)
		return s.substr(0, n);

	size_t i = 0;

	for (size_t seen = 0; i < s.size(); ++i)
	{
		if (isLead(s[i]))
		{
			if (seen == n)
				break;

			++seen;
		}
	}

	return s.substr(0, i);
}

// value in a column of the given width: cut when longer, else padded on the
// left (right) or on the right.
std::string CompactTable::cell(const std::string& value, size_t width, bool right) const
{
	const size_t len = charLength(value);

	if (len >= width)
		return firstChars(value, width);

	const std::string blanks(width - len, ' ');
	return right ? blanks + value : value + blanks;
}

std::vector<std::string> CompactTable::format(unsigned width, std::vector<std::string>* dropped) const
{
	size_t cols = titles.size();

	for (const auto& r : rows)
		cols = std::max(cols, r.size());

	// The values without the padding, the width and the alignment of each
	// column: to the right when isql padded a value on the left (numbers,
	// <null> of a numeric column).
	std::vector<std::vector<std::string>> values(rows.size());
	std::vector<size_t> widths(cols, 1);
	std::vector<bool> right(cols, false);

	for (size_t c = 0; c < titles.size(); ++c)
		widths[c] = std::max(widths[c], charLength(titles[c]));

	for (size_t r = 0; r < rows.size(); ++r)
	{
		values[r].resize(cols);

		for (size_t c = 0; c < rows[r].size(); ++c)
		{
			const std::string& raw = rows[r][c];
			values[r][c] = trim(raw);
			widths[c] = std::max(widths[c], charLength(values[r][c]));

			if (!raw.empty() && raw[0] == ' ' && !values[r][c].empty())
				right[c] = true;
		}
	}

	// The columns that fit the width: the first that does not is cut when
	// enough of it remains, the ones after it are left out.
	size_t shown = cols;

	if (width > 1)
	{
		size_t avail = width - 1;

		for (size_t c = 0; c < cols; ++c)
		{
			const size_t sep = c ? 1 : 0;

			if (widths[c] + sep <= avail)
			{
				avail -= widths[c] + sep;
				continue;
			}

			if (avail >= sep + MIN_CUT_WIDTH)
			{
				widths[c] = avail - sep;
				shown = c + 1;
			}
			else
				shown = c;

			break;
		}
	}

	if (dropped)
	{
		for (size_t c = shown; c < cols; ++c)
			dropped->push_back(c < titles.size() ? titles[c] : std::to_string(c + 1));
	}

	std::vector<std::string> lines;

	auto addLine = [&](const std::vector<std::string>& cells, bool titleLine)
	{
		std::string line;

		for (size_t c = 0; c < shown; ++c)
		{
			if (c)
				line += ' ';

			// Titles are aligned as their values, like in isql.
			line += cell(c < cells.size() ? cells[c] : std::string(), widths[c], right[c]);
		}

		rtrim(line);
		lines.push_back(line);
	};

	if (hasTitles)
	{
		addLine(titles, true);

		std::vector<std::string> rule(shown);

		for (size_t c = 0; c < shown; ++c)
			rule[c].assign(widths[c], '=');

		addLine(rule, true);
	}

	for (const auto& v : values)
		addLine(v, false);

	return lines;
}

}	// namespace Hq
