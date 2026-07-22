#!/bin/sh
#
# Build altairsim from a fresh clone, in one command.
#
#   ./build.sh              a plain Release build -- no dependencies, no flags
#   ./build.sh --with-sdl   also link a static SDL3 so the video boards open a window
#   ./build.sh --help       this text
#
# WHAT THIS IS FOR. The README gives three commands; that is three too many for a first
# encounter. This script is the one command: it configures a Release build, builds it,
# and tells you where the binary landed and what it calls itself. If CMake is missing it
# says so in a sentence you can act on, rather than failing three lines deep in a configure.
#
# SDL3 STAYS OPTIONAL -- that is the project's loudest claim, and a plain `./build.sh`
# keeps it: it builds against a null display with nothing installed and every test passes.
# A window is the ONE thing that costs a dependency, so it is the ONE thing behind a flag.
# `--with-sdl` builds a private static SDL3 (tools/build-sdl3-static.sh) and links it, so
# the resulting binary carries its own SDL3 and asks nothing of the machine it runs on.
#
# Windows has its own: build.bat, a plain-PowerShell twin that needs no Developer shell.

set -eu

# Run from anywhere: everything is relative to the repository this script lives in, never
# to the caller's cwd. A newcomer who runs `sh /path/to/altairsim/build.sh` from $HOME
# should still build the right tree.
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build="$root/build"

# The oldest macOS the SDL3-linked binary must start on. This MUST match the same constant
# in tools/build-sdl3-static.sh: SDL3 and altairsim have to target the same floor or the
# link fails on the mismatch. Only consulted on --with-sdl on macOS.
MACOS_DEPLOYMENT_TARGET=11.0

# Where tools/build-sdl3-static.sh installs its static SDL3. Same default as that script;
# passed explicitly so the two never disagree about the prefix.
SDL3_PREFIX="$HOME/opt/sdl3-static"

with_sdl=no
for arg in "$@"; do
    case "$arg" in
        --with-sdl) with_sdl=yes ;;
        -h|--help)
            cat <<'EOF'
build.sh -- build altairsim from a fresh clone, in one command.

  ./build.sh              a plain Release build -- no dependencies, no flags
  ./build.sh --with-sdl   also link a static SDL3 so the video boards open a window
  ./build.sh --help       this text

SDL3 is optional: a plain build works with nothing installed. --with-sdl builds a
private static SDL3 (tools/build-sdl3-static.sh) once per machine and links it, so the
binary carries its own SDL3. On Windows, use build.bat instead.
EOF
            exit 0 ;;
        *)
            echo "build.sh: unknown argument '$arg' (try --help)" >&2
            exit 2 ;;
    esac
done

# CMake is the one hard prerequisite. Name it plainly and say how to get it, per platform --
# a newcomer who hits this should not have to search.
if ! command -v cmake >/dev/null 2>&1; then
    echo "build.sh: CMake is required and was not found on your PATH." >&2
    case "$(uname -s)" in
        Darwin) echo "  Install it with:  brew install cmake      (or from https://cmake.org/download/)" >&2 ;;
        Linux)  echo "  Install it with:  sudo apt install cmake  (or your distro's package manager)" >&2 ;;
        *)      echo "  Get it from https://cmake.org/download/ and put cmake on your PATH." >&2 ;;
    esac
    exit 1
fi

extra=""
if [ "$with_sdl" = yes ]; then
    echo "build.sh: --with-sdl -- building a static SDL3 first (once per machine)"
    echo
    "$root/tools/build-sdl3-static.sh" "$SDL3_PREFIX"
    echo
    extra="-DCMAKE_PREFIX_PATH=$SDL3_PREFIX"
    if [ "$(uname -s)" = "Darwin" ]; then
        extra="$extra -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET"
    fi
fi

echo "build.sh: configuring a Release build in $build"
# shellcheck disable=SC2086
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release $extra

echo
echo "build.sh: building"
cmake --build "$build" --parallel

# Find what landed. A single-config generator (Make, Ninja) puts it at build/altairsim;
# a multi-config one (rare on Unix) at build/Release/altairsim. Check both rather than
# assume, so the closing message is never a lie.
bin=""
for cand in "$build/altairsim" "$build/Release/altairsim"; do
    if [ -x "$cand" ]; then bin="$cand"; break; fi
done

echo
if [ -n "$bin" ]; then
    echo "build.sh: done. The binary is at:"
    echo "    $bin"
    echo
    printf 'build.sh: it reports version '
    "$bin" --version
    echo
    echo "Run it with:   $bin            # the default machine"
    echo "List machines: $bin --list"
else
    echo "build.sh: the build finished but no altairsim binary was found under $build." >&2
    echo "This is a bug in build.sh -- please report it." >&2
    exit 1
fi
