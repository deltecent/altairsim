#!/bin/sh
#
# Download the CP/M disk images that are NOT in this repository, and verify them.
#
#   usage: tools/fetch-disk-images.sh [name ...]
#
#          no argument -> everything missing
#
# WHY ONLY SOME IMAGES ARE HERE. Three bootable CP/M disks ARE tracked (the buffered 8"
# floppy, now examples/cpm/cpm22b23-56k.dsk, and the pair of 5.25" minidisks -- see
# .gitignore, which names them one at a time), and they are the ones the acceptance tests
# boot. What is left is the awkward pair, and NEITHER IS NEEDED BY ANY TEST (2026-07-19):
#
#   CPM22-8MB-56K.DSK   8.6 MB, which is 20x the tracked disks put together. It is what
#                       hostbridge.cmake's MODE=build wants -- the 78 KB of .ASM that mode
#                       PIPs in does not fit in the tracked floppy's 18K free -- and that
#                       mode is run BY HAND when a .ASM changes, not by ctest.
#   cpm22b23-24k.dsk    the same CP/M relocated for a 24K machine -- which boots in a 56K
#                       machine perfectly well, that machine simply having more RAM than
#                       the image uses. Kept because it is the interesting half of the
#                       "the machine must be at least as big as the image" demonstration
#                       in disks/mits-88dcdd/cpm22/buffered/README.md.
#
# Nothing here is required to build or to run the suite; this script turns a README
# treasure hunt into one command.
#
# THE CHECKSUMS ARE THE POINT. deramp.com is a live site, not an archive with a content
# hash, and an image that changed under us would surface as a baffling acceptance
# failure days later. A mismatch stops here instead, and says so.

set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE="https://deramp.com/downloads/altair/software"

# name | destination (repo-relative) | sha256 | url
# Verified 2026-07-18; deramp's copies are dated 2021-06-28 and have not moved since.
IMAGES="
8mb|disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK|700ac2caad5bd15d1f021543e86c09c2eef45982f2cd6807c527d4e7c1738c0a|$BASE/8_inch_floppy/CPM/CPM%202.2/FDC+%208Mb%20CPM%202.2/CPM22-8MB-56K.DSK
24k|disks/mits-88dcdd/cpm22/buffered/cpm22b23-24k.dsk|1f9f533020ba2a4a954714a1edd4dcd9551f35dda4a75af04ddff1c9771fdade|$BASE/8_inch_floppy/CPM/CPM%202.2/CPM%202.2B/cpm22b23-24k.dsk
"

command -v curl >/dev/null 2>&1 || { echo "fetch-disk-images: needs curl on PATH" >&2; exit 1; }

# shasum is the macOS spelling, sha256sum the Linux one. Both ship with the OS.
if command -v shasum >/dev/null 2>&1; then
    sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
elif command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d' ' -f1; }
else
    echo "fetch-disk-images: no shasum or sha256sum on PATH" >&2
    exit 1
fi

wanted=${*:-}
fetched=""    # only for the summary; nothing here modifies what it fetched

# A `while read` fed by a HERE-DOC runs in this shell; fed by a PIPE it runs in a subshell,
# where `exit 1` on a bad download would end the subshell and let the script sail on to
# print a cheerful summary. Redirect, do not pipe.
while IFS='|' read -r name dest want url; do
    [ -n "$name" ] || continue
    if [ -n "$wanted" ]; then
        echo " $wanted " | grep -q " $name " || continue
    fi

    out="$REPO_ROOT/$dest"

    # ALREADY THERE IS NOT ALREADY RIGHT. A half-finished download from a dropped
    # connection is a file of the correct name and the wrong length, and the test it
    # feeds would fail somewhere far away from here.
    #
    # This check works ONLY BECAUSE NOTHING EVER WRITES TO THESE FILES. Whatever needs a
    # prepared disk -- the host-bridge utilities on the 8 MB and the 24K images, say --
    # copies to scratch and prepares the copy (tests/acceptance/hostbridge.cmake:198). Install
    # anything into the images here and the pinned hash stops matching the very next run,
    # and a checksum that always fails is a checksum nobody reads.
    if [ -f "$out" ]; then
        if [ "$(sha256 "$out")" = "$want" ]; then
            echo "  ok       $dest"
            continue
        fi
        echo "  REPLACE  $dest (checksum does not match -- refetching)"
        rm -f "$out"
    fi

    echo "  fetch    $dest"
    mkdir -p "$(dirname "$out")"

    # Download beside the target, not onto it: an interrupted curl must not leave a
    # ruin where a good image used to be.
    tmp="$out.partial"
    curl -fsSL -o "$tmp" "$url" || {
        rm -f "$tmp"
        echo "fetch-disk-images: download failed: $url" >&2
        exit 1
    }

    have=$(sha256 "$tmp")
    if [ "$have" != "$want" ]; then
        rm -f "$tmp"
        echo "fetch-disk-images: CHECKSUM MISMATCH for $dest" >&2
        echo "  expected $want" >&2
        echo "  got      $have" >&2
        echo "The upstream file changed, or the download was corrupted. Do not just" >&2
        echo "update the hash -- find out which." >&2
        exit 1
    fi
    mv "$tmp" "$out"
    fetched="$fetched $out"
done <<EOF
$IMAGES
EOF

echo
echo "Re-run cmake so the gated tests are registered:"
echo "  cmake -S \"$REPO_ROOT\" -B \"$REPO_ROOT/build\" && cmake --build \"$REPO_ROOT/build\""
