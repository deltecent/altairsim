# THE 88-2SIO + 88-ACR ACCEPTANCE TEST: boot Altair 8K BASIC off a period cassette.
#
# The sibling of basic4k.cmake, and NOT a duplicate of it. That test proves the ACR and
# the 88-SIO; this one proves the ACR and the 88-2SIO -- a different console card, a
# different chip (MC6850, not COM2502), a different port, and a different set of sense
# switches, all of them named by the bootstrap MITS shipped:
#
#     ** Set A15 on (cassette load) **
#     ** Set A11, A10 on (2SIO terminal) **
#
# The period artifacts run UNMODIFIED: `tapes/8KBasic32/8K BASIC Ver 3-2.tap` and the
# bootstrap in `tapes/8KBasic32/LDR8K32.ASM`.
#
# IF THIS FAILS, THE BOARD IS WRONG, NOT THE TAPE.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir>

# BASIC never exits -- it polls the console forever, which is what a computer does. So
# the timeout is not a failure condition here, it is the OFF SWITCH, and the verdict is
# entirely in what came out of the terminal. Long enough for a 2 MHz 8080 to read 7,168
# bytes off a 300-baud cassette and print a prompt; short enough to notice a hang.
#
# THE FIRST BYTE OF basic8k.keys IS A NUL, AND IT IS SACRIFICIAL -- exactly as in
# basic4k.keys, and for exactly the same reason. A file is not a person: every keystroke
# is already in the buffer before the machine is switched on, so the UART is holding a
# byte while BASIC is still starting up, and BASIC's cold start clears the receive
# register. It eats precisely one byte. The NUL absorbs it; delete it and every prompt
# is answered by the previous prompt's answer.
execute_process(
  COMMAND           "${SIM}" basic8k -s "${SRC}/tests/acceptance/basic8k.cmd"
  WORKING_DIRECTORY "${SRC}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic8k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)

# What has to be on the terminal, and why each one is here:
#
#   MEMORY SIZE?          the loader ran, PCHL'd into the second stage, and BASIC's cold
#                         start is alive on the 2SIO. Reaching this proves the ACR, the
#                         leader skip, AND that the sense switches sent BASIC to the
#                         right console card -- get A11/A10 wrong and it loads fine and
#                         then talks to a card that is not there, in silence.
#   TERMINAL WIDTH?
#   WANT SIN-COS-TAN-ATN? the two prompts 4K BASIC does not have. They are the cheapest
#                         possible proof that this is the EIGHT-K image and not the four.
#   ALTAIR BASIC VERSION 3.2 / [EIGHT-K VERSION]
#                         the image is INTACT. A tape that lost or duplicated one byte
#                         does not print its own banner; it crashes or prints garbage.
#   10312 BYTES FREE      the RAM scan agrees with the 16K the machine actually has, and
#                         with the 8,110 bytes of interpreter sitting under it.
#   42 / TAPE OK          and it RUNS. The interpreter that came off the cassette
#                         executes a program somebody typed at it.
#
# "MEMORY SIZE" is checked WITHOUT the trailing '?' on purpose -- see below.
set(expected
  "MEMORY SIZE"
  "TERMINAL WIDTH"
  "WANT SIN-COS-TAN-ATN"
  "ALTAIR BASIC VERSION 3.2"
  "[EIGHT-K VERSION]"
  "10312 BYTES FREE"
  " 42"
  "TAPE OK"
)

foreach(want IN LISTS expected)
  string(FIND "${out}" "${want}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR
      "88-2SIO/ACR acceptance: 8K BASIC did not reach '${want}'.\n"
      "--- terminal ---\n${out}")
  endif()
endforeach()

# strip7out, pinned as a REGRESSION and not as a nicety -- and pinned HERE as well as in
# basic4k.cmake because on THIS card the wrong fix is not even available.
#
# MITS BASIC ends a message by setting bit 7 of its LAST character: the prompt leaves
# BASIC as ...'S','I','Z','E'|0x80 = 0xC5. It is a string TERMINATOR, not data.
#
# On the 88-SIO you could at least be TEMPTED to strap the card for 7 data bits. There is
# no such strap on a 2SIO: the word format is the 6850's control register, and 8K BASIC
# programs it for 8N2 -- EIGHT data bits -- with its own hands. The chip is doing exactly
# what the guest told it to, and all eight bits go on the wire because that is what the
# guest asked for. There is nothing in the card to "fix", and a mask anywhere on the line
# would corrupt the next XMODEM transfer through the port, silently.
#
# The Teletype ignored the eighth bit -- on a Model 33 that is the parity position and the
# printer never decodes it. So `[console] strip7out = true` (machines/basic8k.toml) is the
# TERMINAL, which is the only thing in the simulator allowed to alter a byte (DESIGN.md
# 7.2). Turn it off and the prompt reads "MEMORY SIZ?" with a garbage character, and every
# prompt in the machine is corrupt. The check above would still pass, because it stops at
# "MEMORY SIZE". This one does not.
string(FIND "${out}" "MEMORY SIZE?" pos)
if(pos EQUAL -1)
  message(FATAL_ERROR
    "88-2SIO/ACR acceptance: BASIC booted, but the prompt is CORRUPT -- the high bit of "
    "the last character reached the terminal. `[console] strip7out` is off, or the "
    "console lost its transform chain (host/console.h).\n--- terminal ---\n${out}")
endif()

message(STATUS "88-2SIO/ACR acceptance: 8K BASIC v3.2 booted from tape and ran a program.")
