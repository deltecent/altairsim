# THE JADE DOUBLE D FORMAT ACCEPTANCE TEST: Jade's own FORMAT.COM (Release 2A) makes new disks on
# the jadedd machine, and CP/M reads them after a remount.
#
# Nothing here is ours but the machine. The disk is the 56K DDBIOS2 system disk
# (tests/media/jade), and FORMAT.COM is the one on it. The board takes each track's layout from
# the Write Track stream (boards/floppy-drive.cpp), and a mount takes the layout from the file
# size (docs/boards/jade-doubled.md). So the file must follow the format, in both directions.
#
# IT DRIVES THE GUEST OVER --mcp (the pattern of disk1a.cmake): each `run` sends its keys only
# when the one before has stopped at the text it waited for. FORMAT reads a buffered line, so
# every answer ends with RETURN.
#
# What it proves:
#   1. function 1 formats a blank disk (B.DSK) double density, 489,728 bytes, and functions 4
#      and 5 copy the system tracks from drive A to it;
#   2. function 2 reformats a double-density disk (C.DSK) single density. The file falls to
#      259,328 bytes, and CP/M reads the disk after a remount. Before the fix the file stayed at
#      489,728, the remount read it as double density, and STAT gave `Bad Sector`;
#   3. function 3 formats a blank disk (D.DSK) as a 3740 disk, 256,256 bytes;
#   4. FORMAT's `*` function reformats only tracks 0 and 1 of D.DSK, and track 1 becomes double
#      density. The file grows to 259,328, and the file that PIP wrote to the data tracks
#      still reads after a remount, because the tracks after track 1 moved with it;
#   5. B.DSK boots by itself, and DIR lists the file that PIP wrote.
#
# The guest writes, so the test works on copies, never the tracked image.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(work "${BIN}/jadedd-format-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
configure_file("${SRC}/tests/media/jade/jadedd-dd-sysgen-56k.dsk" "${work}/A.DSK" COPYONLY)
configure_file("${SRC}/tests/media/jade/jadedd-dd-sysgen-56k.dsk" "${work}/C.DSK" COPYONLY)

# One tools/call line. `args` is the JSON object of the tool's arguments.
set(session "")
function(call id tool args)
  set(session "${session}{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":{\"name\":\"${tool}\",\"arguments\":${args}}}\n" PARENT_SCOPE)
endfunction()

# One monitor command.
function(mon id text)
  call(${id} monitor "{\"command\":\"${text}\"}")
  set(session "${session}" PARENT_SCOPE)
endfunction()

# Type one line and wait for `until`.
function(key id text until)
  call(${id} run "{\"input\":\"${text}\",\"until\":\"${until}\",\"timeout_ms\":60000}")
  set(session "${session}" PARENT_SCOPE)
endfunction()

# Type one command at A> and wait for the next A> at the start of a line.
function(cmd id text)
  key(${id} "${text}\\r" "\\r\\nA>")
  set(session "${session}" PARENT_SCOPE)
endfunction()

set(init "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")

# Session 1: make the disks. The BIOS on this disk is built for two drives, and FORMAT and CP/M
# both refuse C:, so every new disk goes in drive B, one image after another.
set(session "${init}")
mon(2 "MOUNT fd0:drive0 A.DSK")
mon(3 "MOUNT fd0:drive1 B.DSK CREATE")
# MCP does not run the machine file's startup, so start the boot PROM at F000 here.
call(4 run "{\"from\":61440,\"until\":\"A>\",\"timeout_ms\":20000}")
# 1. B.DSK: double density, then the system tracks from drive A.
key(5 "FORMAT\\r" "FORMATTED: ")
key(6 "B\\r" "NUMBER: ")
key(7 "1\\r" "NUMBER: ")
key(8 "4\\r" "DRIVE: ")
key(9 "A\\r" "NUMBER: ")
key(10 "5\\r" "NUMBER: ")
key(11 "\\u0003" "A>")
cmd(12 "PIP B:=STAT.COM")
cmd(13 "STAT B:*.*")
# 2. C.DSK, a double-density disk, reformatted single density.
mon(14 "UNMOUNT fd0:drive1")
mon(15 "MOUNT fd0:drive1 C.DSK")
key(16 "\\u0003" "A>")
key(17 "FORMAT\\r" "FORMATTED: ")
key(18 "B\\r" "NUMBER: ")
key(19 "2\\r" "NUMBER: ")
key(20 "\\u0003" "A>")
cmd(21 "PIP B:=STAT.COM")
# The remount reads the layout from the file size again.
mon(22 "UNMOUNT fd0:drive1")
mon(23 "MOUNT fd0:drive1 C.DSK")
key(24 "\\u0003" "A>")
cmd(25 "STAT B:*.*")
# 3. D.DSK, a blank disk, formatted 3740.
mon(26 "UNMOUNT fd0:drive1")
mon(27 "MOUNT fd0:drive1 D.DSK CREATE")
key(28 "\\u0003" "A>")
key(29 "FORMAT\\r" "FORMATTED: ")
key(30 "B\\r" "NUMBER: ")
key(31 "3\\r" "NUMBER: ")
key(32 "\\u0003" "A>")
cmd(33 "PIP B:=STAT.COM")
# 4. D.DSK again, with `*`: tracks 0 and 1 only. The file on the data tracks stays.
key(34 "FORMAT\\r" "FORMATTED: ")
key(35 "B\\r" "NUMBER: ")
key(36 "*\\r" "NUMBER: ")
key(37 "\\u0003" "A>")
mon(38 "UNMOUNT fd0:drive1")
mon(39 "MOUNT fd0:drive1 D.DSK")
key(40 "\\u0003" "A>")
cmd(41 "STAT B:*.*")
file(WRITE "${work}/make.jsonl" "${session}")

# Session 2: 5. boot the disk that step 1 made.
set(session "${init}")
mon(2 "MOUNT fd0:drive0 B.DSK")
call(3 run "{\"from\":61440,\"until\":\"A>\",\"timeout_ms\":20000}")
cmd(4 "DIR")
file(WRITE "${work}/boot.jsonl" "${session}")

function(fail why)
  message(FATAL_ERROR "jadedd-format: ${why}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

# The reply to call `id`, one JSON-RPC message per stdout line.
function(reply id var)
  string(REPLACE "\r" "" out_lf "${out}")
  string(REGEX MATCH "(^|\n){[^\n]*\"id\":${id}[,}][^\n]*" line "${out_lf}")
  set(${var} "${line}" PARENT_SCOPE)
endfunction()

# Run one session, and check that every run in it stopped because its text arrived.
macro(session_run name runs)
  execute_process(
    COMMAND           "${SIM}" jadedd --mcp
    WORKING_DIRECTORY "${work}"
    INPUT_FILE        "${work}/${name}.jsonl"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    err
    RESULT_VARIABLE   rc
    TIMEOUT           60
  )
  if(NOT rc EQUAL 0)
    fail("`altairsim jadedd --mcp` (${name}) exited ${rc}, expected 0")
  endif()
  foreach(id ${runs})
    reply(${id} r)
    if(NOT r MATCHES "\"stopped\":\"match\"")
      fail("${name}: run ${id} did not stop at the text it waited for")
    endif()
  endforeach()
  foreach(bad "FORMAT TRACK ERROR" "TRANSFER INCOMPLETE" "SYSTEM TRACKS NOT LOADED" "Bdos Err")
    if(out MATCHES "${bad}")
      fail("${name}: the guest printed `${bad}`")
    endif()
  endforeach()
endmacro()

session_run(make "4;5;6;7;8;9;10;11;12;13;16;17;18;19;20;21;24;25;28;29;30;31;32;33;34;35;36;37;40;41")

# Each file has the size of the layout it was last formatted with.
foreach(pair "B.DSK=489728" "C.DSK=259328" "D.DSK=259328")
  string(REPLACE "=" ";" pair "${pair}")
  list(GET pair 0 disk)
  list(GET pair 1 want)
  file(SIZE "${work}/${disk}" size)
  if(NOT size EQUAL want)
    fail("${disk} is ${size} bytes, expected ${want}")
  endif()
endforeach()

# STAT finds the file that PIP wrote on each disk, and the free space of each layout. Each
# disk is in drive B.
foreach(pair "13=460k" "25=235k" "41=235k")
  string(REPLACE "=" ";" pair "${pair}")
  list(GET pair 0 id)
  list(GET pair 1 free)
  reply(${id} r)
  if(NOT r MATCHES "R/W B:STAT\\.COM" OR NOT r MATCHES "Bytes Remaining On B: ${free}")
    fail("STAT B: (run ${id}) did not list STAT.COM with ${free} free")
  endif()
endforeach()

session_run(boot "3;4")
reply(3 r)
if(NOT r MATCHES "56K CP/M 2\\.2 DDBIOS2")
  fail("the new disk did not boot to the 56K CP/M 2.2 DDBIOS2 sign-on")
endif()
reply(4 r)
if(NOT r MATCHES "STAT     COM")
  fail("DIR on the new disk did not list STAT.COM")
endif()

file(REMOVE_RECURSE "${work}")
message(STATUS "jadedd-format: Jade's FORMAT made DD, SD and 3740 disks, reformatted DD to SD "
               "and two tracks of a 3740 disk, and the new DD disk booted.")
