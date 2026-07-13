# THE MITS PROGRAMMING SYSTEM II ACCEPTANCE TEST: boot the PS2 monitor off a period
# cassette, with interrupts OFF (sense A9 up).
#
# The third of the ACR's acceptance tests, and it is not a third copy of the first two.
# basic4k proves the ACR + the 88-SIO; basic8k proves the ACR + the 88-2SIO. This one
# proves the thing NEITHER of them can: that the machine is reconstructed from the
# DOCUMENT AND THE ARTIFACT, and that when those two disagree, the artifact wins.
#
# Because they did disagree. The ReadMe says on page 1 that the monitor uses "the same
# bootstrap loader as would be used for BASIC version 3.2", and on page 2 that it uses
# "the same loader used to load 4K BASIC version 3.2" -- and those are different loaders
# with the SAME 0xAE leader byte, so nothing about the boot can tell them apart. The tape
# can: the second stage that comes off PS2-MON.TAP is assembled for page 0F (LXI SP,0FB2,
# JC 0F28). Loaded at 1FAE by the 8K loader it runs 4K above where it was linked, and the
# machine wanders off with nothing on the terminal -- which is precisely how this test
# failed the first time it was run.
#
# The period artifacts run UNMODIFIED. `tapes/MitsPS2/LDRPS2.HEX` is the toggled-in
# bootstrap, which is not an artifact -- it is 20 bytes a human entered on the switches.
#
# IF THIS FAILS, THE BOARD IS WRONG, NOT THE TAPE.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir>

# The monitor never exits -- it polls the 2SIO forever, which is what a computer does. So
# the TIMEOUT is not a failure condition, it is the OFF SWITCH, and the verdict is entirely
# in what came out of the terminal. It is also the only thing standing between a hung guest
# and a hung `ctest`: a machine that boots into the weeds (see above) prints nothing and
# spins, so bound it here AND in add_test (CMakeLists.txt), and never wait on it by hand.
execute_process(
  COMMAND           "${SIM}" ps2 -s "${SRC}/tests/acceptance/ps2.cmd"
  WORKING_DIRECTORY "${SRC}"
  INPUT_FILE        "${SRC}/tests/acceptance/ps2.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)

# THE MONITOR'S ENTIRE BANNER IS TWO SPACES AND A '?'. There is no sign-on, no version, no
# copyright: if you are waiting for one you will wait forever. So the prompt is the proof
# that ~2.5K of monitor came off the cassette intact, relocated itself into low memory, and
# found the console card the sense switches told it to look for.
string(FIND "${out}" "  ?" pos)
if(pos EQUAL -1)
  message(FATAL_ERROR
    "PS2 acceptance: the monitor never reached its prompt.\n"
    "Most likely the bootstrap: PS2-MON.TAP's second stage is linked for page 0F and must "
    "be loaded with tapes/MitsPS2/LDRPS2.HEX (lxi h,0FAEh), NOT with 8K BASIC's "
    "LDR8K32.HEX (lxi h,1FAEh). Both tapes have a 0xAE leader.\n"
    "--- terminal ---\n${out}")
endif()

# AND IT IS ALIVE, not merely booted -- and this one check carries two proofs.
#
# `OPN ABS,AC` (tests/acceptance/ps2.keys) is the ReadMe's own first command: assign the
# program-load device to the audio cassette. The monitor echoes it, parses it, and comes
# back with a SECOND prompt. That round trip exercises the console in both directions --
# 2SIO transmit for the echo, 2SIO receive for the keystrokes.
#
# It also pins strip7out as a REGRESSION, because the echo has to arrive CLEAN. THE PS2
# MONITOR TRANSMITS EVEN PARITY IN BIT 7, computed in software, on EVERY character. This is
# not MITS BASIC's high-bit-on-the-last-character-of-a-message; it is a parity bit, and it
# lands on roughly half the alphabet:
#
#       ?OPN ABS,AC     unfiltered becomes     ??PN?ABS?A?
#
# By hand: 'O' = 0x4F, five bits set, odd -> bit 7 goes up -> 0xCF, and the regex below
# misses on the very first character. 'P' = 0x50, two bits, even -> clean. CR = 0x0D, three
# bits -> 0x8D, so the newline in the middle of the regex is the second thing to break.
# That is even parity and nothing else.
#
# Which is what the ReadMe means by "set Serial Port to 7 bits plus space parity": strap the
# TERMINAL for seven data bits and it never decodes the eighth. So `[console] strip7out`
# (machines/ps2.toml) throws the bit away at the terminal, the only thing in the simulator
# allowed to alter a byte (DESIGN.md 7.2). It does NOT go on the 2SIO, whose word format is
# a register the guest writes (8N2 -- eight bits, at the guest's own request), and it does
# NOT go on the line, which must stay 8-bit clean because the monitor loads .BIN files --
# the editor, the assembler, the debugger -- through this very port.
#
# The prompt check above survives strip7out being off (spaces and '?' both have even
# parity, so they come through clean either way). This one does not.
string(REGEX MATCH "OPN ABS,AC[\r\n]+  \\?" roundtrip "${out}")
if(NOT roundtrip)
  message(FATAL_ERROR
    "PS2 acceptance: the monitor booted, but the command round trip failed -- 'OPN ABS,AC' "
    "did not echo cleanly and come back with a fresh prompt.\n"
    "Two suspects: the 2SIO receive path, or `[console] strip7out` being off, in which case "
    "the monitor's software parity bit reached the terminal and shredded the echo (look for "
    "'?PN?ABS?A?' below).\n"
    "--- terminal ---\n${out}")
endif()

# It is the ONLY safe thing to type at this prompt, incidentally. An unrecognised word is
# not an error to the PS2 monitor -- it is the NAME OF A PROGRAM, and the monitor will start
# reading the cassette looking for it and hang there until someone plays it a tape. The
# ReadMe puts that in capitals ("STOP! Do not type any commands to see what happens") and it
# means it. Do not add keystrokes to ps2.keys casually.

message(STATUS "PS2 acceptance: the MITS Programming System II monitor booted from tape (no interrupts).")
