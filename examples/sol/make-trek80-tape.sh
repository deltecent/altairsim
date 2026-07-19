#!/bin/sh
# Rebuild TRK80.TAP and TRK80.WAV from TREK80.ENT.
#
#     ./make-trek80-tape.sh [path/to/altairsim] [path/to/altair_tapetool]
#
# TREK80.ENT IS THE SOURCE; the .TAP and the .WAV beside it are DERIVED, and this script
# is the derivation. Same relationship tools/install-hostbridge-utils.sh has to the CP/M
# image: the artifacts are committed so nobody needs a toolchain to run the example, and
# the recipe is committed so nobody has to take them on trust.
#
# WHY A TAPE CANNOT BE HAND-ASSEMBLED. A SOLOS cassette is not leader + sync + header +
# data. It is:
#
#     50 bytes of 00 | 01 sync | 16-byte header | 1 header-checksum byte
#                    | data in 256-byte blocks, each followed by 1 checksum byte
#
# Get the checksums wrong and the tape is INVISIBLE -- `CA` lists nothing and `GE` never
# finds the file, with no error to tell you why. So the tape is written by SOLOS ITSELF:
# load the image into a running Sol-20, put a blank tape in deck 1 in record mode, and
# `SA`ve. The checksums are then the machine's own arithmetic, not ours.
#
# The cross-check that this is the real artifact and not merely a plausible one: the header
# checksum SOLOS computes here is D9, the same byte in the same position as on the genuine
# archived tape (deramp.com's TRK80.WAV). That recording is unusable as DATA -- it decodes
# with 27 framing errors and 6,778 of its 7,840 payload bytes wrong -- but its header is
# readable, and it agrees.
#
# The archived ENTER script is the authority instead: 7,840 bytes loading at 0000, entry
# AF C3 5C 1D, matching the SIZE field (1EA0) in the genuine tape's header. See
# docs/sources.md for where it came from.
set -eu

cd "$(dirname "$0")"

SIM=${1:-altairsim}
TAPETOOL=${2:-altair_tapetool}

command -v expect >/dev/null 2>&1 || { echo "need expect" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "need python3" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# ---- 1. THE .ENT IS A SOLOS `ENTER` SCRIPT, not a binary. ------------------------------
#
#     ENTER 0000
#     0000: AF C3 5C 1D 3E 03 ...
#
# -- an address, a colon, and hex bytes, which is what you would have typed at SOLOS's
# prompt. Turn it into Intel HEX so the monitor's own LOAD can put it in memory.
python3 - "$work" <<'PY'
import re, sys, pathlib

work = pathlib.Path(sys.argv[1])
mem  = {}
for line in pathlib.Path("TREK80.ENT").read_text(errors="replace").splitlines():
    m = re.match(r'^\s*([0-9A-Fa-f]{4}):\s*((?:[0-9A-Fa-f]{2}\s*)+)', line)
    if not m:
        continue
    addr = int(m.group(1), 16)
    for i, b in enumerate(m.group(2).split()):
        mem[addr + i] = int(b, 16)

# THE IMAGE IS 0000-1E9F, WHICH IS 7,840 BYTES -- the SIZE field in the genuine tape's
# header reads 1EA0, and that is a LENGTH, not a last address. The .ENT's final line is
# `1EA0: 00/`: one byte past the end, an artifact of whoever dumped it. Take the 7,840 and
# leave the stray byte, or `SA TRK80 0000 1E9F` and the file disagree by one.
lo, hi = min(mem), 0x1E9F
assert lo == 0x0000 and max(mem) >= hi, f"unexpected extent {lo:04X}-{max(mem):04X}"
assert all(a in mem for a in range(lo, hi + 1)), "the .ENT has holes"
data = bytes(mem[a] for a in range(lo, hi + 1))
assert data[:4] == b'\xaf\xc3\x5c\x1d', "entry bytes are not AF C3 5C 1D"

out = []
for off in range(0, len(data), 16):
    chunk = data[off:off + 16]
    rec   = [len(chunk), (off >> 8) & 0xFF, off & 0xFF, 0x00, *chunk]
    rec.append((-sum(rec)) & 0xFF)
    out.append(":" + "".join(f"{b:02X}" for b in rec))
out.append(":00000001FF")
(work / "TREK80.HEX").write_text("\n".join(out) + "\n")
print(f"TREK80.HEX: {len(data)} bytes, {lo:04X}-{hi:04X}")
PY

# ---- 2. LET SOLOS WRITE THE TAPE. ------------------------------------------------------
#
# `SA TRK80 0000 1E9F 0000` -- name, first, last, and the fourth argument is the EXECUTE
# address, which is what makes `XE TRK80` work rather than just `GE`. The write runs in
# emulated real time at 1200 baud, hence the wait; ^E takes the keyboard back from SOLOS,
# and UNMOUNT is what COMMITS the file (docs/tapes.md).
cat > "$work/mktape.toml" <<TOML
[machine]
name = "mktape"
base = "sol20"
startup = [
  "MOUNT sol0:tape1 \"TRK80.TAP\"",
  "SET sol0:tape1 mode=record",
  "LOAD \"TREK80.HEX\"",
  "RUN C000",
]
TOML

# A BLANK TAPE HAS TO EXIST BEFORE YOU CAN PUT IT IN THE DECK. MOUNT opens a file; it does
# not create one, and "there is no cassette in deck 1" three steps later is what forgetting
# this looks like.
: > "$work/TRK80.TAP"

expect -f - "$SIM" "$work" <<'EXP'
set timeout 300
set sim  [lindex $argv 0]
set work [lindex $argv 1]
cd $work
spawn $sim mktape.toml
expect "console -- "
send "SA TRK80 0000 1E9F 0000\r"
sleep 30
send "\005"
expect "altairsim> "
send "UNMOUNT sol0:tape1\r"
expect "altairsim> "
send "QUIT\r"
expect eof
EXP

test -s "$work/TRK80.TAP" || { echo "SOLOS wrote no tape" >&2; exit 1; }
cp "$work/TRK80.TAP" TRK80.TAP
echo "TRK80.TAP: $(wc -c < TRK80.TAP | tr -d ' ') bytes"

# ---- 3. THE AUDIO. ---------------------------------------------------------------------
#
# cuts1200 at 22050 Hz, 3 s of leader and 2 s of trailer. The leader is not decoration:
# a real deck needs tone before the data to lock on, and the trailer keeps the last byte
# off the end of the file. Decodes back byte-identical with 0 framing errors -- which the
# check below insists on, because a WAV that merely EXISTS is not a WAV that loads.
"$TAPETOOL" encode TRK80.TAP TRK80.WAV cuts1200 22050 3 2
"$TAPETOOL" decode TRK80.WAV "$work/roundtrip.bin" cuts1200
cmp TRK80.TAP "$work/roundtrip.bin"
echo "TRK80.WAV: $(wc -c < TRK80.WAV | tr -d ' ') bytes, round-trips byte-identical"
