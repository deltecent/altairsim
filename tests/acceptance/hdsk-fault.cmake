# BAD SECTORS REACH THE 88-HDSK (#972). An Altair boots CP/M 2.2 off an 88-HDSK platter whose
# machine file puts a fault on the Pack Descriptor Page, 0/0/0:
#
#   1. count=2: HDBL reads 0/0/0 once for each boot and gives LOAD ERR on a bad read. Two boots
#      fail, the third reaches A0>. FAULT then shows the fault used two times and worn off.
#   2. A hard data, id or missing fault on the directory (1/0/0), in turn: the BIOS reads the
#      sector once, and the BDOS says `Bdos Err On A: Bad Sector`. FAULT shows 1 hit.
#   3. On a copy of the disk, write faults on every sector of cylinder 1, side 0: SAVE writes
#      four directory sectors there, each write is lost and the guest is not told. After a warm
#      boot the new file is not in the directory, and those sectors of the copy are unchanged.
#
# The controller sets its own error bits (CSTAT, port A1): bit 2 for data, bit 3 for id, bit 4
# for missing. The guest's BIOS reads them, so this test proves the guest sees each fault.
#
# EACH ERROR IS DISMISSED WITH NO FAULT ON THE DISK. After a key the BDOS goes on with DIR. The
# fault is cleared first, so DIR lists the files and comes back to A0>.
#
# A RUN THAT ENDS IN LOAD ERR STOPS AT ITS TIMEOUT. HDBL loops at BFD9 after the message and
# reads no key, so the run never goes idle.
#
# THE FIXTURE MUST NOT MOVE. Sessions 1 and 2 mount it read-only; session 3 mounts a copy in the
# work folder. The fixture is hashed before and after.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(machine "${SRC}/tests/acceptance/hdsk-fault.toml")
set(dsk "${SRC}/tests/media/hdsk/HDCPM22v16-48K.DSK")
set(work "${BIN}/hdsk-fault-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
file(COPY "${dsk}" DESTINATION "${work}")
file(RENAME "${work}/HDCPM22v16-48K.DSK" "${work}/hd.dsk")

set(init "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")

# One tools/call line. `args` is the JSON object of the tool's arguments.
function(call id tool args)
  set(session "${session}{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":{\"name\":\"${tool}\",\"arguments\":${args}}}\n" PARENT_SCOPE)
endfunction()
function(monitor id command)
  call(${id} monitor "{\"command\":\"${command}\"}")
  set(session "${session}" PARENT_SCOPE)
endfunction()

function(fail why)
  message(FATAL_ERROR "hdsk-fault: ${why}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

# The reply to call `id`, one JSON-RPC message per stdout line.
function(reply id var)
  string(REPLACE "\r" "" out_lf "${out}")
  string(REGEX MATCH "(^|\n){[^\n]*\"id\":${id}[,}][^\n]*" line "${out_lf}")
  set(${var} "${line}" PARENT_SCOPE)
endfunction()

# Each run must stop for the reason the step expects.
function(stopped id why)
  reply(${id} r)
  if(NOT r MATCHES "stopped: ${why}")
    fail("run ${id} did not stop ${why}: ${r}")
  endif()
endfunction()

# The reply to call `id` must match `re`.
function(expect id re why)
  reply(${id} r)
  if(NOT r MATCHES "${re}")
    fail("${why}: ${r}")
  endif()
endfunction()

# Runs `session` through `altairsim hdsk-fault.toml --mcp`; leaves `out` and `err`.
macro(play name)
  file(WRITE "${work}/${name}.in" "${init}${session}")
  execute_process(
    COMMAND "${SIM}" "${machine}" --mcp
    WORKING_DIRECTORY "${work}"
    INPUT_FILE "${work}/${name}.in"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc
    TIMEOUT 150)
  if(NOT rc EQUAL 0)
    fail("`altairsim hdsk-fault.toml --mcp` (${name}) exited ${rc}, expected 0")
  endif()
endmacro()

file(SHA1 "${dsk}" before)

# ---- Session 1: the boot and the directory, on the read-only fixture. ----
set(session "")
call(2 run "{\"from\":64512,\"timeout_ms\":3000}")
monitor(3 "FAULT hd0:drive0")
call(4 run "{\"from\":64512,\"timeout_ms\":3000}")
call(5 run "{\"from\":64512,\"timeout_ms\":30000}")
monitor(6 "FAULT hd0:drive0")
monitor(7 "FAULT hd0:drive0 CLEAR ALL")
set(id 10)
foreach(kind data id missing)
  monitor(${id} "FAULT hd0:drive0 ADD 1/0/0 ${kind}")
  math(EXPR id "${id} + 1")
  call(${id} run "{\"input\":\"DIR\\r\",\"timeout_ms\":30000}")
  math(EXPR id "${id} + 1")
  monitor(${id} "FAULT hd0:drive0")
  math(EXPR id "${id} + 1")
  monitor(${id} "FAULT hd0:drive0 CLEAR ALL")
  math(EXPR id "${id} + 1")
  call(${id} run "{\"input\":\"\\r\",\"timeout_ms\":30000}")
  math(EXPR id "${id} + 6")
endforeach()
play(boot)

# 1. Two boots give LOAD ERR and use the count; the third reaches A0>.
stopped(2 timeout)
expect(2 "LOAD ERR" "the first boot read 0/0/0 without an error")
expect(3 "0/0/0 data count=2 +1 +1" "FAULT does not show one hit and one left after the first boot")
stopped(4 timeout)
expect(4 "LOAD ERR" "the second boot read 0/0/0 without an error")
stopped(5 idle)
expect(5 "48K CP/M 2.2b v1.6" "the third boot has no CP/M banner")
expect(5 "A0>" "the third boot did not reach A0>")
expect(6 "0/0/0 data count=2 +2 +0" "FAULT does not show the count=2 fault used twice with none left")

# 2. Each hard fault: Bad Sector after 1 hit, then A0> once the error is dismissed.
set(id 10)
foreach(kind data id missing)
  math(EXPR runId "${id} + 1")
  math(EXPR faultId "${id} + 2")
  math(EXPR againId "${id} + 4")
  stopped(${runId} idle)
  expect(${runId} "Bdos Err On A: Bad Sector"
         "DIR through a hard ${kind} fault does not end in a bad sector error")
  expect(${faultId} "1/0/0 ${kind} +1 "
         "the hard ${kind} fault was not hit once, for the BIOS's one read")
  stopped(${againId} idle)
  # HDIR.COM is the last file in the directory, on a later sector than 1/0/0.
  expect(${againId} "HDIR     COM\\\\r\\\\nA0>"
         "a key after the ${kind} error did not finish DIR and come back to A0>")
  math(EXPR id "${id} + 10")
endforeach()

# ---- Session 2: write faults, on a copy of the disk. ----
set(session "")
monitor(2 "MOUNT hd0:drive0 ${work}/hd.dsk")
call(3 run "{\"from\":64512,\"timeout_ms\":30000}")
foreach(s RANGE 23)
  math(EXPR id "10 + ${s}")
  monitor(${id} "FAULT hd0:drive0 ADD 1/0/${s} write")
endforeach()
call(40 run "{\"input\":\"SAVE 1 NEW.COM\\r\",\"timeout_ms\":30000}")
monitor(41 "FAULT hd0:drive0")
monitor(42 "FAULT hd0:drive0 CLEAR ALL")
call(43 run "{\"input\":\"\\u0003\",\"timeout_ms\":30000}")
call(44 run "{\"input\":\"DIR NEW.COM\\r\",\"timeout_ms\":30000}")
play(write)

# MOUNT clears the machine file's count=2 fault, so the copy boots at once.
expect(2 "hd0:drive0: mounted" "MOUNT of the copy failed")
stopped(3 idle)
expect(3 "A0>" "the copy did not boot to A0>")
stopped(40 idle)
reply(40 r)
if(r MATCHES "Bdos Err")
  fail("a write fault gave the guest an error; the guest is never told: ${r}")
endif()
foreach(s 1 4 11 18)
  expect(41 "1/0/${s} write +1 " "SAVE did not write directory sector 1/0/${s} through its fault")
endforeach()
stopped(44 idle)
expect(44 "DIR NEW.COM(\\\\r)+\\\\nNo file" "NEW.COM reached the directory through a write fault")

# Cylinder 1, side 0 of the copy is byte for byte the fixture's: every write there was lost.
file(READ "${work}/hd.dsk" copyDir OFFSET 12288 LIMIT 6144 HEX)
file(READ "${dsk}" fixtureDir OFFSET 12288 LIMIT 6144 HEX)
if(NOT copyDir STREQUAL fixtureDir)
  fail("a sector behind a write fault changed on the copy")
endif()

file(SHA1 "${dsk}" after)
if(NOT before STREQUAL after)
  fail("THE FIXTURE CHANGED. A fault must never touch the disk image: ${before} then ${after}")
endif()

message(STATUS "hdsk-fault: HDBL gave LOAD ERR past a count=2 fault and booted the third time, "
               "a hard data, id or missing fault on the directory gave Bdos Err On A: Bad Sector "
               "after 1 read, write faults lost SAVE's directory writes without an error, and the "
               "fixture is byte-identical")
