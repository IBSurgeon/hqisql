#ifndef REPLXX_HQ_WIN_COMPAT_HXX_INCLUDED
#define REPLXX_HQ_WIN_COMPAT_HXX_INCLUDED 1

/*
 * Windows portability shims for building replxx with MSVC as a plain static
 * library (hqisql fork of ClickHouse/replxx, 2026-09).
 * The ClickHouse fork is regularly built on POSIX only; the few POSIX-only
 * bits used by platform independent code are provided here.
 */

#include <io.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef fsync
#define fsync(fd) _commit(fd)
#endif

#ifdef dprintf
#undef dprintf
#endif

inline int hq_replxx_dprintf(int fd_, char const* format_, ...) {
	va_list args;
	va_start(args, format_);
	int len = _vscprintf(format_, args);
	if (len < 0) {
		va_end(args);
		return -1;
	}
	std::string buf(static_cast<size_t>(len) + 1, '\0');
	int n = vsnprintf(&buf[0], static_cast<size_t>(len) + 1, format_, args);
	va_end(args);
	if (n < 0) {
		return -1;
	}
	if (n > len) {
		n = len;
	}
	return _write(fd_, buf.data(), n);
}

#define dprintf hq_replxx_dprintf

#endif /* REPLXX_HQ_WIN_COMPAT_HXX_INCLUDED */
