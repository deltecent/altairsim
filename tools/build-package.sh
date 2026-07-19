#!/bin/sh
#
# Assemble the distribution: the zip we actually hand people.
#
#   altairsim                the program
#   altairsim-manual.pdf     the manual -- and NOTHING in it names a file that is not here
#   USING-ALTAIRSIM.md       the same machines, written for an AI assistant driving them over MCP
#   examples/cpm/            \
#   examples/basic/           > the examples, each a self-contained folder: a machine file and
#   examples/sol/            /  the media it mounts, lying beside it
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
#   usage: tools/build-package.sh [outdir]

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/dist}
map=$root/docs/package.map

sim=$root/build/altairsim
[ -x "$sim" ] || { echo "build-package: $sim is not built. cmake --build build" >&2; exit 1; }

ver=$("$sim" --version | awk '{print $2}')
pkg=$out/altairsim-$ver
rm -rf "$pkg"
mkdir -p "$pkg"

cp "$sim" "$pkg/"

# The manual. It is a DELIVERABLE, not an optional extra -- a package without it is a binary
# and a pile of disk images, and nobody can tell what to do with those.
"$root/tools/build-docs.sh" "$root/docs" > /dev/null
cp "$root/docs/altairsim-manual.pdf" "$pkg/"

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

echo
echo "build-package: $pkg"
( cd "$out" && zip -qr "altairsim-$ver.zip" "altairsim-$ver" )
echo "build-package: $out/altairsim-$ver.zip"
echo
echo "Now check the one thing that matters, from OUTSIDE the repository:"
echo "    cd $pkg && ./altairsim examples/cpm/cpm22-buffered.toml"
