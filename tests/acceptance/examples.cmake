# THE EXAMPLES, IN THE LAYOUT A USER ACTUALLY GETS.
#
# examples/ is not a test fixture. It is what we SHIP: a user gets the `altairsim` binary and
# that tree, and nothing else -- no repository, no build directory, no tests. So this file
# does not ask "does the simulator work" -- the simulator's own tests answer that from their
# own fixtures (tests/media, media.cmake), and none of them may read examples/. It asks:
#
#     DOES THE THING WE HAND PEOPLE WORK WHERE WE HAND IT TO THEM?
#
# So it COPIES each example out of the tree, leaving the repository behind, and runs EVERY
# machine file in it from there -- the only arrangement in which a repo-root-relative path in
# an example is visible as the bug it is.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir> -DHAVE_SDL=<0|1>

set(dist "${BIN}/examples-work")
file(REMOVE_RECURSE "${dist}")

# The whole of an MCP session, sent up front: every `run` waits for its `until` before the
# next request is read, so the file is a script, not a race.
function(mcp_session file)
  set(lines [=[{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"examples","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}]=])
  set(id 2)
  foreach(req ${ARGN})
    string(APPEND lines "\n{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":${req}}")
    math(EXPR id "${id} + 1")
  endforeach()
  file(WRITE "${file}" "${lines}\n")
endfunction()

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

# ---- 1. examples/cpm -- THE QUICK START'S OWN MACHINE, and every other file beside it. ------
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

# The flagship's .ini twin, by -s from the dist root: the floppy is beside the script (#575).
execute_process(
  COMMAND           "${SIM}" -s examples/cpm/cpm22-buffered.ini
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/cpm-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_cpm("${out}" "`altairsim -s examples/cpm/cpm22-buffered.ini` from the dist root did not boot CP/M")

# The README's `--set` command (#736): the same machine at 2 MHz, with the file as it is. The
# setting is applied before `startup> RUN FF00`, CP/M boots and lists the disk, and SHOW CLOCK
# afterwards reports the pacing that the boot had. Without --set the file gives full speed --
# that is the control, and it is what makes `paced` a claim about the option.
execute_process(
  COMMAND           "${SIM}" cpm22-buffered.toml --set "cpu0 clock_hz=2000000" -x "SHOW CLOCK"
  WORKING_DIRECTORY "${cpm}"
  INPUT_FILE        "${SRC}/tests/acceptance/cpm-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_cpm("${out}" "`altairsim cpm22-buffered.toml --set \"cpu0 clock_hz=2000000\"` did not boot CP/M")
string(FIND "${out}" "set> SET cpu0 clock_hz=2000000" at_set)
string(FIND "${out}" "startup> RUN FF00" at_startup)
if(at_set LESS 0 OR at_startup LESS 0 OR NOT at_set LESS at_startup)
  message(FATAL_ERROR "examples: --set was not applied before `startup> RUN FF00`\n--- output ---\n${out}")
endif()
if(NOT out MATCHES "pacing +paced")
  message(FATAL_ERROR "examples: with --set \"cpu0 clock_hz=2000000\", SHOW CLOCK does not say `paced`\n--- output ---\n${out}")
endif()
execute_process(
  COMMAND           "${SIM}" cpm22-buffered.toml -x "SHOW CLOCK"
  WORKING_DIRECTORY "${cpm}"
  INPUT_FILE        "${SRC}/tests/acceptance/cpm-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
if(NOT out MATCHES "pacing +free")
  message(FATAL_ERROR "examples: without --set, SHOW CLOCK does not say `free` (the --set check proves nothing)\n--- output ---\n${out}")
endif()

# cpm22-terminal.toml puts the console in the built-in windowed VT100. A build with SDL loads it
# -- under SDL's dummy video driver, so no window opens on a test machine -- and the console
# line must hold the terminal. A headless build (no SDL) must REFUSE it, with the one message
# that says why: the spec parsed, and only the missing window stopped it. Each leg asserts the
# answer its own build must give; neither passes on the other's.
mcp_session("${dist}/terminal.jsonl"
  [=[{"name":"monitor","arguments":{"command":"SHOW sio0"}}]=])
execute_process(
  COMMAND           "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy
                    "${SIM}" cpm22-terminal.toml --mcp
  WORKING_DIRECTORY "${cpm}"
  INPUT_FILE        "${dist}/terminal.jsonl"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           30
)
if(HAVE_SDL)
  expect_contains("${out}" "`altairsim cpm22-terminal.toml` did not load on a build with SDL"
                  "sio0  (2sio)" "serial  terminal?emulation=vt100&size=80x24")
else()
  expect_contains("${out}" "a headless build did not refuse cpm22-terminal.toml for its missing window"
                  "terminal: this build has no window")
endif()

# ---- 2. examples/debugger -- the walkthrough, symbols and hex loaded from beside the file. ----
#
# examples/debugger is a taught exercise: a 54-byte program, its .PRN listing and its .HEX,
# and a README that walks the monitor's debugger. This runs that walkthrough non-
# interactively, from the example's own directory, so it proves the README is true: the
# disassembly names labels and operands the way it says, and the .prn/.hex resolve beside
# the machine file rather than beside the repo.
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
        "14 symbol(s) from HELLO.PRN"   # the .PRN resolved beside debugger.toml
        "loaded 54 bytes"               # so did the .HEX
        "START:"                        # a program label heads its own line
        "CALL PUTC"                     # a 16-bit operand reads as a label
        "LXI SP,STACK"                  # ...and as an EQU-address -- the CALL BDOS case
        "IN 10"                         # a BYTE operand stays a number (a port is not an address)
        "HELLO, WORLD"                  # it ran, on the console the file wired up
        "stopped at 011A")              # and stopped at the DONE breakpoint
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

# ...and debugger.ini, the same bench built by monitor commands. -s stops at the first line
# that fails and exits with its status, so a clean exit plus the last lines' answers is the
# whole script having run.
execute_process(
  COMMAND           "${SIM}" -s examples/debugger/debugger.ini
  WORKING_DIRECTORY "${dist}"
  RESULT_VARIABLE   rc
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           30
)
expect_contains("${out}" "`altairsim -s examples/debugger/debugger.ini` did not build the bench"
                "sio0:a: connected to console" "mem0:0: ram  0000-7FFF  32K" "power cycled")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "examples: debugger.ini exited ${rc}.\n--- output ---\n${out}")
endif()

# ---- 3. examples/ai-mcp -- the machine an AI assistant debugs over --mcp. -----------------
#
# The README's first step, as the assistant takes it: boot (MCP does not run the startup, so
# `run from` FF00), assemble HELLO.ASM, LOAD it, and run it -- which prints the bug the rest
# of the README goes on to find. `ELLO, WORLD` is the proof: the source on the disk, the
# assembler and the program all ran, and the bug is still there for the reader to fix. It runs
# on the COPY: ASM and LOAD write to the disk, and the tracked image must not move.
file(COPY "${SRC}/examples/ai-mcp" DESTINATION "${dist}/examples")
set(ai "${dist}/examples/ai-mcp")

mcp_session("${dist}/ai.jsonl"
  [=[{"name":"run","arguments":{"from":65280,"until":"A>","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"input":"ASM HELLO\r","until":"END OF ASSEMBLY","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"until":"A>","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"input":"LOAD HELLO\r","until":"RECORDS WRITTEN","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"until":"A>","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"input":"HELLO\r","until":"WORLD","timeout_ms":30000}}]=])
execute_process(
  COMMAND           "${SIM}" cpm-ai.toml --mcp
  WORKING_DIRECTORY "${ai}"
  INPUT_FILE        "${dist}/ai.jsonl"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_contains("${out}" "the ai-mcp README's first step did not go as it says"
                "56K CP/M 2.2b v2.3" "END OF ASSEMBLY" "FIRST ADDRESS 0100" "ELLO, WORLD")

# ...and cpm-ai.ini, by -s from the dist root: the disk is beside the script (#575).
execute_process(
  COMMAND           "${SIM}" -s examples/ai-mcp/cpm-ai.ini
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/cpm-dir.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_cpm("${out}" "`altairsim -s examples/ai-mcp/cpm-ai.ini` from the dist root did not boot CP/M")

# ---- 4. examples/basic4k -- Altair 4K BASIC off its cassette. ------------------------------
#
# The worked-examples and tapes chapters of the manual boot this one. From its own folder, by
# path from the dist root, and its .ini twin by -s: the tape and the bootstrap are found beside
# the file that names them every time. `TAPE OK` is the output of a program typed into a BASIC
# that read itself off the .tap, which a machine that merely started cannot print.
file(COPY "${SRC}/examples/basic4k" DESTINATION "${dist}/examples")
set(basic "${dist}/examples/basic4k")

function(expect_basic out why)
  expect_contains("${out}" "${why}" "ALTAIR BASIC VERSION 3.1" "OK" "42" "TAPE OK")
endfunction()

execute_process(
  COMMAND           "${SIM}" basic4k.toml
  WORKING_DIRECTORY "${basic}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`cd examples/basic4k && altairsim basic4k.toml` did not boot BASIC")

execute_process(
  COMMAND           "${SIM}" examples/basic4k/basic4k.toml
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`altairsim examples/basic4k/basic4k.toml` from the dist root did not boot BASIC")

execute_process(
  COMMAND           "${SIM}" -s examples/basic4k/basic4k.ini
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`altairsim -s examples/basic4k/basic4k.ini` from the dist root did not boot BASIC")

# The same tape as 88-ACR audio: the tapes chapter mounts it, and basic4k-wav.toml boots it. The
# program typed into it proves the WAV decoded to the same bytes as the .tap.
execute_process(
  COMMAND           "${SIM}" basic4k-wav.toml
  WORKING_DIRECTORY "${basic}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`cd examples/basic4k && altairsim basic4k-wav.toml` did not boot BASIC")

execute_process(
  COMMAND           "${SIM}" -s examples/basic4k/basic4k-wav.ini
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`altairsim -s examples/basic4k/basic4k-wav.ini` from the dist root did not boot BASIC")

# The tapes chapter's two MOUNT transcripts, on the shipped files: the FSK tape decodes clean, and
# the Kansas City tape is refused, because a real 88-ACR cannot hear it.
execute_process(
  COMMAND           "${SIM}" basic4k
                    -x "MOUNT acr0:tape \"4K BASIC Ver 3-1.wav\""
                    -x "MOUNT acr0:tape \"4K BASIC (Kansas City).wav\""
                    -x QUIT
  WORKING_DIRECTORY "${basic}"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_contains("${out}" "the tapes chapter's WAV mounts did not print what the chapter shows"
  "4K BASIC Ver 3-1.wav: fsk300, 4439 bytes, 0 framing errors (100.0% of frames intact)"
  "4K BASIC (Kansas City).wav: this board's modem cannot hear that tape -- it carries 2400 Hz / 1200 Hz, and this board reads fsk300")

# ---- 5. examples/basic1 -- Altair BASIC 1.0, the two-step boot. ----------------------------
#
# LOAD10 copies the tape into 0000 and loops forever, so the boot is RUN 1800, ^E, RUN 0. A pipe
# cannot give that ^E at the right moment (it fires the instant the run loop reads stdin, before
# the tape is in), so the boot is driven over --mcp, where RUN 1800 only sets the PC and `run`
# stops at `timeout_ms`. The whole tape is in memory after 50 ms flat out; 3 s is the margin.
# `TAPE OK` after `RUN` is a program typed into a BASIC that read itself off the tape. (A stored
# line gets no READY in BASIC 1.0, so that `run` stops when BASIC waits for the next key.)
file(COPY "${SRC}/examples/basic1" DESTINATION "${dist}/examples")
set(basic1 "${dist}/examples/basic1")

set(basic1_boot
  [=[{"name":"run","arguments":{"timeout_ms":3000}}]=]
  [=[{"name":"run","arguments":{"from":0,"until":"MEMSIZ?","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"input":"\r","until":"SIN-COS-ATN?","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"input":"\r","until":"READY","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"input":"10 PRINT \"TAPE OK\"\r","timeout_ms":30000}}]=]
  [=[{"name":"run","arguments":{"input":"RUN\r","until":"TAPE OK","timeout_ms":30000}}]=])

function(expect_basic1 out why)
  expect_contains("${out}" "${why}" "2000 BYTES FREE" "8080 BASIC VER 1.0" "RUN\\r\\nTAPE OK")
endfunction()

# The machine files, from their own folder. MCP does not run `startup`, so the session types its
# MOUNT and LOAD (typed paths resolve beside the machine file) and puts the PC at the loader.
foreach(tape "BASIC Ver 1-0.tap" "BASIC Ver 1-0.wav")
  if(tape MATCHES "wav$")
    set(toml basic1-wav.toml)
  else()
    set(toml basic1.toml)
  endif()
  mcp_session("${dist}/basic1.jsonl"
    "{\"name\":\"monitor\",\"arguments\":{\"command\":\"MOUNT acr0:tape \\\"${tape}\\\"\"}}"
    [=[{"name":"monitor","arguments":{"command":"LOAD \"LOAD10.HEX\""}}]=]
    [=[{"name":"monitor","arguments":{"command":"RUN 1800"}}]=]
    ${basic1_boot})
  execute_process(
    COMMAND           "${SIM}" ${toml} --mcp
    WORKING_DIRECTORY "${basic1}"
    INPUT_FILE        "${dist}/basic1.jsonl"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    out
    TIMEOUT           60
  )
  expect_basic1("${out}" "`cd examples/basic1 && altairsim ${toml}` did not boot BASIC 1.0")
endforeach()

# ...and their startup lines, by path from the dist root: the tape and the bootstrap are found
# beside the file, and RUN 1800 enters the loader. The ^E that stops it is the only key.
string(ASCII 5 ctrl_e)
file(WRITE "${dist}/ctrl-e.keys" "${ctrl_e}")
foreach(toml basic1.toml basic1-wav.toml)
  execute_process(
    COMMAND           "${SIM}" examples/basic1/${toml}
    WORKING_DIRECTORY "${dist}"
    INPUT_FILE        "${dist}/ctrl-e.keys"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    out
    TIMEOUT           60
  )
  # The two paths below are joined by the program, which uses the host's separator: on Windows
  # it prints a backslash. Match one form.
  string(REPLACE "\\" "/" out "${out}")
  expect_contains("${out}" "`altairsim examples/basic1/${toml}` from the dist root did not start its loader"
    "acr0:tape: mounted examples/basic1/BASIC Ver 1-0."
    "loaded 20 bytes (1 page) from examples/basic1/LOAD10.HEX (1800-1813)"
    "^E returns to the monitor")
endforeach()

# The .ini twins, by DO from the dist root over --mcp: the script builds the machine, its paths
# resolve beside the script (#575), and its RUN 1800 leaves the PC at the loader.
foreach(ini basic1.ini basic1-wav.ini)
  mcp_session("${dist}/basic1.jsonl"
    "{\"name\":\"monitor\",\"arguments\":{\"command\":\"DO examples/basic1/${ini}\"}}"
    ${basic1_boot})
  execute_process(
    COMMAND           "${SIM}" --mcp
    WORKING_DIRECTORY "${dist}"
    INPUT_FILE        "${dist}/basic1.jsonl"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    out
    TIMEOUT           60
  )
  expect_basic1("${out}" "`DO examples/basic1/${ini}` from the dist root did not boot BASIC 1.0")
endforeach()

file(REMOVE_RECURSE "${dist}")
message(STATUS "examples: every machine file in the shipped examples works from its own folder.")
