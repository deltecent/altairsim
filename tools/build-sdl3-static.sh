#!/bin/sh
#
# Build a STATIC SDL3 for packaging, into a fixed prefix. Run ONCE per build machine.
#
#   usage: tools/build-sdl3-static.sh [prefix]        default: ~/opt/sdl3-static
#
# WHY THIS EXISTS. A distributed package must run on a machine that has never had SDL3
# installed. Linking the system SDL3 does not achieve that: on macOS a Homebrew build
# links /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib BY ABSOLUTE PATH, so the binary starts
# on the machine that built it and nowhere else. The alternative to static linking is to
# copy the library into the package and rewrite the install name (install_name_tool,
# @rpath, $ORIGIN) -- three steps, each of which can be got wrong silently.
#
# Static costs about 0.3 MB and removes all three. Measured on macOS/arm64 2026-07-20:
# a dynamic altairsim is 1.7 MB plus a 2.4 MB dylib it must carry; the static one is
# 4.4 MB and carries nothing. `otool -L` on it names only system frameworks.
#
# HOMEBREW CANNOT DO THIS. `brew install sdl3` ships libSDL3.0.dylib and no static
# library at all -- the one .a in there is libSDL3_test.a, which is SDL's test harness,
# not SDL. SDL3Config.cmake even names an SDL3::SDL3-static target, but there is no
# SDL3staticTargets.cmake to define it. Hence a source build.
#
# THE VERSION IS PINNED HERE, AND THAT IS THE POINT. Each build machine keeps its own
# SDL3 (DISTRIBUTION.md 3.1) and nothing at release time checks that four machines agree.
# This file is the agreement. Change it deliberately, and rerun this script everywhere.
#
# WINDOWS HAS ITS OWN: tools/build-sdl3-static.bat, which pins the same version. It is a
# .bat rather than a .ps1 because PowerShell's execution policy blocks unsigned scripts by
# default, and it carries one trap this file does not -- the MSVC C runtime must match
# between SDL3 and altairsim (/MT vs /MD), or the link fails or the two get separate heaps.
# It is UNVERIFIED; nobody has run it. See docs/building-windows.md.

set -eu

SDL3_VERSION=3.4.12

# The oldest macOS the shipped binary must start on. This is NOT the same question as
# static linking and is just as mandatory: without it a binary targets whatever the build
# machine runs and refuses to launch on anything older. It must be passed to BOTH this
# build and the altairsim build, or the link fails on the mismatch.
MACOS_DEPLOYMENT_TARGET=11.0

prefix=${1:-$HOME/opt/sdl3-static}
stamp="$prefix/.altairsim-sdl3-version"

# Already built, at the version we want? Then there is nothing to do. Rebuilding is
# harmless but takes minutes, and this script is named in a release runbook where
# "it was already fine" is the common case.
if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$SDL3_VERSION" ] && [ -f "$prefix/lib/libSDL3.a" ]; then
    echo "build-sdl3-static: SDL3 $SDL3_VERSION already installed in $prefix"
    echo
    echo "Build altairsim against it with:"
    echo "    -DCMAKE_PREFIX_PATH=$prefix"
    exit 0
fi

for t in cmake curl tar; do
    command -v "$t" >/dev/null 2>&1 || { echo "build-sdl3-static: needs $t on PATH" >&2; exit 1; }
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

tarball="SDL3-$SDL3_VERSION.tar.gz"
url="https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/$tarball"

echo "build-sdl3-static: fetching SDL3 $SDL3_VERSION"
curl -fsSL -o "$work/$tarball" "$url"
tar xzf "$work/$tarball" -C "$work"
src="$work/SDL3-$SDL3_VERSION"

# SDL_SHARED=OFF as well as SDL_STATIC=ON, deliberately: with both built, find_package
# resolves SDL3::SDL3 to the SHARED one and the whole exercise is silently undone. The
# prefix holding only a static library is what makes the outcome unambiguous.
args="-DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=ON -DSDL_SHARED=OFF -DCMAKE_INSTALL_PREFIX=$prefix"
if [ "$(uname -s)" = "Darwin" ]; then
    args="$args -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET"
fi

echo "build-sdl3-static: building (this takes a few minutes)"
# shellcheck disable=SC2086
cmake -S "$src" -B "$work/b" $args > "$work/configure.log" 2>&1 || {
    echo "build-sdl3-static: configure failed:" >&2; tail -20 "$work/configure.log" >&2; exit 1; }
cmake --build "$work/b" --parallel > "$work/build.log" 2>&1 || {
    echo "build-sdl3-static: build failed:" >&2; tail -20 "$work/build.log" >&2; exit 1; }
cmake --install "$work/b" > "$work/install.log" 2>&1 || {
    echo "build-sdl3-static: install failed:" >&2; tail -20 "$work/install.log" >&2; exit 1; }

# A static library must actually be what landed. If SDL ever changes its option names,
# this is where we find out -- rather than three steps later, when a package that was
# supposed to be self-contained turns out to link the system SDL3 after all.
[ -f "$prefix/lib/libSDL3.a" ] || {
    echo "build-sdl3-static: no libSDL3.a in $prefix/lib -- SDL3 did not build static" >&2
    ls "$prefix/lib" >&2 || true
    exit 1
}
if [ -e "$prefix/lib/libSDL3.dylib" ] || [ -e "$prefix/lib/libSDL3.so" ]; then
    echo "build-sdl3-static: a SHARED library is also present, which defeats the purpose:" >&2
    ls "$prefix/lib" >&2
    exit 1
fi

echo "$SDL3_VERSION" > "$stamp"

echo
echo "build-sdl3-static: SDL3 $SDL3_VERSION installed static in $prefix"
echo
echo "Build altairsim against it with:"
if [ "$(uname -s)" = "Darwin" ]; then
    echo "    cmake -B build -DCMAKE_BUILD_TYPE=Release \\"
    echo "          -DCMAKE_PREFIX_PATH=$prefix \\"
    echo "          -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET"
    echo
    echo "Then confirm it took, WITH BOTH CHECKS:"
    echo "    otool -L build/altairsim | grep -i sdl      # must print NOTHING"
    echo "    nm build/altairsim | grep -c SDL_           # must NOT be 0"
    echo
    echo "The first alone cannot tell a static build from a HEADLESS one -- neither has an"
    echo "SDL line. Use nm, not strings: SDL_CreateWindow is a symbol, not a string literal,"
    echo "so 'strings | grep SDL_CreateWindow' reports 0 on a perfectly good static build."
else
    echo "    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$prefix"
    echo
    echo "Then confirm it took, WITH BOTH CHECKS:"
    echo "    ldd build/altairsim | grep -i sdl           # must print NOTHING"
    echo "    nm build/altairsim | grep -c SDL_           # must NOT be 0"
    echo
    echo "The first alone cannot tell a static build from a HEADLESS one. Use nm, not"
    echo "strings: SDL_CreateWindow is a symbol, not a string literal."
fi
