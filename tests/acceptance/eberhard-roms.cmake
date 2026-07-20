# THE EBERHARD ROM MACHINES START -- `amon`, `acuter` and `cdbl`.
#
# tests/test_machines.cpp already proves every built-in LOADS and round-trips through
# CONFIG SAVE. That is a different claim from this one, and the gap between them is
# where a machine file rots: a config can parse perfectly, put the ROM at the wrong
# address, give it too little RAM, or point the console at a port the firmware does not
# use -- and the loader has nothing to object to. Only running it says so.
#
# EVERYTHING CHECKED HERE IS SOMETHING THE MACHINE SAYS ON ITS OWN, with nothing typed
# at it. That restriction is not tidiness, it is the correctness condition for a PIPE:
# keystrokes fed down one are already in the console buffer before the guest is switched
# on, and they clock into the 6850's one-byte receive register at 9600 baud whether or
# not the guest has read the last one. Type at a monitor that is still starting up and
# the command arrives mangled -- sometimes. The first version of this file did exactly
# that, passed here, and failed on the macOS CI leg.
#
# The typing lives in eberhard-roms.exp, which waits for the prompt on a real terminal
# the way an operator does. If `expect` is missing, that half is not registered -- so
# this file deliberately keeps a liveness check for `acuter` too, weak as it is, rather
# than leaving that machine with no run test at all on such a host.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir>

# A machine that reaches a prompt does not exit -- it polls the console forever, which
# is what a computer does. The keystroke file is what keeps it alive long enough to get
# there (the run loop stops a guest that begs at an ended input three slices running),
# and TIMEOUT is only so a hang is a failure rather than a hung build.
function(run_machine out_var)
  cmake_parse_arguments(A "" "KEYS" "ARGS" ${ARGN})
  execute_process(
    COMMAND           "${SIM}" ${A_ARGS}
    WORKING_DIRECTORY "${SRC}"
    INPUT_FILE        "${SRC}/tests/acceptance/${A_KEYS}"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    out
    TIMEOUT           60
  )
  set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

function(expect out what who)
  string(FIND "${out}" "${what}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR
      "${who}: the terminal never showed '${what}'.\n--- terminal ---\n${out}")
  endif()
endfunction()

# ---- amon ----
#
# It needs no keystrokes at all to say both of these, so it is given none.
run_machine(out KEYS eberhard-quiet.keys ARGS amon)
expect("${out}" "AMON 3.1 by M. Eberhard" "amon")
# THE STRONGEST CHECK IN THIS FILE. AMON hunts for the highest contiguous 256-byte page
# of RAM at startup, relocates into it, and prints what it found -- so this line is the
# GUEST measuring the memory card. 56K stops at DFFF, so the top page is DF00. Change the
# region in machines/amon.toml and this number moves with it.
expect("${out}" "RAM: DF00" "amon")

# ---- acuter ----
#
# A bare `>` is ACUTER's entire sign-on, and a weak check: it says the machine reached
# its command loop and nothing about the image being intact. eberhard-roms.exp makes the
# real assertion by dumping the ROM's own first bytes.
run_machine(out KEYS eberhard-quiet.keys ARGS acuter)
expect("${out}" ">" "acuter")

# ---- cdbl ----
#
# The disk is TRACKED (examples/cpm/), so this runs on every machine and every CI leg --
# unlike the tests that need a fetched image.
#
# THE KEYSTROKES HERE ARE CARRIAGE RETURNS AND THEY ARE LOAD-BEARING, in a way `amon`'s
# absence of them is not. CDBL prints nothing while it relocates, probes for sector 16
# and reads the boot track, and the BIOS polls the console on the way up -- so with an
# input that has already ended, the run loop stops the guest before CP/M ever prints its
# banner. It does, reliably, with /dev/null. The CRs are consumed at the `A>` prompt
# AFTER the banner, which is the point: they are what keeps the machine alive through the
# boot, and they cannot mangle anything because CP/M is not being told to do anything.
run_machine(out KEYS eberhard-cdbl.keys ARGS cdbl
            -x "MOUNT dsk0:drive0 examples/cpm/cpm22b23-56k.dsk" -x "RUN FF00")
expect("${out}" "56K CP/M 2.2b" "cdbl")
expect("${out}" "For Altair 8\" Floppy" "cdbl")

message(STATUS "Eberhard ROMs: amon banner + RAM scan, acuter prompt, cdbl booted CP/M.")
