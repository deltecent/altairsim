# THE VECTOR GRAPHIC ACCEPTANCE TEST: Monitor 4.3 runs on the FlashWriter II on the `vector`
# machine, through the real CLI.
#
# The Monitor writes its text into the board's memory, so nothing comes back through `run`. The
# test reads the screen with DUMP, as a person does with no window. A line of the screen is 80
# bytes and DUMP prints 16 to a row, so each string below fits in one DUMP row at the place the
# Monitor puts it.
#
# What it proves:
#   1. the ROM boots from the machine file's memory and the banner reaches the screen;
#   2. the guest polling the board's status port is seen as waiting, so `run` stops as `idle`
#      and not at its timeout;
#   3. a key sent to the board reaches the Monitor. `R` prints its name and the registers, and
#      `D` takes a whole line of keys and prints the memory it was asked for, then the prompt
#      comes back.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(work "${BIN}/vector-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# One tools/call line. `args` is the JSON object of the tool's arguments.
set(session "")
function(call id tool args)
  set(session "${session}{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":{\"name\":\"${tool}\",\"arguments\":${args}}}\n" PARENT_SCOPE)
endfunction()

set(session "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n")
# MCP does not run the machine file's startup, so start the Monitor at E000 here.
call(2 run "{\"from\":57344,\"timeout_ms\":20000}")
call(3 monitor "{\"command\":\"DUMP F000-F77F\"}")
call(4 run "{\"input\":\"R\",\"timeout_ms\":20000}")
call(5 monitor "{\"command\":\"DUMP F000-F77F\"}")
call(6 run "{\"input\":\"DE000E03F\\r\",\"timeout_ms\":20000}")
call(7 monitor "{\"command\":\"DUMP F000-F77F\"}")

file(WRITE "${work}/session.in" "${session}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy "${SIM}" vector --mcp
  WORKING_DIRECTORY "${work}"
  INPUT_FILE "${work}/session.in"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 90)

function(fail why)
  message(FATAL_ERROR "vector: ${why}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

if(NOT rc EQUAL 0)
  fail("`altairsim vector --mcp` exited ${rc}, expected 0")
endif()

# The reply to call `id`, one JSON-RPC message per stdout line.
function(reply id var)
  string(REPLACE "\r" "" out_lf "${out}")
  string(REGEX MATCH "(^|\n){[^\n]*\"id\":${id}[,}][^\n]*" line "${out_lf}")
  set(${var} "${line}" PARENT_SCOPE)
endfunction()

# Each run must end with the guest at a prompt. A timeout means the board did not show as
# waiting, and a guest that stopped for any other reason did not reach the prompt.
foreach(id 2 4 6)
  reply(${id} r)
  if(NOT r MATCHES "stopped: idle")
    fail("run ${id} did not stop idle at the prompt: ${r}")
  endif()
endforeach()

# 1. The banner and the prompt, before any key.
reply(3 r)
foreach(text "VECTOR GRAPHIC" "VERSION 4\\.3" "Mon> ")
  if(NOT r MATCHES "${text}")
    fail("the screen after the boot does not hold `${text}`")
  endif()
endforeach()
if(r MATCHES "REGISTERS")
  fail("the screen shows REGISTERS before the key was sent")
endif()

# 2. R. The stack pointer is the Monitor's own, and shows the register row is real.
reply(5 r)
foreach(text "Mon> REGISTERS" "FFD0")
  if(NOT r MATCHES "${text}")
    fail("the screen after R does not hold `${text}`")
  endif()
endforeach()

# 3. D. The first bytes of the ROM come back in the Monitor's own table, with the addresses
#    that were typed.
reply(7 r)
foreach(text "Mon> HEX DUMP E0" "E000 C3 15 E0 C3" "E030 0C 0C 3D 20")
  if(NOT r MATCHES "${text}")
    fail("the screen after D does not hold `${text}`")
  endif()
endforeach()

message(STATUS "vector: Monitor 4.3 banner, R and D on the FlashWriter II passed")
