# THE SSM PB1 ACCEPTANCE TEST: run real period EPROM-burner software against the board and
# read the burned chip back out as an Intel HEX file (issues #397, #382).
#
# This is the whole point of the board (issue #382): not "prepare a ROM image with LOAD ...
# ROM", but RUN the software a 1970s operator ran. `roms/SSM-PB1/PB1PROG.HEX` is the SSM PB1
# manual's own 2708 programmer (section 4.2), object code verbatim; it arms the board, copies
# 1K from RAM into the programming socket at D000, and disarms. We then SAVE the socket to a
# host hex file and prove two things:
#
#   1. the burned socket, read back off the bus, is byte-for-byte the source; and
#   2. SAVE turned it into a valid Intel HEX file -- the "make a hex file" of issue #382.
#
# IF THIS FAILS, THE BOARD IS WRONG, NOT THE SOFTWARE: PB1PROG is a faithful transcription of
# the manual (only its exit is a HLT instead of a jump to the SSM monitor).
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

set(work "${BIN}/pb1-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# A batch script (-s) runs non-interactively. Point the guest console at null first so RUN is
# not waiting on (closed) stdin -- the burner does no console I/O, it just runs to its HLT.
set(script "${work}/burn.cmds")
file(WRITE "${script}"
  "CONNECT sio0:a null\n"
  "LOAD ${SRC}/roms/SSM-PB1/PB1PROG.HEX\n"
  "RUN 100\n"
  "SAVE ${work}/eprom.hex D000-D3FF\n"
  "SAVE ${work}/eprom.bin D000-D3FF FORMAT=BIN\n"
  "SAVE ${work}/src.bin 4000-43FF FORMAT=BIN\n"
  "QUIT\n")

execute_process(
  COMMAND           "${SIM}" pb1 -s "${script}"
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

# 2. SAVE must have written a real Intel HEX file, and it must carry the burned bytes
#    (the source pattern is byte = low byte of address, so D000 begins 00 01 02 03 ...).
file(READ "${work}/eprom.hex" hextext)
if(NOT hextext MATCHES ":10D00000000102030405060708090A0B0C0D0E0F")
  message(FATAL_ERROR "pb1: eprom.hex is not the expected Intel HEX of the burn.\n"
                      "--- eprom.hex ---\n${hextext}\n--- transcript ---\n${out}")
endif()

file(REMOVE_RECURSE "${work}")
message(STATUS "pb1: ran the SSM 2708 burner, burned the socket, and saved it as Intel HEX "
               "(socket == source).")
