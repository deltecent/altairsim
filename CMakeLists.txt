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
  src/core/paths.cpp
  src/core/bus.cpp
  src/core/clock.cpp
  src/core/board.cpp
  src/core/machine.cpp
  src/core/roms.cpp
  src/core/machines.cpp
  src/core/value.cpp
  src/core/expr.cpp
  src/core/debug.cpp
  src/core/spindle.cpp
  src/isa/isa8080.cpp
  src/isa/isaZ80.cpp
  src/cpu/cpu8080.cpp
  src/cpu/cpuZ80.cpp
  src/host/stream.cpp
  src/host/filter.cpp
  src/host/console.cpp
  src/host/endpoint.cpp
  src/host/hostserial.cpp
  src/host/tcp.cpp
  src/host/media.cpp
  src/host/disk.cpp
  src/host/tape.cpp
  # The sandboxed host directory the Host Bridge reaches through. It is here, and not
  # on the board, because the board must never see a path, a symlink or a file handle
  # -- and because the escape tests that matter are tests of THIS, with no card in
  # sight (DESIGN.md 7, src/host/hostdir.h).
  src/host/hostdir.cpp
  # The chips. A chip is not a card: it is modeled from its DATA SHEET, it knows
  # nothing about S-100, and it is shared by every card that has one soldered to it.
  src/chips/mc6850.cpp
  src/chips/uart1602.cpp
  src/chips/wd17xx.cpp
  src/boards/s100-memory.cpp
  src/boards/mits-88cpu.cpp
  # The generic Z80 CPU card -- the 88-CPU's twin (mits-z80cpu.h), carrying a
  # CpuZ80 instead of a Cpu8080. Listed after it so the reader sees the pair.
  src/boards/mits-z80cpu.cpp
  # The two MITS hard-sector floppy controllers, and the register model they share.
  # HardSectorFdc is not a convenience: MITS built the 88-MDS to present the 88-DCDD's
  # programming model, and the period BOOT.ASM declares the ports and every status/command
  # bit OUTSIDE its `if MINIDSK` blocks to prove it. What the two cards do NOT share --
  # 360 vs 300 RPM, 32 vs 64 us a byte, a head solenoid vs a motor that stops -- is what
  # each .cpp below actually contains. See src/boards/mits-hardsector.h.
  src/boards/mits-hardsector.cpp
  src/boards/mits-88dcdd.cpp
  src/boards/mits-88mds.cpp
  src/boards/mits-2sio.cpp
  src/boards/mits-88sio.cpp
  # The 88-ACR derives from the 88-SIO, because the card IS an 88-SIO B with a modem
  # mated to it -- see mits-88acr.h. It must be listed after it for no reason at all;
  # this list is not ordered. It is listed after it so the reader sees the pair.
  src/boards/mits-88acr.cpp
  src/boards/mits-frontpanel.cpp
  src/boards/mits-88virtc.cpp
  # OUR OWN CARD -- the only one in this list that is not a period board, and the
  # first genuinely new hardware built against the board API (DESIGN.md 12.1).
  src/boards/hostbridge.cpp
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

# The win32 socket layer (src/platform/win32/socket_win32.cpp) pulls in Winsock;
# link it. POSIX needs no equivalent -- sockets live in libc. This if(WIN32) is a
# build-script OS conditional, which is fine: the 2.1 lint governs C++ source, not
# CMake. PUBLIC so the test/tool executables that link altair_core inherit it.
if(WIN32)
  target_link_libraries(altair_core PUBLIC ws2_32)
endif()

add_executable(altairsim src/main.cpp)
target_link_libraries(altairsim PRIVATE altair_core)

# ---------------------------------------------------------------------------
# The manual's reference chapters, printed from the binary's own reflection layer.
#
# It links altair_core rather than shelling out to `altairsim` because no monitor command
# emits what a parameter table needs -- BOARDS TYPES has the help and not the defaults,
# SHOW has the defaults and not the help. See tools/gen-reference.cpp for the whole
# argument. It is a TOOL, not a feature: the shipping command surface is a contract, and
# a doc generator has no business in it.
#
# `docs-reference` rewrites docs/manual/ref/*.md in the source tree, on purpose -- those
# files are committed, so that a reader of the repo (and the PDF build) needs no toolchain
# to see them. `docs-reference-check` below is what stops them going stale.
# ---------------------------------------------------------------------------
add_executable(altair_genref tools/gen-reference.cpp)
target_link_libraries(altair_genref PRIVATE altair_core)

add_custom_target(docs-reference
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/docs/manual/ref"
  COMMAND altair_genref "${CMAKE_SOURCE_DIR}/docs/manual/ref"
  COMMENT "Generating docs/manual/ref/ from the binary")

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
  tests/test_z80_isa.cpp
  tests/test_cpu.cpp
  tests/test_z80_cpu.cpp
  tests/test_expr.cpp
  tests/test_debug.cpp
  tests/test_dma.cpp
  tests/test_sio2.cpp
  tests/test_88sio.cpp
  tests/test_88acr.cpp
  tests/test_lines.cpp
  tests/test_wd17xx.cpp
  tests/test_spindle.cpp
  tests/test_dcdd.cpp
  tests/test_mds.cpp
  tests/test_frontpanel.cpp
  tests/test_lamp.cpp
  tests/test_virtc.cpp
  tests/test_hostdir.cpp
  tests/test_hostbridge.cpp
  tests/test_mcp.cpp)
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
# The SOCKET half of the same idea, against the real kernel TCP stack. The `unit`
# aggregate's test_lines.cpp already proves the loopback happy path; this proves the
# NON-BLOCKING CONNECT, and above all the REFUSED connect that surfaces in select()'s
# except set -- the one Winsock behaviour docs/porting-notes.md flagged as most likely
# wrong. It uses only platform/socket.h, so it needs no hardware and no opt-in: a free
# loopback port is always there. Labelled `hw` because, like serialtest, it touches the
# real world (the kernel) rather than a ScriptedStream -- so `ctest -L hw` runs the
# whole real-world leg at once.
# ---------------------------------------------------------------------------
add_executable(altair_sockettest tests/sockettest.cpp)
target_link_libraries(altair_sockettest PRIVATE altair_core)
add_test(NAME socket-hw COMMAND altair_sockettest)
set_tests_properties(socket-hw PROPERTIES LABELS hw)

# ---------------------------------------------------------------------------
# The TERMINAL half, on a real console. The pipe path is proven in CI already
# (acceptance-basic4k/8k drive readInput's pipe branch and its broken-pipe EOF with a
# live guest); this proves the CONSOLE path a piped test can never reach -- SetConsoleMode
# raw mode, ENABLE_VIRTUAL_TERMINAL_INPUT, and the PeekConsoleInput peek-and-discard loop
# that keeps readInput non-blocking (porting-notes' #1 most-likely-wrong item).
#
# It lives in src/platform/win32/ and NOT in tests/ on purpose: proving that loop with no
# human means MANUFACTURING console input (WriteConsoleInput), which is a Win32 call, and
# DESIGN.md 2.1 keeps OS calls in the platform layer. A test of OS-specific code is
# OS-specific code -- so it sits beside the file it tests, is WIN32-only, and SKIPs (77)
# where no console can be taken. The POSIX terminal is proven the mirror way, by hand
# against a pty.
# ---------------------------------------------------------------------------
if(WIN32)
  add_executable(altair_terminaltest src/platform/win32/terminaltest_win32.cpp)
  target_link_libraries(altair_terminaltest PRIVATE altair_core)
  add_test(NAME terminal-hw COMMAND altair_terminaltest)
  set_tests_properties(terminal-hw PROPERTIES LABELS hw SKIP_RETURN_CODE 77)
endif()

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

# The Z80 exercisers -- the same gate, one core down. ZEXDOC validates documented
# behavior; ZEXALL additionally pins the undocumented F5/F3 bits. Each is ~46
# billion instructions, so both are `slow` and skipped by `ctest -LE slow`.
add_test(NAME cpu-zexdoc COMMAND altair_cputest ZEXDOC)
add_test(NAME cpu-zexall COMMAND altair_cputest ZEXALL)
set_tests_properties(cpu-zexdoc cpu-zexall PROPERTIES LABELS slow TIMEOUT 3600)

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
# THE HOST BRIDGE: a file crosses into CP/M and comes back byte for byte.
#
# TWO TESTS, ONE SCRIPT, AND THE SPLIT IS ABOUT WHAT EACH ONE IS ACTUALLY TESTING.
#
#   acceptance-hostbridge        THE CARD. Boots CP/M, LOADs the utilities from the .HEX
#                                that is checked in, and moves files -- including 256 bytes
#                                holding every value 00..FF, compared on the HOST's disk,
#                                which is the only place the answer actually is. Seconds.
#
#   acceptance-hostbridge-build  THE SOURCES. Assembles R/W/HDIR.ASM from scratch inside the
#                                machine (PIP through the console, ASM, LOAD) and proves the
#                                result is bit-for-bit the .COM/.HEX/.PRN committed beside
#                                them -- so a checked-in binary cannot rot. Then runs the
#                                SAME binary on an 88-MDS minidisk, which is how "every disk
#                                call is a BDOS call" gets proved rather than asserted.
#                                `slow`, because it only has anything to say when a .ASM
#                                changes.
#
# The reason the second one is slow is not the 8080 -- it is the WIRE. `PIP FOO.ASM=CON:` is
# the only expensive thing in either test: 78 KB of source, echoed back byte for byte, down
# a 2SIO that models a real serial line. R and W never touch the console at all.
#
# BOTH GATED ON THE 8 MB IMAGE, which is not in git (disks/ is an allowlist -- see
# .gitignore). A test that quietly passes because its input is missing is worse than none.
# ---------------------------------------------------------------------------
set(HB_DISK "${CMAKE_CURRENT_SOURCE_DIR}/disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK")
if(EXISTS "${HB_DISK}")
  add_test(NAME acceptance-hostbridge
           COMMAND ${CMAKE_COMMAND}
                   -DSIM=$<TARGET_FILE:altairsim>
                   -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                   -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                   -DMODE=fast
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/hostbridge.cmake)
  # ~1.5s. The 60 is a BACKSTOP, not a budget -- and the script has its own 30s leash
  # inside it, which fails with "THE MACHINE HUNG" instead of a confusing missing-string
  # error. A test with a generous timeout is a test that can waste an hour of your evening.
  set_tests_properties(acceptance-hostbridge PROPERTIES LABELS acceptance TIMEOUT 60)

  add_test(NAME acceptance-hostbridge-build
           COMMAND ${CMAKE_COMMAND}
                   -DSIM=$<TARGET_FILE:altairsim>
                   -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                   -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                   -DMODE=build
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/hostbridge.cmake)
  set_tests_properties(acceptance-hostbridge-build PROPERTIES
                       LABELS "acceptance;slow" TIMEOUT 360)
else()
  message(STATUS "8 MB CP/M image not found -- SKIPPING the Host Bridge acceptance tests. "
                 "See disks/mits-88dcdd/cpm22/8mb/README.md to download it.")
endif()

# ---------------------------------------------------------------------------
# WHICH MACHINE DID I GET? ./altairsim.toml, found in the working directory.
#
# The one file the simulator FINDS rather than is GIVEN -- and the only thing keeping
# that from being the trap looksLikeFile() refuses to walk into is that it happens ONLY
# on a command line that names nothing. So most of this test is the negative controls:
# `altairsim basic4k`, `-m default` and `-n` must all be stone deaf to the directory
# they run in. If they ever are not, `altairsim basic4k` has started meaning different
# things in different places, which is precisely the 2am bug the CLI is arranged to
# prevent -- reintroduced by the back door, by a convenience.
# ---------------------------------------------------------------------------
add_test(NAME acceptance-cwdconfig
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/cwdconfig.cmake)
set_tests_properties(acceptance-cwdconfig PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# THE EXAMPLES, IN THE LAYOUT WE ACTUALLY SHIP THEM IN.
#
# tapes/ and disks/ are not fixtures -- they are the product. A user gets the binary and
# those two trees, and NOT this repository. Every other test in this file runs from the
# repo root, which is the one arrangement in which a repo-root-relative path in an example
# looks like it works. So this test copies an example OUT of the tree and boots it from
# there, which is the only place the truth shows up.
# ---------------------------------------------------------------------------
add_test(NAME acceptance-examples
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/examples.cmake)
set_tests_properties(acceptance-examples PROPERTIES LABELS acceptance TIMEOUT 120)

# ---------------------------------------------------------------------------
# THE MANUAL'S REFERENCE MAY NOT DRIFT FROM THE BINARY.
#
# docs/manual/ref/*.md is committed (a reader of the repo, and the PDF build, should need
# no toolchain to see it) and generated (a hand-typed parameter table is a second schema,
# and the first draft of one in this very project got three of the memory card's eight
# defaults wrong). Committed + generated = it can rot, so it is tested.
# ---------------------------------------------------------------------------
add_test(NAME docs-reference
         COMMAND ${CMAKE_COMMAND}
                 -DGEN=$<TARGET_FILE:altair_genref>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/docs-reference.cmake)
set_tests_properties(docs-reference PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# THE USER MANUAL MAY NOT NAME ANYTHING THE READER DOES NOT HAVE.
#
# It ships in the zip, next to the binary and the disks. There is no src/, no CMakeLists and
# no DESIGN.md in there. "Self-contained" erodes one helpful cross-reference at a time, in
# good faith, and nobody notices until a user follows one -- so it is a test, not a habit.
# The Developer Guide is deliberately exempt: it is FOR the source.
# ---------------------------------------------------------------------------
add_test(NAME docs-manual
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/docs-manual.cmake)
set_tests_properties(docs-manual PROPERTIES LABELS acceptance TIMEOUT 60)

# ---------------------------------------------------------------------------
# The PDFs. NOT part of the build, and it must never become part of it.
#
# altairsim's loudest claim is that it builds with a C++20 compiler and CMake and nothing
# else. A PDF wants pandoc and a browser, and neither is allowed anywhere near the thing that
# produces the simulator. So this is a target you ASK for, and its absence is not an error --
# `cmake -S . -B build` on a machine with no pandoc must still configure and still build.
#
# YOU DO NOT HAVE TO RUN THIS ANY MORE, and you do not have to remember to commit what it
# makes: .github/workflows/docs.yml rebuilds both PDFs on every push to master that touches
# docs/, and commits them if their text actually moved. Nobody remembering was exactly the
# problem -- the shipped developer guide spent two days insisting the project had no CI.
#
# This target is still worth having: it is how you SEE a change before you push it, and the
# output is the same as CI's, because the fonts travel inside the document (docs/fonts/) and
# CI pins the same pandoc. Committing its output is fine and so is not committing it -- if
# your PDF matches, CI keeps it; if it does not, CI replaces it. What you must not do is
# hand-edit a PDF, or expect the committed one to stay as you built it.
# ---------------------------------------------------------------------------
add_custom_target(docs
  COMMAND "${CMAKE_SOURCE_DIR}/tools/build-docs.sh"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Building altairsim-manual.pdf and altairsim-devguide.pdf"
  VERBATIM)
add_dependencies(docs docs-reference)

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

  # ---------------------------------------------------------------------------
  # THE 88-2SIO, DRIVEN BY REAL PERIOD SOFTWARE -- polled AND interrupt-driven.
  #
  # Two of Mike Douglas's 2SIO test programs (tests/programs/2sio, see PROVENANCE.md), a
  # dozen bytes each. ECHO polls; ECHOINT enables receive interrupts and then does nothing
  # but NOP -- so every character it echoes has to travel the whole chain, with the guest
  # never touching a register. If the receiver only advanced when a register was read,
  # ECHOINT would echo nothing for ever.
  #
  # The script carries its OWN negative control (ECHOINT with the jumper out must go deaf),
  # so this one test cannot pass for the wrong reason and needs no WILL_FAIL twin.
  #
  # It is FAST -- no tape, no disk, no monitor -- which is the point: `ps2int` covers the
  # same wire, but it takes a cassette boot and 2.5K of MITS monitor to say so.
  add_test(NAME acceptance-2sio-echo
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/2sio-echo.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-2sio-echo PROPERTIES
                       LABELS acceptance TIMEOUT 120
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # THE 88-MDS: CP/M 2.2b off a 5.25" Altair minidisk.
  #
  # IT HAS TO BE AN EXPECT TEST. CP/M's DIR polls the console between directory entries and
  # ABORTS the listing if a key is waiting -- so a buffered keystroke file has its SECOND
  # command silently truncate its FIRST command's output. You get one filename, and it looks
  # exactly like a card that only read one sector. The input must be paced.
  #
  # AND IT IS GATED ON THE DISK, which is not in git (disks/ is an allowlist -- see
  # .gitignore). A test that quietly passes because its input is missing is worse than no
  # test, so if the image is not there we say so and skip.
  # ---------------------------------------------------------------------------
  set(MDS_DISK "${CMAKE_CURRENT_SOURCE_DIR}/disks/mits-88mds/cpm22/CPM56K-1.DSK")
  if(EXISTS "${MDS_DISK}")
    add_test(NAME acceptance-minidisk
             COMMAND ${EXPECT_EXECUTABLE}
                     -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/minidisk.exp
                     $<TARGET_FILE:altairsim>)
    set_tests_properties(acceptance-minidisk PROPERTIES
                         LABELS acceptance TIMEOUT 120
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # ...AND THE NEGATIVE CONTROL, which is the only reason to believe the test above.
    #
    # The SAME script against the SAME disk in an 88-DCDD -- which is what this tree actually
    # did for months -- must FAIL. It now refuses at MOUNT: 76,800 bytes matches no format an
    # 8" controller knows. If this ever starts booting, the minidisk has crept back into the
    # wrong card and acceptance-minidisk has stopped testing the 88-MDS.
    #
    # (My FIRST control swapped the PROM instead -- DBL for MDBL -- and IT BOOTED, because the
    # two controllers are register-compatible and Mike's own loader says DBL loads it. The
    # control did its job: it disproved my claim, not the card. See minidisk-dcdd.toml.)
    add_test(NAME acceptance-minidisk-control
             COMMAND ${EXPECT_EXECUTABLE}
                     -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/minidisk.exp
                     $<TARGET_FILE:altairsim> tests/acceptance/minidisk-dcdd.toml)
    set_tests_properties(acceptance-minidisk-control PROPERTIES
                         LABELS acceptance TIMEOUT 60 WILL_FAIL TRUE
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  else()
    message(STATUS "disks/mits-88mds/cpm22/CPM56K-1.DSK not found -- SKIPPING the 88-MDS "
                   "acceptance test. See disks/mits-88mds/cpm22/README.md to download it.")
  endif()

  # ---------------------------------------------------------------------------
  # A WRITE TO A WRITE-PROTECTED DISK MUST NOT HANG THE GUEST.
  #
  # It used to. armWrite() saw readOnly() and returned without setting writing_, so ENWD --
  # the bit the BIOS polls to be handed the next byte -- never asserted, and every period BIOS
  # polls it in a loop with no timeout. The card did not fail the write, it WEDGED THE MACHINE.
  #
  # WHICH IS WHY THE TIMEOUT IS THE ASSERTION. The old code does not print a wrong string here;
  # it prints NOTHING, forever. 90s is generous on purpose: a timeout that fires under load
  # would be indistinguishable from the bug, and this is the one test where that would be a
  # false ALARM rather than a false pass.
  #
  # Gated on the same disk as the buffered CP/M, and it MOUNTS THAT DISK READ-ONLY -- the test
  # cannot dirty it, and it shasums the image to prove it.
  # ---------------------------------------------------------------------------
  set(DCDD_DISK "${CMAKE_CURRENT_SOURCE_DIR}/disks/mits-88dcdd/cpm22/buffered/cpm22b23-56k.dsk")
  if(EXISTS "${DCDD_DISK}")
    add_test(NAME acceptance-dcdd-readonly
             COMMAND ${EXPECT_EXECUTABLE}
                     -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/dcdd-readonly.exp
                     $<TARGET_FILE:altairsim>)
    set_tests_properties(acceptance-dcdd-readonly PROPERTIES
                         LABELS acceptance TIMEOUT 90
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  else()
    message(STATUS "disks/mits-88dcdd/cpm22/buffered/cpm22b23-56k.dsk not found -- SKIPPING "
                   "acceptance-dcdd-readonly (the write-protected-disk hang).")
  endif()

  # ---------------------------------------------------------------------------
  # TWO GEOMETRIES, ONE CONTROLLER: an 8 MB image in drive 0 and a 77-track floppy in drive
  # 2, at the same time. The FDC+ manual says to do exactly this (3.7.4), and it is the only
  # door in or out of an 8 MB system -- so it is the arrangement people will actually run.
  #
  # It works only because the format and the Spindle are PER DRIVE. The day either one gets
  # promoted to the card, this test breaks -- and it breaks as CORRUPTION, not as an error,
  # which is why it weighs the bytes instead of reading a directory listing.
  #
  # IT IS ALSO THE REGRESSION TEST FOR THE 24K IMAGE. It reads R.COM off the floppy, and
  # R.COM is only there because the host-bridge utilities were installed through this very
  # path -- a 24K CP/M cannot boot in a 56K machine, so it can install nothing for itself.
  #
  # Needs both images, and neither is in git. It copies both into the build tree first: the
  # write half of it would otherwise land in the repository's own floppy.
  # ---------------------------------------------------------------------------
  set(MIX_8IN "${CMAKE_CURRENT_SOURCE_DIR}/disks/mits-88dcdd/cpm22/buffered/cpm22b23-24k.dsk")
  if(EXISTS "${HB_DISK}" AND EXISTS "${MIX_8IN}")
    add_test(NAME acceptance-dcdd-mixed
             COMMAND ${EXPECT_EXECUTABLE}
                     -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/dcdd-mixed.exp
                     $<TARGET_FILE:altairsim>
                     ${CMAKE_CURRENT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/dcdd-mixed)
    set_tests_properties(acceptance-dcdd-mixed PROPERTIES
                         LABELS acceptance TIMEOUT 120
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  else()
    message(STATUS "no 8 MB and/or 24K image -- SKIPPING acceptance-dcdd-mixed (an 8 MB disk "
                   "and a 77-track floppy in one 88-DCDD).")
  endif()
else()
  message(STATUS "expect not found -- SKIPPING the interactive CLI test (acceptance-cli). "
                 "The tty paths (prompt echo, ATTN, clock pacing) will not be covered.")
endif()
