# THE EBERHARD ROM MACHINES: `amon`, `acuter` and `cdbl`, each actually running.
#
# tests/test_machines.cpp already proves every built-in LOADS and round-trips through
# CONFIG SAVE. That is a different claim from this one, and the gap between them is
# where a machine file rots: a config can parse perfectly, put the ROM at the wrong
# address, give it too little RAM, or point the console at a port the firmware does not
# use -- and the loader has nothing to object to. Only running it says so.
#
# So this switches each machine on and reads the terminal:
#
#   amon    prints its banner AND the RAM page it relocated into. That second line is
#           the strongest single check here -- AMON hunts for the highest contiguous
#           256-byte page of RAM at startup, so `RAM: DF00` is the memory card being
#           measured by the guest. Shrink the RAM below and this number moves.
#   acuter  answers a command. Its sign-on is a bare `>`, which would pass against
#           almost any garbage, so the verdict is a DUMP whose bytes are the ROM's own
#           first instruction -- the image is at the right address and intact.
#   cdbl    boots CP/M off the tracked 8" image. The Combo loader auto-detects the
#           drive, and reaching the banner means the whole path worked.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir>

# A machine that reaches a prompt does not exit -- it polls the console forever, which
# is what a computer does. The keystroke files are the input and EOF is the off switch;
# TIMEOUT is only there so a hang is a failure rather than a hung build.
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
run_machine(out KEYS eberhard-monitor.keys ARGS amon)
expect("${out}" "AMON 3.1 by M. Eberhard" "amon")
# The RAM page AMON found. 56K stops at DFFF, so the top page is DF00 -- this is the
# machine file's memory region, read back out of the guest.
expect("${out}" "RAM: DF00" "amon")
# ...and it takes a command: the first bytes of AMON's own command table.
expect("${out}" "41 44 3F" "amon")

# ---- acuter ----
run_machine(out KEYS eberhard-monitor.keys ARGS acuter)
# ACUTER's first three bytes are 7F C3 81 -- the JMP at its cold-start entry. Dumping
# them proves the 2 KB image sits at F000 and arrived whole; the `>` prompt alone
# would not.
expect("${out}" "7F C3 81 F0" "acuter")

# ---- cdbl ----
#
# The disk is TRACKED (examples/cpm/), so this runs on every machine and every CI leg --
# unlike the tests that need a fetched image.
run_machine(out KEYS eberhard-cdbl.keys ARGS cdbl
            -x "MOUNT dsk0:drive0 examples/cpm/cpm22b23-56k.dsk" -x "RUN FF00")
expect("${out}" "56K CP/M 2.2b" "cdbl")
expect("${out}" "For Altair 8\" Floppy" "cdbl")

message(STATUS "Eberhard ROMs: amon banner + RAM scan, acuter dump, cdbl booted CP/M.")
