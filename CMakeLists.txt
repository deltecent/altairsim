cmake_minimum_required(VERSION 3.20)
project(altairsim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Built-in ROMs (DESIGN.md 10.3.1)
#
# Each subdirectory of roms/ is one ROM. Its .HEX or .BIN is embedded VERBATIM
# as a byte array; the C++ side runs the ordinary Intel HEX loader over those
# bytes at startup. One parser, two sources (a host file, or .rodata) -- so a
# built-in cannot drift from a mounted file, and CMake never learns what Intel
# HEX is. Provenance and CRC32 for every ROM live in docs/roms.md.
# ---------------------------------------------------------------------------
# CONFIGURE_DEPENDS: re-glob on every BUILD, not just on configure. Without it,
# dropping a new ROM into roms/ does nothing until someone remembers to re-run
# cmake -- and DELETING one breaks the build outright, with an error naming a
# file you just deliberately removed. Both are ten confusing minutes, and both
# are avoidable for one word.
set(ROMS_GENERATED "${CMAKE_BINARY_DIR}/generated/roms_generated.cpp")
file(GLOB ROM_DIRS RELATIVE "${CMAKE_SOURCE_DIR}/roms"
     CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/roms/*")

set(ROM_IMAGE_FILES "")
foreach(d ${ROM_DIRS})
  if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/roms/${d}")
    file(GLOB img CONFIGURE_DEPENDS
                  "${CMAKE_SOURCE_DIR}/roms/${d}/*.HEX" "${CMAKE_SOURCE_DIR}/roms/${d}/*.hex"
                  "${CMAKE_SOURCE_DIR}/roms/${d}/*.BIN" "${CMAKE_SOURCE_DIR}/roms/${d}/*.bin")
    list(APPEND ROM_IMAGE_FILES ${img})
  endif()
endforeach()

add_custom_command(
  OUTPUT "${ROMS_GENERATED}"
  COMMAND ${CMAKE_COMMAND}
          -DROMS_DIR=${CMAKE_SOURCE_DIR}/roms
          -DOUT=${ROMS_GENERATED}
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_roms.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_roms.cmake ${ROM_IMAGE_FILES}
  COMMENT "Embedding ROMs from roms/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Built-in machines (DESIGN.md 10.0)
#
# machines/*.toml are BUILD-TIME INPUTS, exactly like a .cpp file. They are read
# here, at compile time, and emitted as byte arrays that link into .rodata.
#
# THE SHIPPED BINARY IS SELF-CONTAINED. It never looks for a machines/ directory,
# never consults an install prefix, and does not care where it was copied to --
# `altairsim default` works from a USB stick. That is a hard requirement: this
# ships as ONE EXECUTABLE plus documentation, and a simulator that cannot find
# its own default machine is a simulator that does not start.
#
# The .toml files are still real TOML, in the real config format, because they
# are parsed at runtime by the same loadTomlText() that reads your config file.
# So they double as worked examples, and they cannot drift from the format.
# ---------------------------------------------------------------------------
set(MACHINES_GENERATED "${CMAKE_BINARY_DIR}/generated/machines_generated.cpp")
file(GLOB MACHINE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/machines/*.toml")

add_custom_command(
  OUTPUT "${MACHINES_GENERATED}"
  COMMAND ${CMAKE_COMMAND}
          -DMACHINES_DIR=${CMAKE_SOURCE_DIR}/machines
          -DOUT=${MACHINES_GENERATED}
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_machines.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_machines.cmake ${MACHINE_FILES}
  COMMENT "Embedding machines from machines/"
  VERBATIM)

# ---------------------------------------------------------------------------
# The platform layer (DESIGN.md 2.1). NO #ifdefs -- ANYWHERE.
#
# OS differences are one interface header with zero conditionals in it, plus one
# implementation file per OS, and CMAKE picks the directory. This block is the
# entire mechanism, and it is the only place in the build that asks what OS it is
# on. Nothing in src/ ever does.
#
# The win32 half is WRITTEN BUT UNBUILT on this machine -- see docs/porting-notes.md,
# which says so plainly rather than letting someone discover it on a Windows box.
# ---------------------------------------------------------------------------
if(WIN32)
  set(PLATFORM_SOURCES
    src/platform/win32/serial_win32.cpp
    src/platform/win32/socket_win32.cpp
    src/platform/win32/terminal_win32.cpp)
else()
  set(PLATFORM_SOURCES
    src/platform/posix/serial_posix.cpp
    src/platform/posix/socket_posix.cpp
    src/platform/posix/terminal_posix.cpp)
endif()

# ---------------------------------------------------------------------------
# ...AND THE LINT THAT KEEPS IT TRUE (DESIGN.md 2.1).
#
# The rule above is worth exactly as much as its enforcement. Without this, the first
# time somebody needs one small thing on Windows an #ifdef appears "just this once",
# and inside a month you are SIMH.
#
# It is a BUILD dependency of the library, not a test, because 2.1 says "fails the
# build" -- a rule you can merge and fix later is a rule you have already lost. It
# greps for OS macros AND for OS headers; cmake/lint_platform.cmake explains why the
# second half is not optional.
# ---------------------------------------------------------------------------
add_custom_target(platform_lint
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                           -P ${CMAKE_SOURCE_DIR}/cmake/lint_platform.cmake
  COMMENT "DESIGN.md 2.1: checking the OS has not escaped src/platform/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Core library -- everything except main(). Tests link this.
# ---------------------------------------------------------------------------
add_library(altair_core STATIC
  ${PLATFORM_SOURCES}
  src/core/crc32.cpp
  src/core/hex.cpp
  src/core/bus.cpp
  src/core/clock.cpp
  src/core/board.cpp
  src/core/machine.cpp
  src/core/roms.cpp
  src/core/machines.cpp
  src/core/value.cpp
  src/core/debug.cpp
  src/core/spindle.cpp
  src/isa/isa8080.cpp
  src/cpu/cpu8080.cpp
  src/host/stream.cpp
  src/host/filter.cpp
  src/host/console.cpp
  src/host/endpoint.cpp
  src/host/hostserial.cpp
  src/host/tcp.cpp
  src/host/media.cpp
  src/host/disk.cpp
  src/host/tape.cpp
  # The chips. A chip is not a card: it is modeled from its DATA SHEET, it knows
  # nothing about S-100, and it is shared by every card that has one soldered to it.
  src/chips/mc6850.cpp
  src/chips/uart1602.cpp
  src/chips/wd17xx.cpp
  src/boards/s100-memory.cpp
  src/boards/mits-88cpu.cpp
  src/boards/mits-88dcdd.cpp
  src/boards/mits-2sio.cpp
  src/boards/mits-88sio.cpp
  # The 88-ACR derives from the 88-SIO, because the card IS an 88-SIO B with a modem
  # mated to it -- see mits-88acr.h. It must be listed after it for no reason at all;
  # this list is not ordered. It is listed after it so the reader sees the pair.
  src/boards/mits-88acr.cpp
  src/boards/mits-frontpanel.cpp
  src/boards/mits-88virtc.cpp
  src/boards/registry.cpp
  src/config/toml.cpp
  src/cli/monitor.cpp
  src/cli/commands.cpp
  src/cli/lineedit.cpp
  src/util/json.cpp
  src/mcp/server.cpp
  "${ROMS_GENERATED}"
  "${MACHINES_GENERATED}")

target_include_directories(altair_core PUBLIC src)

# Nothing links until the OS is where it belongs.
add_dependencies(altair_core platform_lint)

add_executable(altairsim src/main.cpp)
target_link_libraries(altairsim PRIVATE altair_core)

# ---------------------------------------------------------------------------
# Tests -- no framework; a test is an executable that returns non-zero.
# ---------------------------------------------------------------------------
enable_testing()
add_executable(altair_tests
  tests/main.cpp
  tests/test_bus.cpp
  tests/test_clock.cpp
  tests/test_memory.cpp
  tests/test_hex.cpp
  tests/test_media.cpp
  tests/test_roms.cpp
  tests/test_phantom.cpp
  tests/test_cli.cpp
  tests/test_boundary.cpp
  tests/test_numbers.cpp
  tests/test_units.cpp
  tests/test_machines.cpp
  tests/test_isa.cpp
  tests/test_cpu.cpp
  tests/test_debug.cpp
  tests/test_sio2.cpp
  tests/test_88sio.cpp
  tests/test_88acr.cpp
  tests/test_lines.cpp
  tests/test_wd17xx.cpp
  tests/test_spindle.cpp
  tests/test_dcdd.cpp
  tests/test_frontpanel.cpp
  tests/test_virtc.cpp)
target_link_libraries(altair_tests PRIVATE altair_core)
add_test(NAME unit COMMAND altair_tests)

# ---------------------------------------------------------------------------
# The serial-line hardware gate. TWO REAL PORTS, A NULL MODEM BETWEEN THEM.
#
# Everything else in this project can be proved with a ScriptedStream and a
# MemoryMedia, and should be. Modem control cannot: whether RTS on one card really
# raises CTS on another is a question about a CABLE, and the only honest way to
# answer it is to send a volt down one.
#
# So this test is REAL HARDWARE, and it is opt-in -- CI has no serial ports and
# neither does a fresh checkout. Point it at the two ends of a null modem:
#
#   ALTAIR_SERIAL_A=/dev/tty.usbserial-AL009KFH \
#   ALTAIR_SERIAL_B=/dev/tty.usbserial-AB0NW409 ctest -L hw
#
# Unset, it SKIPS and says so. A hardware test that silently passes when the
# hardware is absent is worse than no hardware test at all -- so it exits 77 and
# SKIP_RETURN_CODE turns that into a `Skipped` in ctest's summary. Without this the
# run reads `serial-hw ... Passed` on a machine with no serial ports at all, which
# is a green tick for a cable nobody plugged in.
# ---------------------------------------------------------------------------
add_executable(altair_serialtest tests/serialtest.cpp)
target_link_libraries(altair_serialtest PRIVATE altair_core)
add_test(NAME serial-hw COMMAND altair_serialtest)
set_tests_properties(serial-hw PROPERTIES LABELS hw SKIP_RETURN_CODE 77)

# ---------------------------------------------------------------------------
# The 8080 validation gate (DESIGN.md 3.2). Period CP/M test suites, run against
# our core. Separate from the unit tests because these are the tests we did NOT
# write, which is the entire reason they are worth anything.
#
# They read their .COM files off disk at runtime rather than being embedded --
# the ONE EXECUTABLE rule is about `altairsim`, and this is not it.
# ---------------------------------------------------------------------------
add_executable(altair_cputest tests/cputest.cpp)
target_link_libraries(altair_cputest PRIVATE altair_core)
target_compile_definitions(altair_cputest
  PRIVATE ALTAIR_CPU_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/cpu")

# Each suite is its own test, so a failure names the suite. `ctest` runs ALL of
# them, 8080EXM included -- it is the gate, and a gate you have to remember to
# run is not a gate. It costs ~10 minutes (2.9 billion instructions).
#
# While iterating, skip it with `ctest -LE slow`. Run it alone with `ctest -L slow`.
add_test(NAME cpu-tst8080 COMMAND altair_cputest TST8080)
add_test(NAME cpu-8080pre COMMAND altair_cputest 8080PRE)
add_test(NAME cpu-cputest COMMAND altair_cputest CPUTEST)
add_test(NAME cpu-8080exm COMMAND altair_cputest 8080EXM)
set_tests_properties(cpu-8080exm PROPERTIES LABELS slow TIMEOUT 1800)
set_tests_properties(cpu-cputest PROPERTIES TIMEOUT 600)

# ---------------------------------------------------------------------------
# THE ACCEPTANCE TESTS -- period software, on the whole machine, through the CLI.
#
# A board test proves a card against its manual. This proves the MACHINE against an
# artifact somebody shipped in 1975, running unmodified: `altairsim basic4k`, a real
# cassette image, and the bootstrap MITS printed in the manual. It is the test the
# 88-ACR was built to pass, and it is the only one that can fail for a reason no unit
# test can see -- a card that is right in isolation and wrong in a backplane.
#
# It drives the REAL binary, so it also covers the monitor: MOUNT, LOAD, RUN, and a
# quoted path with spaces in it (which every period artifact has).
# ---------------------------------------------------------------------------
add_test(NAME acceptance-basic4k
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/basic4k.cmake)
set_tests_properties(acceptance-basic4k PROPERTIES LABELS acceptance TIMEOUT 120)

# ...and the same tape drive against the OTHER console card. 8K BASIC 3.2 wants an
# 88-2SIO (a 6850, at 0x10) where 4K BASIC 3.1 wanted an 88-SIO (a COM2502, at 0), and
# it reads the sense switches to find out which -- so this is a genuinely different
# machine, not a bigger tape. Neither test can fail for the other's reason.
add_test(NAME acceptance-basic8k
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/basic8k.cmake)
set_tests_properties(acceptance-basic8k PROPERTIES LABELS acceptance TIMEOUT 120)

# ...and the same two cards again, booting something that is NOT BASIC: the MITS
# Programming System II monitor, with serial interrupts off (sense A9 up). Same ACR, same
# 2SIO, same 0xAE leader -- and a DIFFERENT bootstrap, which is the whole point of it.
# PS2's second stage is linked for page 0F, so it needs the 4K-BASIC-3.2 loader; boot it
# with 8K BASIC's and it lands 4K too high and dies in silence. Only an acceptance test
# catches that, because every card in the machine is behaving perfectly while it happens.
#
# The interrupt-driven half of PS2 (A9 down) is NOT here: it needs an 88-VI/RTC vectored
# interrupt board, which this simulator does not have yet.
add_test(NAME acceptance-ps2
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/ps2.cmake)
set_tests_properties(acceptance-ps2 PROPERTIES LABELS acceptance TIMEOUT 120)

# ---------------------------------------------------------------------------
# THE INTERACTIVE ONE. Everything above drives the CLI through a PIPE, and a pipe is not
# a terminal -- so three things that only exist on a tty go untested by all of them: the
# echoed prompt and line editing, ATTN (^E) taking the console back from a running guest,
# and the run loop's decision to pace against a wall clock. This test types.
#
# It needs `expect`, which ships with macOS and is a package everywhere else. If it is not
# there we do not register the test rather than fail the suite over a missing tool -- and
# we SAY SO at configure time, because a test that silently is not there is worse than one
# that fails.
# ---------------------------------------------------------------------------
find_program(EXPECT_EXECUTABLE expect)
if(EXPECT_EXECUTABLE)
  add_test(NAME acceptance-cli
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/cli.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-cli PROPERTIES LABELS acceptance TIMEOUT 60)

  # ...and the PS2 package USED, not merely booted: the ReadMe's steps 5-8, including a
  # CASSETTE CHANGE while the machine is running (^E, MOUNT the editor tape, RUN, EDT).
  # That exercises the monitor's own tape loader -- which is not the bootstrap, it is 2.5K
  # of monitor reading a .BIN through the ACR -- and the ACR being re-mounted mid-session.
  # A piped test cannot do it: ^E is acted on when the console READS STDIN, so a keystroke
  # file fires it at PC=0003, before the monitor has even loaded.
  add_test(NAME acceptance-ps2-interactive
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/ps2-interactive.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-ps2-interactive PROPERTIES
                       LABELS acceptance TIMEOUT 120
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # MITS Programming System II WITH INTERRUPTS, through a real 88-VI/RTC: the console
  # 6850 pulls VI7, the 88-VI prioritizes it and pulls pin 73, and claims the IntAck
  # cycle to jam RST 7 onto the bus. The test hangs the monitor on purpose (a word it
  # does not know is a program name it hunts for on the tape, forever) and rescues it
  # with Ctrl-C -- which is the one thing the ReadMe says interrupts are for.
  add_test(NAME acceptance-ps2-int
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/ps2-int.exp
                   $<TARGET_FILE:altairsim> ps2int)
  set_tests_properties(acceptance-ps2-int PROPERTIES
                       LABELS acceptance TIMEOUT 120
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ...AND THE NEGATIVE CONTROL, which is the only reason to believe the test above.
  #
  # The SAME script against `ps2` -- A9 up, no 88-VI, nothing strapped to an interrupt --
  # must FAIL: the machine stays hung and Ctrl-C is just a character nobody delivers. If
  # this ever starts passing, then acceptance-ps2-int is passing on something that is not
  # an interrupt, and it is worthless. WILL_FAIL makes ctest assert exactly that.
  add_test(NAME acceptance-ps2-int-control
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/ps2-int.exp
                   $<TARGET_FILE:altairsim> ps2 10)
  set_tests_properties(acceptance-ps2-int-control PROPERTIES
                       LABELS acceptance TIMEOUT 60 WILL_FAIL TRUE
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
else()
  message(STATUS "expect not found -- SKIPPING the interactive CLI test (acceptance-cli). "
                 "The tty paths (prompt echo, ATTN, clock pacing) will not be covered.")
endif()
