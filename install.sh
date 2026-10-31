#!/bin/sh
#
# install.sh [--check | --remove | --no-hook] <firebird-tree>
#
# Installs hqisql into the Firebird source tree <firebird-tree>:
#
#   1. copies the files of overlay/ (src/hqisql, extern/replxx, the build
#      files of hqisql);
#   2. applies patches/<major>.<minor>/isql.epp.patch (the hqisql code of
#      isql, all under #ifdef HQISQL) and build-hook.patch (the regular
#      Firebird build also builds and tests hqisql);
#   3. writes the list of the installed files to src/hqisql/.install.
#
#   --check    only check that the patches apply; change nothing
#   --remove   remove hqisql: reverse the patches, delete the files
#   --no-hook  do not apply build-hook.patch: build hqisql with a separate
#              command (builds/posix/Makefile.hqisql, make_hqisql.bat)
#
# Needs git (for "git apply"); the Firebird tree does not have to be a git
# work tree. The patches have LF line ends; files with CRLF line ends keep
# them.
#

set -eu

die() { echo "install.sh: $*" >&2; exit 1; }
say() { echo "install.sh: $*"; }

HERE=$(cd "$(dirname "$0")" && pwd)
VERSION=$(tr -d '\r\n' < "$HERE/VERSION")

MODE=install
HOOK=yes
FB=
for a in "$@"; do
	case $a in
		--check) MODE=check ;;
		--remove) MODE=remove ;;
		--no-hook) HOOK=no ;;
		-h|--help) sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) die "unknown option $a" ;;
		*) [ -z "$FB" ] || die "more than one Firebird tree given"; FB=$a ;;
	esac
done
[ -n "$FB" ] || die "usage: install.sh [--check | --remove | --no-hook] <firebird-tree>"

# Windows paths (C:\src\firebird) from install.bat.
if command -v cygpath > /dev/null 2>&1; then
	FB=$(cygpath -u "$FB")
fi
[ -f "$FB/src/jrd/build_no.h" ] || die "$FB: not a Firebird source tree (no src/jrd/build_no.h)"
FB=$(cd "$FB" && pwd)
command -v git > /dev/null 2>&1 || die "git is not found: it applies the patches"

MARK=$FB/src/hqisql/.install

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Files changed by a patch.
patch_files() {
	sed -n 's|^+++ b/||p' "$1" | tr -d '\r'
}

# run_patch <patch> <git apply options>: applies the patch to LF copies of
# its files in a scratch directory, outside any git work tree. Returns the
# exit code of git apply; with --check the files do not change.
run_patch() {
	p=$1; shift
	w=$TMP/w
	rm -rf "$w"; mkdir -p "$w"
	for f in $(patch_files "$p"); do
		mkdir -p "$w/$(dirname "$f")"
		[ -f "$FB/$f" ] && tr -d '\r' < "$FB/$f" > "$w/$f"
	done
	tr -d '\r' < "$p" > "$TMP/p"
	(cd "$w" && GIT_DIR=/nonexistent git -c core.autocrlf=false -c core.eol=lf apply "$@" "$TMP/p") 2> "$TMP/err"
}

# copy_back <patch>: copies the patched files into the tree, with CRLF line
# ends where the file had them.
copy_back() {
	for f in $(patch_files "$1"); do
		# Not grep: the MSYS grep does not see CR.
		if ! tr -d '\r' < "$FB/$f" | cmp -s - "$FB/$f"; then
			awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' "$TMP/w/$f" > "$FB/$f"
		else
			cat "$TMP/w/$f" > "$FB/$f"
		fi
	done
}

# patch_state <patch>: prints "applied", "clean" or "conflict".
patch_state() {
	if run_patch "$1" --check -R; then
		echo applied
	elif run_patch "$1" --check; then
		echo clean
	else
		echo conflict
	fi
}

apply_patch() {
	run_patch "$1" || { cat "$TMP/err" >&2; die "$(basename "$1") does not apply"; }
	copy_back "$1"
}

reverse_patch() {
	run_patch "$1" -R || { cat "$TMP/err" >&2; die "$(basename "$1") does not reverse"; }
	copy_back "$1"
}

remove_installed() {
	[ -d "$MARK" ] || die "hqisql is not installed in $FB (no src/hqisql/.install)"
	for p in "$MARK"/patches/*.patch; do
		[ -f "$p" ] || continue
		case $(patch_state "$p") in
			applied) reverse_patch "$p"; say "reversed $(basename "$p")" ;;
			clean) say "$(basename "$p") is not applied, skipped" ;;
			*) cat "$TMP/err" >&2; die "$(basename "$p"): the patched files were changed, reverse it by hand" ;;
		esac
	done
	cp "$MARK/files" "$TMP/files"
	rm -rf "$MARK"
	while IFS= read -r f; do
		rm -f "$FB/$f"
		d=$(dirname "$f")
		while [ "$d" != . ] && rmdir "$FB/$d" 2> /dev/null; do
			d=$(dirname "$d")
		done
	done < "$TMP/files"
	say "removed hqisql $(cat "$TMP/version" 2> /dev/null || true)from $FB"
}

if [ "$MODE" = remove ]; then
	[ -f "$MARK/version" ] && { tr -d '\r\n' < "$MARK/version"; echo ' '; } > "$TMP/version"
	remove_installed
	exit 0
fi

MAJ=$(tr -d '\r' < "$FB/src/jrd/build_no.h" | sed -n 's/^#define FB_MAJOR_VER "\(.*\)"/\1/p')
MIN=$(tr -d '\r' < "$FB/src/jrd/build_no.h" | sed -n 's/^#define FB_MINOR_VER "\(.*\)"/\1/p')
PATCHES=$HERE/patches/$MAJ.$MIN
[ -d "$PATCHES" ] ||
	die "Firebird $MAJ.$MIN is not supported; supported versions: $(ls "$HERE/patches" | tr '\n' ' ')"

LIST="isql.epp.patch"
[ "$HOOK" = yes ] && LIST="$LIST build-hook.patch"

# The checksum of the files and patches to install: the same version with
# other contents (a development build) is installed again.
SUM=$( (cd "$HERE/overlay" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 cat; cd "$PATCHES" && cat $LIST) | cksum | cut -d' ' -f1)

# An installed hqisql: the same version and contents - nothing to do,
# otherwise remove it first.
if [ -d "$MARK" ]; then
	OLD=$(tr -d '\r\n' < "$MARK/version" 2> /dev/null || true)
	OLDSUM=$(tr -d '\r\n' < "$MARK/checksum" 2> /dev/null || true)
	if [ "$OLD" = "$VERSION" ] && [ "$OLDSUM" = "$SUM" ]; then
		say "hqisql $VERSION is already installed in $FB"
		exit 0
	fi
	if [ "$MODE" = check ]; then
		say "hqisql $OLD is installed in $FB: install would remove it first"
		exit 0
	fi
	say "removing hqisql $OLD"
	remove_installed
fi

# Check all patches before changing anything.
TODO=
for p in $LIST; do
	case $(patch_state "$PATCHES/$p") in
		applied) say "$p: already applied" ;;
		clean) say "$p: applies"; TODO="$TODO $p" ;;
		*) cat "$TMP/err" >&2; die "$p does not apply to $FB; the tree is not changed" ;;
	esac
done

if [ "$MODE" = check ]; then
	say "check passed: hqisql $VERSION can be installed into Firebird $MAJ.$MIN"
	exit 0
fi

(cd "$HERE/overlay" && find . -type f | sed 's|^\./||' | sort) > "$TMP/files"
(cd "$HERE/overlay" && tar -cf - .) | (cd "$FB" && tar -xf -)

for p in $TODO; do
	apply_patch "$PATCHES/$p"
done

mkdir -p "$MARK/patches"
for p in $LIST; do
	cp "$PATCHES/$p" "$MARK/patches/"
done
echo "$VERSION" > "$MARK/version"
echo "$SUM" > "$MARK/checksum"
cp "$TMP/files" "$MARK/files"

say "installed hqisql $VERSION into $FB ($(wc -l < "$TMP/files" | tr -d ' ') files, patches:$(echo " $LIST"))"
if [ "$HOOK" = yes ]; then
	say "build Firebird as usual: hqisql is built and tested with it"
else
	say "build hqisql after Firebird: make -C gen -f ../builds/posix/Makefile.hqisql, or builds\win32\make_hqisql.bat"
fi
