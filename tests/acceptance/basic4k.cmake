# THE 88-ACR ACCEPTANCE TEST: boot Altair 4K BASIC off a period cassette.
#
# This is the test the ACR was built for, and it is not a unit test. The board tests
# (tests/test_88acr.cpp) prove the card against the manual with a MemoryMedia and no
# filesystem; this proves the whole MACHINE against an artifact from 1975 -- a real
# .TAP, and a bootstrap MITS shipped -- running UNMODIFIED. Same standing as the 8080
# validation suites, and the same reason it reads its data off disk: the ONE EXECUTABLE
# rule is about `altairsim`, and this is not it.
#
# IF THIS FAILS, THE BOARD IS WRONG, NOT THE TAPE.
#
# Driven by cmake -P so it needs no shell: execute_process gives us the keystrokes
# (INPUT_FILE) and the kill (TIMEOUT), portably.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir>

# BASIC never exits -- it polls the console forever, which is what a computer does. So
# the timeout is not a failure condition here, it is the OFF SWITCH, and the verdict is
# entirely in what came out of the terminal. Long enough for a 2 MHz 8080 to read 4,439
# bytes off a 300-baud cassette and print a prompt; short enough to notice a hang.
#
# THE FIRST BYTE OF basic4k.keys IS A NUL, AND IT IS SACRIFICIAL. Do not "tidy" it away.
#
# A file is not a person. Every keystroke is already in the buffer before the machine is
# switched on, so the SIO's receive register is holding a byte while BASIC is still
# starting up -- and BASIC's cold start clears that register, exactly as a real one does.
# It eats precisely one byte. An operator in 1975 never noticed, because an operator did
# not type until the machine asked. Feed it a bare CR and BASIC eats the CR, "MEMORY
# SIZE?" goes unanswered, and every prompt after it is answered by the wrong line.
#
# So the first byte is a NUL, which is what a period terminal actually sent as padding,
# and which BASIC ignores if it survives. It absorbs the clear, and the CRs behind it
# land on the prompts they were meant for.
execute_process(
  COMMAND           "${SIM}" basic4k -s "${SRC}/tests/acceptance/basic4k.cmd"
  WORKING_DIRECTORY "${SRC}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)

# What has to be on the terminal, and why each one is here:
#
#   MEMORY SIZE?   the loader ran, PCHL'd into 0F00, and BASIC's cold start is alive.
#                  Reaching this proves the ACR, the inverted status bit and the leader
#                  skip -- everything the card does.
#   ALTAIR BASIC VERSION 3.1 / [FOUR-K VERSION]
#                  the image is INTACT. A tape that lost or duplicated one byte does not
#                  print its own banner; it crashes or prints garbage.
#   742 BYTES FREE the RAM scan agrees with the 4K the machine actually has.
#   42 / TAPE OK   and it RUNS. The interpreter that came off the cassette executes a
#                  program somebody typed at it.
#
# "MEMORY SIZE" is checked WITHOUT the trailing '?' on purpose -- see below.
set(expected
  "MEMORY SIZE"
  "ALTAIR BASIC VERSION 3.1"
  "[FOUR-K VERSION]"
  "742 BYTES FREE"
  " 42"
  "TAPE OK"
)

foreach(want IN LISTS expected)
  string(FIND "${out}" "${want}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR
      "88-ACR acceptance: 4K BASIC did not reach '${want}'.\n"
      "--- terminal ---\n${out}")
  endif()
endforeach()

# strip7out, pinned as a REGRESSION and not as a nicety.
#
# MITS BASIC ends a message by setting bit 7 of its LAST character: the prompt leaves
# BASIC as ...'S','I','Z','E'|0x80 = 0xC5. It is a string TERMINATOR, not data. The real
# card sent all eight bits and the TELETYPE ignored the eighth -- on a Model 33 that is
# the parity position and the printer never decodes it -- so the operator read a clean
# "MEMORY SIZE?". `[console] strip7out = true` (machines/basic4k.toml) is that Teletype.
#
# THE TWO WRONG PLACES TO FIX IT, and the whole reason this check is here:
#
#   `data_bits = 7` on the card. A 7-bit strap is a FRAME -- the eighth bit does not
#   travel at all, for anybody -- and the port is not BASIC's. It is a general-purpose
#   serial port and the next thing through it is XMODEM, which is 8-bit binary.
#
#   A transform on the LINE. Same corruption, reached from the other side: a mask on
#   sio0 mangles that XMODEM transfer just as thoroughly, and silently. There is no such
#   knob any more on any card -- tests/test_sio2.cpp pins that the line is 8-bit clean.
#
# Turn strip7out off and the byte reaches the screen as 0xC5: the prompt reads
# "MEMORY SIZ?" with a garbage character and EVERY prompt in the machine is corrupt. The
# check above would still pass, because it stops at "MEMORY SIZE". This one does not.
string(FIND "${out}" "MEMORY SIZE?" pos)
if(pos EQUAL -1)
  message(FATAL_ERROR
    "88-ACR acceptance: BASIC booted, but the prompt is CORRUPT -- the high bit of the "
    "last character reached the terminal. `[console] strip7out` is off, or the console "
    "lost its transform chain (host/console.h).\n--- terminal ---\n${out}")
endif()

message(STATUS "88-ACR acceptance: 4K BASIC v3.1 booted from tape and ran a program.")
