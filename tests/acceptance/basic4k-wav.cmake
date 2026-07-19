# THE WAV ACCEPTANCE TEST: boot Altair 4K BASIC off cassette AUDIO.
#
# basic4k.cmake boots the same BASIC off a .TAP -- a file of BYTES. This one boots it off
# a file of TONES, and that is the entire point: it is the same artifact, the same period
# bootstrap and the same expected banner, with the modem, the RIFF container and the
# demodulator spliced into the middle of the path.
#
# WHAT IT ACTUALLY EXERCISES, in order, and nothing mocked:
#
#     the .TAP's 4,439 bytes
#       -> modulate()          bytes to 2400/1850 Hz FSK at 300 baud
#       -> buildWav()          samples to a RIFF/WAVE file on disk
#       -> parseWav()          ...and back off disk, through the container parser
#       -> demodulate()        tones to bytes, self-calibrating, with a UART framer
#       -> AudioTapeMedia      the decoded bytes wearing a MediaFile's clothes
#       -> TapeImage           a position on a strip
#       -> TapeStream          a tape wearing a serial line's clothes
#       -> the 1602 UART       at 300 baud, inverted status bits and all
#       -> an 8080             running MITS's own 1975 bootstrap, unmodified
#
# THE ORACLE IS BASIC'S OWN BANNER, and that is what makes this worth having. A decoder
# that loses ONE byte in 4,439 does not print "742 BYTES FREE" -- it crashes, or prints
# garbage. There is no partial credit and nothing to interpret.
#
# NOTHING IS DOWNLOADED. The WAV is built here, at test time, from a file already in the
# repo. A fixture would have been 6.4 MB of binary in git and would have proved less:
# generating it means the MODULATOR is under test too, not just the demodulator.
#
# Expects: -DSIM=<altairsim> -DTAPETOOL=<altair_tapetool> -DSRC=<source dir> -DWORK=<scratch dir>

file(MAKE_DIRECTORY "${WORK}")
set(wav "${WORK}/basic4k.wav")
set(cmd "${WORK}/basic4k-wav.cmd")

# 22050 Hz, which is the low end of what an archive actually holds -- a 2400 Hz tone gets
# 9.19 samples per cycle there. Deliberately not 44100: if this passes at the coarser
# rate, the sub-sample edge interpolation is doing its job.
execute_process(
  COMMAND ${TAPETOOL} encode "${SRC}/examples/basic/4K BASIC Ver 3-1.tap" "${wav}" fsk300 22050
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE encout
  ERROR_VARIABLE  encout)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "WAV acceptance: could not modulate the tape.\n${encout}")
endif()

# The operator's session, byte for byte what basic4k.cmd does -- except that the thing
# going into the recorder is audio. MOUNT is still "press PLAY".
file(WRITE "${cmd}"
"MOUNT acr0:tape \"${wav}\"\n"
"LOAD \"examples/basic/LDR4K31.HEX\"\n"
"RUN 0\n")

# The same off-switch and the same sacrificial NUL as basic4k.cmake -- see the long note
# there. Do not "tidy" the first byte of basic4k.keys away.
execute_process(
  COMMAND           "${SIM}" basic4k -s "${cmd}"
  WORKING_DIRECTORY "${SRC}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)

# THE MOUNT MUST HAVE SAID WHAT IT DID. A demodulation that silently half-worked is the
# failure mode this whole feature has to avoid, so the narration is part of the contract
# and not a nicety: 4,439 bytes, and NOT ONE framing error.
foreach(want "fsk300" "4439 bytes" "0 framing errors")
  string(FIND "${out}" "${want}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR
      "WAV acceptance: the mount did not report '${want}'.\n--- terminal ---\n${out}")
  endif()
endforeach()

# ...and then the identical verdict basic4k.cmake reaches off the .TAP. Same list, on
# purpose: if audio and bytes ever diverge, they diverge HERE, against the same oracle.
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
      "WAV acceptance: 4K BASIC did not reach '${want}' off audio.\n"
      "--- terminal ---\n${out}")
  endif()
endforeach()

message(STATUS "WAV acceptance: 4K BASIC v3.1 booted from a cassette RECORDING and ran a program.")
