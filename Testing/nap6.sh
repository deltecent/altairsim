#!/bin/bash
# BUG #6, THE DECISIVE EXPERIMENT.
#
# Claim under test: the run loop's idle nap fires DURING a transfer that is running on a
# NON-CONSOLE line, because the "a byte arrived, do not nap" defence (con.consumed()) only
# watches the CONSOLE -- and the transfer is out on 2SIO port B. A napping loop retires
# ~2000 instructions per 4 ms, i.e. a T-state ceiling of ~4.5 MHz that DOES NOT DEPEND ON
# clock_hz -- while the 6850 spaces characters by hz*bits/baud, which does. Hence a faster
# crystal is slower.
#
# PREDICTION, written before the run:
#   idle=true   naps > 0, and elapsed climbs with the clock   (110 / 120 / 207)
#   idle=false  naps = 0, and THE INVERSION VANISHES          (all -> the 78 s wire floor)
#
# If idle=false changes nothing, the hypothesis is DEAD and the run loop must not be patched.
#
# The simulator binary is SNAPSHOT and passed in $SIM: this takes ~25 minutes, and a rebuild
# in build/ during the sweep would swap the binary out from under a live measurement.
cd "$(dirname "$0")"

SIM="${SIM:?set SIM to a snapshot of the altairsim binary}"
export SIM ALTAIRSIM_TIMING=1

MASTER="/Users/patrick/src/altairsim/disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK"
BAUD=9600
CLOCKS="${CLOCKS:-2000000 4000000 8000000}"
IDLES="${IDLES:-true false}"

# 72,419 bytes at 9600 8N1 is a 78 s wire floor. Give it room, then give up.
tmo=$(( 595 * 132 * 10 * 2 / BAUD + 120 ))

printf '%-6s %-9s %-6s %8s %9s %7s %s\n' IDLE CLOCK RESULT SECS RETRIES NAPS TIMING
printf '%-6s %-9s %-6s %8s %9s %7s %s\n' ---- ----- ------ ---- ------- ---- ------

for idle in $IDLES; do
  for clock in $CLOCKS; do
    pkill -f altairsim-nap6 2>/dev/null; pkill -f xmodem.py 2>/dev/null; sleep 1

    cp "$MASTER" scratch-8mb.dsk
    sed -e "s/@CLOCK@/$clock/" -e "s/@BAUD@/$BAUD/" -e "s/@IDLE@/$idle/" sys2.toml.in > sys2.toml

    line=$(BAUD=$BAUD expect -f hostsend.exp "$tmo" 2>&1 | grep '^RESULT:')

    if [ "${line#RESULT: PASS}" != "$line" ]; then
      secs=$(echo "$line" | awk '{print $3}')
      ret=$(echo  "$line" | grep -o 'retries=[0-9?]*' | cut -d= -f2)
      naps=$(echo "$line" | grep -o 'naps=[0-9]*'     | cut -d= -f2)
      tim=$(echo  "$line" | sed 's/.*| //')
      printf '%-6s %-9s %-6s %8s %9s %7s %s\n' "$idle" "$clock" PASS "$secs" "$ret" "${naps:-?}" "$tim"
    else
      printf '%-6s %-9s %-6s %8s %9s %7s %s\n' "$idle" "$clock" FAIL - - - "${line#RESULT: FAIL }"
    fi
  done
done

pkill -f altairsim-nap6 2>/dev/null; pkill -f xmodem.py 2>/dev/null
echo "done."
