#!/bin/sh
#
# Put R.COM, W.COM and HDIR.COM onto a CP/M disk image.
#
# WHY THIS SCRIPT EXISTS AT ALL. The manual's file-transfer chapter tells the reader to
# type HDIR, R and W at the A> prompt. Those are ordinary CP/M programs and they have to
# be ON THE DISK -- and they are ours, so no disk fetched from deramp.com has them.
#
#     tools/install-hostbridge-utils.sh examples/cpm/cpm22b23-56k.dsk
#     tools/install-hostbridge-utils.sh "disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK"
#
# THE TRACKED IMAGE ALREADY HAS THEM. cpm22b23-56k.dsk is in git WITH R/W/HDIR installed --
# this script is how they got there, and it is the recipe that makes that blob auditable.
# For the images that are NOT tracked, prefer running this against a SCRATCH COPY: a fetched
# image that has been written to can never match its pinned checksum again, which is why
# tests/acceptance/dcdd-mixed.exp copies first and installs into the copy.
#
# THE BOOTSTRAP, AND IT IS NOT THE OBVIOUS ONE (Patrick, 2026-07-14). The obvious way is
# to PIP all three .HEX files in through the console and LOAD each. It does not fit: the
# buffered 8" floppy ships with 26K FREE, and the three hex files are 12.5K -- which has
# to coexist with the 6K of .COM they produce. So instead:
#
#     PIP R.HEX=CON:   ...   LOAD R   ...   ERA R.HEX      <- R.COM, and the hex is gone
#     R W.COM                                              <- and now R fetches the rest
#     R HDIR.COM                                              off the host, ITSELF
#
# Only ONE hex file is ever on the disk, and it is deleted before the other two arrive.
# The card bootstraps its own utilities, which is a pleasing thing for a card to do.
# Cost: 8K on the buffered floppy (26K free -> 18K).
#
# It is idempotent: LOAD and R both overwrite, so running it twice is running it once.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
sim=${ALTAIRSIM:-$root/build/altairsim}   # a caller with its own binary says so; ctest does
hb=$root/cpm/hostbridge

[ -x "$sim" ] || { echo "install-hostbridge-utils: no $sim -- build first." >&2; exit 1; }
[ $# -ge 1 ] || { echo "usage: $0 <disk-image> [<disk-image> ...]" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

CR=$(printf '\r')
SUB=$(printf '\032')          # ^Z -- what ends a PIP from the console

for img in "$@"; do
  [ -f "$img" ] || { echo "install-hostbridge-utils: no such image: $img" >&2; exit 1; }
  echo "=== $(basename "$img") ==="

  cp "$img" "$work/work.dsk"  # work on a COPY; the original is replaced only at the end

  # A BARE CR IN FRONT OF EVERY COMMAND -- a lightning rod. The BIOS re-initialises the
  # 2SIO on each warm boot, the 6850's master reset clears its receive register, and the
  # byte sitting there when a program exits is lost. A human never notices; a pipe always
  # does. The rod gets eaten and the command behind it survives. (Same trap, same fix, as
  # tests/acceptance/hostbridge.cmake -- feed `LOAD R` raw and it arrives as `OAD`.)
  {
    printf '%s' "$CR"; printf 'PIP R.HEX=CON:\r'
    sed 's/$/\r/' "$hb/R.HEX"                      # a command line is CR; a FILE is CRLF
    printf '%s' "$SUB"
    printf '%s' "$CR"; printf 'LOAD R\r'           # -> R.COM
    printf '%s' "$CR"; printf 'ERA R.HEX\r'        # ...and the hex is gone before the rest
    printf '%s' "$CR"; printf 'R W.COM\r'          # R fetches its own siblings
    printf '%s' "$CR"; printf 'R HDIR.COM\r'
    printf '%s' "$CR"; printf 'STAT\r'
    printf '%s' "$CR"; printf 'DIR *.HEX\r'        # expect: No file

    # THIS BIOS FLUSHES ITS TRACK BUFFER ON CONSOLE INPUT, not on close (see
    # docs/boards/mits-dcdd.md). The writes above do not reach the image until CP/M reads
    # the keyboard again -- so leave it keystrokes to do that with, or the last one is lost.
    i=0; while [ $i -lt 10 ]; do printf '%s' "$CR"; i=$((i+1)); done
  } > "$work/keys"

  cat > "$work/cmd" <<EOF
SET CONSOLE UPPER=OFF
SET hb0 HOSTDIR=$hb
MOUNT dsk0:drive0 "$work/work.dsk"
RUN FF00
EOF

  "$sim" -s "$work/cmd" < "$work/keys" > "$work/log" 2>&1 || true

  # DID IT ACTUALLY LAND? Ask the machine, not the script. Boot the image AGAIN, from
  # scratch, and have W write each .COM back out to the host -- then diff those against
  # the originals. That proves the file is on the disk, that it RUNS off the disk, and
  # that it is byte-for-byte what we shipped. A DIR listing would prove only the first.
  mkdir -p "$work/out"
  {
    printf '%s' "$CR"; printf 'W R.COM BACK-R.COM\r'
    printf '%s' "$CR"; printf 'W W.COM BACK-W.COM\r'
    printf '%s' "$CR"; printf 'W HDIR.COM BACK-HDIR.COM\r'
    i=0; while [ $i -lt 10 ]; do printf '%s' "$CR"; i=$((i+1)); done
  } > "$work/vkeys"
  cat > "$work/vcmd" <<EOF
SET CONSOLE UPPER=OFF
SET hb0 HOSTDIR=$work/out
MOUNT dsk0:drive0 "$work/work.dsk"
RUN FF00
EOF
  "$sim" -s "$work/vcmd" < "$work/vkeys" > "$work/vlog" 2>&1 || true

  ok=yes
  for u in R W HDIR; do
    if [ ! -f "$work/out/BACK-$u.COM" ]; then
      echo "  FAIL  $u.COM did not come back off the disk" >&2; ok=no
    elif ! cmp -s "$work/out/BACK-$u.COM" "$hb/$u.COM"; then
      echo "  FAIL  $u.COM on the disk differs from cpm/hostbridge/$u.COM" >&2; ok=no
    else
      echo "  ok    $u.COM  (runs off the disk, byte-for-byte cpm/hostbridge/$u.COM)"
    fi
  done

  if [ "$ok" != yes ]; then
    echo "  the image was NOT modified. transcripts: $work/log, $work/vlog" >&2
    trap - EXIT                       # keep the evidence
    exit 1
  fi

  sh -c "grep -a 'Space:' '$work/log' | tail -1 | sed 's/^/  /'" || true
  cp "$work/work.dsk" "$img"
  echo "  installed into $img"
done
