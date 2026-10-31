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
#include <stdio.h>
#include "../isql/isql.h"
#include "../common/classes/fb_string.h"
#include "../hqisql/HqIsql.h"
#include "../hqisql/HqColors.h"
#include "../hqisql/HqImportCommand.h"
#include "../hqisql/HqIsqlCommands.h"
#include "../hqisql/HqKeywords.h"
#include "../hqisql/HqLexer.h"
#include "../hqisql/HqMetadata.h"
#include "../hqisql/HqStatements.h"

#include <replxx.hxx>

#include <algorithm>
#include <errno.h>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using replxx::Replxx;

#ifdef WIN_NT
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

// Globals of isql.epp used here: the statement terminator comes from
// isqlGlob.global_Term (declared in isql.h), the attachment is the gpre
// database handle (shared with show.epp and extract.epp the same way).
extern Firebird::IAttachment* DB;

namespace
{

replxx::Replxx* g_rx = NULL;
bool g_consoleOk = false;		// stdin/stdout are a TTY
bool g_noEdit = false;

// Lines of multi-line or pasted input not given to isql yet, in the console
// code page.
struct PendingLine
{
	std::string text;
	bool echo;		// not shown by the editor: print it with the prompt
};

std::deque<PendingLine> g_pendingLines;
std::string g_context;			// statement text accumulated before the line being edited
Hq::StatementScope* g_scope = NULL;	// statement being assembled by get_statement()
bool g_metadataStale = false;	// reload the completion data before the next prompt
bool g_historyDirty = false;	// history added during a paste, not saved yet
Hq::ColorScheme g_colors;		// built-in colors, or the scheme file

// The color codes of ColorScheme are the values of Replxx::Color.
static_assert((unsigned) Replxx::Color::DEFAULT == Hq::ColorScheme::DEFAULT_COLOR, "color encoding");
static_assert((unsigned) Replxx::Color::WHITE == 15, "color encoding");
static_assert((unsigned) Replxx::Color::YELLOW == 11, "color encoding");

Replxx::Color colorOf(Hq::ColorScheme::Element e)
{
	return static_cast<Replxx::Color>(g_colors.color(e));
}

std::string historyPath()
{
#ifdef WIN_NT
	const char* appdata = getenv("LOCALAPPDATA");
	if (!appdata || !*appdata)
		return "hqisql_history.txt";

	std::string path(appdata);
	path += "\\hqisql";
	CreateDirectoryA(path.c_str(), NULL);
	path += "\\history.txt";
	return path;
#else
	const char* home = getenv("HOME");

	if (!home || !*home)
		return ".hqisql_history";

	return std::string(home) + "/.hqisql_history";
#endif
}

// The scheme file: HQISQL_COLORS, or colors.txt next to the history file.
std::string colorSchemePath()
{
	const char* env = getenv("HQISQL_COLORS");

	if (env && *env)
		return env;

#ifdef WIN_NT
	const char* appdata = getenv("LOCALAPPDATA");

	if (!appdata || !*appdata)
		return "";

	return std::string(appdata) + "\\hqisql\\colors.txt";
#else
	const char* home = getenv("HOME");

	if (!home || !*home)
		return "";

	return std::string(home) + "/.hqisql_colors";
#endif
}

// Reads the scheme file, if there is one, into g_colors. A missing file
// keeps the built-in colors silently, unless HQISQL_COLORS names it; an
// invalid line is reported and skipped.
void loadColorScheme()
{
	const std::string path(colorSchemePath());

	if (path.empty())
		return;

	std::ifstream file(path.c_str(), std::ios::binary);

	if (!file)
	{
		const char* env = getenv("HQISQL_COLORS");

		if (env && *env)
			fprintf(stderr, "hqisql: color scheme %s: cannot open the file\n", path.c_str());

		return;
	}

	std::stringstream text;
	text << file.rdbuf();
	std::vector<std::string> errors;
	g_colors.parse(text.str(), &errors);

	for (const std::string& e : errors)
		fprintf(stderr, "hqisql: color scheme %s: %s\n", path.c_str(), e.c_str());
}

bool stdinTty()
{
#ifdef WIN_NT
	return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
	return isatty(fileno(stdin)) && isatty(fileno(stdout));
#endif
}

bool noColor()
{
	return getenv("NO_COLOR") != NULL;
}

bool envNoEdit()
{
	const char* value = getenv("HQISQL_NOEDIT");
	return value && *value && strcmp(value, "0") != 0;
}

#ifdef WIN_NT
std::string convertCodePage(const std::string& s, UINT from, UINT to)
{
	if (from == to || s.empty())
		return s;

	const int wideLength = MultiByteToWideChar(from, 0, s.data(), (int) s.size(), NULL, 0);

	if (wideLength <= 0)
		return s;

	std::wstring wide(wideLength, L'\0');
	MultiByteToWideChar(from, 0, s.data(), (int) s.size(), &wide[0], wideLength);

	const int length = WideCharToMultiByte(to, 0, wide.data(), wideLength, NULL, 0, NULL, NULL);

	if (length <= 0)
		return s;

	std::string result(length, '\0');
	WideCharToMultiByte(to, 0, wide.data(), wideLength, &result[0], length, NULL, NULL);
	return result;
}
#endif

// The editor works with UTF-8 text, isql reads the console in its input code
// page (win32ReadConsole() in isql.epp). Convert the edited line to what
// isql would read, and the statement text isql has assembled back to UTF-8
// for the history and the completion context. Call outside of input(): the
// editor switches the console to UTF-8 while a line is edited.
std::string toConsole(const std::string& s)
{
#ifdef WIN_NT
	return convertCodePage(s, CP_UTF8, GetConsoleCP());
#else
	return s;
#endif
}

std::string fromConsole(const std::string& s)
{
#ifdef WIN_NT
	return convertCodePage(s, GetConsoleCP(), CP_UTF8);
#else
	return s;
#endif
}

// Splits text into lines at '\n' and drops '\r' at the line ends. The text
// after the last '\n' is a line too, when it is not empty.
void splitLines(const std::string& text, std::vector<std::string>& lines)
{
	size_t start = 0;

	while (start < text.size())
	{
		size_t end = text.find('\n', start);

		if (end == std::string::npos)
			end = text.size();

		std::string line(text, start, end - start);

		while (!line.empty() && line.back() == '\r')
			line.pop_back();

		lines.push_back(line);
		start = end + 1;
	}
}

#ifdef WIN_NT
bool isModifierKey(WORD vk)
{
	switch (vk)
	{
		case VK_SHIFT:
		case VK_CONTROL:
		case VK_MENU:
		case VK_CAPITAL:
		case VK_NUMLOCK:
		case VK_SCROLL:
		case VK_LWIN:
		case VK_RWIN:
			return true;
	}

	return false;
}

// A Windows console has no bracketed paste: each line end of pasted text is
// an Enter that ends input(), and an editor call per line makes the paste of
// a long script slow. Take the complete lines that already wait in the
// console input buffer at once, in the console code page. Stop at the first
// key that is not text (an arrow, a Ctrl key) and leave it and an incomplete
// last line to the editor.
std::string readPastedLines()
{
	const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	const DWORD ctrlAlt = LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED | LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED;
	const DWORD altGr = LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED;
	std::wstring text;
	std::vector<INPUT_RECORD> records;

	for (;;)
	{
		DWORD count = 0;

		if (!GetNumberOfConsoleInputEvents(in, &count) || count == 0)
		{
			// A long paste comes in parts: wait a little for the next one,
			// but only when a paste is already seen.
			if (text.empty() || WaitForSingleObject(in, 20) != WAIT_OBJECT_0)
				break;

			continue;
		}

		records.resize(std::min<DWORD>(count, 65536));

		if (!PeekConsoleInputW(in, records.data(), (DWORD) records.size(), &count) || count == 0)
			break;

		std::wstring chunk;
		size_t chunkLines = 0;	// length of chunk up to its last line end
		DWORD recordLines = 0;	// records up to the last line end
		bool stop = false;
		wchar_t prev = 0;

		for (DWORD i = 0; i < count && !stop; ++i)
		{
			if (records[i].EventType != KEY_EVENT)
				continue;

			const KEY_EVENT_RECORD& key = records[i].Event.KeyEvent;
			const wchar_t c = key.uChar.UnicodeChar;

			// Characters come with key down; Alt+numpad ones with the Alt
			// key up (as in replxx).
			if (!key.bKeyDown && !(key.wVirtualKeyCode == VK_MENU && c))
				continue;

			DWORD state = key.dwControlKeyState;

			if ((state & altGr) == altGr)
				state &= ~altGr;

			if (c == 0)
			{
				stop = !isModifierKey(key.wVirtualKeyCode);
				continue;
			}

			if (key.bKeyDown && (state & ctrlAlt))
			{
				stop = true;
				continue;
			}

			const size_t repeat = key.wRepeatCount ? key.wRepeatCount : 1;

			if (c == L'\r' || c == L'\n')
			{
				if (!(c == L'\n' && prev == L'\r'))
					chunk.append(repeat, L'\n');

				chunkLines = chunk.size();
				recordLines = i + 1;
			}
			else if (c == L'\t' || (c >= 0x20 && c != 0x7f))
				chunk.append(repeat, c);
			else
				stop = true;

			prev = c;
		}

		if (recordLines == 0)
			break;

		// Remove the records of the complete lines from the input buffer.
		for (DWORD done = 0; done < recordLines; )
		{
			DWORD read = 0;

			if (!ReadConsoleInputW(in, records.data(), recordLines - done, &read) || read == 0)
				break;

			done += read;
		}

		text.append(chunk, 0, chunkLines);

		if (recordLines < count)
			break;
	}

	if (text.empty())
		return std::string();

	const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int) text.size(), NULL, 0, NULL, NULL);
	std::string result(length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), (int) text.size(), &result[0], length, NULL, NULL);
	return toConsole(result);
}
#endif

// replxx colors are indexed by code points, the lexer works with byte
// offsets - build a byte offset to code point index map.
std::vector<int> byteToCodePoint(const std::string& s)
{
	std::vector<int> map(s.size() + 1, 0);
	int cp = 0;

	for (size_t i = 0; i < s.size(); )
	{
		map[i] = cp;
		const unsigned char b = (unsigned char) s[i];
		i += b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
		++cp;
	}

	map[s.size()] = cp;
	return map;
}

std::string upperWord(const std::string& s, const Hq::Lexer::HighlightRange& r)
{
	std::string word(s.substr(r.start, r.length));

	for (auto& c : word)
		c = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;

	return word;
}

// One flag per range: true for a word of an isql command (SET STATS ON,
// SHOW TABLES, IN file). The words of each statement (between terminators)
// are classified together: SET is a command only with an isql option after
// it, ON only inside such a command.
std::vector<bool> isqlCommandFlags(const std::string& input,
	const std::vector<Hq::Lexer::HighlightRange>& ranges)
{
	std::vector<bool> flags(ranges.size(), false);
	std::vector<std::string> words;		// the words of the current statement
	std::vector<size_t> wordRanges;		// and the index of their ranges

	const auto flush = [&]()
	{
		const std::vector<bool> cmd = Hq::IsqlCommands::commandWords(words);

		for (size_t i = 0; i < cmd.size(); ++i)
			flags[wordRanges[i]] = cmd[i];

		words.clear();
		wordRanges.clear();
	};

	for (size_t i = 0; i < ranges.size(); ++i)
	{
		const Hq::Lexer::HighlightRange& r = ranges[i];

		if (r.type == Hq::Lexer::Token::TYPE_TERMINATOR)
			flush();
		else if (r.type == Hq::Lexer::Token::TYPE_WORD)
		{
			words.push_back(upperWord(input, r));
			wordRanges.push_back(i);
		}
	}

	flush();
	return flags;
}

Replxx::Color mapColor(const Hq::Lexer::HighlightRange& r, const std::string& input, bool isqlWord)
{
	if (r.type == Hq::Lexer::Token::TYPE_WORD)
	{
		// Checked first: SET, IN and ON are reserved words too.
		if (isqlWord)
			return colorOf(Hq::ColorScheme::COMMAND);

		const std::string word(upperWord(input, r));

		if (Hq::isReservedWord(word.data(), word.size()))
			return colorOf(Hq::ColorScheme::RESERVED);

		// Non-reserved keywords (NAME, TYPE, ...) are often names: dimmer.
		if (Hq::isKeyword(word.data(), word.size()))
			return colorOf(Hq::ColorScheme::KEYWORD);

		return Replxx::Color::DEFAULT;
	}

	switch (r.type)
	{
		case Hq::Lexer::Token::TYPE_STRING:
			return colorOf(Hq::ColorScheme::STRING);

		case Hq::Lexer::Token::TYPE_META_STRING:
			return colorOf(Hq::ColorScheme::QUOTED);

		case Hq::Lexer::Token::TYPE_NUMBER:
			return colorOf(Hq::ColorScheme::NUMBER);

		case Hq::Lexer::Token::TYPE_COMMENT:
			return colorOf(Hq::ColorScheme::COMMENT);

		case Hq::Lexer::Token::TYPE_PARAM:
			// "?" alone is the isql help command.
			if (r.firstInStatement && input.compare(r.start, r.length, "?") == 0)
				return colorOf(Hq::ColorScheme::COMMAND);

			return colorOf(Hq::ColorScheme::PARAM);

		case Hq::Lexer::Token::TYPE_TERMINATOR:
			return colorOf(Hq::ColorScheme::TERMINATOR);

		default:
			return Replxx::Color::DEFAULT;
	}
}

void highlightCallback(const std::string& input, Replxx::colors_t& colors)
{
	for (auto& c : colors)
		c = Replxx::Color::DEFAULT;

	if (noColor() || colors.empty())
		return;

	// Lex the previous lines of the statement too: a string or a comment
	// opened on a CON> line continues on this one. Only the ranges inside
	// the edited line are colored.
	const std::string full(g_context + input);
	const size_t base = g_context.size();
	const std::string term(isqlGlob.global_Term);
	const std::vector<Hq::Lexer::HighlightRange> ranges = Hq::Lexer::highlight(full, term);
	const std::vector<bool> isqlWords = isqlCommandFlags(full, ranges);
	const std::vector<int> cpMap = byteToCodePoint(input);
	const int cpCount = (int) colors.size();

	for (size_t i = 0; i < ranges.size(); ++i)
	{
		const Hq::Lexer::HighlightRange& r = ranges[i];
		const size_t end = r.start + r.length;

		if (end <= base)
			continue;

		const Replxx::Color color = mapColor(r, full, isqlWords[i]);

		if (color == Replxx::Color::DEFAULT)
			continue;

		int from = cpMap[std::max<size_t>(r.start, base) - base];
		int to = cpMap[end - base];

		if (from < 0 || from >= cpCount)
			continue;

		if (to > cpCount)
			to = cpCount;

		for (int cp = from; cp < to; ++cp)
			colors[cp] = color;
	}
}

// True when the word at byte offset [start, end) is inside a string,
// a quoted identifier or a comment - no completion there.
bool insideNonCompletable(const std::vector<Hq::Lexer::HighlightRange>& ranges, size_t start)
{
	for (size_t i = 0; i < ranges.size(); ++i)
	{
		const Hq::Lexer::HighlightRange& r = ranges[i];

		if (r.start <= start && start < r.start + r.length)
		{
			return r.type == Hq::Lexer::Token::TYPE_STRING ||
				   r.type == Hq::Lexer::Token::TYPE_META_STRING ||
				   r.type == Hq::Lexer::Token::TYPE_COMMENT;
		}
	}

	return false;
}

//---- qualified completion (TABLE.COLUMN, PACKAGE.ENTRY, alias support) -----

struct SqlToken
{
	Hq::Lexer::Token::Type type;
	std::string text;	// token text as written
	std::string name;	// identifier as resolved: upper case, or unquoted "name"
	size_t start;		// byte offset in the input
	size_t end;			// byte offset just past the token
};

std::string upperAscii(std::string s)
{
	for (auto& c : s)
		c = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;

	return s;
}

// System tables are rarely typed by hand - keep them out of the
// table-reference completion.
bool isSystemTableName(const std::string& name)
{
	return name.compare(0, 4, "RDB$") == 0 || name.compare(0, 4, "MON$") == 0 ||
		   name.compare(0, 4, "SEC$") == 0 || name.compare(0, 4, "TMP$") == 0;
}

// A quoted identifier is open when its closing quote is not typed yet:
// "Abc or "Ab"" (odd number of quotes).
bool isOpenQuotedIdent(const std::string& s)
{
	return !s.empty() && s.front() == '"' && std::count(s.begin(), s.end(), '"') % 2 == 1;
}

// Strip double quotes and undo the "" escaping of a quoted identifier,
// also of an open one.
std::string unquoteIdent(const std::string& s)
{
	if (s.empty() || s.front() != '"')
		return s;

	const size_t innerLength = isOpenQuotedIdent(s) ? s.size() - 1 : s.size() - 2;
	const std::string inner = s.substr(1, innerLength);
	std::string out;

	for (size_t i = 0; i < inner.size(); ++i)
	{
		out += inner[i];

		if (inner[i] == '"' && i + 1 < inner.size() && inner[i + 1] == '"')
			++i;
	}

	return out;
}

// Code point length of a UTF-8 string.
int cpLength(const std::string& s)
{
	int cp = 0;

	for (size_t i = 0; i < s.size(); )
	{
		const unsigned char b = (unsigned char) s[i];
		i += b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
		++cp;
	}

	return cp;
}

std::vector<SqlToken> buildTokens(const std::string& input,
	const std::vector<Hq::Lexer::HighlightRange>& ranges)
{
	std::vector<SqlToken> toks;

	for (const auto& r : ranges)
	{
		SqlToken t;
		t.type = r.type;
		t.start = r.start;
		t.end = r.start + r.length;
		t.text = input.substr(r.start, r.length);

		switch (r.type)
		{
			case Hq::Lexer::Token::TYPE_WORD:
			case Hq::Lexer::Token::TYPE_META_STRING:
			case Hq::Lexer::Token::TYPE_OPERATOR:
			case Hq::Lexer::Token::TYPE_TERMINATOR:
				t.name = r.type == Hq::Lexer::Token::TYPE_WORD ? upperAscii(t.text) :
					r.type == Hq::Lexer::Token::TYPE_META_STRING ? unquoteIdent(t.text) : t.text;
				toks.push_back(t);
				break;

			default:
				break;	// strings, numbers, comments, parameters are skipped
		}
	}

	return toks;
}

// Words that may start a (sub)clause the table/alias list ends at.
bool isListStopWord(const std::string& up)
{
	static const char* const STOP[] =
	{
		"WHERE", "GROUP", "ORDER", "HAVING", "UNION", "SET", "RETURNING",
		"SELECT", "ON", "USING", "INNER", "LEFT", "RIGHT", "FULL", "CROSS",
		"OUTER", "NATURAL", "PLAN", "ROWS", "FIRST", "SKIP", "OFFSET",
		"FETCH", "NEXT", "LOCK", "WITH", "WINDOW", "VALUES", "INTO"
	};

	for (size_t i = 0; i < sizeof(STOP) / sizeof(STOP[0]); ++i)
	{
		if (up == STOP[i])
			return true;
	}

	return false;
}

bool isClauseKeyword(const std::string& up)
{
	return up == "FROM" || up == "JOIN" || up == "UPDATE";
}

// Collect alias -> table pairs from FROM/JOIN/UPDATE clauses:
// "FROM EMPLOYEE E JOIN DEPT D ON ..." / "UPDATE EMPLOYEE E" /
// "FROM A, B b". Later declarations win at lookup (last scope).
std::vector<std::pair<std::string, std::string>> scanAliases(const std::vector<SqlToken>& toks)
{
	std::vector<std::pair<std::string, std::string>> res;

	for (size_t i = 0; i < toks.size(); ++i)
	{
		if (toks[i].type != Hq::Lexer::Token::TYPE_WORD ||
			!isClauseKeyword(upperAscii(toks[i].text)))
		{
			continue;
		}

		size_t j = i + 1;

		for (;;)
		{
			if (j >= toks.size() || toks[j].type == Hq::Lexer::Token::TYPE_TERMINATOR)
				break;

			// Table reference: an identifier or a quoted identifier.
			if (toks[j].type != Hq::Lexer::Token::TYPE_WORD &&
				toks[j].type != Hq::Lexer::Token::TYPE_META_STRING)
			{
				break;	// e.g. a derived table "(SELECT ...)"
			}

			const std::string table = toks[j].name;
			++j;

			if (j < toks.size() && toks[j].type == Hq::Lexer::Token::TYPE_WORD &&
				toks[j].name == "AS")
			{
				++j;
			}

			if (j < toks.size() &&
				((toks[j].type == Hq::Lexer::Token::TYPE_WORD && !isListStopWord(toks[j].name)) ||
				 toks[j].type == Hq::Lexer::Token::TYPE_META_STRING))
			{
				res.push_back({toks[j].name, table});
				++j;
			}

			if (j < toks.size() && toks[j].type == Hq::Lexer::Token::TYPE_OPERATOR &&
				toks[j].text == ",")
			{
				++j;
				continue;
			}

			break;
		}
	}

	return res;
}

struct SuggestCtx
{
	enum Kind
	{
		NONE,		// nothing to complete (no word, not after a qualifier)
		FLAT,		// plain word - flat dictionary lookup
		TABLE_REF,	// word at a table-reference slot (after FROM/JOIN/UPDATE/comma)
		QUALIFIED,	// qualifier before the last dot: TABLE. / ALIAS. / PACKAGE.
		WORDS		// a word of an hqisql command (a format, an option, a PREPARE name)
	};

	Kind kind = NONE;
	std::vector<std::string> words;	// WORDS: the words offered, inserted as they are
	std::string qualifier;	// resolved name before the dot
	std::string typed;		// the word being completed as written ("" right after a dot)
	std::string prefix;		// the same word without the opening quote
	bool quoted = false;	// the word is an open quoted name: "Abc
	size_t checkByte = 0;	// byte offset for the insideNonCompletable check
	int contextCpLen = 0;	// code point length of the word being completed
};

// The name matches the word typed so far: case-insensitively for a plain
// word (it may complete to a quoted name), exactly in an open quoted name.
bool nameMatches(const std::string& name, const SuggestCtx& ctx)
{
	if (name.size() < ctx.prefix.size())
		return false;

	if (ctx.quoted)
		return name.compare(0, ctx.prefix.size(), ctx.prefix) == 0;

	return upperAscii(name.substr(0, ctx.prefix.size())) == upperAscii(ctx.prefix);
}

// The name as inserted: quoted when needed; a command word as it is.
std::string insertText(const std::string& name, const SuggestCtx& ctx)
{
	return ctx.kind == SuggestCtx::WORDS ? name : std::string(Hq::sqlName(name));
}

// The name as inserted by the completion; empty when it is what is typed.
std::string completionText(const std::string& name, const SuggestCtx& ctx)
{
	const std::string text(insertText(name, ctx));
	return text == ctx.typed ? std::string() : text;
}

// A ghost hint shows the rest of the inserted text: only when it continues
// what is typed.
bool hintable(const std::string& text, const SuggestCtx& ctx)
{
	return text.size() > ctx.typed.size() &&
		upperAscii(text.substr(0, ctx.typed.size())) == upperAscii(ctx.typed);
}

// True when the token at idx sits in a table-reference slot of a
// FROM/JOIN/UPDATE clause: right after the keyword, or after a comma that
// belongs to such a list (walking back over names, aliases, AS and commas).
bool atTableRefPosition(const std::vector<SqlToken>& toks, size_t idx)
{
	for (ptrdiff_t k = (ptrdiff_t) idx - 1; k >= 0; --k)
	{
		const SqlToken& t = toks[k];

		if (t.type == Hq::Lexer::Token::TYPE_WORD)
		{
			const std::string up = upperAscii(t.text);

			if (isClauseKeyword(up))
				return true;

			if (isListStopWord(up))
				return false;
		}
	}

	return false;
}

// The hqisql command slot of the word at idx (IsqlCommands::completionSlot),
// as a completion kind: IMPORT INTO | is a table reference, the others
// offer their own words.
void hqCommandSlot(const std::vector<SqlToken>& toks, size_t idx, SuggestCtx& ctx)
{
	size_t first = idx;

	while (first > 0 && toks[first - 1].type != Hq::Lexer::Token::TYPE_TERMINATOR)
		--first;

	std::vector<std::string> before;

	for (size_t i = first; i < idx; ++i)
		before.push_back(toks[i].type == Hq::Lexer::Token::TYPE_WORD ? toks[i].name : toks[i].text);

	const Hq::IsqlCommands::Slot slot = Hq::IsqlCommands::completionSlot(before);

	// In an open quoted name only a table name makes sense.
	if (ctx.quoted && slot != Hq::IsqlCommands::SLOT_TABLE)
		return;

	switch (slot)
	{
		case Hq::IsqlCommands::SLOT_TABLE:
			ctx.kind = SuggestCtx::TABLE_REF;
			break;

		case Hq::IsqlCommands::SLOT_CSV:
			ctx.kind = SuggestCtx::WORDS;
			ctx.words = Hq::Import::formatNames();
			break;

		case Hq::IsqlCommands::SLOT_OPTION:
			ctx.kind = SuggestCtx::WORDS;
			ctx.words = Hq::Import::optionNames();
			break;

		case Hq::IsqlCommands::SLOT_PREPARED:
			ctx.kind = SuggestCtx::WORDS;
			ctx.words = Hq::preparedNames();
			break;

		default:
			break;
	}
}

// What is under the cursor: a plain word, or a word right after
// "IDENT." / "IDENT"." (spaces around the dot allowed).
SuggestCtx analyzeSuggest(const std::string& input,
	const std::vector<Hq::Lexer::HighlightRange>& ranges)
{
	SuggestCtx ctx;
	const std::vector<SqlToken> toks = buildTokens(input, ranges);

	if (toks.empty())
		return ctx;

	const size_t n = toks.size();
	const SqlToken& last = toks[n - 1];

	// The word under the cursor must end exactly at the cursor - trailing
	// whitespace or comments mean there is nothing to complete (the old
	// isql-like behavior).
	if (last.end != input.size())
		return ctx;

	if (last.type == Hq::Lexer::Token::TYPE_WORD ||
		(last.type == Hq::Lexer::Token::TYPE_META_STRING && isOpenQuotedIdent(last.text)))
	{
		ctx.kind = atTableRefPosition(toks, n - 1) ? SuggestCtx::TABLE_REF : SuggestCtx::FLAT;
		ctx.quoted = last.type == Hq::Lexer::Token::TYPE_META_STRING;
		ctx.typed = last.text;
		ctx.prefix = ctx.quoted ? last.name : last.text;
		ctx.checkByte = last.start;
		ctx.contextCpLen = cpLength(last.text);

		hqCommandSlot(toks, n - 1, ctx);

		if (n >= 3 &&
			toks[n - 2].type == Hq::Lexer::Token::TYPE_OPERATOR && toks[n - 2].text == "." &&
			(toks[n - 3].type == Hq::Lexer::Token::TYPE_WORD ||
			 toks[n - 3].type == Hq::Lexer::Token::TYPE_META_STRING))
		{
			ctx.kind = SuggestCtx::QUALIFIED;
			ctx.qualifier = toks[n - 3].name;
		}

		return ctx;
	}

	if (last.type == Hq::Lexer::Token::TYPE_OPERATOR && last.text == "." && n >= 2 &&
		(toks[n - 2].type == Hq::Lexer::Token::TYPE_WORD ||
		 toks[n - 2].type == Hq::Lexer::Token::TYPE_META_STRING))
	{
		// Cursor right after the dot - offer the full member list.
		ctx.kind = SuggestCtx::QUALIFIED;
		ctx.qualifier = toks[n - 2].name;
		ctx.checkByte = last.start;
		ctx.contextCpLen = 0;
		return ctx;
	}

	return ctx;	// NONE
}

// The text of the edited buffer after the cursor. replxx passes the
// callbacks only the text before the cursor (input).
std::string textAfterCursor(const std::string& input)
{
	const std::string text(g_rx->get_state().text());

	if (text.size() <= input.size() || text.compare(0, input.size(), input) != 0)
		return std::string();

	return text.substr(input.size());
}

// Alias -> table pairs of the statement under the cursor. The text after
// the cursor counts too: an alias is often declared after the place it is
// used ("SELECT E.| FROM EMPLOYEE E"). Other statements of the same buffer
// (up to the last terminator before the cursor, from the first one after
// it) do not count.
std::vector<std::pair<std::string, std::string>> statementAliases(const std::string& before,
	const std::string& after)
{
	const std::string statement(before + after);
	const std::string term(isqlGlob.global_Term);
	const std::vector<SqlToken> toks =
		buildTokens(statement, Hq::Lexer::highlight(statement, term, NULL));
	size_t first = 0;
	size_t last = toks.size();

	for (size_t i = 0; i < toks.size(); ++i)
	{
		if (toks[i].type != Hq::Lexer::Token::TYPE_TERMINATOR)
			continue;

		if (toks[i].end <= before.size())
			first = i + 1;
		else
		{
			last = i;
			break;
		}
	}

	return scanAliases(std::vector<SqlToken>(toks.begin() + first, toks.begin() + last));
}

// Columns of the table (directly or through an alias) or entries of the
// package the qualifier resolves to; empty when unresolvable.
std::vector<std::string> resolveQualifier(const SuggestCtx& ctx,
	const std::vector<std::pair<std::string, std::string>>& aliases)
{
	for (const auto& a : aliases)
	{
		if (a.first == ctx.qualifier)
		{
			// An alias hides the object of the same name in its scope.
			const Hq::TableInfo* t = Hq::findTable(a.second);
			return t ? t->columns : std::vector<std::string>();
		}
	}

	if (const Hq::TableInfo* t = Hq::findTable(ctx.qualifier))
		return t->columns;

	if (const Hq::PackageInfo* p = Hq::findPackage(ctx.qualifier))
		return p->entries;

	return std::vector<std::string>();
}

// Calls f(name) for the names offered at the word under the cursor until it
// returns false: relation names in a table reference slot (system tables
// filtered), the members of the qualifier, or all metadata names. full is
// the statement up to the cursor, after is the rest of the edited buffer.
template <typename F>
void forEachName(const SuggestCtx& ctx, const std::string& full, const std::string& after, F f)
{
	if (ctx.kind == SuggestCtx::WORDS)
	{
		for (const std::string& w : ctx.words)
		{
			if (!f(w))
				return;
		}
	}
	else if (ctx.kind == SuggestCtx::TABLE_REF)
	{
		for (const Hq::TableInfo& t : Hq::tables())
		{
			if (!isSystemTableName(t.name) && !f(t.name))
				return;
		}
	}
	else if (ctx.kind == SuggestCtx::QUALIFIED)
	{
		for (const std::string& m : resolveQualifier(ctx, statementAliases(full, after)))
		{
			if (!f(m))
				return;
		}
	}
	else
	{
		for (const std::string& w : Hq::metadataWords())
		{
			if (!f(w))
				return;
		}
	}
}

Replxx::completions_t completionCallback(const std::string& input, int& contextLen)
{
	Replxx::completions_t res;
	Hq::syncMetadata();

	// Analyze the whole statement: aliases and qualifiers may be declared on
	// the previous lines of a multi-line input (the CON> continuation lines)
	// or after the cursor.
	std::string full(g_context);
	full += input;
	const std::string term(isqlGlob.global_Term);
	const std::vector<Hq::Lexer::HighlightRange> ranges =
		Hq::Lexer::highlight(full, term, NULL);
	const SuggestCtx ctx = analyzeSuggest(full, ranges);

	if (ctx.kind == SuggestCtx::NONE || (!ctx.quoted && insideNonCompletable(ranges, ctx.checkByte)))
		return res;

	// The length (in code points) of the word being completed - exactly what
	// gets replaced by the completion.
	contextLen = ctx.contextCpLen;

	// Keywords for a plain word; after a dot or at a table reference slot
	// only names are meaningful.
	if (ctx.kind == SuggestCtx::FLAT && !ctx.quoted)
	{
		const std::string prefix(upperAscii(ctx.prefix));

		for (unsigned i = 0; i < Hq::keywordCount() && res.size() < 100; ++i)
		{
			const char* kw = Hq::keyword(i);

			if (strncmp(kw, prefix.c_str(), prefix.size()) == 0 && strcmp(kw, prefix.c_str()) != 0)
				res.push_back({kw, colorOf(Hq::ColorScheme::COMPLETION)});
		}
	}

	forEachName(ctx, full, textAfterCursor(input), [&](const std::string& name)
	{
		if (nameMatches(name, ctx))
		{
			const std::string text(completionText(name, ctx));

			if (!text.empty())
				res.push_back({text, Replxx::Color::DEFAULT});
		}

		return res.size() < 150;
	});

	return res;
}

Replxx::hints_t hintCallback(const std::string& input, int& contextLen, Replxx::Color& color)
{
	Replxx::hints_t res;
	color = colorOf(Hq::ColorScheme::HINT);
	Hq::syncMetadata();

	// See completionCallback: analyze the whole statement, not just the line.
	std::string full(g_context);
	full += input;
	const std::string term(isqlGlob.global_Term);
	const std::vector<Hq::Lexer::HighlightRange> ranges =
		Hq::Lexer::highlight(full, term, NULL);
	const SuggestCtx ctx = analyzeSuggest(full, ranges);

	if (ctx.kind == SuggestCtx::NONE || (!ctx.quoted && insideNonCompletable(ranges, ctx.checkByte)))
		return res;

	contextLen = ctx.contextCpLen;

	// A ghost hint only when the word is already started; a plain word
	// needs three characters, otherwise the hint is noise.
	if (ctx.typed.empty() || (ctx.kind == SuggestCtx::FLAT && ctx.prefix.size() < 3))
		return res;

	// Today replxx asks for hints only with the cursor at the end of the
	// buffer (the text after it is empty); do not depend on that.
	forEachName(ctx, full, textAfterCursor(input), [&](const std::string& name)
	{
		if (nameMatches(name, ctx))
		{
			const std::string text(insertText(name, ctx));

			if (hintable(text, ctx))
			{
				res.push_back(text);
				return false;	// a single ghost-text suggestion is enough
			}
		}

		return true;
	});

	// Fall back to a keyword when exactly one keyword matches the prefix -
	// ambiguous or absent suggestions would make the ghost text noise.
	if (res.empty() && ctx.kind == SuggestCtx::FLAT && !ctx.quoted)
	{
		const std::string prefix(upperAscii(ctx.prefix));
		const char* match = NULL;
		int matches = 0;

		for (unsigned i = 0; i < Hq::keywordCount() && matches < 2; ++i)
		{
			const char* kw = Hq::keyword(i);

			if (strncmp(kw, prefix.c_str(), prefix.size()) == 0)
			{
				++matches;
				match = kw;
			}
		}

		if (matches == 1 && strcmp(match, prefix.c_str()) != 0)
			res.push_back(match);
	}

	return res;
}

char* dupLine(const std::string& s)
{
	char* p = (char*) malloc(s.size() + 1);

	if (p)
		memcpy(p, s.c_str(), s.size() + 1);

	return p;
}

bool editorActive()
{
	return g_rx != NULL && g_consoleOk && !g_noEdit;
}

const std::string& savedHistoryPath()
{
	static const std::string path(historyPath());
	return path;
}

// Significant (not comment) tokens of a statement.
std::vector<Hq::Lexer::HighlightRange> statementTokens(const std::string& statement)
{
	std::vector<Hq::Lexer::HighlightRange> tokens =
		Hq::Lexer::highlight(statement, isqlGlob.global_Term);

	tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
		[](const Hq::Lexer::HighlightRange& r) { return r.type == Hq::Lexer::Token::TYPE_COMMENT; }),
		tokens.end());

	return tokens;
}

// CONNECT ... PASSWORD, CREATE DATABASE ... PASSWORD, CREATE / ALTER USER ...
// PASSWORD: such statements do not go into the history file.
bool hasPassword(const std::string& statement)
{
	for (const auto& r : statementTokens(statement))
	{
		if (r.type == Hq::Lexer::Token::TYPE_WORD && upperWord(statement, r) == "PASSWORD")
			return true;
	}

	return false;
}

// Statements after which the completion data may be out of date. COMMIT is
// here for DDL run with SET AUTODDL OFF.
bool changesMetadata(const std::string& statement)
{
	const std::vector<Hq::Lexer::HighlightRange> tokens = statementTokens(statement);

	if (tokens.empty() || tokens[0].type != Hq::Lexer::Token::TYPE_WORD)
		return false;

	const std::string first(upperWord(statement, tokens[0]));

	return first == "CREATE" || first == "ALTER" || first == "DROP" || first == "RECREATE" ||
		first == "DECLARE" || first == "COMMIT";
}

bool hasSignificantText(const std::string& s)
{
	for (const char c : s)
	{
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\0')
			return true;
	}

	return false;
}

// Text that ends the statement being assembled in any state its previous
// lines left it: it closes an unterminated string, quoted name or block
// comment and adds the terminator.
std::string closingText(const std::string& statement)
{
	const std::string term(isqlGlob.global_Term);
	bool incomplete = false;
	const std::vector<Hq::Lexer::HighlightRange> ranges =
		Hq::Lexer::highlight(statement, term, &incomplete);
	std::string text;

	if (incomplete && !ranges.empty())
	{
		const Hq::Lexer::HighlightRange& last = ranges.back();
		const char* p = statement.c_str() + last.start;

		switch (last.type)
		{
			case Hq::Lexer::Token::TYPE_STRING:
				if ((p[0] == 'q' || p[0] == 'Q') && p[1] == '\'' && p[2])
				{
					switch (p[2])
					{
						case '{': text += '}'; break;
						case '(': text += ')'; break;
						case '[': text += ']'; break;
						case '<': text += '>'; break;
						default: text += p[2];
					}
				}

				text += '\'';
				break;

			case Hq::Lexer::Token::TYPE_META_STRING:
				text += '"';
				break;

			case Hq::Lexer::Token::TYPE_COMMENT:
				text += "*/";
				break;

			default:
				break;
		}
	}

	return text + term;
}

// The completion data follows the current attachment: (re)load it in the
// background before the prompt when isql has connected, reconnected or
// disconnected, or after a statement that may change the metadata.
void refreshMetadataIfNeeded()
{
	static Firebird::IAttachment* loadedDb = NULL;
	static std::string loadedName;

	const std::string name(DB ? isqlGlob.global_Db_name : "");

	if (DB == loadedDb && name == loadedName && !g_metadataStale)
		return;

	loadedDb = DB;
	loadedName = name;
	g_metadataStale = false;
	Hq::refreshMetadata(DB);
}

// Gives the next pending line to isql. A pasted line is printed after the
// prompt, as the console echo shows it in isql.
char* takePendingLine(const char* prompt)
{
	const PendingLine line = g_pendingLines.front();
	g_pendingLines.pop_front();

	if (line.echo)
	{
		fputs(prompt ? prompt : "", stdout);
		fputs(line.text.c_str(), stdout);
		fputc('\n', stdout);
		fflush(stdout);
	}

	return dupLine(line.text);
}

// Returns NULL on EOF (Ctrl-D, Ctrl-Z) and on Ctrl-C, *interrupted tells
// them apart.
char* editorReadLine(const char* prompt, bool* interrupted)
{
	*interrupted = false;

	const char* input = g_rx->input(prompt ? prompt : "");

	if (!input)
	{
		*interrupted = (errno == EAGAIN);
		return NULL;
	}

	// Split multi-line input (pasted text) into lines for the regular
	// character-oriented statement assembly in isql.epp.
	std::vector<std::string> lines;
	splitLines(toConsole(input), lines);

	while (!lines.empty() && lines.back().empty())
		lines.pop_back();

	if (lines.empty())
		lines.push_back("");

	for (size_t i = 1; i < lines.size(); ++i)
		g_pendingLines.push_back({lines[i], false});

#ifdef WIN_NT
	std::vector<std::string> pasted;
	splitLines(readPastedLines(), pasted);

	for (const std::string& line : pasted)
		g_pendingLines.push_back({line, true});
#endif

	return dupLine(lines[0]);
}

}	// anonymous namespace


namespace Hq {

void init(int& argc, char** argv)
{
	// Remove the hqisql-only switches before the regular parser sees them.
	g_noEdit = envNoEdit();
	int out = 1;

	for (int i = 1; i < argc; ++i)
	{
#ifdef WIN_NT
		if (argv[i] && _stricmp(argv[i], "-noedit") == 0)
#else
		if (argv[i] && strcasecmp(argv[i], "-noedit") == 0)
#endif
		{
			g_noEdit = true;
			continue;
		}

		argv[out++] = argv[i];
	}

	argc = out;

	g_consoleOk = stdinTty();

	// Without the editor hqisql reads and writes exactly like isql: no
	// console mode, code page or history file changes.
	if (!g_consoleOk || g_noEdit)
		return;

	// The console code pages and modes stay as they are: replxx switches
	// them to UTF-8 and VT processing only while a line is edited and
	// restores them before isql continues, so the data is read and printed
	// in the console code page like in isql.
	g_rx = new replxx::Replxx(std::cin, std::cout, fileno(stdin), fileno(stdout), fileno(stderr));
	g_rx->set_highlighter_callback(highlightCallback);
	g_rx->set_completion_callback(completionCallback);
	g_rx->set_hint_callback(hintCallback);
	// No hint delay: with a positive delay the delayed repaint competes with
	// the refresh throttling in this replxx fork and hints become unreliable.
	g_rx->set_hint_delay(0);
	g_rx->set_double_tab_completion(false);
	g_rx->set_complete_on_empty(false);
	g_rx->set_no_color(noColor());

	if (!noColor())
		loadColorScheme();

	g_rx->set_max_history_size(5000);
	g_rx->history_sync(savedHistoryPath());

#ifndef WIN_NT
	// A pasted script comes into the editor as one input: one refresh, not
	// one per line. Enter runs it. A Windows console has no bracketed paste,
	// see readPastedLines().
	g_rx->enable_bracketed_paste();
#endif
}

void shutdown()
{
	Hq::waitMetadata();

	if (g_rx)
	{
		g_rx->history_save(savedHistoryPath());
		delete g_rx;
		g_rx = NULL;
	}
}

bool readLine(const char* prompt, bool interactive, char** line)
{
	if (!interactive || !editorActive())
		return false;

	if (!g_pendingLines.empty())
	{
		*line = takePendingLine(prompt);

		if (g_scope)
			g_scope->fromEditor = true;

		return true;
	}

	if (g_historyDirty)
	{
		g_rx->history_save(savedHistoryPath());
		g_historyDirty = false;
	}

	refreshMetadataIfNeeded();

	// Aliases and qualifiers may be declared on the previous lines of the
	// statement being assembled - they are the context of the callbacks.
	if (g_scope)
		g_context = fromConsole(g_scope->statement.c_str());
	else
		g_context.clear();

	for (;;)
	{
		bool interrupted;
		*line = editorReadLine(prompt, &interrupted);

		if (!interrupted)
			break;

		// Ctrl-C drops the line being edited and shows the prompt again.
		// A statement started on the previous lines is dropped too: isql
		// gets the text that ends it, complete() skips it.
		if (g_scope && hasSignificantText(g_scope->statement.c_str()))
		{
			g_scope->cancelled = true;
			*line = dupLine(closingText(g_scope->statement.c_str()));
			break;
		}
	}

	// EOF: isql disconnects and exits without complete().
	if (!*line)
		Hq::waitMetadata();

	if (g_scope)
		g_scope->fromEditor = true;

	return true;
}


StatementScope::StatementScope(Firebird::string& aStatement)
	: statement(aStatement),
	  outer(g_scope),
	  fromEditor(false),
	  cancelled(false)
{
	g_scope = this;
}

StatementScope::~StatementScope()
{
	g_scope = outer;
}

processing_state StatementScope::complete(processing_state ret, ExecuteFunc execute)
{
	// isql uses the attachment again: the background metadata load must
	// end first. The next prompt reloads it after DDL.
	Hq::waitMetadata();

	if (cancelled)
		return SKIP;

	if (editorActive() && ret == CONT && changesMetadata(statement.c_str()))
		g_metadataStale = true;

	// Put the statement typed in the editor into the editor history with
	// the terminator it was typed with, so it can be re-executed. SET TERM
	// is not executed yet, its old terminator is still current. The file
	// is saved at once: a killed session keeps its history. In a paste it
	// is saved before the next prompt, not after each statement.
	if (fromEditor && editorActive() && statement.hasData() &&
		strcmp(statement.c_str(), "?") != 0)
	{
		std::string historyStatement(fromConsole(statement.c_str()));

		if (!hasPassword(historyStatement))
		{
			historyStatement += isqlGlob.global_Term;
			g_rx->history_add(historyStatement);

			if (g_pendingLines.empty())
			{
				g_rx->history_save(savedHistoryPath());
				g_historyDirty = false;
			}
			else
				g_historyDirty = true;
		}
	}

	// PREPARE / EXEC SQL ... USING / DEALLOCATE. EXEC SQL rewrites the
	// statement with the supplied values and falls through to the regular
	// execution path.
	if (ret == CONT)
		ret = statementCommand(statement, execute);

	return ret;
}

}	// namespace Hq
