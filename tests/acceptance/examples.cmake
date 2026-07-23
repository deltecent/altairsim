# THE EXAMPLES, IN THE LAYOUT A USER ACTUALLY GETS.
#
# examples/ is not a test fixture. It is what we SHIP: a user gets the `altairsim`
# binary and that tree, and nothing else -- no repository, no build
# directory, no tests. So the question this file asks is not "do the boards work", which
# every other test already answers. It is:
#
#     DOES THE THING WE HAND PEOPLE WORK WHERE WE HAND IT TO THEM?
#
# Nothing asked that until now, and the coincidence that hid it is that the repo root and
# the distribution root were the same directory on this machine. Every example carried a
# repo-root-relative path, every test ran from the repo root, and the whole suite was green
# while `cd tapes/MitsPS2 && altairsim ps2int.toml` -- the documented way to run it --
# could not find its own tape.
#
# So this test COPIES an example out of the tree, leaving the repository behind, and boots
# it from there. That is the only arrangement in which the bug is visible, which is exactly
# why it is the arrangement to test in.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

set(dist "${BIN}/examples-work")
file(REMOVE_RECURSE "${dist}")

# The distribution: the binary (already built, wherever it is) plus this ONE example
# directory. Note what is NOT here -- machines/, roms/, src/, the repo. If the example
# needs any of it, this test fails, and it should.
file(COPY "${SRC}/examples/basic" DESTINATION "${dist}/examples")

set(example "${dist}/examples/basic")

# What 4K BASIC has to print for the machine to have actually booted off the cassette.
# `TAPE OK` is the one that cannot be faked by a machine that merely started: it is the
# output of a BASIC program typed into a BASIC that read itself off a period .TAP.
function(expect_basic out why)
  foreach(want "ALTAIR BASIC" "OK" "42" "TAPE OK")
    string(FIND "${out}" "${want}" hit)
    if(hit LESS 0)
      message(FATAL_ERROR "examples: ${why}\n"
                          "  '${want}' never reached the terminal.\n--- output ---\n${out}")
    endif()
  endforeach()
endfunction()

# ---- 1. THE DOCUMENTED WAY: cd into the example's directory and name the file. --------
#
# This is the case the examples are FOR, and the case that was broken. `basic4k.toml` says
# MOUNT "4K BASIC Ver 3-1.tap" -- the tape lying beside it -- and here that is also the
# working directory, so this would pass even under the old cwd-relative rule. It passes
# here because the tape is where the file says it is, which is the point: the file is now
# true no matter where the directory has been moved to.
execute_process(
  COMMAND           "${SIM}" basic4k.toml
  WORKING_DIRECTORY "${example}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`cd examples/basic && altairsim basic4k.toml` did not boot BASIC")

# ---- 2. ...AND BY PATH, from the top of the distribution. ------------------------------
#
# The same file, the same machine, from a different directory. THIS is the half that needs
# the loader to resolve against the file rather than the process: the tape is not in the
# working directory and never will be. If a machine file meant something different
# depending on where you launched it from, it would be the very trap looksLikeFile()
# refuses to walk into (core/machines.h) -- so it must not.
execute_process(
  COMMAND           "${SIM}" examples/basic/basic4k.toml
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`altairsim examples/basic/basic4k.toml` from the dist root did not boot BASIC")

# ---- 3. THE NEGATIVE CONTROL, and the only reason to believe either of the above. ------
#
# A path a HUMAN TYPES is relative to the SHELL, and it must stay that way. If the config's
# directory leaked out of the startup list and went on colouring commands typed afterwards,
# then `MOUNT dsk0:drive1 "scratch.dsk"` at the prompt would quietly mean a file next to
# somebody's example instead of the one the operator can see -- which is the same class of
# bug as the one this whole change fixes, only pointing the other way, and far nastier
# because it would find a file rather than fail to.
#
# So: a machine file whose startup mounts the tape BESIDE IT (which must succeed), followed
# by the identical MOUNT typed at the prompt from a directory where that tape is NOT
# (which must FAIL). The two commands are character-for-character the same. Only their
# provenance differs, and provenance is the entire feature.
file(WRITE "${example}/probe.toml"
     "[machine]\nname = \"probe\"\nbase = \"basic4k\"\n"
     "startup = [\"MOUNT acr0:tape \\\"4K BASIC Ver 3-1.tap\\\"\"]\n")

execute_process(
  COMMAND           "${SIM}" examples/basic/probe.toml -x "MOUNT acr0:tape \"4K BASIC Ver 3-1.tap\""
  WORKING_DIRECTORY "${dist}"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           30
)

# The startup one found it -- resolved against the file, which lives beside the tape.
string(FIND "${out}" "mounted examples/basic/4K BASIC Ver 3-1.tap" hit)
if(hit LESS 0)
  message(FATAL_ERROR
    "examples: the STARTUP mount did not resolve against the machine file's directory.\n"
    "--- output ---\n${out}")
endif()

# ...and the typed one did NOT, because the operator is standing somewhere else.
string(FIND "${out}" "no such file" hit)
if(hit LESS 0)
  message(FATAL_ERROR
    "examples: A TYPED PATH WAS RESOLVED AGAINST THE CONFIG'S DIRECTORY.\n"
    "  `MOUNT acr0:tape \"4K BASIC Ver 3-1.tap\"` was typed from ${dist}, where no such\n"
    "  tape exists -- and it found one anyway, next to the machine file. The config's\n"
    "  directory has escaped its startup list and is now colouring what humans type.\n"
    "--- output ---\n${out}")
endif()

# ---- 4. THE QUICK START'S OWN COMMAND, which was never tested until now. ---------------
#
# `examples/cpm` is the flagship: it is what docs/manual/quick-start.md promises, and until
# 2026-07-19 NO test booted it. `acceptance-dcdd-readonly` boots a test-owned machine file
# that happens to mount the same image, which proves the CARD and proves nothing at all
# about the example -- and that is much of how the manual drifted as far as it did.
#
# The keys are cpm-dir.keys, and its first byte is a NUL for the same reason basic4k.keys's
# is: every keystroke is in the buffer before the machine is switched on, and CP/M's cold
# start clears the SIO's receive register and eats exactly one byte.
file(COPY "${SRC}/examples/cpm" DESTINATION "${dist}/examples")
set(cpm "${dist}/examples/cpm")

# WHY ONLY THE FIRST DIRECTORY LINE IS CLAIMED, and it is CP/M's behaviour, not a hedge:
# DIR polls the console between lines so the operator can stop a long listing, and ANY key
# already waiting aborts it. Our keys are all in the pipe before the machine starts, so the
# CR behind `DIR` stops the listing after one line -- every time, and on purpose, since the
# alternative is a test whose output depends on how fast the host is.
#
# One line is enough to be a real claim. `A: L80      COM` is a directory entry read off
# the image through the 88-DCDD: a machine that merely started prints the banner and stops.
function(expect_cpm out why)
  foreach(want "56K CP/M 2.2b v2.3" "For Altair 8\" Floppy" "A>" "A: L80      COM")
    string(FIND "${out}" "${want}" hit)
    if(hit LESS 0)
      message(FATAL_ERROR "examples: ${why}\n"
                          "  '${want}' never reached the terminal.\n--- output ---\n${out}")
    endif()
  endforeach()
endfunction()

execute_process(
  COMMAND           "${SIM}" cpm22-buffered.toml
  WORKING_DIRECTORY "${cpm}"
  INPUT_FILE        "${SRC}/tests/acceptance/cpm-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_cpm("${out}" "`cd examples/cpm && altairsim cpm22-buffered.toml` did not boot CP/M")

# ...and by path from the distribution root, where the disk is NOT. Same machine, and the
# floppy has to be found beside the file that names it rather than beside the operator.
execute_process(
  COMMAND           "${SIM}" examples/cpm/cpm22-buffered.toml
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/cpm-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_cpm("${out}" "`altairsim examples/cpm/cpm22-buffered.toml` from the dist root did not boot CP/M")

# ---- 4b. THE HARD DISK -- CP/M booted through the 88-HDSK Datakeeper controller. -------
#
# Same shape as the floppy CP/M above, and here for the same reason: the disk is beside the
# machine file, so booting it from its own directory and by path proves the shipped folder
# works where the user is handed it. But it also proves something the floppy cannot -- the
# whole 88-HDSK command/handshake controller and its (cyl,side,sector) mapping, exercised
# by HDBL loading the boot pages and by CP/M reading the directory off the platter.
#
# One directory line is enough to be a real claim: `A: BOOT     ASM` is an entry read off
# the image through the controller, and DIR stops after one line because a CR is already
# waiting in the pipe (the same CP/M behaviour, and the same reason, as the floppy case).
file(COPY "${SRC}/examples/hdsk" DESTINATION "${dist}/examples")
set(hdsk "${dist}/examples/hdsk")

function(expect_hdsk out why)
  foreach(want "HDBL 2.00" "48K CP/M 2.2b v1.6" "For MITS 88-HDSK" "A0>" "A: BOOT     ASM")
    string(FIND "${out}" "${want}" hit)
    if(hit LESS 0)
      message(FATAL_ERROR "examples: ${why}\n"
                          "  '${want}' never reached the terminal.\n--- output ---\n${out}")
    endif()
  endforeach()
endfunction()

execute_process(
  COMMAND           "${SIM}" hdsk.toml
  WORKING_DIRECTORY "${hdsk}"
  INPUT_FILE        "${SRC}/tests/acceptance/hdsk-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_hdsk("${out}" "`cd examples/hdsk && altairsim hdsk.toml` did not boot CP/M off the hard disk")

# ...and by path from the distribution root, where the platter is NOT.
execute_process(
  COMMAND           "${SIM}" examples/hdsk/hdsk.toml
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/hdsk-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_hdsk("${out}" "`altairsim examples/hdsk/hdsk.toml` from the dist root did not boot CP/M")

# ---- 4b. THE 8800bt -- THE SAME CP/M, BOOTED BY THE TURNKEY MODULE. ---------------------
#
# examples/turnkey is the front-panel-less 8800b: one card (the Systems Turnkey Module)
# carries the boot PROM, the 6850 console at 0x10, the sense switches at FF, and the
# Auto-Start circuit. floppy.toml and hdsk.toml are deltas on the built-in `turnkey`
# machine that borrow the images from the cpm and hdsk directories copied above -- so this
# proves the WHOLE card end to end: `RUN 0000` jams `JMP` onto the bus, DBL/HDBL runs out
# of the phantom PROM, and the top 1K of the machine's 64K becomes RAM once the PROM
# switches itself out. The two directories above must already be in ${dist}/examples for
# the ../cpm and ../hdsk mounts to resolve.
file(COPY "${SRC}/examples/turnkey" DESTINATION "${dist}/examples")
set(turnkey "${dist}/examples/turnkey")

function(expect_contains out why)
  math(EXPR last "${ARGC} - 1")
  foreach(i RANGE 2 ${last})
    string(FIND "${out}" "${ARGV${i}}" hit)
    if(hit LESS 0)
      message(FATAL_ERROR "examples: ${why}\n"
                          "  '${ARGV${i}}' never reached the terminal.\n--- output ---\n${out}")
    endif()
  endforeach()
endfunction()

# Floppy: DBL, jammed at reset out of the phantom PROM, to 56K CP/M.
execute_process(
  COMMAND           "${SIM}" floppy.toml
  WORKING_DIRECTORY "${turnkey}"
  INPUT_FILE        "${SRC}/tests/acceptance/hdsk-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_contains("${out}" "`cd examples/turnkey && altairsim floppy.toml` did not boot CP/M off the floppy"
                "56K CP/M 2.2b" "For Altair 8" "A>")

# Hard disk: HDBL from socket L1, the Auto-Start switches moved to FC00, to 48K CP/M.
execute_process(
  COMMAND           "${SIM}" hdsk.toml
  WORKING_DIRECTORY "${turnkey}"
  INPUT_FILE        "${SRC}/tests/acceptance/hdsk-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_contains("${out}" "`cd examples/turnkey && altairsim hdsk.toml` did not boot CP/M off the hard disk"
                "HDBL 2.00" "48K CP/M 2.2b v1.6" "For MITS 88-HDSK" "A0>")

# ---- 5. THE DEBUGGER WALKTHROUGH -- symbols and hex loaded from beside the file. -------
#
# examples/debugger is a taught exercise: a 46-byte program, its .PRN listing and its .HEX,
# and a README that walks the monitor's debugger. This runs that walkthrough non-
# interactively through the SHIPPED binary, from the example's own directory, so it proves
# two things at once -- the symbolic disassembler names labels and operands the way the
# README says, and the .prn/.hex resolve beside the machine file rather than beside the repo.
file(COPY "${SRC}/examples/debugger" DESTINATION "${dist}/examples")
set(dbg "${dist}/examples/debugger")

execute_process(
  COMMAND           "${SIM}" debugger.toml
                    -x "SYMBOLS LOAD HELLO.PRN"
                    -x "LOAD HELLO.HEX"
                    -x "DISASM START-DONE"
                    -x "DISASM PUTC 7"
                    -x "EXAMINE START"
                    -x "BREAK DONE"
                    -x "RUN"
  WORKING_DIRECTORY "${dbg}"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           30
)

# What has to be true: the files loaded from beside the machine file; the disassembly is
# symbolic (a leading label, a label operand, and the EQU-address operand that is the whole
# point of the feature); and the program actually ran and printed through the 2SIO.
foreach(want
        "12 symbol(s) from HELLO.PRN"   # the .PRN resolved beside debugger.toml
        "loaded 46 bytes"               # so did the .HEX
        "START:"                        # a program label heads its own line
        "CALL PUTC"                     # a 16-bit operand reads as a label
        "LXI SP,STACK"                  # ...and as an EQU-address -- the CALL BDOS case
        "IN 10"                         # a BYTE operand stays a number (a port is not an address)
        "HELLO, WORLD"                  # it ran, on the console the file wired up
        "stopped at 0112")              # and stopped at the DONE breakpoint
  string(FIND "${out}" "${want}" hit)
  if(hit LESS 0)
    message(FATAL_ERROR "examples: the debugger walkthrough did not behave as the README says.\n"
                        "  '${want}' never reached the terminal.\n--- output ---\n${out}")
  endif()
endforeach()

# And the byte operand that must NOT be named: IN 10 stays IN 10, never IN TTYS, because a
# port is a byte and only a 16-bit operand is an address (README section 2).
string(FIND "${out}" "IN TTYS" hit)
if(hit GREATER_EQUAL 0)
  message(FATAL_ERROR "examples: a BYTE operand was annotated as a symbol.\n"
                      "  'IN 10' read as 'IN TTYS' -- a port is not an address.\n--- output ---\n${out}")
endif()

file(REMOVE_RECURSE "${dist}")
message(STATUS "examples: the shipped examples boot from their own directory, and a typed "
               "path still means the shell's.")
