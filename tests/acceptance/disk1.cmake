# THE COMPUPRO DISK 1 ACCEPTANCE TEST: boot CompuPro CP/M 2.2N off a real 8" disk through the
# board's own boot EPROM, and read and write files on it.
#
# The disk is CompuPro's 2.2N system disk, as raw sectors and not changed (docs/sources.md).
# The boot is routine 4 of builtin:DISK1, a factory EPROM (roms/DISK1/README.md). The EPROM
# reads sectors 1-4 of track 0 (single density, 128 bytes) by DMA. The loader in them reads
# the rest of the system, and the CBIOS reads and writes the double-density tracks
# (8 x 1024) through the 765. tests/test_disk1.cpp checks the board on a hand-built machine;
# this asks whether the machine FILE does it through the real CLI.
#
# IT DRIVES THE GUEST OVER --mcp (the pattern of pb1.cmake): each `run` sends its keys only when
# the one before has stopped at the text it waited for.
#
# What it proves:
#   1. the EPROM boots the disk to the 2.2N sign-on and A>, on the Interfacer I console;
#   2. DIR reads the directory, and STAT DSK: reads the BIOS's disk parameters;
#   3. the write path: PIP copies STAT.COM, the copy runs and prints the same disk parameters,
#      and ERA removes it;
#   4. routine 6 passes C = 2, and the same disk boots to A> on the System Support 1 serial
#      channel (cpm22n-ss1.toml), with the Interfacer not connected;
#   5. FORMAT 3.1c formats a blank two-sided disk in drive B (cpm22n-format.toml, which sets
#      `media = "8ds"`), and STAT reads it as an empty 1200K drive. A blank with no `media` is
#      refused at mount (issue #777). Before #755, a blank read one-sided, and FORMAT stopped
#      with Drive Format Error;
#   6. the M-Drive/H (cpm22n-mdrive.toml): the BIOS counts 1 board and its MFORM formats M:,
#      PIP copies STAT.COM to M: and it runs from there. After a bus RESET the BIOS finds M:
#      formatted and the file still there. With `boards = 8` and POWER it counts 8 boards, and
#      M: is a new, empty 2016K drive.
#
# The guest writes, so the test works on a copy of tests/media/compupro, never the tracked
# image.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(work "${BIN}/disk1-work")
file(REMOVE_RECURSE "${work}")
file(COPY "${SRC}/tests/media/compupro" DESTINATION "${work}")
set(example "${work}/compupro")

# One tools/call line. `args` is the JSON object of the tool's arguments.
set(session "")
function(call id tool args)
  set(session "${session}{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":{\"name\":\"${tool}\",\"arguments\":${args}}}\n" PARENT_SCOPE)
endfunction()

# Type one command at A> and wait for the next A> at the start of a line.
function(cmd id text)
  call(${id} run "{\"input\":\"${text}\\r\",\"until\":\"\\r\\nA>\",\"timeout_ms\":20000}")
  set(session "${session}" PARENT_SCOPE)
endfunction()

set(session "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")
# MCP does not run the machine file's startup, so start the boot EPROM at 0000 here.
call(2 run "{\"from\":0,\"until\":\"A>\",\"timeout_ms\":20000}")
cmd(3 "DIR")
cmd(4 "STAT DSK:")
cmd(5 "PIP X.COM=STAT.COM")
cmd(6 "X DSK:")
cmd(7 "ERA X.COM")
cmd(8 "DIR X.COM")
file(WRITE "${example}/in.jsonl" "${session}")

execute_process(
  COMMAND           "${SIM}" cpm22n.toml --mcp
  WORKING_DIRECTORY "${example}"
  INPUT_FILE        "${example}/in.jsonl"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  RESULT_VARIABLE   rc
  TIMEOUT           110
)

function(fail why)
  message(FATAL_ERROR "disk1: ${why}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

if(NOT rc EQUAL 0)
  fail("`altairsim cpm22n.toml --mcp` exited ${rc}, expected 0")
endif()

# The reply to call `id`, one JSON-RPC message per stdout line.
function(reply id var)
  string(REPLACE "\r" "" out_lf "${out}")
  string(REGEX MATCH "(^|\n){[^\n]*\"id\":${id}[,}][^\n]*" line "${out_lf}")
  set(${var} "${line}" PARENT_SCOPE)
endfunction()

# Every run must have stopped because its text arrived, not at the timeout.
foreach(id 2 3 4 5 6 7 8)
  reply(${id} r)
  if(NOT r MATCHES "\"stopped\":\"match\"")
    fail("run ${id} did not come back to A>")
  endif()
endforeach()

# 1. The sign-on of the CBIOS that the EPROM loaded.
reply(2 r)
if(NOT r MATCHES "CompuPro Systems 64K CP/M 2\\.2N")
  fail("the boot did not print the 64K CP/M 2.2N sign-on")
endif()

# 2. The directory and the disk parameters.
reply(3 r)
if(NOT r MATCHES "HMXFPROM ASM" OR NOT r MATCHES "SYSGEN   COM")
  fail("DIR did not list HMXFPROM.ASM and SYSGEN.COM")
endif()
reply(4 r)
if(NOT r MATCHES "600: Kilobyte Drive  Capacity")
  fail("STAT DSK: did not report the 600K drive")
endif()

# 3. The copy that PIP wrote must run and print the same parameters, and ERA must remove it.
reply(6 r)
if(NOT r MATCHES "600: Kilobyte Drive  Capacity")
  fail("X.COM, the copy of STAT.COM that PIP wrote, did not run")
endif()
reply(8 r)
if(NOT r MATCHES "NO FILE")
  fail("ERA did not remove X.COM")
endif()

# 4. Routine 6, on the System Support 1 console. A fresh copy of the disk, so the first
# session's writes do not matter.
file(COPY "${SRC}/tests/media/compupro/CPM22N.DSK" DESTINATION "${example}")
set(session "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")
call(2 run "{\"from\":0,\"until\":\"A>\",\"timeout_ms\":20000}")
cmd(3 "DIR")
file(WRITE "${example}/in-ss1.jsonl" "${session}")
execute_process(
  COMMAND           "${SIM}" cpm22n-ss1.toml --mcp
  WORKING_DIRECTORY "${example}"
  INPUT_FILE        "${example}/in-ss1.jsonl"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  RESULT_VARIABLE   rc
  TIMEOUT           60
)
if(NOT rc EQUAL 0)
  fail("`altairsim cpm22n-ss1.toml --mcp` exited ${rc}, expected 0")
endif()
foreach(id 2 3)
  reply(${id} r)
  if(NOT r MATCHES "\"stopped\":\"match\"")
    fail("routine 6: run ${id} did not come back to A> on the System Support 1")
  endif()
endforeach()
reply(2 r)
if(NOT r MATCHES "CompuPro Systems 64K CP/M 2\\.2N")
  fail("routine 6 did not print the 2.2N sign-on on the System Support 1")
endif()
reply(3 r)
if(NOT r MATCHES "HMXFPROM ASM")
  fail("routine 6: DIR did not list the disk")
endif()

# 5. FORMAT 3.1c on a blank two-sided disk in drive B. `create` makes BLANK.DSK. FORMAT's
# prompts one at a time: the drive, the format type, the confirmation, and the exit. Each
# answer is one key with no RETURN, and FORMAT then waits for RETURN to go back to A>.
file(COPY "${SRC}/tests/media/compupro/CPM22N.DSK" DESTINATION "${example}")
set(session "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")
call(2 run "{\"from\":0,\"until\":\"A>\",\"timeout_ms\":20000}")
call(3 run "{\"input\":\"FORMAT\\r\",\"until\":\"(A: - P:)\",\"timeout_ms\":20000}")
call(4 run "{\"input\":\"B\",\"until\":\"(0-3): ==>\",\"timeout_ms\":20000}")
call(5 run "{\"input\":\"3\",\"until\":\"(Y/N).\",\"timeout_ms\":20000}")
call(6 run "{\"input\":\"Y\",\"until\":\"exit back to system.\",\"timeout_ms\":60000}")
call(7 run "{\"input\":\"N\",\"until\":\"press <return>\",\"timeout_ms\":20000}")
call(8 run "{\"input\":\"\\r\",\"until\":\"A>\",\"timeout_ms\":20000}")
cmd(9 "STAT B:")
cmd(10 "STAT B:DSK:")
file(WRITE "${example}/in-format.jsonl" "${session}")
execute_process(
  COMMAND           "${SIM}" cpm22n-format.toml --mcp
  WORKING_DIRECTORY "${example}"
  INPUT_FILE        "${example}/in-format.jsonl"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  RESULT_VARIABLE   rc
  TIMEOUT           110
)
if(NOT rc EQUAL 0)
  fail("`altairsim cpm22n-format.toml --mcp` exited ${rc}, expected 0")
endif()
# The blank read one-sided, so FORMAT's write to side 1 failed (#755).
reply(6 r)
if(r MATCHES "Drive Format Error")
  fail("FORMAT 3.1c stopped with Drive Format Error: drive B read as one-sided")
endif()
foreach(id 2 3 4 5 6 7 8 9 10)
  reply(${id} r)
  if(NOT r MATCHES "\"stopped\":\"match\"")
    fail("FORMAT: run ${id} did not stop at the text it waited for")
  endif()
endforeach()
reply(4 r)
if(NOT r MATCHES " disk: 77 tracks, NOT formatted\\.")
  fail("FORMAT did not find a blank 8\" disk in drive B")
endif()
reply(6 r)
if(NOT r MATCHES "Format complete")
  fail("FORMAT 3.1c did not format the blank two-sided disk")
endif()
# Track 0 side 0 is 26 x 128 (single density). Every other track of both sides is 8 x 1024.
file(SIZE "${example}/BLANK.DSK" size)
if(NOT size EQUAL 1256704)
  fail("the formatted disk is ${size} bytes, expected 1256704 (3328 + 153 x 8192)")
endif()
reply(9 r)
if(NOT r MATCHES "Bytes Remaining On B: 1192k")
  fail("STAT B: did not find an empty two-sided disk")
endif()
reply(10 r)
if(NOT r MATCHES "1200: Kilobyte Drive  Capacity")
  fail("STAT B:DSK: did not report the 1200K two-sided drive")
endif()

# 6. The M-Drive/H. RESET keeps its contents; POWER loses them. A fresh copy of the disk.
file(COPY "${SRC}/tests/media/compupro/CPM22N.DSK" DESTINATION "${example}")
set(session "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")
call(2 run "{\"from\":0,\"until\":\"A>\",\"timeout_ms\":20000}")
cmd(3 "PIP M:=STAT.COM")
cmd(4 "M:STAT M:DSK:")
call(5 monitor "{\"command\":\"RESET\"}")
call(6 run "{\"from\":0,\"until\":\"A>\",\"timeout_ms\":20000}")
cmd(7 "DIR M:")
call(8 monitor "{\"command\":\"SET md0 boards=8\"}")
call(9 monitor "{\"command\":\"POWER\"}")
call(10 run "{\"from\":0,\"until\":\"A>\",\"timeout_ms\":20000}")
cmd(11 "STAT M:DSK:")
cmd(12 "DIR M:")
file(WRITE "${example}/in-mdrive.jsonl" "${session}")
execute_process(
  COMMAND           "${SIM}" cpm22n-mdrive.toml --mcp
  WORKING_DIRECTORY "${example}"
  INPUT_FILE        "${example}/in-mdrive.jsonl"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  RESULT_VARIABLE   rc
  TIMEOUT           60
)
if(NOT rc EQUAL 0)
  fail("`altairsim cpm22n-mdrive.toml --mcp` exited ${rc}, expected 0")
endif()
foreach(id 2 3 4 6 7 10 11 12)
  reply(${id} r)
  if(NOT r MATCHES "\"stopped\":\"match\"")
    fail("M-Drive/H: run ${id} did not come back to A>")
  endif()
endforeach()
reply(2 r)
if(NOT r MATCHES "1 M-Drive/H" OR NOT r MATCHES "FORMATTING MEMORY DRIVE")
  fail("the BIOS did not count 1 M-Drive/H and format M:")
endif()
reply(4 r)
if(NOT r MATCHES "M: Drive Characteristics" OR NOT r MATCHES "508: Kilobyte Drive  Capacity")
  fail("STAT.COM did not run from M: and report the 508K drive")
endif()
reply(6 r)
if(NOT r MATCHES "1 M-Drive/H" OR r MATCHES "FORMATTING")
  fail("after RESET the BIOS formatted M: again: the board lost its contents")
endif()
reply(7 r)
if(NOT r MATCHES "M: STAT     COM")
  fail("after RESET, DIR M: did not list STAT.COM")
endif()
reply(10 r)
if(NOT r MATCHES "8 M-Drive/H" OR NOT r MATCHES "FORMATTING MEMORY DRIVE")
  fail("with boards = 8 and POWER, the BIOS did not count 8 boards and format M:")
endif()
reply(11 r)
if(NOT r MATCHES "2016: Kilobyte Drive  Capacity")
  fail("STAT M:DSK: did not report the 2016K drive of 8 boards")
endif()
reply(12 r)
if(NOT r MATCHES "NO FILE")
  fail("after POWER, M: was not empty")
endif()

file(REMOVE_RECURSE "${work}")
message(STATUS "disk1: the boot EPROM loaded CompuPro CP/M 2.2N; DIR, STAT, and a PIP "
               "copy that ran and was erased. Routine 6 booted it on the System Support 1. "
               "FORMAT 3.1c formatted a blank two-sided disk. The M-Drive/H kept M: over a "
               "RESET and counted 8 boards.")
