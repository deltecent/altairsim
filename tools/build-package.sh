#!/bin/sh
#
# Assemble the distribution: the zip we actually hand people.
#
#   altairsim                the program
#   altairsim-manual.pdf     the manual -- and NOTHING in it names a file that is not here
#   disks/  tapes/           the examples, each a self-contained folder
#
# THE CONTENTS COME FROM docs/package.map, and from nowhere else. That file is the single
# source of truth for three things that would otherwise drift: the tokens the manual writes
# ({{MACHINE_CPM}} and friends), the directories this script copies, and what the docs tests
# check. Add an example by adding a DIR line there -- not by editing this script.
#
# THE DISK IMAGES ARE NOT IN GIT (*.dsk / *.DSK are gitignored: they are large and they are not
# ours to redistribute). So this script copies whatever is actually on disk in the source tree
# and TELLS YOU what was missing, rather than quietly shipping a folder with a machine file and
# no machine in it -- which would boot to a dead prompt and look like our bug.
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

sed -n 's/^FILE[[:blank:]]*\([^[:blank:]]*\)[[:blank:]]*<=[[:blank:]]*\(.*[^[:blank:]]\)[[:blank:]]*$/\1|\2/p' "$map" |
while IFS='|' read -r dest src; do
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
done

# ---------------------------------------------------------------------------
# The examples, from the DIR table.
# ---------------------------------------------------------------------------
missing=""

# POSIX classes, not \t: BSD sed does not read `\t` as a tab, it reads it as the LETTER t --
# so `[ \t]*` happily ate the leading "t" of "tapes/..." and then complained that
# "apes/4KBasic31" did not exist. A portable script may not assume GNU sed.
sed -n 's/^DIR[[:blank:]]*\([^[:blank:]]*\)[[:blank:]]*<=[[:blank:]]*\(.*[^[:blank:]]\)[[:blank:]]*$/\1|\2/p' "$map" |
while IFS='|' read -r dest src; do
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
  rm -f "$pkg/$dest"/*.ASM "$pkg/$dest"/*.PRN "$pkg/$dest"/README.md "$pkg/$dest"/-ReadMe.pdf 2>/dev/null || true

  # Did any actual MEDIA come with it?
  if ! ls "$pkg/$dest"/*.dsk "$pkg/$dest"/*.DSK "$pkg/$dest"/*.tap 2>/dev/null | head -1 | grep -q .; then
    echo "  !! $dest has a machine file and NO MEDIA -- the image is gitignored and is not"
    echo "     in your tree. Download it (see the README in $src) before shipping this."
  fi
done

echo
echo "build-package: $pkg"
( cd "$out" && zip -qr "altairsim-$ver.zip" "altairsim-$ver" )
echo "build-package: $out/altairsim-$ver.zip"
echo
echo "Now check the one thing that matters, from OUTSIDE the repository:"
echo "    cd $pkg && ./altairsim disks/cpm22/cpm22-buffered.toml"
