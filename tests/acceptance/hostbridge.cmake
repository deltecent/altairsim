# THE HOST BRIDGE ACCEPTANCE TEST: a file crosses between the host and CP/M, and comes
# back byte for byte.
#
# TWO TESTS, ONE SCRIPT. -DMODE picks which:
#
#   MODE=fast    Boot CP/M, LOAD the utilities from the .HEX that is CHECKED IN, and move
#                files. This is the test of THE CARD, and it is the one that runs on every
#                edit. A few seconds.
#
#   MODE=build   Assemble R.ASM, W.ASM and HDIR.ASM FROM SOURCE inside the machine, and
#                prove that what comes out is bit-for-bit what is committed beside them.
#                This is the test of THE SOURCES, and it only has anything to say when a
#                .ASM changes. Labelled `slow`, and it also carries the minidisk phase.
#
# WHY THE SPLIT, in one number: `PIP FOO.ASM=CON:` is the only slow thing here. The three
# sources are 78 KB, PIP ECHOES every byte it is fed, and a 2SIO is a real serial line --
# so the bootstrap is ~156 KB of wire time and nothing else. R and W themselves never touch
# the console at all: they read the disk and OUT to the bridge, so a whole transfer costs
# about as much as one prompt. Rebuilding the assembler on every edit to test a card that
# does not need it was two minutes for a one-second answer.
#
# Between them the two modes pin the whole chain: .ASM -> .HEX -> .COM. `build` proves the
# source still assembles to the committed hex and binary; `fast` proves the committed hex
# still LOADs to the committed binary, and that the binary works.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir> [-DMODE=fast|build]

if(NOT DEFINED MODE)
  set(MODE fast)
endif()

string(ASCII 13 CR)                     # a CCP command line ends in CR AND NOTHING ELSE
string(ASCII 26 SUB)                    # ^Z -- what ends a PIP from the console

# A CP/M TEXT FILE IS CRLF -- BUT WE DO NOT GET TO WRITE THE CR OURSELVES ON WINDOWS.
#
# `file(WRITE)` opens in TEXT MODE, so on Windows every LF it writes becomes CRLF. Asking
# for "${CR}\n" there therefore lands on disk as CR CR LF, and every line of every file we
# PIP into the guest is doubled. The commands still ran -- a bare CCP line has no LF after
# its CR, so nothing translated it -- and only the PIP'd SOURCE rotted, which is why this
# surfaced as ASM cheerfully printing END OF ASSEMBLY over a HEX that LOAD then rejected.
#
# So: ask for the bytes we want to END UP WITH, and let each platform's file(WRITE) get
# there its own way. There is no portable escape in script mode -- string(ASCII 10) is
# translated identically, and file(GENERATE) needs project context, which -P does not have.
#
# Found on Windows by the PR #12 review; the line dates to db35fa7 and had never run there,
# because the 8 MB image it needs is gitignored.
if(WIN32)
  set(CRLF "\n")                        # file(WRITE) supplies the CR for us
else()
  set(CRLF "${CR}\n")
endif()

set(work "${BIN}/hostbridge-${MODE}")   # per-mode: the two must never share a scratch dir
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/host/SRC")

# ---------------------------------------------------------------------------
# THE HOST SIDE, STAGED.
#
# hostdir is a scratch directory, and NOT the working directory -- which is the whole point
# of the `hostdir` property. The default (the shell's CWD) is what a user gets and what the
# machine file documents; a test that used it would be writing its fixtures into the repo.
# ---------------------------------------------------------------------------
file(WRITE "${work}/host/HELLO.TXT" "HELLO FROM THE HOST.${CRLF}LINE TWO.${CRLF}")
file(WRITE "${work}/host/SRC/DEEP.TXT" "IN A SUBDIRECTORY OF HOSTDIR.${CRLF}")

# THE BINARY ONE, AND IT IS THE ONE THAT MATTERS: 256 bytes, every value from 00 to FF. It
# is a committed fixture and not generated here because CMake cannot write a NUL.
#
# It contains 00 (which would end a C string), 1A (CP/M's ^Z, which a text-mode transfer
# truncates at), and FF (what an undecoded port reads as). A transfer that mangles any of
# those three has a bug a text file would never find.
file(COPY "${SRC}/tests/acceptance/hostbridge.bin" DESTINATION "${work}/host")
file(RENAME "${work}/host/hostbridge.bin" "${work}/host/BIN.DAT")

# ---------------------------------------------------------------------------
# BUILDING THE KEYSTROKES. Three traps live in here, all of them paid for.
# ---------------------------------------------------------------------------

# 1. EVERY WARM BOOT EATS ONE BUFFERED CHARACTER. The BIOS re-initialises the 2SIO on its
#    way back to the CCP, and the 6850's master reset clears the receive register -- so the
#    byte sitting in it when a program exits is gone. A human never notices, because a human
#    is not typing at that instant. A pipe always is.
#
#    So a bare CR goes in front of every command: a LIGHTNING ROD. It gets eaten, and the
#    command behind it survives. (Feed the command directly and `LOAD R` arrives as `OAD`.)
set(SAC "${CR}")

# 2. A CCP COMMAND LINE ENDS IN CR AND NOTHING ELSE -- no LF. The CCP reads to the CR and
#    stops, and a trailing LF would be left in the buffer for the NEXT reader. That reader
#    is PIP, and the LF becomes the first byte of the file it writes.
macro(cmd line)
  set(keys "${keys}${SAC}${line}${CR}")
endmacro()

# 3. ...but the FILE we feed PIP is CP/M text, so it IS CRLF. The two rules are not in
#    conflict: one is a command line and the other is a file. Confusing them cost an
#    afternoon and produced an assembler listing whose first line was corrupt.
macro(pip file into)
  file(READ "${file}" body)
  string(REPLACE "\r" "" body "${body}")
  string(REPLACE "\n" "${CRLF}" body "${body}")
  cmd("PIP ${into}=CON:")
  set(keys "${keys}${body}${SUB}")
endmacro()

set(keys "")

if(MODE STREQUAL build)
  # FROM SOURCE. 78 KB through the console, and this is the whole cost of this mode.
  foreach(u R W HDIR)
    pip("${SRC}/cpm/hostbridge/${u}.ASM" "${u}.ASM")
    cmd("ASM ${u}")                     # -> .HEX and .PRN
    cmd("LOAD ${u}")                    # -> .COM
  endforeach()
else()
  # ERASE THE UTILITIES THE DISK ARRIVES WITH, BEFORE BUILDING THEM AGAIN.
  #
  # The tracked image ships R/W/HDIR.COM already installed -- that is what makes it not
  # byte-identical to upstream (tools/install-hostbridge-utils.sh). Leaving them there
  # would MASK THE FAILURE THIS MODE EXISTS TO CATCH: if LOAD stopped producing a working
  # .COM, the pre-installed one would run instead and every check below would pass while
  # testing the wrong binary. Erasing first means the program that runs can only be the
  # one we just built out of the committed .HEX.
  #
  # It also buys back the ~5K they occupy, and on an 18K disk that is not spare change.
  foreach(u R W HDIR)
    cmd("ERA ${u}.COM")
  endforeach()

  # FROM THE COMMITTED HEX. Six times less wire, no assembler -- and LOADing the hex that
  # is checked in is what makes the .COM beside it provable rather than merely present.
  #
  # EACH .HEX IS ERASED THE MOMENT LOAD HAS EATEN IT, and on this disk that is not
  # housekeeping. 12.5 KB of .HEX against 18K free left the disk with EXACTLY 0K remaining
  # at the end of the run -- passing, but one file away from failing, and failing as a
  # silently truncated PIP that LOADs into a subtly wrong .COM. Freeing each .HEX as we
  # finish with it holds the peak down and leaves ~12K spare instead of nothing.
  foreach(u R W HDIR)
    pip("${SRC}/cpm/hostbridge/${u}.HEX" "${u}.HEX")
    cmd("LOAD ${u}")
    cmd("ERA ${u}.HEX")
  endforeach()
endif()

# ---- and now the part both modes care about: does the card move files ----
cmd("HDIR")                             # what is on the host, before we touch it
cmd("R HELLO.TXT")                      # text, host -> CP/M
cmd("R BIN.DAT")                        # ...and the binary. 256 bytes = 2 whole records
cmd("R SRC/DEEP.TXT")                   # out of a subdirectory: the CP/M name drops SRC/
cmd("W BIN.DAT BACKBIN.DAT")            # THE ROUND TRIP. B (the default) = every byte
cmd("W HELLO.TXT TRIM.TXT T")           # T = stop at the first ^Z, so no padding comes back
cmd("R ../SECRET.TXT")                  # THE SANDBOX, through the whole stack
cmd("HDIR *.TXT")

# The .COM of each, byte for byte, to hold up against what is checked in. In `fast` mode
# this proves the committed .HEX still LOADs to the committed .COM; in `build` mode it
# proves the committed .ASM still assembles to it.
foreach(u R W HDIR)
  cmd("W ${u}.COM")
endforeach()

if(MODE STREQUAL build)
  # ...and in build mode, the OTHER two artifacts as well, so all nine are pinned. These
  # are text, so T trims the ^Z padding CP/M records carry -- a .PRN full of ^Z would be a
  # poor thing to keep in a diff.
  foreach(u R W HDIR)
    cmd("W ${u}.HEX ${u}.HEX T")
    cmd("W ${u}.PRN ${u}.PRN T")
  endforeach()
endif()

# The CCP prints its prompt and then waits. Ending the input there is how a scripted run
# stops -- but CP/M's DIR, TYPE and PIP all ABORT ON A PENDING KEYSTROKE, so anything still
# in the buffer truncates the output of the command in front of it. Nothing above prints
# more than a line or two, and R/W/HDIR are ours and do not poll. The trailing CRs are empty
# commands, and they exist only to keep the run alive to the end.
foreach(i RANGE 8)
  set(keys "${keys}${CR}")
endforeach()

file(WRITE "${work}/dcdd.keys" "${keys}")

# ---------------------------------------------------------------------------
# THE MACHINE -- an 88-DCDD running 56K CP/M 2.2, on a DIFFERENT DISK PER MODE.
#
#   fast   the TRACKED 8" floppy, which is in git. This test therefore runs on a fresh
#          clone and in CI instead of skipping silently, which is the entire point of
#          tracking the image. It arrives with 18K free and this mode needs about 14 of
#          them: 12.5 KB of .HEX, plus the three small files we move in. Tight, and
#          deliberately so -- see the ERA below.
#
#   build  the 8 MB image, because PIPing R/W/HDIR.ASM in needs 78 KB of free disk. It is
#          not in git, so CMakeLists.txt gates this mode on finding it.
#
# NEVER MUTATE A REPO IMAGE: both are copied to scratch first. For the tracked one that is
# not merely tidy -- dcdd-readonly.exp and ddt.exp shasum that file and fail if it moves.
# ---------------------------------------------------------------------------
if(MODE STREQUAL build)
  set(image "${SRC}/disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK")
else()
  set(image "${SRC}/disks/mits-88dcdd/cpm22/buffered/cpm22b23-56k.dsk")
endif()
configure_file("${image}" "${work}/work.dsk" COPYONLY)

# NO `SET ... BAUD` HERE, AND RESIST THE URGE.
#
# The console is a 9600-baud line and this test lives with it, which is why `fast` PIPs the
# 12 KB of .HEX and only `build` PIPs the 78 KB of source.
#
# An earlier draft turned the line up to 76800 to make the bootstrap bearable, and it was
# chasing the wrong thing: at the time the test took 17 seconds and only ~2 of them were the
# wire. The other 15 were the disk -- HostFile::sync() rewrote the entire 8 MB image after
# every 128-byte sector (src/host/media.h). Fix that and the same test is 1.3 seconds with
# the line untouched. Turn the baud up here and you will hide the next such bug just as
# effectively.
file(WRITE "${work}/dcdd.cmd"
  "SET CONSOLE UPPER=OFF\n"                     # the sources have lower case IN STRINGS
  "SET hb0 HOSTDIR=${work}/host\n"
  "MOUNT dsk0:drive0 \"${work}/work.dsk\"\n"
  "RUN FF00\n")

# EVERY RUN IS ON A LEASH, AND THE LEASH IS SHORT.
#
# `fast` takes about 1.5 seconds and `build` about 30. These numbers are 20x that, which
# is enough slack for a loaded machine and nowhere near enough to sit there for an hour.
#
# A HANG MUST BE REPORTED AS A HANG. Without RESULT_VARIABLE, a timed-out run just returns
# whatever output it managed before the axe fell, and the checks below then fail with
# "'R: HELLO.TXT' never reached the terminal" -- which sends you looking at the card when
# the truth is that the machine never got there at all.
set(leash 30)
if(MODE STREQUAL build)
  set(leash 180)
endif()

execute_process(
  COMMAND           "${SIM}" -s "${work}/dcdd.cmd"
  WORKING_DIRECTORY "${SRC}"
  INPUT_FILE        "${work}/dcdd.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           ${leash}
  RESULT_VARIABLE   rc
)
file(WRITE "${work}/dcdd.log" "${out}")

if(rc STREQUAL "Process terminated due to timeout")
  message(FATAL_ERROR
    "hostbridge(${MODE}): THE MACHINE HUNG -- killed after ${leash}s.\n"
    "  It did not finish the keystroke file. Nothing below was tested.\n"
    "  transcript: ${work}/dcdd.log\n--- tail ---\n${out}")
endif()

function(want text why)
  string(FIND "${out}" "${text}" hit)
  if(hit LESS 0)
    message(FATAL_ERROR "hostbridge(${MODE}): ${why}\n"
                        "  '${text}' never reached the terminal.\n"
                        "  full transcript: ${work}/dcdd.log\n--- tail ---\n${out}")
  endif()
endfunction()

if(MODE STREQUAL build)
  # If a .ASM has drifted, this is where it shows -- and an ASM.COM error line is easy to
  # miss in 90 KB of listing, so we do not look for one. We look for the thing that only a
  # CLEAN assembly produces.
  want("END OF ASSEMBLY" "ASM.COM did not finish -- the sources did not assemble")
endif()

# EVERYTHING ABOVE THE LAST `LOAD` IS ECHO, NOT OUTPUT.
#
# `PIP FOO=CON:` echoes every character it is fed, so most of this transcript is the file we
# poured in -- and in build mode that file is the SOURCE, INCLUDING the text of every message
# the programs can print. Search the whole thing for "no host bridge" and you find it, in the
# DB that defines it, on a run where the card worked perfectly.
#
# So the runtime checks look only at what came after the last LOAD, which is the part where
# our programs were actually running.
string(FIND "${out}" "RECORDS WRITTEN" loaded REVERSE)

# SAY WHAT HAPPENED, RATHER THAN INDEXING WITH -1.
#
# If LOAD never got that far there is no marker, `loaded` is -1, and the SUBSTRING below
# used to throw `string begin index: -1 is out of range` -- a CMake error, naming this
# harness, about a failure that happened inside CP/M. That is what the Windows CRLF bug
# looked like for an hour, and it pointed at everything except the cause.
if(loaded LESS 0)
  message(FATAL_ERROR
          "hostbridge(${MODE}): LOAD never printed RECORDS WRITTEN -- the .HEX it was given "
          "was not loadable, so nothing we built ever ran. The assembler's own output is "
          "above; look for INVERTED LOAD ADDRESS or a NO SOURCE FILE from ASM, and suspect "
          "the bytes we PIP'd in rather than the card.\n"
          "----- transcript -----\n${out}")
endif()

string(SUBSTRING "${out}" ${loaded} -1 ran)

function(want_ran text why)
  string(FIND "${ran}" "${text}" hit)
  if(hit LESS 0)
    message(FATAL_ERROR "hostbridge(${MODE}): ${why}\n"
                        "  '${text}' never reached the terminal.\n"
                        "  full transcript: ${work}/dcdd.log\n--- what ran ---\n${ran}")
  endif()
endfunction()

want_ran("R: HELLO.TXT -> HELLO.TXT"   "R did not copy the text file in")
want_ran("R: BIN.DAT -> BIN.DAT"       "R did not copy the binary file in")
want_ran("R: SRC/DEEP.TXT -> DEEP.TXT" "R did not reach into a subdirectory of hostdir")
want_ran("W: BIN.DAT -> BACKBIN.DAT"   "W did not copy the binary file back out")

# THE SANDBOX, PROVED THROUGH THE WHOLE STACK -- the CCP, R.COM, the card, HostDir. The unit
# tests prove HostDir refuses `..`; only this proves that a program running inside CP/M
# cannot talk the machine into it.
want_ran("outside the host directory" "R was not stopped from escaping hostdir with `..`")

# A machine with no bridge in it prints this, and so does one where a utility found 0FFH on
# a floating bus and believed it. Either way, everything above would be a lie.
string(FIND "${ran}" "no host bridge" hit)
if(NOT hit LESS 0)
  message(FATAL_ERROR
    "hostbridge(${MODE}): IDENT failed -- a utility could not find the card.\n${ran}")
endif()

# ---------------------------------------------------------------------------
# THE VERDICT IS ON THE HOST'S DISK, NOT ON THE TERMINAL.
#
# "2 records" on the console means R counted to two. It does not mean the bytes are right,
# and a transfer that dropped the high bit of every byte would print exactly the same thing.
# So we read what is actually there.
# ---------------------------------------------------------------------------
file(READ "${SRC}/tests/acceptance/hostbridge.bin" orig HEX)
file(READ "${work}/host/BACKBIN.DAT" back HEX)
if(NOT orig STREQUAL back)
  message(FATAL_ERROR
    "hostbridge(${MODE}): THE BINARY DID NOT SURVIVE THE ROUND TRIP.\n"
    "  256 bytes went host -> CP/M -> host and came back different.\n"
    "  sent: ${orig}\n  got : ${back}")
endif()

# T MODE, and what it is for. HELLO.TXT is 33 bytes; CP/M stored it in one 128-byte record
# padded with ^Z, because CP/M keeps no byte count anywhere. B brings all 128 back. T stops
# at the first ^Z, so the file comes back the length it started.
file(SIZE "${work}/host/TRIM.TXT" trimsz)
file(SIZE "${work}/host/HELLO.TXT" origsz)
if(NOT trimsz EQUAL origsz)
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W ... T` did not trim the ^Z padding. HELLO.TXT is ${origsz} "
    "bytes and came back as ${trimsz}.")
endif()

# ---------------------------------------------------------------------------
# THE COMMITTED ARTIFACTS ARE STILL THE ONES THE SOURCES BUILD.
#
# cpm/hostbridge/ carries R.COM, W.COM and HDIR.COM (and their .HEX and .PRN) so that a
# person who clones this repo has working utilities without pasting 28 KB of assembler into
# PIP. They were produced exactly the way these were -- assembled in the machine, then W'd
# out of it -- and they are the only build artifacts committed anywhere in the tree.
#
# A CHECKED-IN BINARY THAT NOBODY CHECKS IS A BINARY THAT ROTS. Edit R.ASM, forget to
# rebuild, and R.COM quietly goes on being the old program -- passing every other test here,
# because every other test builds its own.
#
#   fast:  the committed .HEX must still LOAD to the committed .COM
#   build: the committed .ASM must still assemble to the committed .HEX, .PRN and .COM
#
# If either fires, nothing is wrong with the card. Rebuild the utilities and commit them:
# cpm/hostbridge/README.md says how.
if(MODE STREQUAL build)
  set(artifacts COM HEX PRN)
else()
  set(artifacts COM)
endif()

foreach(u R W HDIR)
  foreach(ext IN LISTS artifacts)
    file(READ "${SRC}/cpm/hostbridge/${u}.${ext}" committed HEX)
    file(READ "${work}/host/${u}.${ext}"          fresh     HEX)
    if(NOT committed STREQUAL fresh)
      message(FATAL_ERROR
        "hostbridge(${MODE}): cpm/hostbridge/${u}.${ext} IS STALE.\n"
        "  It is no longer what ${u}.ASM builds. Rebuild the utilities and commit them --\n"
        "  cpm/hostbridge/README.md says how.")
    endif()
  endforeach()
endforeach()

if(MODE STREQUAL fast)
  message(STATUS "hostbridge: the committed R/W/HDIR moved 256 binary bytes host -> CP/M -> "
                 "host EXACTLY, T trimmed ${origsz} bytes clean, and the sandbox held.")
  return()
endif()

message(STATUS "hostbridge(build): the sources still assemble to the committed .HEX, .PRN "
               "and .COM, byte for byte.")

# ---------------------------------------------------------------------------
# AND THE SAME BINARY, ON A DIFFERENT DISK CONTROLLER.  (build mode only)
#
# This is the test of the claim R.ASM makes at the top of itself: that every disk operation
# in it is a BDOS call, and that it therefore does not care what the disk is.
#
# So: take the R.HEX and W.HEX we just built -- the exact same bytes, sitting on the host
# because W put them there -- and LOAD them on an 88-MDS minidisk. A different card, a
# different BIOS, a different DPB, a different sector size, a fourteenth of the capacity. If
# anything in R or W had reached past BDOS to the controller underneath, this is where it
# dies.
#
# (The .HEX is text and PIP can carry it, which is the only reason this is possible at all:
# you cannot PIP a .COM through the console, because the first 1AH in it would end it.)
#
# Gated on the disk, which is not in git.
# ---------------------------------------------------------------------------
set(mds1 "${SRC}/disks/mits-88mds/cpm22/CPM56K-1.DSK")
if(NOT EXISTS "${mds1}")
  message(STATUS "hostbridge(build): no minidisk image -- SKIPPING the BDOS portability "
                 "phase. See disks/mits-88mds/cpm22/README.md.")
  return()
endif()

configure_file("${mds1}" "${work}/mini1.dsk" COPYONLY)

# NO `startup` HERE, AND THAT IS THE BUG THIS COMMENT EXISTS TO STOP YOU REINTRODUCING.
#
# A machine file's `startup` runs BEFORE the -s script does. Put `RUN FF00` in the TOML and
# CP/M boots, eats the whole keystroke file and exits -- and only THEN does
# `SET hb0 HOSTDIR=...` execute, against a machine that has finished running. The card
# spends the entire test pointed at the shell's working directory, every transfer fails with
# "no such file", and the SET lands in the transcript after the postmortem where it is very
# easy to miss.
#
# So the RUN goes at the END of mds.cmd, after the SETs.
file(WRITE "${work}/mini.toml"
  "[machine]\n"
  "name    = \"hostbridge-mini\"\n"
  "base    = \"minidisk\"\n"
  "startup = []\n"
  "\n"
  "[[board]]\n"
  "id = \"mds0\"\n"
  "  [[board.drive]]\n"
  "  unit  = 0\n"
  "  mount = \"mini1.dsk\"\n")

# A MINIDISK IS 71,680 BYTES AND IT ARRIVES WITH 6 KB FREE, so before anything else we have
# to make room -- for two .HEX files, the two .COMs LOAD builds from them, and the file we
# are about to transfer. We are erasing from a SCRATCH COPY of the image, which is the only
# reason this is allowed to be so blunt.
#
# What goes: MOVCPM5 (the system relocator), SYSGEN, ACOPY/AFORMAT (disk copy and format)
# and LS. None of them is needed to run a .COM, and none of them is what is under test.
set(keys "")
foreach(gone MOVCPM5.COM SYSGEN.COM ACOPY.COM AFORMAT.COM LS.COM IOBYTE.TXT)
  cmd("ERA ${gone}")
endforeach()

foreach(u R W)
  pip("${work}/host/${u}.HEX" "${u}.HEX")
  cmd("LOAD ${u}")
endforeach()

cmd("R BIN.DAT")                        # host -> minidisk CP/M
cmd("W BIN.DAT MDSBIN.DAT")             # ...and back out again
foreach(i RANGE 8)
  set(keys "${keys}${CR}")
endforeach()
file(WRITE "${work}/mds.keys" "${keys}")

file(WRITE "${work}/mds.cmd"
  "SET CONSOLE UPPER=OFF\n"
  "SET hb0 HOSTDIR=${work}/host\n"
  "RUN FF00\n")

execute_process(
  COMMAND           "${SIM}" "${work}/mini.toml" -s "${work}/mds.cmd"
  WORKING_DIRECTORY "${SRC}"
  INPUT_FILE        "${work}/mds.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           120
  RESULT_VARIABLE   rc
)
file(WRITE "${work}/mds.log" "${out}")

if(rc STREQUAL "Process terminated due to timeout")
  message(FATAL_ERROR
    "hostbridge(build): THE MINIDISK MACHINE HUNG -- killed after 120s.\n"
    "  transcript: ${work}/mds.log\n--- tail ---\n${out}")
endif()

want("R: BIN.DAT -> BIN.DAT"     "R.COM did not run on the minidisk")
want("W: BIN.DAT -> MDSBIN.DAT"  "W.COM did not run on the minidisk")

if(NOT EXISTS "${work}/host/MDSBIN.DAT")
  message(FATAL_ERROR "hostbridge(build): W wrote nothing on the minidisk machine.\n${out}")
endif()
file(READ "${work}/host/MDSBIN.DAT" mback HEX)
if(NOT orig STREQUAL mback)
  message(FATAL_ERROR
    "hostbridge(build): THE SAME BINARY DOES NOT SURVIVE A DIFFERENT CONTROLLER.\n"
    "  R.COM and W.COM were built on an 8 MB 88-DCDD image and run unchanged on an\n"
    "  88-MDS minidisk. Something in them reached past BDOS.\n"
    "  sent: ${orig}\n  got : ${mback}")
endif()

message(STATUS "hostbridge(build): 88-MDS -- the SAME R.COM and W.COM, built on an 8 MB "
               "88-DCDD image, round-tripped the same 256 bytes on a minidisk. BDOS-only "
               "holds.")
