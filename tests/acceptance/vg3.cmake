# THE VECTOR GRAPHIC ACCEPTANCE TEST: the Monitor of each Vector 3 machine runs on the Flashwriter II,
# through the real CLI.
#
# The Monitor writes its text into the board's memory, so nothing comes back through `run`. The
# test reads the screen with DUMP, as a person does with no window. A line of the screen is 80
# bytes and DUMP prints 16 to a row, so each string below fits in one DUMP row at the place the
# Monitor puts it.
#
# What it proves, on each machine:
#   1. the ROM boots from the machine file and the banner, with that machine's Monitor version,
#      reaches the screen;
#   2. the guest polling the board's status port is seen as waiting, so `run` stops as `idle`
#      and not at its timeout;
#   3. a key sent to the board reaches the Monitor. `R` prints its name and the registers, and
#      `D` takes a whole line of keys and prints the memory it was asked for.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(work "${BIN}/vg3-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# One tools/call line. `args` is the JSON object of the tool's arguments.
set(session "")
function(call id tool args)
  set(session "${session}{\"jsonrpc\":\"2.0\",\"id\":${id},\"method\":\"tools/call\",\"params\":{\"name\":\"${tool}\",\"arguments\":${args}}}\n" PARENT_SCOPE)
endfunction()

function(fail why)
  message(FATAL_ERROR "${machine}: ${why}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

# The reply to call `id`, one JSON-RPC message per stdout line.
function(reply id var)
  string(REPLACE "\r" "" out_lf "${out}")
  string(REGEX MATCH "(^|\n){[^\n]*\"id\":${id}[,}][^\n]*" line "${out_lf}")
  set(${var} "${line}" PARENT_SCOPE)
endfunction()

# Each machine, its Monitor version, and the first bytes of its ROM.
foreach(entry "vg3vip|VERSION 4\\.1|E000 C3 18 E0 C3"
              "vg3vipt|VERSION 4\\.3|E000 C3 15 E0 C3"
              "vg3sb|VERSION 4\\.0|E000 C3 16 E0 C3")
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 machine)
  list(GET fields 1 version)
  list(GET fields 2 rom)

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
    COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy "${SIM}" "${machine}" --mcp
    WORKING_DIRECTORY "${work}"
    INPUT_FILE "${work}/session.in"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc
    TIMEOUT 90)

  if(NOT rc EQUAL 0)
    fail("`altairsim ${machine} --mcp` exited ${rc}, expected 0")
  endif()

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
  foreach(text "VECTOR GRAPHIC" "${version}" "Mon> ")
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
  foreach(text "Mon> HEX DUMP E0" "${rom}")
    if(NOT r MATCHES "${text}")
      fail("the screen after D does not hold `${text}`")
    endif()
  endforeach()


  message(STATUS "${machine}: Monitor banner, R and D on the Flashwriter II passed")
endforeach()
