#!/bin/sh
#
# tools/export.sh <firebird-tree> <base> [<commit>]
#
# Fills overlay/ and patches/<major>.<minor>/ of this repository from a
# Firebird git tree that has hqisql in it (for example HQbird):
#
#   <firebird-tree>  the git work tree
#   <base>           the commit before hqisql: the patches are the diff
#                    from it
#   <commit>         the commit to export, HEAD by default
#
# overlay/ gets the hqisql files as they are stored in git (LF line ends),
# patches/ gets isql.epp.patch (the changes of src/isql/isql.epp) and
# build-hook.patch (Makefile.in and make_all.bat). Check the result with
# "install.sh --check" on a clean Firebird tree of every supported version.
#

set -eu

die() { echo "export.sh: $*" >&2; exit 1; }

[ $# -ge 2 ] || die "usage: export.sh <firebird-tree> <base> [<commit>]"

REPO=$(cd "$(dirname "$0")/.." && pwd)
TREE=$1
BASE=$2
REV=${3:-HEAD}

git() { command git -C "$TREE" -c core.autocrlf=false -c core.eol=lf "$@"; }

git rev-parse --verify -q "$REV^{commit}" > /dev/null || die "$REV: no such commit in $TREE"
git rev-parse --verify -q "$BASE^{commit}" > /dev/null || die "$BASE: no such commit in $TREE"

VER=$(git show "$REV:src/jrd/build_no.h" | tr -d '\r' |
	sed -n 's/^#define FB_MAJOR_VER "\(.*\)"/\1/p;s/^#define FB_MINOR_VER "\(.*\)"/.\1/p' | tr -d '\n')
[ -n "$VER" ] || die "no FB_MAJOR_VER in src/jrd/build_no.h"

# The files of hqisql in the Firebird tree.
FILES="src/hqisql extern/replxx
	builds/posix/Makefile.hqisql builds/posix/make.hqisql
	builds/win32/make_hqisql.bat
	builds/win32/msvc15/HQisql.sln builds/win32/msvc15/hqisql.vcxproj
	builds/win32/msvc15/hqisql_test.vcxproj builds/win32/msvc15/replxx_static.vcxproj"

# The Firebird files that hqisql changes.
ISQL_PATCH="src/isql/isql.epp"
HOOK_PATCH="builds/posix/Makefile.in builds/win32/make_all.bat"

rm -rf "$REPO/overlay"
mkdir -p "$REPO/overlay" "$REPO/patches/$VER"
# shellcheck disable=SC2086
git archive "$REV" -- $FILES | tar -x -C "$REPO/overlay"
rm -rf "$REPO/overlay/src/hqisql/.install"

# shellcheck disable=SC2086
git diff --no-color --no-ext-diff "$BASE" "$REV" -- $ISQL_PATCH > "$REPO/patches/$VER/isql.epp.patch"
# shellcheck disable=SC2086
git diff --no-color --no-ext-diff "$BASE" "$REV" -- $HOOK_PATCH > "$REPO/patches/$VER/build-hook.patch"

for p in isql.epp.patch build-hook.patch; do
	[ -s "$REPO/patches/$VER/$p" ] || die "patches/$VER/$p is empty: no changes from $BASE"
done

echo "Exported $(cd "$REPO/overlay" && find . -type f | wc -l) files to overlay/ and patches/$VER/ from $(git rev-parse --short "$REV")"
