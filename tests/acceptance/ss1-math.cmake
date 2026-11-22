# THE SYSTEM SUPPORT 1 MATH-CHIP ACCEPTANCE TEST: the SS-1 manual's own 9511A test program
# (pp.70-72), assembled and run under CompuPro CP/M 2.2N on the SS-1 console.
#
# ss1math.asm is the program as the manual prints its source, the MP9511 path, with the two
# print faults fixed (the CR and LF EQUs are swapped, and `LXI HCTBL2` is `LXI H,TBL2`). The
# manual's printed object code is hand-made and wrong (reference/AMD 9511A.md sec. 8), so the
# test assembles the source with the disk's own ASM. The file has no comments: a `;` would
# split the CMake list of its lines.
#
# The program pushes 3000H and 4000H, issues SADD, and pops the sum without polling status:
# PAUSE holds its first IN until the command is done. It prints OK when it reads 70H then 00H,
# ERROR when it does not.
#
# What it proves:
#   1. with `math = 9511a`, the program prints OK -- push order, SADD, pop order and the
#      PAUSE wait all line up through the real CPU, bus and board;
#   2. with the socket empty (`math = none`, the stock board) the same program prints ERROR,
#      as it would on a board with no chip: the ports float.
#
# IT DRIVES THE GUEST OVER --mcp (the pattern of disk1.cmake). PIP copies CON: to T.ASM one
# line at a time; ^Z ends the file.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(work "${BIN}/ss1-math-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
file(COPY "${SRC}/tests/media/compupro/cpm22n-ss1.toml" "${SRC}/tests/media/compupro/disk1.toml"
          "${SRC}/tests/media/compupro/CPM22N.DSK"
     DESTINATION "${work}")

set(session "")
function(call id tool args)
  set(session "${session}{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":{\"name\":\"${tool}\",\"arguments\":${args}}}\n" PARENT_SCOPE)
endfunction()

# Type one command at A> and wait for `until`.
function(cmd id text until)
  call(${id} run "{\"input\":\"${text}\\r\",\"until\":\"${until}\",\"timeout_ms\":30000}")
  set(session "${session}" PARENT_SCOPE)
endfunction()

set(session "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")
call(2 monitor "{\"command\":\"SET ss1 math=9511a\"}")
# MCP does not run the machine file's startup, so start the boot EPROM at 0000 here.
call(3 run "{\"from\":0,\"until\":\"A>\",\"timeout_ms\":20000}")

# PIP T.ASM=CON:, then each source line with CR LF (PIP keeps what it is given), then ^Z.
# A line has nothing to wait for, so each run stops when the guest goes idle for input.
call(4 run "{\"input\":\"PIP T.ASM=CON:\\r\",\"timeout_ms\":2000}")
file(STRINGS "${SRC}/tests/acceptance/ss1math.asm" lines)
set(id 100)
foreach(line IN LISTS lines)
  string(REPLACE "\t" " " line "${line}")
  call(${id} run "{\"input\":\"${line}\\r\\n\",\"timeout_ms\":2000}")
  math(EXPR id "${id} + 1")
endforeach()
call(5 run "{\"input\":\"\\u001a\",\"until\":\"\\r\\nA>\",\"timeout_ms\":20000}")

cmd(6 "ASM T" "END OF ASSEMBLY")
call(7 run "{\"until\":\"\\r\\nA>\",\"timeout_ms\":20000}")
cmd(8 "LOAD T" "RECORDS WRITTEN")
call(9 run "{\"until\":\"\\r\\nA>\",\"timeout_ms\":20000}")
cmd(10 "T" "\\r\\nOK")
call(11 run "{\"until\":\"\\r\\nA>\",\"timeout_ms\":20000}")

# 2. Pull the chip: the same program must fail.
call(12 monitor "{\"command\":\"SET ss1 math=none\"}")
cmd(13 "T" "\\r\\nERROR")
file(WRITE "${work}/in.jsonl" "${session}")

execute_process(
  COMMAND           "${SIM}" cpm22n-ss1.toml --mcp
  WORKING_DIRECTORY "${work}"
  INPUT_FILE        "${work}/in.jsonl"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  RESULT_VARIABLE   rc
  TIMEOUT           150
)

function(fail why)
  message(FATAL_ERROR "ss1-math: ${why}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

if(NOT rc EQUAL 0)
  fail("`altairsim cpm22n-ss1.toml --mcp` exited ${rc}, expected 0")
endif()

function(reply id var)
  string(REPLACE "\r" "" out_lf "${out}")
  string(REGEX MATCH "(^|\n){[^\n]*\"id\":${id}[,}][^\n]*" line "${out_lf}")
  set(${var} "${line}" PARENT_SCOPE)
endfunction()

foreach(id 3 5 6 7 8 9 10 11 13)
  reply(${id} r)
  if(NOT r MATCHES "\"stopped\":\"match\"")
    fail("run ${id} did not stop at the text it waited for")
  endif()
endforeach()

reply(6 r)
if(NOT r MATCHES "018B")
  fail("ASM did not assemble the test program to its end at 018B")
endif()
reply(10 r)
if(NOT r MATCHES "9511 TEST BEGUN" OR r MATCHES "ERROR")
  fail("with a 9511A fitted, the manual's test program did not print OK")
endif()
reply(13 r)
if(NOT r MATCHES "ERROR")
  fail("with the socket empty, the test program did not print ERROR")
endif()
