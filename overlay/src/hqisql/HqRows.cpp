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
#include "../hqisql/HqRows.h"

#include <stdio.h>
#include <stdlib.h>

namespace
{

const int64_t NANOS_PER_SECOND = 1000000000;
const int64_t SECONDS_PER_DAY = 86400;

// a / b rounded down (b > 0).
int64_t floorDiv(int64_t a, int64_t b)
{
	return a / b - (a % b < 0 ? 1 : 0);
}

int64_t floorMod(int64_t a, int64_t b)
{
	const int64_t m = a % b;
	return m < 0 ? m + b : m;
}

// The digits of an unsigned number in base 256, most significant first,
// as a decimal string.
std::string toDecimal(std::vector<unsigned char> digits)
{
	std::string result;

	for (;;)
	{
		size_t first = 0;

		while (first < digits.size() && digits[first] == 0)
			++first;

		if (first == digits.size())
			break;

		unsigned remainder = 0;

		for (size_t i = first; i < digits.size(); ++i)
		{
			const unsigned value = remainder * 256 + digits[i];
			digits[i] = (unsigned char) (value / 10);
			remainder = value % 10;
		}

		result.insert(result.begin(), char('0' + remainder));
	}

	return result.empty() ? "0" : result;
}

// Digits (without a sign) with scale digits after the point.
std::string withScale(std::string digits, int scale)
{
	if (scale <= 0)
		return scale < 0 && digits != "0" ? digits + std::string(-scale, '0') : digits;

	if (digits.size() <= (size_t) scale)
		digits.insert(0, scale + 1 - digits.size(), '0');

	digits.insert(digits.size() - scale, 1, '.');
	return digits;
}

}	// anonymous namespace


namespace Hq {
namespace Rows {

bool Reader::select(const std::vector<unsigned>&)
{
	return true;
}

std::string csvLine(const Record& record)
{
	std::string line;

	for (size_t i = 0; i < record.size(); ++i)
	{
		if (i)
			line += ',';

		const Field& field = record[i];

		if (field.null)
			continue;

		const std::string& v = field.value;
		const bool quote = v.empty() || v.find_first_of(",\"\r\n") != std::string::npos ||
			v.front() == ' ' || v.back() == ' ';

		if (!quote)
		{
			line += v;
			continue;
		}

		line += '"';

		for (const char c : v)
		{
			line += c;

			if (c == '"')
				line += '"';
		}

		line += '"';
	}

	return line;
}

std::string formatDate(int64_t days)
{
	// The civil date of a day number (H. Hinnant, "chrono-Compatible
	// Low-Level Date Algorithms").
	const int64_t z = days + 719468;
	const int64_t era = floorDiv(z, 146097);
	const int64_t doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp = (5 * doy + 2) / 153;
	const int64_t d = doy - (153 * mp + 2) / 5 + 1;
	const int64_t m = mp < 10 ? mp + 3 : mp - 9;
	const int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);

	char buf[32];
	snprintf(buf, sizeof(buf), "%04lld-%02d-%02d", (long long) y, (int) m, (int) d);
	return buf;
}

std::string formatTime(int64_t nanosOfDay)
{
	nanosOfDay = floorMod(nanosOfDay, SECONDS_PER_DAY * NANOS_PER_SECOND);
	const int64_t seconds = nanosOfDay / NANOS_PER_SECOND;
	const int fraction = int(nanosOfDay % NANOS_PER_SECOND / 100000);	// 1/10000 s

	char buf[32];

	if (fraction)
	{
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%04d", int(seconds / 3600), int(seconds / 60 % 60),
			int(seconds % 60), fraction);
	}
	else
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d", int(seconds / 3600), int(seconds / 60 % 60), int(seconds % 60));

	return buf;
}

std::string formatTimestamp(int64_t seconds, int64_t nanos)
{
	seconds += floorDiv(nanos, NANOS_PER_SECOND);
	nanos = floorMod(nanos, NANOS_PER_SECOND);

	const int64_t days = floorDiv(seconds, SECONDS_PER_DAY);
	const int64_t secondOfDay = floorMod(seconds, SECONDS_PER_DAY);

	return formatDate(days) + " " + formatTime(secondOfDay * NANOS_PER_SECOND + nanos);
}

std::string formatUuid(std::string_view bytes)
{
	static const char hex[] = "0123456789abcdef";
	std::string result;

	for (size_t i = 0; i < bytes.size(); ++i)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10)
			result += '-';

		const unsigned char c = (unsigned char) bytes[i];
		result += hex[c >> 4];
		result += hex[c & 15];
	}

	return result;
}

std::string formatHex(std::string_view bytes)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string result;

	for (const char b : bytes)
	{
		const unsigned char c = (unsigned char) b;
		result += hex[c >> 4];
		result += hex[c & 15];
	}

	return result;
}

std::string formatDouble(double value)
{
	// The shortest text that reads back as the same value.
	char buf[64];

	for (int precision = 15; precision <= 17; ++precision)
	{
		snprintf(buf, sizeof(buf), "%.*g", precision, value);

		if (strtod(buf, nullptr) == value)
			break;
	}

	return buf;
}

std::string formatFloat(float value)
{
	char buf[64];

	for (int precision = 6; precision <= 9; ++precision)
	{
		snprintf(buf, sizeof(buf), "%.*g", precision, (double) value);

		if (strtof(buf, nullptr) == value)
			break;
	}

	return buf;
}

std::string formatBigDecimal(std::string_view bytes, int scale)
{
	std::vector<unsigned char> digits(bytes.begin(), bytes.end());
	const bool negative = !digits.empty() && (digits[0] & 0x80);

	if (negative)
	{
		// The magnitude: the two's complement negated.
		unsigned carry = 1;

		for (size_t i = digits.size(); i-- > 0;)
		{
			const unsigned value = (unsigned char) ~digits[i] + carry;
			digits[i] = (unsigned char) value;
			carry = value >> 8;
		}
	}

	return (negative ? "-" : "") + withScale(toDecimal(digits), scale);
}

std::string formatDecimal(int64_t value, int scale)
{
	const bool negative = value < 0;
	const uint64_t magnitude = negative ? ~uint64_t(value) + 1 : uint64_t(value);

	char buf[32];
	snprintf(buf, sizeof(buf), "%llu", (unsigned long long) magnitude);

	return (negative ? "-" : "") + withScale(buf, scale);
}

}	// namespace Rows
}	// namespace Hq
