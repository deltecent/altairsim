# THE HOST BRIDGE ACCEPTANCE TEST: a file crosses between the host and CP/M, and comes
# back byte for byte.
#
# TWO TESTS, ONE SCRIPT. -DMODE picks which:
#
#   MODE=fast    Boot CP/M, LOAD the utilities from the .HEX that is CHECKED IN, and move
#                files. This is the test of THE CARD, and it is the one that runs on every
#                edit. A few seconds.
#
#   MODE=build   Assemble R.ASM, W.ASM, HDIR.ASM and SIMCLK.ASM FROM SOURCE inside the machine, and
#                prove that what comes out is bit-for-bit what is committed beside them.
#                This is the test of THE SOURCES, and it only has anything to say when a
#                .ASM changes. Labelled `slow`, and it also carries the minidisk phase.
#
# WHY THE SPLIT, in one number: `PIP FOO.ASM=CON:` is the only slow thing here. The four
# sources are 64 KB, PIP ECHOES every byte it is fed, and a 2SIO is a real serial line --
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

# ABSOLUTE PATHS, because the by-hand command in CMakeLists.txt gives relative ones. The
# paths are written into dcdd.cmd, and a monitor script reads a relative path from the
# script's own folder. `-DBIN=build` then mounts build/hostbridge-build/build/... and the
# MOUNT fails before the machine starts.
foreach(v SIM SRC BIN)
  get_filename_component(${v} "${${v}}" ABSOLUTE)
endforeach()

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
# LOWER CASE ON PURPOSE. The CCP folds `W MIX.* OUT` to capitals, so on a case-sensitive host
# (the Linux leg) only the card's fold on every part of a path finds this folder.
file(MAKE_DIRECTORY "${work}/host/out")

# ---------------------------------------------------------------------------
# THE HOST SIDE, STAGED.
#
# hostdir is a scratch directory, and NOT the working directory -- which is the whole point
# of the `hostdir` property. The default (the shell's CWD) is what a user gets and what the
# machine file documents; a test that used it would be writing its fixtures into the repo.
# ---------------------------------------------------------------------------
file(WRITE "${work}/host/HELLO.TXT" "HELLO FROM THE HOST.${CRLF}LINE TWO.${CRLF}")
file(WRITE "${work}/host/SRC/DEEP.TXT" "IN A SUBDIRECTORY OF HOSTDIR.${CRLF}")

# TWO NAMES THAT ARE ORDINARY ON A HOST AND ARE NOT 8.3 (issue #633). The first has a space,
# a comma and brackets, none of which the CCP can take in a name; the second has `-`, `(`
# and `)`, which are all legal in one. They are upper case because the CCP folds what is
# typed and a Linux host matches a name exactly.
file(WRITE "${work}/host/SP ACE,D[1].TXT" "A NAME THAT HAD A SPACE IN IT.${CRLF}")
file(WRITE "${work}/host/MY-NOTES(2).TXT" "NOTES.${CRLF}")

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
  # FROM SOURCE. 64 KB through the console, and this is the whole cost of this mode.
  foreach(u R W HDIR SIMCLK)
    pip("${SRC}/cpm/hostbridge/${u}.ASM" "${u}.ASM")
    cmd("ASM ${u}")                     # -> .HEX and .PRN
    cmd("LOAD ${u}")                    # -> .COM
  endforeach()
else()
  # ERASE THE UTILITIES THE DISK ARRIVES WITH, BEFORE BUILDING THEM AGAIN.
  #
  # The tracked image ships R/W/HDIR.COM already installed (but not SIMCLK.COM, which is
  # only on the disk in examples/cpm) -- that is what makes it not
  # byte-identical to upstream (tools/install-hostbridge-utils.sh). Leaving them there
  # would MASK THE FAILURE THIS MODE EXISTS TO CATCH: if LOAD stopped producing a working
  # .COM, the pre-installed one would run instead and every check below would pass while
  # testing the wrong binary. Erasing first means the program that runs can only be the
  # one we just built out of the committed .HEX.
  #
  # It also buys back the ~5K they occupy, and on an 18K disk that is not spare change.
  foreach(u R W HDIR SIMCLK)
    cmd("ERA ${u}.COM")
  endforeach()

  # ...AND A GAME, FOR ROOM. With SIMCLK.COM (#918) added to the three, the run ended with
  # 0K free: every file still fit, but the next file added to this test would not. LADDER.COM
  # is 40K and no step uses it. This is the scratch copy, never the tracked image.
  cmd("ERA LADDER.COM")

  # FROM THE COMMITTED HEX. Six times less wire, no assembler -- and LOADing the hex that
  # is checked in is what makes the .COM beside it provable rather than merely present.
  #
  # EACH .HEX IS ERASED THE MOMENT LOAD HAS EATEN IT, and on this disk that is not
  # housekeeping. 12.5 KB of .HEX against 18K free left the disk with EXACTLY 0K remaining
  # at the end of the run -- passing, but one file away from failing, and failing as a
  # silently truncated PIP that LOADs into a subtly wrong .COM. Freeing each .HEX as we
  # finish with it holds the peak down. (With SIMCLK.HEX, the four are 15.6 KB.)
  foreach(u R W HDIR SIMCLK)
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
cmd("W HELLO.TXT TRIM.TXT T")           # T = stop at the first ^Z: the tail of the record stays

# NO SWITCH: THE EXTENSION DECIDES (issue #725). .TXT is on W's list of text extensions, so
# this comes back trimmed with nothing typed after the name. Then the two cases where a
# switch is given, and it wins over the extension in both directions.
cmd("W HELLO.TXT AUTO.TXT")             # text, by extension
cmd("W HELLO.TXT PAD.TXT B")            # B on a text extension: the whole record comes back
cmd("W BIN.DAT CUT.DAT T")              # T on a binary extension: cut at the 1AH

# A WILDCARD CHOOSES FOR EACH FILE. Three CP/M files that exist only under these names, so
# the host has no MIX.* until W writes them: two with text extensions and one without.
cmd("R HELLO.TXT MIX.Z80")
cmd("R HELLO.TXT MIX.SYM")
cmd("R BIN.DAT MIX.DAT")
cmd("W MIX.*")
cmd("W MIX.* OUT")                      # a wildcard into a host folder: each keeps its name
cmd("W MIX.* NOPE")                     # ...which must exist: one error, nothing written
cmd("R ../SECRET.TXT")                  # THE SANDBOX, through the whole stack
cmd("R SP*.TXT")                        # a wildcard: the ONLY way a space reaches R
cmd("W SPACED1.TXT SPBACK.TXT T")       # ...and the CCP can name what R stored
cmd("R MY-NOTES(2).TXT")                # `-` stays; `(2)` is legal too, but past the 8
cmd("HDIR *.TXT")

# THE GUEST SETS THE CRYSTAL (issue #918). Everything above ran flat out -- the default
# machine has clock_hz = 0, and SIMCLK with no word only reports it. A rate out of the CPU
# board's range is refused by the board's property check, a word that is not decimal digits
# is refused by the card, and the SIMCLK after them shows that neither changed the rate.
#
# `SIMCLK 2000000` then turns the crystal on, and it is the regression check for the RUN
# throttle. A throttle that kept counting from the start of RUN would owe 2 MHz time for
# every T-state run flat out above it, and sit there far past the leash below. With the
# rate re-based when it changes, the next prompt arrives at once.
#
# The refusals go BEFORE the crystal is on: each SIMCLK is a load from an 8" floppy and a
# warm boot, which at 2 MHz is the real drive's speed, about a second each.
cmd("SIMCLK")
cmd("SIMCLK 999999999")
cmd("SIMCLK 2MHZ")
cmd("SIMCLK")
cmd("SIMCLK 2000000")
cmd("SIMCLK 0")                         # flat out again for the rest of the run

# The .COM of each, byte for byte, to hold up against what is checked in. In `fast` mode
# this proves the committed .HEX still LOADs to the committed .COM; in `build` mode it
# proves the committed .ASM still assembles to it.
foreach(u R W HDIR SIMCLK)
  cmd("W ${u}.COM")
endforeach()

if(MODE STREQUAL build)
  # ...and in build mode, the OTHER two artifacts as well, so all nine are pinned. These
  # are text, and W knows .HEX and .PRN are: with no switch it stops at the ^Z that ends
  # each. The compare against the committed files below fails on a single byte past it.
  foreach(u R W HDIR SIMCLK)
    cmd("W ${u}.HEX")
    cmd("W ${u}.PRN")
  endforeach()
endif()

# The last word on the clock goes to the guest, and the host reads it back: SHOW cpu0 runs
# from dcdd.cmd after the RUN ends, so it reports what the CPU board holds, not what SIMCLK
# printed about it.
cmd("SIMCLK 2000000")

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
#          tracking the image. It arrives with 18K free: 15.6 KB of .HEX passes through
#          it one file at a time, and the four .COM and the files we move in stay. Tight,
#          and deliberately so -- see the ERA below, and the ERA LADDER.COM above it.
#
#   build  the 8 MB image, because PIPing R/W/HDIR/SIMCLK.ASM in needs 64 KB of free disk. It is
#          not in git, so CMakeLists.txt gates this mode on finding it.
#
# NEVER MUTATE A REPO IMAGE: both are copied to scratch first. For the tracked one that is
# not merely tidy -- dcdd-readonly.exp and ddt.exp shasum that file and fail if it moves.
# ---------------------------------------------------------------------------
if(MODE STREQUAL build)
  set(image "${SRC}/disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK")
else()
  set(image "${SRC}/tests/media/cpm/cpm22b23-56k.dsk")
endif()
configure_file("${image}" "${work}/work.dsk" COPYONLY)

# NO `SET ... BAUD` HERE, AND RESIST THE URGE.
#
# The console is a 9600-baud line and this test lives with it, which is why `fast` PIPs the
# 16 KB of .HEX and only `build` PIPs the 64 KB of source.
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
  "RUN FF00\n"
  "SHOW cpu0\n")

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

# A HOST NAME BECOMES A NAME THE CCP CAN TYPE. R drops what CP/M cannot store BEFORE it
# counts to eight, and a space is one of those: stored, it would end the name for the CCP,
# and the file could be reached only through a wildcard. What R prints is not the proof --
# it printed `ZQAB1.TXT` for a file it had stored as `ZQ AB1.TXT`. The proof is that W,
# given the name with no space in it, finds the file and brings it back.
want_ran("R: SP ACE,D[1].TXT -> SPACED1.TXT" "R did not map a host name with a space in it")
want_ran("R: MY-NOTES(2).TXT -> MY-NOTES.TXT"
         "R did not keep the legal `-` and cut the name at eight")
if(NOT EXISTS "${work}/host/SPBACK.TXT")
  message(FATAL_ERROR
    "hostbridge(${MODE}): R STORED A NAME THE CCP CANNOT TYPE.\n"
    "  `SP ACE,D[1].TXT` came in through a wildcard, and `W SPACED1.TXT` did not find it --\n"
    "  a space, or another character the CCP stops at, is in the directory entry.\n"
    "--- what ran ---\n${ran}")
endif()
file(READ "${work}/host/SP ACE,D[1].TXT" spsent HEX)
file(READ "${work}/host/SPBACK.TXT"      spback HEX)
if(NOT spback STREQUAL spsent)
  message(FATAL_ERROR
    "hostbridge(${MODE}): the file with a space in its host name came back different.\n"
    "  sent: ${spsent}\n  got : ${spback}")
endif()

# THE GUEST SET THE CRYSTAL (issue #918), in the order the commands were typed. Each line is
# searched for after the one before it, because `SIMCLK: flat out` is printed three times and
# only the order shows which command printed which. The match starts at `>`, because the
# 8 MB disk in build mode prints `A0>` where the floppy prints `A>`.
set(at 0)
foreach(line
    ">SIMCLK${CR}"                      "SIMCLK: flat out"
    ">SIMCLK 999999999"                 "SIMCLK: clock_hz must be 0..100000000"
    ">SIMCLK 2MHZ"                      "SIMCLK: bad clock rate -- give the rate in Hz as decimal digits"
    ">SIMCLK${CR}"                      "SIMCLK: flat out"
    ">SIMCLK 2000000"                   "SIMCLK: 2000000 Hz"
    ">SIMCLK 0"                         "SIMCLK: flat out"
    ">SIMCLK 2000000"                   "SIMCLK: 2000000 Hz")
  string(SUBSTRING "${ran}" ${at} -1 rest)
  string(FIND "${rest}" "${line}" hit)
  if(hit LESS 0)
    message(FATAL_ERROR
      "hostbridge(${MODE}): SIMCLK did not print '${line}' where it should have.\n"
      "  full transcript: ${work}/dcdd.log\n--- from there on ---\n${rest}")
  endif()
  string(LENGTH "${line}" len)
  math(EXPR at "${at} + ${hit} + ${len}")
endforeach()

# ...and the host agrees. SHOW cpu0 runs after the RUN, so this is the CPU board's own
# property, set by the last SIMCLK, and not the guest's report of it.
if(NOT ran MATCHES "\n  clock_hz +2000000 ")
  message(FATAL_ERROR
    "hostbridge(${MODE}): the guest's last `SIMCLK 2000000` did not reach the CPU board.\n"
    "  SHOW cpu0 after the run should list clock_hz 2000000.\n--- what ran ---\n${ran}")
endif()

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

# T MODE, and what it is for. HELLO.TXT is 33 bytes; it is on the disk as one 128-byte record,
# because CP/M keeps no byte count anywhere, and R filled the rest of that record with ^Z.
# B brings all 128 back. T stops at the first ^Z, so the file comes back the length it started.
file(SIZE "${work}/host/TRIM.TXT" trimsz)
file(SIZE "${work}/host/HELLO.TXT" origsz)
if(NOT trimsz EQUAL origsz)
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W ... T` did not stop at the first ^Z. HELLO.TXT is ${origsz} "
    "bytes and came back as ${trimsz}.")
endif()

# NO SWITCH, AND THE EXTENSION DECIDED (issue #725). The sizes are the proof; the message is
# what makes the choice visible to the person at the terminal, so both are checked.
file(SIZE "${work}/host/AUTO.TXT" autosz)
if(NOT autosz EQUAL origsz)
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W HELLO.TXT AUTO.TXT` did not use text mode for a .TXT file. "
    "HELLO.TXT is ${origsz} bytes and came back as ${autosz}.")
endif()
want_ran("W: HELLO.TXT -> AUTO.TXT (text, by extension)"
         "W did not say that the extension chose text mode")

# ...and it says so ONLY then. A file that is not on the list, and a file the user gave a
# switch for, print the line they always printed.
foreach(quiet "W: BIN.DAT -> BACKBIN.DAT" "W: HELLO.TXT -> TRIM.TXT" "W: HELLO.TXT -> PAD.TXT"
              "W: BIN.DAT -> CUT.DAT")
  want_ran("${quiet}" "W did not copy this file at all")
  string(FIND "${ran}" "${quiet} (text, by extension)" hit)
  if(NOT hit LESS 0)
    message(FATAL_ERROR
      "hostbridge(${MODE}): `${quiet}` claims the extension chose text mode, and it did "
      "not: the extension is not on the list, or a switch was given.\n--- what ran ---\n${ran}")
  endif()
endforeach()

# A SWITCH WINS OVER THE EXTENSION, BOTH WAYS. B on a .TXT keeps the whole record. T on a
# .DAT stops at the 1AH, which in this fixture is byte 26 because the bytes count up from 00.
file(SIZE "${work}/host/PAD.TXT" padsz)
if(NOT padsz EQUAL 128)
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W HELLO.TXT PAD.TXT B` did not copy the whole record. The switch "
    "must win over the extension; ${padsz} bytes came back and one record is 128.")
endif()
file(SIZE "${work}/host/CUT.DAT" cutsz)
if(NOT cutsz EQUAL 26)
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W BIN.DAT CUT.DAT T` did not stop at the 1AH. The switch must "
    "win over the extension; ${cutsz} bytes came back and the 1AH is byte 26.")
endif()

# A WILDCARD CHOSE FOR EACH FILE. One `W MIX.*`, two text files and a binary: the text ones
# are trimmed, and the binary -- 1AH and all -- is whole.
foreach(ext Z80 SYM)
  if(NOT EXISTS "${work}/host/MIX.${ext}")
    message(FATAL_ERROR "hostbridge(${MODE}): `W MIX.*` did not copy MIX.${ext} out.\n${ran}")
  endif()
  file(SIZE "${work}/host/MIX.${ext}" mixsz)
  if(NOT mixsz EQUAL origsz)
    message(FATAL_ERROR
      "hostbridge(${MODE}): `W MIX.*` did not use text mode for MIX.${ext}. It holds "
      "HELLO.TXT, ${origsz} bytes, and came back as ${mixsz}.")
  endif()
  want_ran("W: MIX.${ext} -> MIX.${ext} (text, by extension)"
           "W did not say that the extension chose text mode for a wildcard match")
endforeach()
if(NOT EXISTS "${work}/host/MIX.DAT")
  message(FATAL_ERROR "hostbridge(${MODE}): `W MIX.*` did not copy MIX.DAT out.\n${ran}")
endif()
file(READ "${work}/host/MIX.DAT" mixbin HEX)
if(NOT mixbin STREQUAL orig)
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W MIX.*` DAMAGED THE BINARY FILE IN THE SET.\n"
    "  MIX.DAT is not a text extension and must come back byte for byte.\n"
    "  sent: ${orig}\n  got : ${mixbin}")
endif()

# A WILDCARD INTO A FOLDER. `W MIX.* OUT` puts the same three files, the same bytes, into the
# host folder `out` -- which the CCP made `OUT`, so the card had to fold the folder's name.
foreach(ext Z80 SYM DAT)
  want_ran("W: MIX.${ext} -> OUT/MIX.${ext}" "W did not name the folder in front of each file")
  if(NOT EXISTS "${work}/host/out/MIX.${ext}")
    message(FATAL_ERROR
      "hostbridge(${MODE}): `W MIX.* OUT` did not write MIX.${ext} into host folder out.\n${ran}")
  endif()
endforeach()
foreach(ext Z80 SYM)
  file(SIZE "${work}/host/out/MIX.${ext}" mixsz)
  if(NOT mixsz EQUAL origsz)
    message(FATAL_ERROR
      "hostbridge(${MODE}): `W MIX.* OUT` wrote out/MIX.${ext} as ${mixsz} bytes, not ${origsz}.")
  endif()
endforeach()
file(READ "${work}/host/out/MIX.DAT" mixbin HEX)
if(NOT mixbin STREQUAL orig)
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W MIX.* OUT` DAMAGED out/MIX.DAT.\n  sent: ${orig}\n  got : ${mixbin}")
endif()

# A FOLDER THAT IS NOT THERE STOPS W BEFORE THE FIRST FILE. One error line, no file, no folder.
want_ran("W: NOPE: no such file" "W did not refuse a host folder that does not exist")
string(FIND "${ran}" "-> NOPE/" nope)
if(NOT nope LESS 0 OR EXISTS "${work}/host/NOPE" OR EXISTS "${work}/host/nope")
  message(FATAL_ERROR
    "hostbridge(${MODE}): `W MIX.* NOPE` tried to copy into a folder that does not exist.\n${ran}")
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

foreach(u R W HDIR SIMCLK)
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
  message(STATUS "hostbridge: the committed R/W/HDIR/SIMCLK moved 256 binary bytes host -> CP/M -> "
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
# The minidisk image IS in git, so this phase is unconditional. It carried an EXISTS gate
# until 2026-07-19, left over from when the image was a download; the gate could no longer
# fire and only made this phase look optional. (What is still a download, and still gates
# this whole mode from CMakeLists.txt, is the 8 MB 88-DCDD image above.)
# ---------------------------------------------------------------------------
set(mds1 "${SRC}/disks/mits-88mds/cpm22/CPM56K-1.DSK")

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
