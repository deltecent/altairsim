# BAD SECTORS REACH THE VECTOR GRAPHIC DUAL-MODE BOARD (#971). A Vector 3 boots Vector Graphic
# 56K CP/M 2.22 H off a disk whose machine file puts a fault on track 0:
#
#   1. count=2: sector 0/0/1 reads bad two times. The Monitor's boot reads it again, and CP/M
#      reaches A>. FAULT then shows the fault used two times and worn off.
#   2. A hard data fault on the directory (4/0/0): the BIOS reads the sector 72 times, and the
#      BDOS says `Bdos Err On A: Bad Sector`. FAULT shows the 72 hits.
#   3. A hard id fault there instead: the track byte of the header is wrong. The same error and
#      the same 72 hits.
#   4. A hard missing fault there instead: the slot has no sync byte, so the board leaves RAM as
#      it was. The same error and the same 72 hits.
#
# The guest's own code finds every fault: the board never checks the checksum or the header. So
# this test proves the BIOS sees the fault, not only the board.
#
# THE GUEST PRINTS ON THE FLASHWRITER II, so nothing comes back through `run`. The test reads the
# screen with DUMP, as vg3.cmake does, and turns the bytes into text.
#
# THE BOOT IS RUN TO AN ADDRESS, NOT TO IDLE. At cold boot the BIOS times the processor against
# the Flashwriter's vertical blanking, a loop that polls the keyboard status port. `run` sees
# that as a guest waiting at a prompt and stops (#976). So the boot runs with the idle stop off, to the
# BIOS's console input entry, D486 (the CONIN jump at D309). After A> the idle stop is on again:
# each DIR then stops at the error, where the BDOS waits for a key.
#
# EACH ERROR IS DISMISSED WITH NO FAULT ON THE DISK. After a key the BDOS goes on with DIR. The
# fault is cleared first, so DIR lists the files and comes back to A>.
#
# THE DISK MUST NOT MOVE. A fault is an overlay on the drive; the image never changes. The
# fixture is write-protected too, and hashed before and after.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(machine "${SRC}/tests/acceptance/vgdmdc-fault.toml")
set(dsk "${SRC}/tests/media/vector/VGBOOT.VGI")
set(work "${BIN}/vgdmdc-fault-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(conin 54406)   # D486, the BIOS's console input

# One tools/call line. `args` is the JSON object of the tool's arguments.
set(session "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")
function(call id tool args)
  set(session "${session}{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":{\"name\":\"${tool}\",\"arguments\":${args}}}\n" PARENT_SCOPE)
endfunction()
function(monitor id command)
  call(${id} monitor "{\"command\":\"${command}\"}")
  set(session "${session}" PARENT_SCOPE)
endfunction()

function(fail why)
  message(FATAL_ERROR "vgdmdc-fault: ${why}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

# The reply to call `id`, one JSON-RPC message per stdout line.
function(reply id var)
  string(REPLACE "\r" "" out_lf "${out}")
  string(REGEX MATCH "(^|\n){[^\n]*\"id\":${id}[,}][^\n]*" line "${out_lf}")
  set(${var} "${line}" PARENT_SCOPE)
endfunction()

# The screen in the reply to DUMP call `id`, as one string: 24 lines of 80 characters, end to
# end. Bit 7 (the cursor) is dropped; a byte that is not a letter, a digit or common punctuation
# reads as a dot.
function(screen id var)
  reply(${id} r)
  # CMake's regex has no {n}: eight bytes, two spaces, eight bytes, written out.
  set(hex2 "[0-9A-F][0-9A-F]")
  set(eight "${hex2} ${hex2} ${hex2} ${hex2} ${hex2} ${hex2} ${hex2} ${hex2}")
  string(REGEX MATCHALL "F[0-7][0-9A-F]0  ${eight}  ${eight}" rows "${r}")
  set(text "")
  foreach(row IN LISTS rows)
    string(SUBSTRING "${row}" 6 -1 bytes)
    string(REGEX MATCHALL "${hex2}" bytes "${bytes}")
    foreach(b IN LISTS bytes)
      math(EXPR v "0x${b} & 127")
      string(ASCII ${v} c)
      if(NOT c MATCHES "[A-Za-z0-9 :>|.,()/*+=-]")
        set(c ".")
      endif()
      string(APPEND text "${c}")
    endforeach()
  endforeach()
  string(LENGTH "${text}" n)
  if(NOT n EQUAL 1920)
    fail("DUMP ${id} gave ${n} screen bytes, expected 1920")
  endif()
  set(${var} "${text}" PARENT_SCOPE)
endfunction()

# Each run must stop for the reason the step expects.
function(stopped id why)
  reply(${id} r)
  if(NOT r MATCHES "stopped: ${why}")
    fail("run ${id} did not stop ${why}: ${r}")
  endif()
endfunction()

# 1. The boot, past two bad reads of 0/0/1. The Monitor stops idle at Mon>; B then runs to A>.
call(2 run "{\"from\":0,\"timeout_ms\":20000}")
monitor(3 "SET cpu0 IDLE=false")
call(4 run "{\"input\":\"B\",\"stop_at\":${conin},\"timeout_ms\":60000}")
monitor(5 "SET cpu0 IDLE=true")
monitor(6 "FAULT dm0:drive1")
monitor(7 "DUMP F000-F77F")

# 2-4. One hard fault on the directory, DIR, the hit count and the screen; the fault cleared,
# and a key to go on to A>.
set(id 10)
foreach(kind data id missing)
  monitor(${id} "FAULT dm0:drive1 ADD 4/0/0 ${kind}")
  math(EXPR id "${id} + 1")
  call(${id} run "{\"input\":\"DIR\\r\",\"timeout_ms\":60000}")
  math(EXPR id "${id} + 1")
  monitor(${id} "FAULT dm0:drive1")
  math(EXPR id "${id} + 1")
  monitor(${id} "DUMP F000-F77F")
  math(EXPR id "${id} + 1")
  monitor(${id} "FAULT dm0:drive1 CLEAR ALL")
  math(EXPR id "${id} + 1")
  call(${id} run "{\"input\":\"\\r\",\"timeout_ms\":60000}")
  math(EXPR id "${id} + 1")
  monitor(${id} "DUMP F000-F77F")
  math(EXPR id "${id} + 4")
endforeach()

file(SHA1 "${dsk}" before)
file(WRITE "${work}/session.in" "${session}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy "${SIM}" "${machine}" --mcp
  WORKING_DIRECTORY "${work}"
  INPUT_FILE "${work}/session.in"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 150)
file(SHA1 "${dsk}" after)

if(NOT rc EQUAL 0)
  fail("`altairsim vgdmdc-fault.toml --mcp` exited ${rc}, expected 0")
endif()

# 1. The boot reached the BIOS's console input, with the banner and A> on the screen.
stopped(2 idle)
stopped(4 stop-at)
reply(6 r)
if(NOT r MATCHES "0/0/1 data count=2 +2 +0")
  fail("FAULT does not show the count=2 fault used twice with none left: ${r}")
endif()
screen(7 s)
if(NOT s MATCHES "VECTOR GRAPHIC 56K CP/M - VERSION 2.22 H")
  fail("the screen after the boot has no CP/M banner: ${s}")
endif()
if(NOT s MATCHES "A> *$")
  fail("the screen after the boot does not end at A>: ${s}")
endif()

# 2-4. Each hard fault: Bad Sector after 72 hits, then A> once the error is dismissed.
set(id 10)
foreach(kind data id missing)
  math(EXPR runId "${id} + 1")
  math(EXPR faultId "${id} + 2")
  math(EXPR dumpId "${id} + 3")
  math(EXPR againId "${id} + 5")
  math(EXPR promptId "${id} + 6")
  stopped(${runId} idle)
  reply(${faultId} r)
  if(NOT r MATCHES "4/0/0 ${kind} +72 ")
    fail("the hard ${kind} fault was not hit once for each of the BIOS's 72 reads: ${r}")
  endif()
  screen(${dumpId} s)
  if(NOT s MATCHES "Bdos Err On A: Bad Sector *$")
    fail("DIR through a hard ${kind} fault does not end in a bad sector error: ${s}")
  endif()
  stopped(${againId} idle)
  # CPT.COM is the last file in the directory. After a missing fault the board left RAM as it
  # was, so the BDOS listed the sector read before it: the order of the files changes, the last
  # one does not.
  screen(${promptId} s)
  if(NOT s MATCHES "CPT      COM *A> *$")
    fail("a key after the ${kind} error did not finish DIR and come back to A>: ${s}")
  endif()
  math(EXPR id "${id} + 10")
endforeach()

if(NOT before STREQUAL after)
  fail("THE IMAGE CHANGED. A fault must never touch the disk image: ${before} then ${after}")
endif()

message(STATUS "vgdmdc-fault: the Monitor read again past a count=2 fault to A>, and a hard data, "
               "id or missing fault on the directory gave Bdos Err On A: Bad Sector after 72 reads, "
               "and the disk image is byte-identical")
