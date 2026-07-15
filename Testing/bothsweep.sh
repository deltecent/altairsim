#!/bin/bash
# The real test: HOST <-> SIM, both directions, across baud x clock_hz.
#
# Sim-to-sim is not run, and deliberately so: there the sender's EMULATED ACK timeout races the
# receiver's EMULATED work, so it measures two simulators against each other rather than the
# hardware. Against a real host at one end, each direction isolates one role:
#
#   host -> sim   the sim RECEIVES (PCGET). Can it keep up with real data? retries tells us.
#   sim  -> host  the sim SENDS (PCPUT). Its 4-second ACK window is emulated, so it shrinks in
#                 real time as clock_hz rises while the wire stays real. This one must break.
cd "$(dirname "$0")"
MASTER="/Users/patrick/src/altairsim/disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK"
BAUDS="${BAUDS:-9600 19200 38400}"
CLOCKS="${CLOCKS:-2000000 4000000 8000000 16000000 32000000 0}"

pkill -f build/altairsim 2>/dev/null; pkill -f xmodem.py 2>/dev/null; sleep 1

printf '%-12s %-7s %-9s %-6s %7s %8s %s\n' DIRECTION BAUD CLOCK RESULT SECS BYTES/S NOTE
printf '%-12s %-7s %-9s %-6s %7s %8s %s\n' --------- ---- ----- ------ ---- ------- ----

for baud in $BAUDS; do
  tmo=$(( 595 * 132 * 10 * 2 / baud + 60 ))
  for clock in $CLOCKS; do
    label=$clock; [ "$clock" = 0 ] && label="0(flat)"
    cp "$MASTER" scratch-8mb.dsk
    sed -e "s/@CLOCK@/$clock/" -e "s/@BAUD@/$baud/" sys1.toml.in > sys1.toml
    sed -e "s/@CLOCK@/$clock/" -e "s/@BAUD@/$baud/" sys2.toml.in > sys2.toml

    # host -> sim  (sim is the RECEIVER: PCGET)
    line=$(BAUD=$baud expect -f hostsend.exp "$tmo" 2>&1 | grep '^RESULT:')
    set -- $line
    if [ "$2" = PASS ]; then
      printf '%-12s %-7s %-9s %-6s %7s %8s %s\n' "host->sim" "$baud" "$label" PASS "$3" "$4" "retries=${7#retries=}"
    else
      printf '%-12s %-7s %-9s %-6s %7s %8s %s\n' "host->sim" "$baud" "$label" FAIL - - "${line#*- - }"
    fi
    pkill -f build/altairsim 2>/dev/null; pkill -f xmodem.py 2>/dev/null; sleep 1

    # sim -> host  (sim is the SENDER: PCPUT)
    line=$(BAUD=$baud expect -f hostrecv.exp "$tmo" 2>&1 | grep '^RESULT:')
    set -- $line
    if [ "$2" = PASS ]; then
      printf '%-12s %-7s %-9s %-6s %7s %8s %s\n' "sim->host" "$baud" "$label" PASS "$3" "$4" "${5}"
    else
      printf '%-12s %-7s %-9s %-6s %7s %8s %s\n' "sim->host" "$baud" "$label" FAIL - - "${line#*- - }"
    fi
    pkill -f build/altairsim 2>/dev/null; pkill -f xmodem.py 2>/dev/null; sleep 1
  done
done
