# ===========================================================================
# altairsim -- plain GNU Makefile (convenience build)
# ===========================================================================
#
# A single Makefile that builds the `altairsim` binary on Linux, macOS and
# Windows/MinGW with nothing but `make` and a C++20 compiler -- SDL used when
# present, headless when not. It is modeled on SIMH's makefile so that people
# who build that way ("just make + gcc from the Windows command prompt") can
# build this too.
#
#   make                 build build-make/altairsim (auto-detects SDL3 and CUPS)
#   make NO_SDL=1        force a headless build even if SDL3 is installed
#   make CXX=clang++     pick a compiler
#   make -j              parallel build
#   make clean           remove build-make/ (objects, generated sources, binary)
#
# The binary and all intermediates land in build-make/ -- deliberately NOT in
# build/, which belongs to the CMake build, so the two never collide.
#
# THIS IS NOT THE SUPPORTED RELEASE BUILD. CMake is authoritative and MSVC is
# the only supported Windows toolchain for releases (see DISTRIBUTION.md); this
# file is never used to cut a release. It builds only the binary -- the test
# suite, the doc/reference generation and the platform-#ifdef lint all stay
# with CMake. It reproduces CMake's three generated sources byte-for-byte via
# tools/embed.cpp (run `make check-gen` to prove it against a CMake build/).
# ===========================================================================

# --- OS detection (no shell needed on Windows: $(OS) is an env var) ---------
ifeq ($(OS),Windows_NT)
  WIN32   := 1
  UNAME_S := Windows
  EXE     := .exe
else
  UNAME_S := $(shell uname -s)
  EXE     :=
endif

# --- portable shell commands ------------------------------------------------
# On Windows the recipes run under cmd.exe (a plain MinGW install ships no sh),
# which has no `mkdir -p`, no `rm`, and wants backslashes. These helpers paper
# over that so the same recipes work from a bare Windows command prompt and from
# a POSIX shell. Call as $(call mkdir_p,dir) / $(call rm_rf,path) / $(call run,exe).
ifeq ($(WIN32),1)
  mkdir_p = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
  rm_rf   = if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
  rm_f    = if exist "$(subst /,\,$1)" del /q "$(subst /,\,$1)"
  run     = $(subst /,\,$1)
else
  mkdir_p = mkdir -p $1
  rm_rf   = rm -rf $1
  rm_f    = rm -f $1
  run     = $1
endif

# --- toolchain --------------------------------------------------------------
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
BINDIR   := build-make
OBJDIR   := $(BINDIR)/obj
GENDIR   := $(BINDIR)/generated
EMBED    := $(BINDIR)/embed$(EXE)
BIN      := $(BINDIR)/altairsim$(EXE)

CPPFLAGS += -Isrc -I$(GENDIR)

ifneq ($(WIN32),1)
  CXXFLAGS += -pthread   # libstdc++ threads/atomics on *nix; harmless on macOS
endif

# --- optional SDL3 (video boards get a real window) -------------------------
# Auto-detect is POSIX-only -- it shells out to pkg-config/sdl3-config, which a
# bare Windows cmd cannot run. On Windows the build is headless unless you point
# it at an SDL3 by hand:  make SDL=1 SDL_CFLAGS=-IC:/SDL3/include SDL_LIBS="-LC:/SDL3/lib -lSDL3"
# Elsewhere: pkg-config first (Linux), sdl3-config next, Homebrew keg last (this
# Mac has no pkg-config). Force headless anywhere with NO_SDL=1.
ifndef NO_SDL
ifneq ($(WIN32),1)
  ifneq (,$(shell pkg-config --exists sdl3 2>/dev/null && echo 1))
    SDL        := 1
    SDL_CFLAGS ?= $(shell pkg-config --cflags sdl3)
    SDL_LIBS   ?= $(shell pkg-config --libs sdl3)
  else ifneq (,$(shell sh -c 'command -v sdl3-config >/dev/null 2>&1 && echo 1'))
    SDL        := 1
    SDL_CFLAGS ?= $(shell sdl3-config --cflags)
    SDL_LIBS   ?= $(shell sdl3-config --libs)
  else ifeq ($(UNAME_S),Darwin)
    SDL3_PREFIX := $(shell brew --prefix sdl3 2>/dev/null)
    ifneq (,$(wildcard $(SDL3_PREFIX)/include/SDL3/SDL.h))
      SDL        := 1
      SDL_CFLAGS ?= -I$(SDL3_PREFIX)/include
      SDL_LIBS   ?= -L$(SDL3_PREFIX)/lib -lSDL3
    endif
  endif
endif
endif

ifeq ($(SDL),1)
  CPPFLAGS += -DALTAIRSIM_ENABLE_SDL $(SDL_CFLAGS)
  LDLIBS   += $(SDL_LIBS)
endif

# --- optional CUPS (the printer: endpoint), *nix only -----------------------
ifneq ($(WIN32),1)
  ifndef NO_CUPS
    ifneq (,$(shell sh -c 'command -v cups-config >/dev/null 2>&1 && echo 1'))
      CUPS      := 1
      CPPFLAGS += -DALTAIRSIM_ENABLE_PRINTER $(shell cups-config --cflags)
      LDLIBS   += $(shell cups-config --libs)
    endif
  endif
endif

# --- platform link libraries ------------------------------------------------
ifeq ($(WIN32),1)
  LDLIBS  += -lws2_32 -lwinmm
  # Statically link the MinGW runtime (libstdc++, libgcc, libwinpthread) so the
  # produced altairsim.exe runs on any Windows without the toolchain's bin/ on
  # PATH -- otherwise it fails to launch off a machine that has no MinGW. Override
  # with `make LDFLAGS=` if you deliberately want the DLL-linked build.
  LDFLAGS ?= -static
endif
ifeq ($(SDL),1)
  ifeq ($(UNAME_S),Darwin)
    LDLIBS += -framework AppKit   # foreground_macos.mm only
  endif
endif

# --- sources ----------------------------------------------------------------
# The Makefile is a secondary build path, so it GLOBS (CMake deliberately lists
# every file by hand). Everything under src/, then: drop the other OS's platform
# dir, the standalone terminal test, and the SDL/CUPS-gated TUs -- adding the
# gated ones back only when the feature is on. Objective-C++ (.mm) is added by
# hand because the glob is *.cpp.
ALL_CPP := $(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp)

ifeq ($(WIN32),1)
  OTHER := posix
else
  OTHER := win32
endif

GATED := \
  src/platform/posix/terminaltest_posix.cpp \
  src/platform/win32/terminaltest_win32.cpp \
  src/host/display_sdl.cpp \
  src/host/joystick_sdl.cpp \
  src/platform/posix/foreground_posix.cpp \
  src/platform/win32/foreground_win32.cpp \
  src/platform/posix/printer_cups.cpp

SRCS := $(filter-out src/platform/$(OTHER)/%,$(ALL_CPP))
SRCS := $(filter-out $(GATED),$(SRCS))
MM_SRCS :=

ifeq ($(SDL),1)
  SRCS += src/host/display_sdl.cpp src/host/joystick_sdl.cpp
  ifeq ($(WIN32),1)
    SRCS += src/platform/win32/foreground_win32.cpp
  else ifeq ($(UNAME_S),Darwin)
    MM_SRCS += src/platform/posix/macos/foreground_macos.mm
  else
    SRCS += src/platform/posix/foreground_posix.cpp
  endif
endif

ifeq ($(CUPS),1)
  SRCS += src/platform/posix/printer_cups.cpp
endif

# Generated TUs compile like any other source.
GEN_CPP := $(GENDIR)/roms_generated.cpp $(GENDIR)/machines_generated.cpp
GEN_H   := $(GENDIR)/version_generated.h
GEN     := $(GEN_CPP) $(GEN_H)

OBJS := $(patsubst %,$(OBJDIR)/%.o,$(SRCS) $(GEN_CPP) $(MM_SRCS))

# ===========================================================================
# Rules
# ===========================================================================
.PHONY: all clean help check-gen
all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

# Every object waits on the three generated files (version_generated.h is
# included widely); order-only so touching them does not force a full rebuild.
$(OBJS): | $(GEN)

$(OBJDIR)/%.cpp.o: %.cpp
	@$(call mkdir_p,$(patsubst %/,%,$(dir $@)))
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJDIR)/%.mm.o: %.mm
	@$(call mkdir_p,$(patsubst %/,%,$(dir $@)))
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# --- code generation (tools/embed.cpp; the CMake-free codegen) --------------
$(EMBED): tools/embed.cpp
	@$(call mkdir_p,$(patsubst %/,%,$(dir $@)))
	$(CXX) -std=c++20 -O2 -o $@ $<

# Regenerated every build (FORCE): embed rewrites only when the output actually
# changes, so this is cheap and never re-touches unchanged files. Depending on
# FORCE rather than on the input file list also sidesteps the ROM tree's
# filenames-with-spaces, which make cannot express as prerequisites. The version
# header's commit/dirty flags track the live tree, exactly as CMake re-stamps
# them at configure time.
$(GENDIR)/roms_generated.cpp: $(EMBED) FORCE
	@$(call mkdir_p,$(GENDIR))
	@$(call run,$(EMBED)) roms roms $@

$(GENDIR)/machines_generated.cpp: $(EMBED) FORCE
	@$(call mkdir_p,$(GENDIR))
	@$(call run,$(EMBED)) machines machines $@

$(GENDIR)/version_generated.h: $(EMBED) cmake/version.h.in CMakeLists.txt FORCE
	@$(call mkdir_p,$(GENDIR))
	@$(call run,$(EMBED)) version cmake/version.h.in $@ CMakeLists.txt

FORCE:

# Prove byte-for-byte parity with a CMake build's generated sources.
#   make check-gen CMAKE_BUILD=build
CMAKE_BUILD ?= build
check-gen: $(GEN_CPP)
	@diff $(CMAKE_BUILD)/generated/roms_generated.cpp $(GENDIR)/roms_generated.cpp \
	  && echo "roms_generated.cpp: IDENTICAL"
	@diff $(CMAKE_BUILD)/generated/machines_generated.cpp $(GENDIR)/machines_generated.cpp \
	  && echo "machines_generated.cpp: IDENTICAL"

clean:
	@$(call rm_rf,$(BINDIR))

help:
	@echo "targets: all (default), clean, check-gen, help"
	@echo "switches: NO_SDL=1  NO_CUPS=1  CXX=...  CXXFLAGS=...  SDL=1 SDL_CFLAGS=... SDL_LIBS=..."
	@echo "detected: OS=$(UNAME_S) WIN32=$(WIN32) SDL=$(SDL) CUPS=$(CUPS)"
