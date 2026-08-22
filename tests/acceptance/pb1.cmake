# THE SSM PB1 ACCEPTANCE TEST: run real period EPROM-burner software against the board and
# read the burned chip back out as an Intel HEX file (issues #397, #382).
#
# This is the whole point of the board (issue #382): not "prepare a ROM image with LOAD ...
# ROM", but RUN the software a 1970s operator ran. `examples/pb1/PROG2708.HEX` is the SSM PB1
# manual's own 2708 programmer (section 4.2), object code verbatim -- it arms the board, copies
# 1K from RAM at 4000 into the programming socket at D000, and jumps back to the SSM 8080
# monitor at F021. altairsim does not ship that monitor yet, so we catch the return with a
# breakpoint at F021 (the burn is already complete by then). We then SAVE the socket to a host
# hex file and prove two things:
#
#   1. the burned socket, read back off the bus, is byte-for-byte the source in RAM; and
#   2. SAVE turned it into a valid Intel HEX file -- the "make a hex file" of issue #382.
#
# The source pattern does not matter: an erased socket is all-FF, and programming ANDs the
# byte in (FF & x == x), so the socket ends equal to whatever was at 4000 -- random fill and
# all. IF THIS FAILS, THE BOARD IS WRONG, NOT THE SOFTWARE: PROG2708 is a faithful, verbatim
# transcription of the manual.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

set(work "${BIN}/pb1-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# A batch script (-s) runs non-interactively. Point the guest console at null first so RUN is
# not waiting on (closed) stdin -- the burner does no console I/O, it just runs to F021.
set(script "${work}/burn.cmds")
file(WRITE "${script}"
  "CONNECT sio0:a null\n"
  "LOAD ${SRC}/examples/pb1/PROG2708.HEX\n"
  "BREAK F021\n"
  "RUN 100\n"
  "SAVE ${work}/eprom.hex D000-D3FF\n"
  "SAVE ${work}/eprom.bin D000-D3FF FORMAT=BIN\n"
  "SAVE ${work}/src.bin 4000-43FF FORMAT=BIN\n"
  "QUIT\n")

execute_process(
  COMMAND           "${SIM}" "${SRC}/examples/pb1/pb1.toml" -s "${script}"
  WORKING_DIRECTORY "${work}"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  RESULT_VARIABLE   rc
  TIMEOUT           60
)

if(NOT EXISTS "${work}/eprom.hex" OR NOT EXISTS "${work}/eprom.bin" OR NOT EXISTS "${work}/src.bin")
  message(FATAL_ERROR "pb1: the burn produced no output files.\n--- transcript ---\n${out}")
endif()

# 1. The burned socket, read back off the bus, must equal the source data exactly.
file(READ "${work}/eprom.bin" burned HEX)
file(READ "${work}/src.bin"   source HEX)
if(NOT burned STREQUAL source)
  message(FATAL_ERROR "pb1: the burned 2708 does not match the source data.\n"
                      "--- transcript ---\n${out}")
endif()

# 2. SAVE must have written a real Intel HEX file: a data record at D000 and the EOF record.
file(READ "${work}/eprom.hex" hextext)
if(NOT hextext MATCHES ":..D000" OR NOT hextext MATCHES ":00000001FF")
  message(FATAL_ERROR "pb1: eprom.hex is not a valid Intel HEX of the burn.\n"
                      "--- eprom.hex ---\n${hextext}\n--- transcript ---\n${out}")
endif()

file(REMOVE_RECURSE "${work}")
message(STATUS "pb1: ran the SSM 2708 burner, burned the socket, and saved it as Intel HEX "
               "(socket == source).")
