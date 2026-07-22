# SNAPSHOT / RESTORE, through the REAL monitor (DESIGN.md 13).
#
# The unit tests (tests/test_snapshot.cpp) prove the state round-trips at the
# machine API; this proves the two COMMANDS are wired -- that SNAPSHOT writes a
# file the operator named, that RESTORE reads it back into a running machine, and
# that a file which is not a snapshot is refused with a reason rather than crashing.
#
# Piped, not a pty: it asserts only what the machine says on its own (the monitor's
# own SNAPSHOT/RESTORE/REGS output), never a keystroke typed at a guest.
#
# Expects: -DSIM=<altairsim> -DBIN=<binary dir>

set(work "${BIN}/snapshot-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
set(snap "${work}/state.snap")

# ---- 1. Set a register, snapshot, scribble over it, restore, read it back. ----
execute_process(
  COMMAND "${SIM}"
          -x "SET REG A=42"
          -x "SNAPSHOT ${snap}"
          -x "SET REG A=00"
          -x "RESTORE ${snap}"
          -x "REGS"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE  err
  TIMEOUT         30
)

if(NOT out MATCHES "snapshot written to")
  message(FATAL_ERROR "snapshot: SNAPSHOT did not confirm the write.\n--- stdout ---\n${out}")
endif()
if(NOT out MATCHES "restored from")
  message(FATAL_ERROR "snapshot: RESTORE did not confirm the read.\n--- stdout ---\n${out}")
endif()
if(NOT EXISTS "${snap}")
  message(FATAL_ERROR "snapshot: no file was written to ${snap}")
endif()

# The A register was 42, then 00, then restored. The LAST REGS is the one that
# matters: A must be 42 again.
string(REGEX MATCHALL "A=[0-9A-Fa-f][0-9A-Fa-f]" all_a "${out}")
list(GET all_a -1 last_a)
if(NOT last_a STREQUAL "A=42")
  message(FATAL_ERROR "snapshot: after RESTORE the accumulator was ${last_a}, not A=42.\n"
                      "--- stdout ---\n${out}")
endif()

# ---- 2. A file that is not a snapshot is refused, with a reason. ----
file(WRITE "${work}/garbage.snap" "this is not a snapshot, it is a text file\n")
execute_process(
  COMMAND "${SIM}" -x "RESTORE ${work}/garbage.snap"
  OUTPUT_VARIABLE out2
  ERROR_VARIABLE  err2
  TIMEOUT         30
)
if(NOT out2 MATCHES "RESTORE:")
  message(FATAL_ERROR "snapshot: RESTORE of a non-snapshot did not report an error.\n"
                      "--- stdout ---\n${out2}")
endif()

# ---- 3. A missing file is refused, not crashed on. ----
execute_process(
  COMMAND "${SIM}" -x "RESTORE ${work}/does-not-exist.snap"
  OUTPUT_VARIABLE out3
  RESULT_VARIABLE rc3
  TIMEOUT         30
)
if(NOT out3 MATCHES "RESTORE:.*cannot open")
  message(FATAL_ERROR "snapshot: RESTORE of a missing file did not report cleanly.\n"
                      "--- stdout ---\n${out3}")
endif()

file(REMOVE_RECURSE "${work}")
message(STATUS "snapshot: SNAPSHOT writes, RESTORE reads it back, and a bad file is refused.")
