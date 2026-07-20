#!/bin/sh
#
# Assemble the distribution: the archive we actually hand people.
#
#   altairsim                the program
#   altairsim-manual.pdf     the manual -- and NOTHING in it names a file that is not here
#   USING-ALTAIRSIM.md       the same machines, written for an AI assistant driving them over MCP
#   LICENSE                  ours (MIT)
#   LICENSE-SDL3             SDL3's, because SDL3 is linked STATICALLY INTO the program
#   examples/cpm/            \
#   examples/basic/           \  the examples, each a self-contained folder: a machine file
#   examples/sol/             /  and the media it mounts, lying beside it
#   examples/diskbasic/      /
#
# ONE ARCHIVE, FOR ONE PLATFORM, BUILT ON THAT PLATFORM. --target names it and picks the
# format; it does not cross-compile, because nothing here does. See DISTRIBUTION.md 1 and 4.2.
#
# THE CONTENTS COME FROM docs/package.map, and from nowhere else. That file is the single
# source of truth for three things that would otherwise drift: the tokens the manual writes
# ({{MACHINE_CPM}} and friends), the directories this script copies, and what the docs tests
# check. Add an example by adding a DIR line there -- not by editing this script.
#
# MOST DISK IMAGES ARE NOT IN GIT (*.dsk / *.DSK are gitignored: they are large and they are
# not ours to redistribute) -- but EVERY image this package ships is one of the tracked few
# that .gitignore names one at a time, so a fresh clone builds a zip that actually boots and
# nothing here has to be fetched first. That is what lets this script be strict: media missing
# from an example directory is a file that should be in your tree and is not, so it REFUSES TO
# PACKAGE rather than quietly shipping a folder with a machine file and no machine in it --
# which would boot to a dead prompt and look like our bug.
#
#   usage: tools/build-package.sh [--target <target>] [--pdf <file>] [outdir]
#
#     --target   macos-arm64 | macos-x86_64 | linux-x86_64 | windows-x86_64
#                Names the archive and picks its format (.tar.gz, or .zip for Windows).
#                DEFAULTS TO THIS HOST, which is right on all four machines: each one
#                builds NATIVELY (DISTRIBUTION.md 4.3), so this is a naming and format
#                choice and never cross-compilation.
#     --pdf      Use THIS manual instead of rebuilding one. See below -- it is the flag
#                that keeps four machines shipping the SAME document.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
map=$root/docs/package.map

target=""
pdf=""
out=""

usage() {
  cat <<'USAGE'
usage: tools/build-package.sh [--target <target>] [--pdf <file>] [outdir]

  --target   macos-arm64 | macos-x86_64 | linux-x86_64 | windows-x86_64
             Names the archive and picks its format (.tar.gz, or .zip for Windows).
             Defaults to this host.
  --pdf      Use THIS manual instead of rebuilding one. Pass the manual the
             coordinator built; see DISTRIBUTION.md 4.2 step 6.
  outdir     Where to stage and write the archive. Default: dist/
USAGE
  exit "${1:-0}"
}

while [ $# -gt 0 ]; do
  case $1 in
    --target) [ $# -ge 2 ] || { echo "build-package: --target needs a value" >&2; exit 1; }
              target=$2; shift 2 ;;
    --pdf)    [ $# -ge 2 ] || { echo "build-package: --pdf needs a value" >&2; exit 1; }
              # Check it HERE, not where it is used: by then the staging directory has been
              # wiped and rebuilt, so a mistyped path costs a rebuild to discover.
              [ -f "$2" ] || { echo "build-package: --pdf $2 does not exist" >&2; exit 1; }
              pdf=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
              shift 2 ;;
    -h|--help) usage 0 ;;
    -*)       echo "build-package: unknown option $1" >&2; usage 1 ;;
    *)        [ -z "$out" ] || { echo "build-package: only one outdir, got '$out' and '$1'" >&2; exit 1; }
              out=$1; shift ;;
  esac
done

out=${out:-$root/dist}

# WHICH TARGET, AND SO WHICH ARCHIVE FORMAT. A typo here would otherwise produce a
# correctly-built package under a name nobody is looking for, uploaded to a release where
# nothing checks it -- so an unknown target is fatal, not a warning.
if [ -z "$target" ]; then
  case "$(uname -s)" in
    Darwin)  case "$(uname -m)" in
               arm64)  target=macos-arm64 ;;
               x86_64) target=macos-x86_64 ;;
             esac ;;
    Linux)   [ "$(uname -m)" = "x86_64" ] && target=linux-x86_64 ;;
    MINGW*|MSYS*|CYGWIN*) target=windows-x86_64 ;;
  esac
  [ -n "$target" ] || {
    echo "build-package: cannot tell what this host is ($(uname -s)/$(uname -m))." >&2
    echo "Pass one explicitly: --target macos-arm64|macos-x86_64|linux-x86_64|windows-x86_64" >&2
    exit 1
  }
fi

case $target in
  macos-arm64|macos-x86_64|linux-x86_64) ext=tar.gz ;;
  windows-x86_64)                        ext=zip ;;
  *) echo "build-package: unknown target '$target'" >&2
     echo "It is one of: macos-arm64 macos-x86_64 linux-x86_64 windows-x86_64" >&2
     exit 1 ;;
esac

# WHERE THE BINARY IS. MSVC's generator is multi-config and puts it under Release/, so the
# plain build/altairsim that every Unix machine has is not universal. Probe rather than
# assume -- and do NOT test -x on the Windows paths, where the execute bit means nothing.
sim=""
for cand in "$root/build/altairsim" "$root/build/Release/altairsim.exe" "$root/build/altairsim.exe"; do
  case $cand in
    *.exe) [ -f "$cand" ] && { sim=$cand; break; } ;;
    *)     [ -x "$cand" ] && { sim=$cand; break; } ;;
  esac
done
[ -n "$sim" ] || {
  echo "build-package: no built binary under $root/build. cmake --build build --config Release" >&2
  exit 1
}

# STRIP CR, OR WINDOWS PUTS ONE IN EVERY FILENAME BELOW. MSVC opens stdout in TEXT MODE, so
# the .exe emits "AltairSim 0.2.0\r\n" -- and awk's default field separator is [ \t\n], which
# does NOT include \r. So $2 would be "0.2.0<CR>", and that CR would land in the staging
# directory name, the archive name, and the version echoed in the closing message. A no-op
# everywhere else: there are no carriage returns in this output on Unix.
ver=$("$sim" --version | tr -d '\r' | awk '{print $2}')

# CLEAR STALE SIBLINGS -- BEFORE THE REFUSALS BELOW, NOT AFTER.
#
# A leftover dist/altairsim-<ver>*/ from an earlier run holds whatever build/altairsim existed
# THEN -- and it looks exactly like the release. That is not hypothetical: on 2026-07-20 a stale
# pre-tag staging directory reported "AltairSim 0.2.0 (v0.1.0-82-gb634269) (modified)" and read
# as a version bug in the shipped archives, which were correct.
#
# It runs HERE because the refusals below exit 1, and until 2026-07-20 they exited leaving the
# PREVIOUS run's archive sitting under the exact name the release process expects (observed on
# the Intel Mac: a refused headless run left the good tarball untouched at its original
# timestamp). Anyone uploading by filename rather than by watching the exit code would ship it.
# A refused run must leave nothing that can be mistaken for its output.
rm -rf "$out"/altairsim-*

# ---------------------------------------------------------------------------
# REFUSE TO PACKAGE A BINARY THAT CANNOT OPEN A WINDOW.
#
# THIS IS THE CHECK v0.2.0 DID NOT HAVE. All three archives shipped headless: no CI leg had
# SDL3, find_package failed, display_sdl.cpp compiled nowhere, and the video window the manual
# documents at length could not be opened from anything released. Nothing caught it, because a
# headless binary runs `altairsim vdm1` perfectly happily and draws nothing. DISTRIBUTION.md
# 4.2 step 2 puts a STOP on the configure line -- but that is a line a person has to read, and
# not reading it is exactly how this shipped. This one cannot be not-read.
#
# ASK THE BINARY, DO NOT PROBE THE FILE. nm/otool/dumpbin all mean a toolchain the packaging
# machine may not have -- Git Bash on Windows has no `nm` -- and 7 records that `strings` gives
# a FALSE NEGATIVE here, because SDL_CreateWindow is a symbol and not a literal. SHOW VERSION
# carries a `video` row for this, so the answer is the same on all four machines.
if ! "$sim" -n -x 'SHOW VERSION' 2>/dev/null | grep -q '^ *video *SDL3'; then
  echo "build-package: THIS BINARY IS HEADLESS -- refusing to package it." >&2
  echo >&2
  "$sim" -n -x 'SHOW VERSION' 2>&1 | sed 's/^/    /' >&2
  echo >&2
  echo "A headless build runs the video machines and draws nothing, which is what every" >&2
  echo "v0.2.0 archive shipped. Configure against a real SDL3 and look for" >&2
  echo "    -- SDL3 found -- video boards enabled (windowed)" >&2
  echo "    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<static SDL3 prefix>" >&2
  exit 1
fi

# ...and refuse one that links SDL by a path only this machine has. A Homebrew build names
# /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib absolutely, so it starts on no other machine --
# 3.2's trap, and the reason static is the answer. A path relative to the binary
# (@executable_path, @rpath, $ORIGIN) is the documented dynamic FALLBACK and is allowed.
#
# There is no portable way to ask this, so it runs where the tool exists and SAYS SO when it
# does not, rather than passing quietly and reading as checked.
case $(uname -s) in
  Darwin) linkage=$(otool -L "$sim" 2>/dev/null | grep -i sdl || true) ;;
  Linux)  linkage=$(ldd     "$sim" 2>/dev/null | grep -i sdl || true) ;;
  *)      linkage=""
          echo "build-package: NOTE -- cannot check SDL linkage on this host ($(uname -s))." >&2
          echo "  Per DISTRIBUTION.md 7, run: dumpbin /dependents altairsim.exe" >&2 ;;
esac
if [ -n "$linkage" ] && ! echo "$linkage" | grep -q '@executable_path\|@rpath\|\$ORIGIN'; then
  echo "build-package: THIS BINARY LINKS SDL BY ABSOLUTE PATH -- refusing to package it." >&2
  echo "$linkage" | sed 's/^/    /' >&2
  echo >&2
  echo "It starts on no machine but this one. Build SDL3 static (tools/build-sdl3-static.sh)" >&2
  echo "and configure with -DCMAKE_PREFIX_PATH=<that prefix>. See DISTRIBUTION.md 3.2." >&2
  exit 1
fi

pkg=$out/altairsim-$ver-$target

# The target suffix makes the staging directory self-documenting; the cleanup above (which runs
# before the refusals, deliberately) makes sure it is also the only one here.
mkdir -p "$pkg"

cp "$sim" "$pkg/"

# The manual. It is a DELIVERABLE, not an optional extra -- a package without it is a binary
# and a pile of disk images, and nobody can tell what to do with those.
#
# HAND IT IN (--pdf) FOR A RELEASE. The coordinator builds this document ONCE and gives the
# same file to all four machines. Rebuilding it here instead means each machine's local
# toolchain decides what ships: pandoc's HTML is the paginator's input, docs.yml PINS pandoc
# at 3.6, Homebrew ships 3.10, and A DIFFERENT PANDOC IS A DIFFERENT DOCUMENT. v0.2.0 hit
# exactly this and was fixed by restoring CI's PDF over this script's output. The flag is
# also what keeps pandoc, a Chromium and poppler off the three secondary machines entirely.
if [ -n "$pdf" ]; then
  cp "$pdf" "$pkg/altairsim-manual.pdf"
else
  local_pandoc=$(pandoc --version 2>/dev/null | head -1 || true)
  echo "build-package: WARNING -- rebuilding the manual with the LOCAL toolchain." >&2
  echo "  ${local_pandoc:-pandoc: NOT FOUND}" >&2
  # SAY WHY WHEN THE NUMBERS MATCH, or the warning reads as a false alarm and gets ignored.
  # Reported from the Intel Mac, 2026-07-20: it has pandoc 3.6, so the warning printed
  # "pandoc 3.6" directly above "docs.yml pins pandoc 3.6" and looked like a broken check.
  # It is not -- the rebuild there really did produce a different document.
  if [ "$local_pandoc" = "pandoc 3.6" ]; then
    echo "  docs.yml pins pandoc 3.6 -- the SAME NUMBER, and still not the same document:" >&2
    echo "  fonts and browser differ per machine, and the paginator is what makes the PDF." >&2
  else
    echo "  docs.yml pins pandoc 3.6, and a different pandoc is a different document." >&2
  fi
  echo "  For a RELEASE, pass --pdf with the manual the coordinator built." >&2

  # BUILD IT SOMEWHERE ELSE. build-docs.sh's argument is its OUTPUT directory, and it was
  # handed $root/docs -- so this path rewrote docs/altairsim-manual.pdf AND
  # docs/altairsim-devguide.pdf, both tracked, and said nothing about it. Reported from the
  # Intel Mac, 2026-07-20. The devguide is not even part of packaging.
  #
  # That is the v0.2.0 trap wearing a different hat: --pdf keeps a local PDF out of the
  # PACKAGE, and this kept it in the REPOSITORY, one `git commit -a` away from overwriting
  # CI's. CLAUDE.md's "git checkout -- the PDFs afterwards" rule is attached to build-docs.sh,
  # and nobody running the PACKAGING script has any reason to think they just invoked it.
  docs_tmp=$out/.docs-build
  rm -rf "$docs_tmp"
  mkdir -p "$docs_tmp"
  "$root/tools/build-docs.sh" "$docs_tmp" > /dev/null
  cp "$docs_tmp/altairsim-manual.pdf" "$pkg/"
  rm -rf "$docs_tmp"
fi

# ...and NOT the Developer Guide. That document is about the source, which is not in here.

# ---------------------------------------------------------------------------
# The loose FILES, from the FILE table -- TOKEN-EXPANDED on the way in, exactly like the
# manual's chapters. USING-ALTAIRSIM.md writes {{MACHINE_CPM}} rather than a literal path, so
# the path a user reads is the path that is actually in the zip, and it cannot drift from the
# DIR table above. The substitution is the same sed loop as tools/build-docs.sh's expand().
# ---------------------------------------------------------------------------
expand() {  # expand <src> <dst> : copy, substituting {{TOKEN}} from package.map
  cp "$1" "$2"
  sed -n 's/^\([A-Z_][A-Z0-9_]*\)[[:blank:]]*=[[:blank:]]*\(.*\)$/\1	\2/p' "$map" |
  while IFS="$(printf '\t')" read -r key val; do
    sed "s|{{$key}}|$val|g" "$2" > "$2.tmp" && mv "$2.tmp" "$2"
  done
}

# READ THE TABLES FROM A HERE-DOC, NOT A PIPE. A `while read` fed by a pipe runs in a
# SUBSHELL: an `exit 1` in the body ends only that subshell, and a variable it sets does not
# survive the loop. The second half is what actually bit -- see `missing` below, which was
# assigned in a piped loop and read after it, so it was always empty.
#
# The `exit 1`s here happened to work anyway, because a pipeline's status IS its last
# command's and the `while` is last, so `set -e` fired. That is a thin thing to rest a guard
# on -- append one command to the pipeline and every check in this file goes quiet. A
# here-doc does not depend on it. (Same fix as tools/fetch-disk-images.sh:58-61.)
FILES=$(sed -n 's/^FILE[[:blank:]]*\([^[:blank:]]*\)[[:blank:]]*<=[[:blank:]]*\(.*[^[:blank:]]\)[[:blank:]]*$/\1|\2/p' "$map")

while IFS='|' read -r dest src; do
  [ -n "$dest" ] || continue
  if [ ! -f "$root/$src" ]; then
    echo "build-package: package.map names $src, which does not exist" >&2
    exit 1
  fi
  mkdir -p "$pkg/$(dirname "$dest")"
  expand "$root/$src" "$pkg/$dest"
  # An unexpanded token is a broken instruction shipped to a user -- refuse it, as the manual does.
  if grep -q '{{[A-Z_]*}}' "$pkg/$dest"; then
    echo "build-package: $dest has UNEXPANDED TOKENS -- add them to docs/package.map:" >&2
    grep -n '{{[A-Z_]*}}' "$pkg/$dest" >&2
    exit 1
  fi
done <<EOF
$FILES
EOF

# ---------------------------------------------------------------------------
# The examples, from the DIR table.
# ---------------------------------------------------------------------------
missing=""

# POSIX classes, not \t: BSD sed does not read `\t` as a tab, it reads it as the LETTER t --
# so `[ \t]*` happily ate the leading "t" of "tapes/..." and then complained that
# "apes/4KBasic31" did not exist. A portable script may not assume GNU sed.
DIRS=$(sed -n 's/^DIR[[:blank:]]*\([^[:blank:]]*\)[[:blank:]]*<=[[:blank:]]*\(.*[^[:blank:]]\)[[:blank:]]*$/\1|\2/p' "$map")

while IFS='|' read -r dest src; do
  [ -n "$dest" ] || continue
  if [ ! -d "$root/$src" ]; then
    echo "build-package: package.map names $src, which does not exist" >&2
    exit 1
  fi
  mkdir -p "$pkg/$(dirname "$dest")"
  cp -R "$root/$src" "$pkg/$dest"

  # The assembler listings, the vendor ReadMes, and OUR OWN per-directory README are all
  # repository artifacts, and none of them belong in the zip.
  #
  # The README especially. It is written for someone standing in the source tree -- it talks
  # about .gitignore and about which files are "not in this repository", which are sentences
  # that mean nothing to a person holding a zip, and which quietly contradict the manual. The
  # ONLY docs in the zip are the ones the FILE table put there on purpose (the manual PDF and
  # USING-ALTAIRSIM.md) -- a per-directory README is a repository artifact and is not one.
  # ...and the same rule takes out SOURCE, which is the other thing that is not product:
  # examples/sol ships a tape, not the ENTER script the tape was derived from nor the script
  # that derives it. Both stay in the repository (docs/sources.md has the provenance).
  rm -f "$pkg/$dest"/*.ASM "$pkg/$dest"/*.PRN "$pkg/$dest"/README.md "$pkg/$dest"/-ReadMe.pdf \
        "$pkg/$dest"/*.ENT "$pkg/$dest"/make-*.sh 2>/dev/null || true

  # Did any actual MEDIA come with it?
  if ! ls "$pkg/$dest"/*.dsk "$pkg/$dest"/*.DSK "$pkg/$dest"/*.tap "$pkg/$dest"/*.TAP 2>/dev/null | head -1 | grep -q .; then
    echo "  !! $dest has a machine file and NO MEDIA" >&2
    missing="$missing $dest"
  fi
done <<EOF
$DIRS
EOF

# EVERY shipped example's media is TRACKED (.gitignore names each one), so an empty example
# directory is not "you have not fetched the optional images yet" -- it is a file that should
# be in your tree and is not. The zip would boot to a dead prompt and look like our bug, which
# is the thing the header promises this script will never quietly do. So it is fatal, and it
# is fatal BEFORE the zip exists: refusing to build beats building an archive we have just
# finished calling broken.
if [ -n "$missing" ]; then
  echo >&2
  echo "build-package: NOT PACKAGED -- these ship media that is missing from your tree:" >&2
  for d in $missing; do echo "    $d" >&2; done
  echo "Each one is tracked; restore it with: git checkout -- examples/" >&2
  exit 1
fi

name=altairsim-$ver-$target
archive=$out/$name.$ext

echo
echo "build-package: staged $pkg"

# tar.gz everywhere but Windows. WINDOWS HAS NO `zip`: Git Bash does not ship one, so the
# obvious command is the one that is missing on the one platform that needs this branch.
# Windows 10 1803+ does ship tar.exe (bsdtar), whose -a picks the format from the suffix,
# and PowerShell has Compress-Archive. Try all three rather than rest on any one.
if [ "$ext" = "zip" ]; then
  if command -v zip > /dev/null 2>&1; then
    ( cd "$out" && zip -qr "$name.zip" "$name" )
  elif command -v powershell.exe > /dev/null 2>&1; then
    ( cd "$out" && powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$name' -DestinationPath '$name.zip' -Force" )
  elif tar --version 2>/dev/null | grep -qi bsdtar; then
    ( cd "$out" && tar -a -cf "$name.zip" "$name" )
  else
    echo "build-package: no zip, no powershell.exe, no bsdtar -- cannot make a .zip" >&2
    exit 1
  fi
else
  ( cd "$out" && tar czf "$name.tar.gz" "$name" )
fi

[ -f "$archive" ] || { echo "build-package: $archive was not created" >&2; exit 1; }

echo "build-package: $archive"
echo
# POINT AT THE ARCHIVE, NOT THE STAGING DIRECTORY. This message used to name $pkg, which is
# the copy that is NOT shipped -- and being told to cd into it is how a stale build gets
# mistaken for the release. The archive is the artifact; prove that.
echo "Now prove it, from OUTSIDE the repository -- somewhere \`git rev-parse\` FAILS:"
case $ext in
  tar.gz) echo "    tar xzf $archive -C /tmp && cd /tmp/$name" ;;
  zip)    echo "    unzip $archive -d /tmp && cd /tmp/$name" ;;
esac
# Name the binary the reader actually has: altairsim.exe on Windows, altairsim elsewhere.
# A runbook that says ./altairsim on a machine holding altairsim.exe reads as a broken package.
echo "    ./$(basename "$sim") --version                            # a bare 'AltairSim $ver'"
echo "    ./$(basename "$sim") examples/cpm/cpm22-buffered.toml     # CP/M reaches A>"
