cmake_minimum_required(VERSION 3.20)
project(altairsim VERSION 0.3.0 LANGUAGES CXX)

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
# SANITIZE -- AddressSanitizer + UndefinedBehaviorSanitizer, in one flag.
#
# This is the LOCAL ORACLE for the lifetime bugs that only crash on Windows. Twice
# now (the SNAPSHOT 0xC0000409, then the VersaFloppy FDC test) the `unit` suite has
# SEGFAULTed on Windows CI while Mac and Linux stayed green -- because the macOS
# allocator silently tolerates a use-after-scope that MSVC's does not. The cause each
# time was a board/chip test whose locals destroy in the wrong order: the Clock dies
# first, then the board's destructor calls clock_->cancel() on freed memory. No
# compiler warning fires; only a sanitizer names the exact file:line.
#
# Reproducing that used to be a hand-typed recipe -- a fresh build dir, an explicit
# generator, and the -fsanitize flags threaded through BOTH CMAKE_CXX_FLAGS and
# CMAKE_EXE_LINKER_FLAGS. This is that recipe as one word. Configure a side tree with
# it and the whole binary (library, tools, tests) is instrumented:
#
#   cmake -S . -B build-asan -DSANITIZE=on
#   cmake --build build-asan --target altair_tests
#   ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ./build-asan/altair_tests
#
# Keep it in its OWN build directory -- an instrumented binary is slower and is not
# what you ship. It is deliberately NOT on by default and NOT wired into CI: CI catches
# the Windows crash by running on Windows, and this is the tool you reach for once it
# does. On a board/chip lifetime change, run it before merging.
#
# GCC/Clang get both sanitizers. MSVC has had /fsanitize=address since VS2019 and it
# catches the same use-after-free class, so honour the flag there too -- it just has no
# UBSan half to add. (MSVC ASan wants the RELEASE CRT and its runtime DLL on PATH; a
# Debug config or a stale PATH is the usual "it built but won't launch".)
# ---------------------------------------------------------------------------
option(SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (dev only)" OFF)
if(SANITIZE)
  if(MSVC)
    add_compile_options(/fsanitize=address)
    message(STATUS "SANITIZE=on -- AddressSanitizer (MSVC has no UBSan). "
                   "Use a Release config and keep the ASan runtime DLL on PATH.")
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "SANITIZE=on -- Address + UndefinedBehavior sanitizers active. "
                   "Run with ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1.")
  endif()
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
    # The SHOW ROMS description is read from roms/<NAME>/DESC, so editing one must
    # re-run the embed -- same as changing the image itself.
    file(GLOB descsrc CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/roms/${d}/DESC")
    list(APPEND ROM_IMAGE_FILES ${descsrc})
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
# Which commit this binary is.
#
# Between releases, MOST binaries are untagged: every CI artifact, every local
# build, anything handed to somebody to try. `altairsim 0.1.0` names all of them
# identically, so a bug report against one cannot be traced to the source that
# produced it. `git describe` can, and the build is the only place that knows.
#
# THE FALLBACK IS "unknown", AND THAT IS DELIBERATE. A release tarball has no .git
# in it, and neither does a source zip; there is nothing to describe and nothing
# to infer from. Writing PROJECT_VERSION there instead would produce a string that
# looks like provenance and is not -- and the way you find out it lied is by
# spending an afternoon on a bug in a revision the binary was never built from.
#
# CONFIGURE TIME, not build time: git runs once and the answer is baked into one
# generated header, so every object file in the binary agrees about it and no
# compile shells out. `git status` is consulted too, since
# "which commit" is a half-answer for a tree with uncommitted edits in it -- which
# is exactly the tree most hand-built binaries come from. Both are a snapshot; the
# CONFIGURE_DEPENDS below re-runs this when HEAD moves, and a build that only
# edited files still reports the dirty flag it was configured with. Re-run cmake if
# you need it exact.
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(ALTAIRSIM_GIT_COMMIT "unknown")
set(ALTAIRSIM_GIT_DIRTY 0)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --always
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE git_describe
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE git_rc)
  # A non-zero status means git could not answer -- no commits yet, or this is not a
  # work tree after all. Leave it "unknown"; a half-captured sha is worse than none.
  if(git_rc EQUAL 0 AND git_describe)
    set(ALTAIRSIM_GIT_COMMIT "${git_describe}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE git_status
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE status_rc)
    # Untracked files are excluded on purpose: a build/ directory, an editor's swap
    # file and a scratch disk image are not modifications to the source this binary
    # was built from, and a dirty flag that is always set says nothing.
    if(status_rc EQUAL 0 AND git_status)
      set(ALTAIRSIM_GIT_DIRTY 1)
    endif()
  endif()
  # So that a commit, a checkout or a rebase re-configures and the sha follows. HEAD
  # is the file that moves in all three; the ref it names moves when a commit lands on
  # the branch you are already on.
  #
  # ONLY FOR AN ORDINARY .git DIRECTORY. In a linked work tree `.git` is a FILE naming
  # the real gitdir elsewhere, so there is no .git/HEAD here to watch -- the version is
  # still captured correctly above (git itself follows the pointer), it just does not
  # re-configure by itself. Chasing that indirection would buy one convenience and a
  # second parser for git's on-disk layout.
  if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git" AND EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/.git/HEAD")
    file(READ "${CMAKE_SOURCE_DIR}/.git/HEAD" head_contents)
    if(head_contents MATCHES "^ref: ([^\r\n]+)")
      set(head_ref "${CMAKE_SOURCE_DIR}/.git/${CMAKE_MATCH_1}")
      if(EXISTS "${head_ref}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${head_ref}")
      endif()
    endif()
  endif()
endif()

configure_file("${CMAKE_SOURCE_DIR}/cmake/version.h.in"
               "${CMAKE_BINARY_DIR}/generated/version_generated.h" @ONLY)

# ---------------------------------------------------------------------------
# The platform layer (DESIGN.md 2.1). NO #ifdefs -- ANYWHERE.
#
# OS differences are one interface header with zero conditionals in it, plus one
# implementation file per OS, and CMAKE picks the directory. This block is the
# entire mechanism, and it is the only place in the build that asks what OS it is
# on. Nothing in src/ ever does.
#
# The win32 half is FIELD-PROVEN, not merely written: serial against two real FTDI
# ports, socket against the real TCP stack, terminal against a real console, each a
# leg of `ctest -L hw` (docs/porting-notes.md has the counts). This comment used to
# say it was written-but-unbuilt and cite that same file as agreeing; the file has
# said the opposite since 2026-07-15.
#
# MSVC is the ONLY supported Windows toolchain -- it is what CI builds and what is shipped.
# Nothing here is knowingly MSVC-only (no _MSC_VER anywhere in src/, lowercase Windows headers,
# plain main()); MSVC is simply the only toolchain tested and shipped.
# ---------------------------------------------------------------------------
if(WIN32)
  set(PLATFORM_SOURCES
    src/platform/win32/localtime_win32.cpp
    src/platform/win32/serial_win32.cpp
    src/platform/win32/socket_win32.cpp
    src/platform/win32/terminal_win32.cpp)
else()
  set(PLATFORM_SOURCES
    src/platform/posix/localtime_posix.cpp
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
  src/core/version.cpp
  src/core/crc32.cpp
  src/core/statefile.cpp
  src/core/hex.cpp
  src/core/symbols.cpp
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
  # `ModemLine` -- a software-controlled phone line: an inbound connection is a RING
  # held silent until the guest answers, an outbound dial happens on off-hook, and an
  # idle modem holds no sockets. The PMMI MM-103 (Phase 2) drives it; unlike tcp.cpp's
  # auto-answer server it never picks up on its own.
  src/host/modemline.cpp
  # `file:PATH` -- a host file as a write-only sink: a printout, or a capture of a
  # line. A SINK, so it is trivial next to hostserial/tcp (no pump, no read path).
  src/host/file.cpp
  # `printer:QUEUE` -- a host print queue as a write-only sink that buffers bytes into
  # JOBS (host/printer_stream.h, issue #70). Always compiled and pure: it knows how to
  # decide where one job ends but nothing about CUPS -- the platform submit backend is
  # linked separately and only when found (the printer feature block below).
  src/host/printer_stream.cpp
  # `ENDPOINT|FILE` -- a transparent hex TAP on any line (host/tee_stream.h). A
  # decorator ByteStream: it copies bytes past in both directions and forwards the rest,
  # observe-only and 8-bit clean, so it is exempt from the "one FilterStream" rule.
  src/host/tee_stream.cpp
  src/host/media.cpp
  src/host/disk.cpp
  src/host/tape.cpp
  # The cassette modem and the container it arrives in. Neither knows what a board is:
  # a modulation is four numbers and a flag (host/tapemodem.h), so the 88-ACR and the
  # Sol-20 differ here as VALUES and not as code. Listed by the layering -- a WAV is a
  # container, an FSK signal is a modulation, a program image is neither.
  src/host/wav.cpp
  src/host/tapemodem.cpp
  # ...and the seam that joins them to a mount. A decorator over MediaFile, so a WAV
  # decodes ONCE at MOUNT and everything above it -- TapeImage, TapeStream, the UART --
  # sees the bytes a .TAP would have given it (host/tapecodec.h).
  src/host/tapecodec.cpp
  # The sandboxed host directory the Host Bridge reaches through. It is here, and not
  # on the board, because the board must never see a path, a symlink or a file handle
  # -- and because the escape tests that matter are tests of THIS, with no card in
  # sight (DESIGN.md 7, src/host/hostdir.h).
  src/host/hostdir.cpp
  # The chips. A chip is not a card: it is modeled from its DATA SHEET, it knows
  # nothing about S-100, and it is shared by every card that has one soldered to it.
  src/chips/mc6850.cpp
  # The TMS 5501 -- TI's NMOS I/O controller (UART + five timers + an eight-source
  # interrupt controller). Phase 1 models the UART half only; the Cromemco 4FDC/16FDC/
  # 64FDC each carry one as a console channel, and the TU-ART carries two. See
  # src/chips/tms5501.h.
  src/chips/tms5501.cpp
  src/chips/sio2port.cpp
  src/chips/uart1602.cpp
  # The Intel 8251 USART -- the console chip of the SD Systems SBC-100/200, and the
  # next 8251 card to turn up gets it for free (src/chips/intel8251.h).
  src/chips/intel8251.cpp
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
  # The 88-HDSK Datakeeper hard disk -- a "controller does the work" card (Board
  # directly, not HardSectorFdc): it decodes A0h-A7h and serves 256-byte sectors from
  # a linear .DSK through a command/handshake protocol. See src/boards/mits-88hdsk.h.
  src/boards/mits-88hdsk.cpp
  src/boards/mits-2sio.cpp
  src/boards/mits-88sio.cpp
  # SD Systems SBC-100/200 -- the 8251 serial console (Phase 1). Structural twin of
  # the 88-SIO, embedding one Intel8251. See src/boards/sd-sbc.h.
  src/boards/sd-sbc.cpp
  # SD Systems VersaFloppy I/II -- a WD FD177x soft-sector floppy controller. Owns a
  # wd17xx chip and a generic DiskImageDrive adapter (floppy-drive). Boots SDOS.
  src/boards/sd-versafloppy.cpp
  # Tarbell #1011 (SD) / #2022 (DD) -- one card carrying a WD FD177x floppy controller
  # AND a 32-byte boot PROM that shadows low RAM over PHANTOM*. See src/boards/tarbell.h.
  src/boards/tarbell.cpp
  # Cromemco 4FDC/16FDC/64FDC family -- one card carrying a WD FD177x floppy controller, a
  # TMS 5501 console UART, and an RDOS boot PROM at C000. A base (CromemcoFdcBoard) plus the
  # 16FDC/64FDC leaves (header-only); boots CDOS. See src/boards/cromemco-fdc.h.
  src/boards/cromemco-fdc.cpp
  src/boards/floppy-drive.cpp
  # The 88-ACR derives from the 88-SIO, because the card IS an 88-SIO B with a modem
  # mated to it -- see mits-88acr.h. It must be listed after it for no reason at all;
  # this list is not ordered. It is listed after it so the reader sees the pair.
  src/boards/mits-88acr.cpp
  # The 88-UIO -- a 6850 serial port AND an 88-ACR cassette section on one board. It
  # DERIVES from the 88-ACR (the cassette half is one, verbatim, plus motor control and a
  # SW-1 modulation switch) and embeds a Sio2Port for the serial half. See mits-88uio.h.
  src/boards/mits-88uio.cpp
  # The 88-C700 line-printer controller -- an output-only card that sinks its data
  # port to a ByteStream (a file, the console, a socket). No chip: a printer sends
  # nothing back, so there is no receiver to model. See mits-88c700.h.
  src/boards/mits-88c700.cpp
  # The 88-LPC line-printer controller -- the C700's sibling for the 88-LP printer,
  # but line-buffered: 6-bit codes decoded to glyphs, committed as a line on PRINT or
  # at 80 chars. Sinks to a ByteStream like the C700. See mits-88lpc.h.
  src/boards/mits-88lpc.cpp
  # The 88-PIO parallel port: a bidirectional latch pair (out/in), each a ByteStream.
  # The 88-4PIO: up to four 6820 PIAs with software-set direction. See mits-88pio.h.
  src/boards/mits-88pio.cpp
  src/boards/mits-884pio.cpp
  # The Processor Technology VDM-1: memory-mapped 16x64 video. It renders into a
  # host Display (src/host/display.h) injected at the composition root -- an
  # SdlDisplay in the shipping binary, a NullDisplay headless -- so the card itself
  # links with no graphics library and is tested with no window. See proctech-vdm1.h.
  src/boards/proctech-vdm1.cpp
  # The Cromemco Dazzler: color graphics scanned from a framebuffer in main RAM.
  # Renders into the same host Display seam as the VDM-1; no bus-mastering. See
  # cromemco-dazzler.h.
  src/boards/cromemco-dazzler.cpp
  # The SD Systems VDB-8024: an 80x24 video terminal on one board (two I/O ports, NOT
  # memory-mapped), the video console alternative to the SBC's 8251. Renders the same
  # host Display seam as the VDM-1 and takes its keyboard from a ByteStream like the
  # Sol. See sd-vdb8024.h.
  src/boards/sd-vdb8024.cpp
  # The Cromemco D+7A: analog + parallel I/O. A block of eight ports -- one parallel
  # port and seven two's-complement A/D-in/D/A-out channels -- that reads one or two
  # JS-1 joysticks from an injected host Joystick service (src/host/joystick.h), like
  # the Dazzler reads a Display. See cromemco-d7a.h.
  src/boards/cromemco-d7a.cpp
  # The Processor Technology Sol-PC's integrated I/O as one composite card (serial,
  # keyboard, parallel, CUTS tape) -- reuses the vdm1 card for video. See proctech-sol.h.
  src/boards/pmmi-mm103.cpp
  src/boards/usio.cpp
  src/boards/proctech-sol.cpp
  src/boards/mits-frontpanel.cpp
  src/boards/mits-turnkey.cpp
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

target_include_directories(altair_core PUBLIC src "${CMAKE_BINARY_DIR}/generated")

# ---------------------------------------------------------------------------
# The optional video backend: SDL3 (DESIGN.md 7.4). THE FIRST OPTIONAL LINK-TIME
# DEPENDENCY, and it is DETECTED, not required.
#
# altairsim's loudest claim is that it builds with a C++20 compiler and CMake and
# nothing else -- so a graphics library cannot be a hard dependency. This block
# auto-detects an installed SDL3: found, and the video boards get a real window;
# absent, and they build headless against a NullDisplay (src/host/display_null.h)
# and every test still passes. Either way the build configures and succeeds. See
# docs/devguide/ for how to install SDL3.
#
# ALTAIRSIM_ENABLE_SDL is ON by default (= "use SDL3 if it is here"); set it OFF to
# force a headless build even where SDL3 is installed. That is what a macOS UNIVERSAL
# build needs *against a Homebrew SDL3*, which is single-arch (arm64) and will not link
# into a fat binary. It is NOT a limit of SDL3: upstream's SDL3.xcframework carries a
# macos-arm64_x86_64 slice that is genuinely universal, so a universal windowed build is
# possible -- link that rather than brew's dylib.
#
# EXACTLY ONE CI LEG HAS SDL3: macOS, which installs it from Homebrew and builds native
# arm64. It is therefore the only place display_sdl.cpp is compiled, and the workflow
# FAILS that leg if it comes up headless -- otherwise the coverage lapses in silence,
# which is what happened before. Linux and Windows still take the not-found path.
#
# (Through v0.2.0 no leg had SDL3 at all, so nothing compiled display_sdl.cpp and every
# shipped binary was headless. This comment also used to claim the macOS leg passed
# -DALTAIRSIM_ENABLE_SDL=OFF; it never did.)
#
# ONLY src/host/display_sdl.cpp includes SDL. The boards see host/display.h and
# never learn a window exists, so they compile in every configuration; only this
# one translation unit and the composition root (src/main.cpp) are macro-gated.
# SDL is cross-platform, not an OS header, so it does not trip the 2.1 platform_lint.
# ---------------------------------------------------------------------------
option(ALTAIRSIM_ENABLE_SDL "Build the SDL3-backed video boards if SDL3 is found" ON)
if(ALTAIRSIM_ENABLE_SDL)
  find_package(SDL3 CONFIG QUIET)
  if(SDL3_FOUND)
    target_sources(altair_core PRIVATE src/host/display_sdl.cpp)
    # The SDL3-backed game-controller service (host/joystick.h): a D+7A reads its JS-1
    # sticks from this. Part of the SAME SDL3::SDL3 already linked -- gamepad and audio
    # need no extra find_package. It self-manages the SDL_INIT_GAMEPAD subsystem, so it
    # never contends with the display's video subsystem. See host/joystick_sdl.h.
    target_sources(altair_core PRIVATE src/host/joystick_sdl.cpp)
    target_link_libraries(altair_core PUBLIC SDL3::SDL3)
    target_compile_definitions(altair_core PUBLIC ALTAIRSIM_ENABLE_SDL)

    # Whether the process may be brought to the foreground, and when (2.1: one file per
    # OS, CMake picks; src/platform/foreground.h says why it is needed at all). It is
    # selected HERE, inside the SDL block, rather than alongside the other platform
    # sources, because only a video board can raise the question -- so a headless build
    # never compiles it and macOS never links AppKit for a simulator with no window.
    if(APPLE)
      enable_language(OBJCXX)
      target_sources(altair_core PRIVATE src/platform/posix/macos/foreground_macos.mm)
      target_link_libraries(altair_core PUBLIC "-framework AppKit")
    elseif(WIN32)
      target_sources(altair_core PRIVATE src/platform/win32/foreground_win32.cpp)
    else()
      target_sources(altair_core PRIVATE src/platform/posix/foreground_posix.cpp)
    endif()

    message(STATUS "SDL3 found -- video boards enabled (windowed)")
  else()
    message(STATUS "SDL3 not found -- video boards build headless (null display). "
                   "Install SDL3 (see docs/devguide) for a window.")
  endif()
endif()

# ---------------------------------------------------------------------------
# The optional host-printer backend (docs/printing.md; issue #70). THE SECOND
# optional link-time dependency, and it follows the SDL3 rule above exactly: ON by
# default meaning "use it if it is here", a quiet detect, a message either way, and a
# build without it where `printer:` is simply not in the endpoint grammar and the
# resolver says so by name -- the one endpoint that cannot work on a given host fails
# at CONNECT with a sentence, not at compile.
#
# host/printer_stream.cpp is NOT here: it is pure (it decides job boundaries, it does
# not talk to a spooler) and is always compiled, so its boundary policy is tested with
# a fake sink on every platform, CUPS or no CUPS. Only the platform submit TU and the
# ALTAIRSIM_ENABLE_PRINTER define that lights up the resolver arm are gated.
#
# macOS AND Linux are the SAME leg: both sit on CUPS, so one file (printer_cups.cpp)
# serves both and is selected with find_package(Cups) -- CMake's own module, which
# gives the imported Cups::Cups target. NOT pkg_check_modules: pkg-config is not
# guaranteed on a macOS dev box (this one has cups-config, not pkg-config), and a
# REQUIRED pkg check there would fail to configure a build that should just quietly
# skip the feature. Windows is WinSpool and is a later branch (issue #70 Phase 3).
option(ALTAIRSIM_ENABLE_PRINTER "Print to a host queue if CUPS/WinSpool is found" ON)
if(ALTAIRSIM_ENABLE_PRINTER)
  if(WIN32)
    # Phase 3 -- the file does not exist yet, so the feature is off on Windows until
    # it lands. Left here as the shape it will take (WinSpool, StartDocPrinter RAW).
    message(STATUS "printer: endpoint not yet built on Windows (issue #70 Phase 3)")
  else()
    find_package(Cups QUIET)
    if(Cups_FOUND)
      target_sources(altair_core PRIVATE src/platform/posix/printer_cups.cpp)
      target_link_libraries(altair_core PUBLIC Cups::Cups)
      target_compile_definitions(altair_core PUBLIC ALTAIRSIM_ENABLE_PRINTER)
      message(STATUS "CUPS found -- printer: endpoint enabled")
    else()
      message(STATUS "CUPS not found -- printer: endpoint disabled. "
                     "Install libcups2-dev (Linux) for host printing.")
    endif()
  endif()
endif()

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

# tapetool -- cassette audio in and out, with no machine attached. Same justification
# as altair_genref above: it is the tool you need BEFORE the simulator works, when the
# question is "what is actually on this tape?". See tools/tapetool.cpp.
add_executable(altair_tapetool tools/tapetool.cpp)
target_link_libraries(altair_tapetool PRIVATE altair_core)

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
  tests/test_statefile.cpp
  tests/test_snapshot.cpp
  tests/test_memory.cpp
  tests/test_hex.cpp
  tests/test_symbols.cpp
  tests/test_media.cpp
  tests/test_tapecodec.cpp
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
  tests/test_ddt.cpp
  tests/test_dma.cpp
  tests/test_sio2.cpp
  tests/test_tms5501.cpp
  tests/test_88sio.cpp
  tests/test_sbc.cpp
  tests/test_88acr.cpp
  tests/test_88uio.cpp
  tests/test_c700.cpp
  tests/test_papertape.cpp
  tests/test_pmmi.cpp
  tests/test_usio.cpp
  tests/test_lpc.cpp
  tests/test_printer.cpp
  tests/test_tee.cpp
  tests/test_pio.cpp
  tests/test_vdb8024.cpp
  tests/test_4pio.cpp
  tests/test_vdm1.cpp
  tests/test_dazzler.cpp
  tests/test_d7a.cpp
  tests/test_sol.cpp
  tests/test_tapemount.cpp
  tests/test_lines.cpp
  tests/test_modemline.cpp
  tests/test_wd17xx.cpp
  tests/test_versafloppy.cpp
  tests/test_tarbell.cpp
  tests/test_cromemco-fdc.cpp
  tests/test_spindle.cpp
  tests/test_dcdd.cpp
  tests/test_mds.cpp
  tests/test_hdsk.cpp
  tests/test_frontpanel.cpp
  tests/test_turnkey.cpp
  tests/test_lamp.cpp
  tests/test_virtc.cpp
  tests/test_hostdir.cpp
  tests/test_hostbridge.cpp
  tests/test_mcp.cpp)
target_link_libraries(altair_tests PRIVATE altair_core)
# A couple of sections mount a real committed disk image (the SDOS video boot in
# test_vdb8024.cpp); `ctest` runs `unit` from the build tree, so give it the source root
# to resolve those paths -- the same trick as altair_cputest's ALTAIR_CPU_TEST_DIR.
target_compile_definitions(altair_tests PRIVATE ALTAIR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
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
# where no console can be taken. The POSIX terminal has its own mirror of it, below.
# ---------------------------------------------------------------------------
if(WIN32)
  add_executable(altair_terminaltest src/platform/win32/terminaltest_win32.cpp)
  target_link_libraries(altair_terminaltest PRIVATE altair_core)
  add_test(NAME terminal-hw COMMAND altair_terminaltest)
  set_tests_properties(terminal-hw PROPERTIES LABELS hw SKIP_RETURN_CODE 77)
else()
  # The POSIX mirror, and it is not optional any more. "Proven by hand against a pty"
  # is what let issue #25 through: an empty read on a VMIN=0 tty returns 0, the same
  # as a hung-up pipe, and reporting both as the end of input latched Console::eof_ on
  # every interactive run. It failed for a user in a terminal and passed here and in
  # CI, every time, because a piped stdin never takes that branch. This one opens a
  # pty and takes it. It SKIPs (77) where no pty can be had.
  add_executable(altair_terminaltest src/platform/posix/terminaltest_posix.cpp)
  target_link_libraries(altair_terminaltest PRIVATE altair_core)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(altair_terminaltest PRIVATE util)  # openpty
  endif()
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

# ...and THE SAME BASIC off cassette AUDIO. Same artifact, same bootstrap, same banner --
# with the modulator, the RIFF container and the demodulator spliced into the middle. The
# WAV is generated here at test time rather than committed: 6.4 MB of binary in git would
# have proved LESS, because generating it puts the modulator under test too.
add_test(NAME acceptance-basic4k-wav
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DTAPETOOL=$<TARGET_FILE:altair_tapetool>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}/tape-acceptance
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/basic4k-wav.cmake)
set_tests_properties(acceptance-basic4k-wav PROPERTIES LABELS acceptance TIMEOUT 120)

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
# ONE TEST, AND A SECOND MODE OF THE SAME SCRIPT THAT IS RUN BY HAND.
#
#   acceptance-hostbridge   THE CARD. Boots CP/M off the TRACKED floppy
#                           (examples/cpm/cpm22b23-56k.dsk), LOADs the utilities from the
#                           .HEX that is checked in, and moves files -- including 256 bytes
#                           holding every value 00..FF, compared on the HOST's disk, which
#                           is the only place the answer actually is. About 1.5 seconds, on
#                           a fresh clone and in CI, with nothing to download.
#
#   MODE=build              THE SOURCES. Assembles R/W/HDIR.ASM from scratch inside the
#                           machine (PIP through the console, ASM, LOAD) and proves the
#                           result is bit-for-bit the .COM/.HEX/.PRN committed beside them.
#                           NOT A REGISTERED TEST -- see below.
#
# THE GAP THAT LEAVES IS REAL AND WORTH NAMING: the committed .HEX is checked against the
# committed .COM, but the .ASM behind them is not checked at all unless somebody runs the
# build mode. That is the right way round -- a stale .HEX breaks the utilities a user is
# handed, a stale .ASM breaks the next person to edit them -- but it is a gap, not a
# coincidence. Edit a .ASM and the check is YOURS to run.
#
# WHY IT CANNOT SIMPLY MOVE TO THE TRACKED DISK: PIPing three .ASM into the guest needs
# 78 KB of free disk and the floppy arrives with 18K. Nothing about that is fixable by
# choosing a different fixture, which is why the mode still wants the 8 MB image.
# ---------------------------------------------------------------------------
add_test(NAME acceptance-hostbridge
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -DMODE=fast
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/hostbridge.cmake)
# ~2s. The 60 is a BACKSTOP, not a budget -- and the script has its own 30s leash
# inside it, which fails with "THE MACHINE HUNG" instead of a confusing missing-string
# error. A test with a generous timeout is a test that can waste an hour of your evening.
set_tests_properties(acceptance-hostbridge PROPERTIES LABELS acceptance TIMEOUT 60)

# THE `build` MODE IS NOT REGISTERED, and that is deliberate (2026-07-19).
#
# It needs the 8 MB image, which is an 8.6 MB download and is not in git -- so as a test it
# SKIPPED on every fresh clone and in all of CI, which is a test that reads as protection
# and is not. The script keeps the mode; run it by hand when you edit one of the .ASM, which
# is the only time it has anything to say:
#
#     tools/fetch-disk-images.sh
#     cmake -DSIM=build/altairsim -DSRC=. -DBIN=build -DMODE=build \
#           -P tests/acceptance/hostbridge.cmake

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
# SNAPSHOT / RESTORE through the real monitor -- the command wiring (DESIGN.md 13).
# The state round-trip is proved at the unit level (tests/test_snapshot.cpp); this
# proves the two commands write and read a file the operator named, and refuse one
# that is not a snapshot.
# ---------------------------------------------------------------------------
add_test(NAME acceptance-snapshot
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DBIN=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/snapshot.cmake)
set_tests_properties(acceptance-snapshot PROPERTIES LABELS acceptance TIMEOUT 60)

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
# THE EBERHARD ROM MACHINES ACTUALLY RUN.
#
# test_machines.cpp proves every built-in loads; that cannot tell you the ROM is at the
# address its firmware expects, or that the console is on the port it talks to. This
# switches `amon`, `acuter` and `cdbl` on and reads the terminal. It needs no fetched
# media -- the one disk it uses is tracked.
# ---------------------------------------------------------------------------
add_test(NAME acceptance-eberhard-roms
         COMMAND ${CMAKE_COMMAND}
                 -DSIM=$<TARGET_FILE:altairsim>
                 -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/eberhard-roms.cmake)
set_tests_properties(acceptance-eberhard-roms PROPERTIES LABELS acceptance TIMEOUT 120)

# ...and the half that TYPES is a pty test, registered further down with the other
# expect ones. That split is not organisational: a pipe hands the guest keystrokes it
# has not asked for, which mangles a command line whenever the host wins the race.

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
  # THE EBERHARD MONITORS, ANSWERING. `amon` and `acuter` are asked to dump their own
  # first bytes, which is what proves the image is at the address the machine file put
  # it and that the console is on the port the firmware talks to.
  #
  # It is here rather than in eberhard-roms.cmake because it TYPES, and typing down a
  # pipe races the guest's receive register -- see the header of the .exp file, which
  # records the CI failure that moved it.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-eberhard-typed
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/eberhard-roms.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-eberhard-typed PROPERTIES
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
  # THE DISK IS IN GIT (disks/ is an allowlist -- see .gitignore), so this is UNCONDITIONAL.
  # It was gated on EXISTS until 2026-07-19, from the era when the image was a download; the
  # gate outlived the download and read as protection while protecting nothing. If the image
  # ever goes missing again, this must fail loudly rather than skip.
  # ---------------------------------------------------------------------------
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
  # did for months -- must FAIL. The DCDD now MOUNTS the minidisk image (a hard-sector
  # controller takes a file of any size), but reads it at its OWN geometry -- 32 sectors to a
  # track, not 16 -- so past track 0 every sector offset is wrong and CP/M never boots. If
  # this ever starts booting, the minidisk has crept back into the wrong card and
  # acceptance-minidisk has stopped testing the 88-MDS.
  #
  # The third argument is a SHORT timeout: the control cannot boot, so it can only conclude by
  # NOT seeing the banner, and 20 s is long past when a real boot would have printed it. That
  # keeps the negative control off the fast suite's critical path (it used to hang the full 60).
  #
  # (My FIRST control swapped the PROM instead -- DBL for MDBL -- and IT BOOTED, because the
  # two controllers are register-compatible and Mike's own loader says DBL loads it. The
  # control did its job: it disproved my claim, not the card. See minidisk-dcdd.toml.)
  add_test(NAME acceptance-minidisk-control
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/minidisk.exp
                   $<TARGET_FILE:altairsim> tests/acceptance/minidisk-dcdd.toml 20)
  set_tests_properties(acceptance-minidisk-control PROPERTIES
                       LABELS acceptance TIMEOUT 60 WILL_FAIL TRUE
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # SDOS off a VersaFloppy II -- the SD Systems soft-sector floppy controller.
  #
  # This is the board's reason to exist: the SBC-200's DDBIOS drives a WD FD1791 and cold-boots
  # SDOS from the tracked 8" DD-256 master. It is an EXPECT test because the SBC-200 auto-bauds
  # (nothing prints until a CR is presented in emulated time -- a pipe cannot) and because the
  # boot is a `C` typed at the monitor prompt -- neither survives a piped run.
  #
  # The disk is MOUNTED WRITE-PROTECTED and the .exp shasums it: the boot only reads, and the
  # test cannot dirty the master (the same discipline as acceptance-dcdd-readonly).
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-sdos
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/sdos.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-sdos PROPERTIES
                       LABELS acceptance TIMEOUT 90
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # CDOS 2.58 off a Cromemco 16FDC -- Cromemco's CP/M work-alike, booted from an 8" mixed-
  # density diskette. This boots the SHIPPED example (examples/cdos/cdos.toml) so the file a
  # user is handed is the file under test, then types DIR and reads the whole directory back
  # off the platter through the WD FD1793 -- which a machine that merely booted a resident
  # image could not do.
  #
  # It is an EXPECT test because the proof is a command TYPED at the prompt -- a piped run can
  # only assert what the machine says unprompted. No RETURN is needed to boot: the 16FDC is
  # strapped for its fixed-300-baud modem console (RDOS switch 5, no auto-baud), so the prompt
  # comes up on its own, and the console's rate=full default means that strap costs no speed.
  # The image is mounted read/write in the example, so cdos.exp shasums it before and after: the
  # boot and DIR only read, and the master must come out byte-identical (the same discipline as
  # acceptance-sdos, without a separate fixture toml).
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-cdos
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/cdos.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-cdos PROPERTIES
                       LABELS acceptance TIMEOUT 60
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # SD Systems CP/M 2.2 off the same VersaFloppy II -- but with the SBC-200's INTERRUPTS.
  #
  # Where SDOS polls the keyboard, this CP/M's CBIOS takes console input ONLY through a Z80
  # mode-2 vectored interrupt (the 8251 RxRDY triggering CTC channel 1, vector 0x82) -- so
  # typing `DIR` and getting the directory back is the whole interrupt path proven end to end.
  # It also exercises the onboard-PROM switch-out (OUT 7F,3) of the authentic 64K single-board
  # machine. Same write-protected discipline as acceptance-sdos.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-cpm-sbc
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/cpm.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-cpm-sbc PROPERTIES
                       LABELS acceptance TIMEOUT 90
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # ---------------------------------------------------------------------------
  # THE TARBELL FLOPPY CONTROLLERS -- single density (#1011) and double density (#2022).
  #
  # Each carries its own 32-byte boot PROM, so the boot is AUTOMATIC: RESET arms the PROM at
  # 0000, it reads track 0 through the FD177x and jumps into the cold loader (releasing
  # PHANTOM* on the A5-high fetch), and CP/M 2.2 comes up with no monitor and nothing typed.
  # An expect test because DIR must be typed at the A> prompt (a pipe cannot). The DD case is
  # the mixed-density proof: an SD track 0 then DD tracks 1-76, which hangs mid-load if the
  # card ever builds the wrong FD177x part. Both mount the tracked master WRITE-PROTECTED and
  # shasum it -- the boot only reads.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-tarbell
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/tarbell.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-tarbell PROPERTIES
                       LABELS acceptance TIMEOUT 90
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_test(NAME acceptance-tarbelldd
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/tarbelldd.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-tarbelldd PROPERTIES
                       LABELS acceptance TIMEOUT 90
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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
  # It runs off the TRACKED floppy (examples/cpm/cpm22b23-56k.dsk -- the same one
  # acceptance-hostbridge uses, unguarded), and it MOUNTS THAT DISK READ-ONLY -- the test
  # cannot dirty it, and it shasums the image to prove it.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-dcdd-readonly
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/dcdd-readonly.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-dcdd-readonly PROPERTIES
                       LABELS acceptance TIMEOUT 90
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # ---------------------------------------------------------------------------
  # DDT SETS A BREAKPOINT -- and it needs no interrupt to do it (RST 7 is a one-byte
  # CALL 0038h, written over the guest's own opcode). Same fixture, same protected
  # disk: DDT never writes, and the .exp shasums the image to keep it that way.
  #
  # tests/test_ddt.cpp pins the same mechanic at unit level, on both cores, and runs
  # where expect does not (Windows). This one proves the real DDT.COM off the tracked
  # image drives our 8080 correctly -- which no unit test can claim.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-ddt
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/ddt.exp
                   $<TARGET_FILE:altairsim>)
  set_tests_properties(acceptance-ddt PROPERTIES
                       LABELS acceptance TIMEOUT 120
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # TWO GEOMETRIES, ONE CONTROLLER -- acceptance-dcdd-mixed -- WAS HERE, and was removed
  # on 2026-07-19 with the 8 MB image it needed. An 8 MB disk in drive 0 and a 77-track
  # floppy in drive 2 at once (FDC+ manual 3.7.4) was the only proof that the format and the
  # Spindle are PER DRIVE rather than per card, and it broke as CORRUPTION rather than as an
  # error, which is why it weighed the bytes. It required an 8.6 MB download, so it skipped
  # on every fresh clone and in all of CI. Rebuilding that coverage without the download is an
  # open task, not a settled one.

  # ---------------------------------------------------------------------------
  # THE SOL-20 EXAMPLE AUTO-LAUNCHES ITS GAME OFF THE CASSETTE.
  #
  # examples/sol is product: a machine file, a tape, and a README. The shipped trek80.toml
  # MOUNTs the tape, `TYPE`s `XE TRK80` at the console, and RUNs SOLOS -- so the game comes up
  # by itself. This runs that path end to end, and for the .WAV media it is also the REGRESSION
  # TEST for the CUTS demodulator: TRK80.WAV is Philip Lord's Sol-20 cassette digitization
  # (hosted on deramp.com), which an earlier decoder garbled (27 framing errors, most of the
  # payload wrong) and the matched-filter decoder now reads clean. A SOLOS tape carries a
  # header checksum and one after every 256-byte block, and a tape with the wrong checksums is
  # INVISIBLE: `CA` lists nothing, `GE` finds nothing, and no error is printed anywhere.
  # Reading it back through SOLOS is the only honest check that it is right.
  #
  # IT NEEDS EXPECT because the verdict is a memory dump. TREK80 paints the VDM-1's screen
  # RAM at 0CC00H and nothing it prints reaches stdout, so the machine has to be stopped
  # with ATTN and the screen READ -- which is also what lets this run headless, with no
  # window and no SDL.
  #
  # THE FIXTURE IS NOT THE SHIPPED FILE: trek80-{tap,wav}.toml carry the shipped auto-launch
  # `startup` but flat out with the idle nap off, so the test does not pay the minute a real
  # 2.045 MHz Sol spends reading a cassette. Two fixtures, one per media: the byte stream, and
  # the same tape as CUTS audio at 1200 baud through the modem.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-trek80
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/trek80.exp
                   $<TARGET_FILE:altairsim>
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/trek80-tap.toml)
  set_tests_properties(acceptance-trek80 PROPERTIES LABELS acceptance TIMEOUT 180)

  add_test(NAME acceptance-trek80-wav
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/trek80.exp
                   $<TARGET_FILE:altairsim>
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/trek80-wav.toml)
  set_tests_properties(acceptance-trek80-wav PROPERTIES LABELS acceptance TIMEOUT 180)

  # A SECOND GAME, A SECOND TAPE. trek80 proves the mechanism and the decoder on one tape;
  # this proves ATC off its own -- so a game tape cannot ship without something reading it.
  add_test(NAME acceptance-atc
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/atc.exp
                   $<TARGET_FILE:altairsim>
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/atc-boot.toml)
  set_tests_properties(acceptance-atc PROPERTIES LABELS acceptance TIMEOUT 180)

  # ---------------------------------------------------------------------------
  # THE SHIPPED DISK BASIC EXAMPLE REACHES ITS PROMPT -- acceptance-diskbasic.
  #
  # examples/diskbasic is product: DBL in the PROM at FF00, the 88-DCDD reading track 0, and
  # Altair Disk Extended BASIC 4.1 (MITS, 1977) coming up in a 56K machine.
  #
  # IT MUST BE `expect`, AND THAT IS NOT STYLE. This guest re-initialises the 6850 during
  # startup; a master reset clears the receive register and the byte in it is LOST. Driven by
  # a pipe, the first character after a prompt vanishes -- and how many vanish varies run to
  # run, so padding the input cannot fix it. The visible symptom is a machine re-asking
  # `LINEPRINTER?` forever, which reads as a hang and is not one. So this test cannot move to
  # a plain script, and on Windows (no expect) it does not run at all.
  # ---------------------------------------------------------------------------
  add_test(NAME acceptance-diskbasic
           COMMAND ${EXPECT_EXECUTABLE}
                   -f ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance/diskbasic.exp
                   $<TARGET_FILE:altairsim>
                   ${CMAKE_CURRENT_SOURCE_DIR}/examples/diskbasic)
  set_tests_properties(acceptance-diskbasic PROPERTIES LABELS acceptance TIMEOUT 180)

else()
  message(STATUS "expect not found -- SKIPPING the interactive CLI test (acceptance-cli). "
                 "The tty paths (prompt echo, ATTN, clock pacing) will not be covered.")
endif()
