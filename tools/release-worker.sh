#!/bin/sh
#
# Build, test, package and deliver ONE release archive on THIS machine, for a tag that the
# coordinator made. These are the eight steps of the `release-worker` skill
# (DISTRIBUTION.md 4.2), each with its CHECK and its STOP, as one script that takes arguments.
#
# WHY THIS IS A FILE IN THE TREE. Before, the coordinator wrote a new worker script from the
# skill for each release, and a script that is written again each time gets new bugs each
# time. 1.3.0 had two (issue #643): a check that read only the last 6 lines of the ctest
# output, where the label summary had pushed the pass line out, and a Windows start that
# printed `started` and ran nothing. The second one is tools/release-drive.sh's job. The
# first one is why EVERY CHECK HERE READS THE FULL OUTPUT of its command, from a file.
#
# ONE SCRIPT FOR ALL FOUR TARGETS. On Windows it runs in Git Bash, which the package steps
# need already (build-package.sh is /bin/sh). cmake and ctest run from Git Bash with the
# Visual Studio generator, which needs no Developer shell.
#
# IT NEVER TAGS, NEVER PUBLISHES, NEVER RUNS gh, AND NEVER PICKS A VERSION. The version is an
# argument. If you do not have it, ask the coordinator; do not read it from `git tag`.
#
# RUN A COPY THAT IS OUTSIDE THE CHECKOUT. Step 1 is `git checkout -f vX.Y.Z`, and that can
# replace this file while the shell still reads it. The script refuses to start from inside
# --repo. tools/release-drive.sh copies it out first.
#
#   usage: release-worker.sh --version X.Y.Z [--repo DIR] [--no-parallel] [--path DIRS]
#                            [--serial-a PORT --serial-b PORT] [--deliver USER@HOST:DIR]
#                            [--skip-slow] [<signing> | --unsigned]
#          release-worker.sh --self-test
#
#     --version      The release, X.Y.Z. The tag is vX.Y.Z. Required.
#     --repo         The checkout to build in. Default: the current directory.
#     --no-parallel  Build without --parallel (a machine short of RAM: the compiler gets
#                    OOM-killed).
#     --path         Directories to put in front of PATH. A non-login ssh shell does not read
#                    the profile that finds cmake.
#     --serial-a/-b  The null-modem pair for serial-hw. Give both or neither. With neither,
#                    the two variables are NOT set: an empty value makes the test fail, where
#                    an unset one makes it skip.
#     --deliver      Where step 8 copies the archive, as scp takes it. Without it, steps 1 to 7
#                    run and the report says `delivered no`.
#     --skip-slow    Do not run the CPU exercisers (step 4c). For a dry run only: a release
#                    runs them.
#     <signing>      macOS: --sign <SHA-1> --notary-profile <name> --keychain <path>
#                    --keychain-pass-file <file>, all four. Step 6 signs and notarizes with
#                    them, and step 7 requires the result to be notarized. A macOS release
#                    without them is a STOP, before step 1.
#     --unsigned     macOS: package with an ad hoc signature instead. For a dry run only: a
#                    user of that package has to clear the quarantine mark by hand.
#     --self-test    Run the checks on sample output and print PASS or FAIL. Builds nothing.
#
# THE OUTPUT ends with the report block of the skill, and then ONE last line:
#   release-worker: OK
#   release-worker: FAIL at step N
# The exit status is 0 for OK and 1 for FAIL.

set -u

# ---------------------------------------------------------------------------
# The checks. Each one reads a FILE that holds the full output of a command, or a string.
# --self-test runs them on sample output, so they take no input from anywhere else.
# ---------------------------------------------------------------------------

# The ctest pass line. macOS and Windows print `100% tests passed out of N`. Linux prints
# `100% tests passed, 0 tests failed out of N`. Prints the line; fails if it is not there.
pass_line() {
  grep -E '^100% tests passed(, 0 tests failed)? out of [0-9]+' "$1" | head -n 1 | tr -d '\r' \
    | grep .
}

# ctest's total time, for the report.
total_time() {
  sed -n 's/^Total Test time (real) = *\([0-9.]* sec\).*/\1/p' "$1" | head -n 1
}

# The configure output must say that SDL3 is in. A headless binary runs and draws nothing,
# and that is how v0.2.0 shipped.
sdl_found() {
  grep -F -- '-- SDL3 found -- video boards enabled (windowed)' "$1" > /dev/null
}

# `altairsim --version` must be the one line `AltairSim X.Y.Z`. A `-N-gsha` suffix means the
# build is not on the tag, and `(modified)` means a dirty tree.
version_ok() {  # $1 = the output, $2 = X.Y.Z
  [ "$(printf '%s\n' "$1" | tr -d '\r')" = "AltairSim $2" ]
}

# The verify-package pass line. Prints it; fails if it is not there.
verify_line() {
  grep -E '^verify-package: PASS -- ' "$1" | head -n 1 | tr -d '\r' | grep .
}

# What ctest said about one hardware test: Passed, Skipped, Failed, or `Not run`.
hw_result() {  # $1 = the test name, $2 = the ctest output
  line=$(grep -E "Test +#[0-9]+: $1 " "$2" | head -n 1)
  case $line in
    '')          echo "Not run" ;;
    *Passed*)    echo Passed ;;
    *Skipped*)   echo Skipped ;;
    *)           echo Failed ;;
  esac
}

# Are the signing arguments right for this target? $1 = the target, $2 = --unsigned (yes/no),
# $3..$6 = identity, profile, keychain, pass file. A Mac needs all four, or --unsigned and
# none; any other target needs none.
sign_args_ok() {
  _n=0
  for _v in "$3" "$4" "$5" "$6"; do [ -z "$_v" ] || _n=$((_n + 1)); done
  case $1:$2:$_n in
    macos-*:no:4|macos-*:yes:0) return 0 ;;
    macos-*:*)                  return 1 ;;
    *:no:0)                     return 0 ;;
    *)                          return 1 ;;
  esac
}

# The macos-sign result line. Prints what follows `SIGNED `; fails if it is not there.
signed_line() {
  sed -n 's/^macos-sign: SIGNED //p' "$1" | head -n 1 | grep .
}

# The package target of a machine, from `uname -s` and `uname -m`. Empty for a machine that
# no release is built on.
target_of() {
  case $1 in
    Darwin) case $2 in arm64) echo macos-arm64 ;; x86_64) echo macos-x86_64 ;; esac ;;
    Linux)  [ "$2" = x86_64 ] && echo linux-x86_64 ;;
    MINGW*|MSYS*) echo windows-x86_64 ;;
  esac
  return 0
}

# ---------------------------------------------------------------------------
# --self-test: the checks, on output of the kind that the real commands print.
# ---------------------------------------------------------------------------
self_test() {
  t=$(mktemp -d "${TMPDIR:-/tmp}/release-worker-selftest.XXXXXX")
  bad=0
  expect() {  # $1 = what it proves, $2 = the result (0 or 1), $3 = the result we want
    if [ "$2" = "$3" ]; then echo "  ok    $1"; else echo "  FAIL  $1"; bad=1; fi
  }
  rc() { "$@" > /dev/null 2>&1; echo $?; }

  printf '%s\n' '58/58 Test #58: docs-package ....   Passed    0.05 sec' '' \
    '100% tests passed out of 58' '' 'Total Test time (real) =  41.20 sec' > "$t/mac"
  expect "the macOS pass line"                     "$(rc pass_line "$t/mac")" 0
  expect "the pass line is printed as it is"       \
    "$([ "$(pass_line "$t/mac")" = '100% tests passed out of 58' ]; echo $?)" 0
  expect "the total time"  "$([ "$(total_time "$t/mac")" = '41.20 sec' ]; echo $?)" 0

  printf '%s\n' '100% tests passed, 0 tests failed out of 58' > "$t/linux"
  expect "the Linux pass line"                     "$(rc pass_line "$t/linux")" 0

  # The 1.3.0 bug: the label summary puts the pass line more than 6 lines from the end.
  { printf '%s\n' '100% tests passed out of 58' '' 'Label Time Summary:'
    printf '%s\n' 'acceptance = 30 sec*proc (40 tests)' 'unit = 9 sec*proc (10 tests)' \
      'cli = 1 sec*proc (2 tests)' 'mcp = 1 sec*proc (2 tests)' 'docs = 1 sec*proc (3 tests)' \
      'hw = 0 sec*proc (1 test)' '' 'Total Test time (real) =  41.20 sec'
  } > "$t/labels"
  expect "the pass line above a long label summary" "$(rc pass_line "$t/labels")" 0

  printf '%s\n' '98% tests passed, 1 tests failed out of 58' '' 'The following tests FAILED:' \
    > "$t/fail"
  expect "a failed run has no pass line"           "$(rc pass_line "$t/fail")" 1
  printf '%s\n' 'no tests were found' > "$t/none"
  expect "a run with no tests has no pass line"    "$(rc pass_line "$t/none")" 1
  printf '100%% tests passed out of 58\r\n' > "$t/crlf"
  expect "a pass line with a CR (Windows)"         \
    "$([ "$(pass_line "$t/crlf")" = '100% tests passed out of 58' ]; echo $?)" 0

  printf '%s\n' '-- SDL3 found -- video boards enabled (windowed)' > "$t/sdl"
  expect "the SDL3 line"                           "$(rc sdl_found "$t/sdl")" 0
  printf '%s\n' '-- SDL3 not found -- video boards build headless (null display)' > "$t/nosdl"
  expect "a headless configure is refused"         "$(rc sdl_found "$t/nosdl")" 1

  expect "a version on the tag"       "$(rc version_ok 'AltairSim 1.3.0' 1.3.0)" 0
  expect "a version off the tag is refused" \
    "$(rc version_ok 'AltairSim 1.3.0 (v1.2.0-86-g59dbba8)' 1.3.0)" 1
  expect "a modified tree is refused" "$(rc version_ok 'AltairSim 1.3.0 (modified)' 1.3.0)" 1
  expect "a different version is refused" "$(rc version_ok 'AltairSim 1.2.0' 1.3.0)" 1
  expect "a second line is refused"   \
    "$(rc version_ok "$(printf 'AltairSim 1.3.0\nSDL3 3.4.12')" 1.3.0)" 1

  printf '%s\n' 'verify-package: PASS -- 4/4 manual commands reached their documented prompt' \
    > "$t/verify"
  expect "the verify pass line"                    "$(rc verify_line "$t/verify")" 0
  printf '%s\n' 'verify-package: FAIL -- 3/4 manual commands' > "$t/noverify"
  expect "a verify FAIL is refused"                "$(rc verify_line "$t/noverify")" 1

  printf '%s\n' '1/2 Test #57: serial-hw ........   Passed    2.10 sec' \
    '2/2 Test #58: tnfs-hw ..........***Skipped   0.01 sec' > "$t/hw"
  expect "serial-hw Passed"  "$([ "$(hw_result serial-hw "$t/hw")" = Passed ]; echo $?)" 0
  expect "tnfs-hw Skipped"   "$([ "$(hw_result tnfs-hw "$t/hw")" = Skipped ]; echo $?)" 0
  printf '%s\n' '1/1 Test #57: serial-hw ........***Failed    2.10 sec' > "$t/hwfail"
  expect "serial-hw Failed"  "$([ "$(hw_result serial-hw "$t/hwfail")" = Failed ]; echo $?)" 0
  expect "a test that did not run" \
    "$([ "$(hw_result tnfs-hw "$t/hwfail")" = 'Not run' ]; echo $?)" 0

  expect "macOS arm64"   "$([ "$(target_of Darwin arm64)" = macos-arm64 ]; echo $?)" 0
  expect "macOS x86_64"  "$([ "$(target_of Darwin x86_64)" = macos-x86_64 ]; echo $?)" 0
  expect "Linux x86_64"  "$([ "$(target_of Linux x86_64)" = linux-x86_64 ]; echo $?)" 0
  expect "Git Bash"      "$([ "$(target_of MINGW64_NT-10.0-26100 x86_64)" = windows-x86_64 ]; echo $?)" 0
  expect "an unknown machine has no target" "$([ -z "$(target_of Linux aarch64)" ]; echo $?)" 0

  expect "a Mac with all four signing arguments" "$(rc sign_args_ok macos-arm64 no I P K F)" 0
  expect "a Mac with none is refused"            "$(rc sign_args_ok macos-x86_64 no '' '' '' '')" 1
  expect "a Mac with some is refused"            "$(rc sign_args_ok macos-arm64 no I '' K F)" 1
  expect "a Mac with --unsigned and none"        "$(rc sign_args_ok macos-arm64 yes '' '' '' '')" 0
  expect "--unsigned with an identity is refused" "$(rc sign_args_ok macos-arm64 yes I P K F)" 1
  expect "Linux needs none"                      "$(rc sign_args_ok linux-x86_64 no '' '' '' '')" 0
  expect "Linux with an identity is refused"     "$(rc sign_args_ok linux-x86_64 no I P K F)" 1
  expect "Windows with --unsigned is refused"    "$(rc sign_args_ok windows-x86_64 yes '' '' '' '')" 1

  printf '%s\n' 'macos-sign: submitting' \
    'macos-sign: SIGNED Developer ID Application: A B (X) -- notarized (Accepted)' > "$t/signed"
  expect "the signed line" \
    "$([ "$(signed_line "$t/signed")" = 'Developer ID Application: A B (X) -- notarized (Accepted)' ]; echo $?)" 0
  printf '%s\n' 'macos-sign: notarization finished with status Invalid' > "$t/unsigned"
  expect "no signed line"                        "$(rc signed_line "$t/unsigned")" 1

  rm -rf "$t"
  if [ $bad = 0 ]; then echo "release-worker self-test: PASS"; exit 0; fi
  echo "release-worker self-test: FAIL"; exit 1
}

# ---------------------------------------------------------------------------
# Arguments.
# ---------------------------------------------------------------------------
version=""
repo="."
parallel=yes
pathdirs=""
serial_a=""
serial_b=""
deliver=""
slow=yes
unsigned=no
sign_identity=""
sign_profile=""
sign_keychain=""
sign_passfile=""

usage() {
  sed -n '/^#   usage:/,/^# The exit status/p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

need() { [ $# -ge 2 ] || { echo "release-worker: $1 needs a value" >&2; exit 1; }; }

while [ $# -gt 0 ]; do
  case $1 in
    --version)     need "$@"; version=$2; shift 2 ;;
    --repo)        need "$@"; repo=$2; shift 2 ;;
    --path)        need "$@"; pathdirs=$2; shift 2 ;;
    --serial-a)    need "$@"; serial_a=$2; shift 2 ;;
    --serial-b)    need "$@"; serial_b=$2; shift 2 ;;
    --deliver)     need "$@"; deliver=$2; shift 2 ;;
    --no-parallel) parallel=no; shift ;;
    --skip-slow)   slow=no; shift ;;
    --unsigned)    unsigned=yes; shift ;;
    --sign)        need "$@"; sign_identity=$2; shift 2 ;;
    --notary-profile)     need "$@"; sign_profile=$2; shift 2 ;;
    --keychain)           need "$@"; sign_keychain=$2; shift 2 ;;
    --keychain-pass-file) need "$@"; sign_passfile=$2; shift 2 ;;
    --self-test)   self_test ;;
    -h|--help)     usage 0 ;;
    *)             echo "release-worker: unknown argument: $1" >&2; usage 1 ;;
  esac
done

if ! printf '%s\n' "$version" | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' > /dev/null; then
  echo "release-worker: --version X.Y.Z is required. Ask the coordinator; do not guess it." >&2
  exit 1
fi
if [ -n "$serial_a" ] && [ -z "$serial_b" ] || [ -z "$serial_a" ] && [ -n "$serial_b" ]; then
  echo "release-worker: give both --serial-a and --serial-b, or neither." >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# The report. Every line starts as "not reached", so a STOP still prints the full block.
# ---------------------------------------------------------------------------
target=$(target_of "$(uname -s)" "$(uname -m)")
r_version="not reached"
r_tests="not reached"
r_slow="not reached"
r_serial="not reached"
r_tnfs="not reached"
r_sdl="not reached"
r_verify="not reached"
r_signed="not reached"
r_archive="not reached"
r_delivered="no -- not reached"
work=""

report() {
  echo
  echo "target        ${target:-unknown}"
  echo "version       $r_version"
  echo "tests         $r_tests"
  echo "exercisers    $r_slow"
  echo "serial-hw     $r_serial"
  echo "tnfs-hw       $r_tnfs"
  echo "SDL3          $r_sdl"
  echo "signed        $r_signed"
  echo "verify        $r_verify"
  echo "archive       $r_archive"
  echo "delivered     $r_delivered"
}

stop() {  # $1 = the step, $2 = what is wrong
  report
  echo "stopped at    step $1: $2"
  [ -n "$work" ] && rm -rf "$work"
  echo "release-worker: FAIL at step $1"
  exit 1
}

# Run one command. Its full output goes to the log AND to $out, which is what the check
# reads. $rc is its exit status (a pipe to tee would lose it).
run() {  # $1 = the step, the rest = the command
  out=$work/$1.out
  shift
  echo
  echo "+ $*"
  { "$@"; echo $? > "$work/rc"; } 2>&1 | tee "$out"
  rc=$(cat "$work/rc")
}

[ -n "$target" ] || stop 0 "no release is built on this machine ($(uname -s)/$(uname -m))"
# Before step 1, not at step 6: a missing certificate found after the CPU exercisers costs
# their whole run to find.
sign_args_ok "$target" "$unsigned" "$sign_identity" "$sign_profile" "$sign_keychain" "$sign_passfile" \
  || stop 0 "on macOS give all of --sign, --notary-profile, --keychain and --keychain-pass-file (or --unsigned for a dry run); on any other target give none of them"

# Where this script is, BEFORE the cd: $0 can be a relative path.
self=$(cd "$(dirname "$0")" && pwd -P)

cd "$repo" 2> /dev/null || stop 0 "cannot enter the checkout: $repo"
repo=$(pwd -P)
git rev-parse --show-toplevel > /dev/null 2>&1 || stop 0 "$repo is not a git checkout"

case $self/ in
  "$repo"/*) stop 0 "this script is inside the checkout ($self). Step 1 can replace it while it runs. Copy it out of the checkout and run the copy" ;;
esac

[ -z "$pathdirs" ] || PATH="$pathdirs:$PATH"
export PATH

work=$(mktemp -d "${TMPDIR:-/tmp}/release-worker.XXXXXX")

case $target in
  windows-x86_64) ext=zip;    bin=build/Release/altairsim.exe
                  sdl_prefix=$(cygpath -m "$HOME/opt/sdl3-static") ;;
  *)              ext=tar.gz; bin=build/altairsim
                  sdl_prefix=$HOME/opt/sdl3-static ;;
esac
archive=dist/altairsim-$version-$target.$ext

echo "release-worker: $target, v$version, in $repo"

# --- 1. The source AT THE TAG, never a branch. ---------------------------------------------
# `git checkout -f` discards a change to a tracked file, so a tree with one is a STOP first.
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  git status --short --untracked-files=no
  stop 1 "the checkout has changes to tracked files, and step 1 would discard them"
fi
# --force matters: a re-spin force-moves the tag, and a plain `git fetch --tags` keeps the old
# one and builds the wrong commit.
run 1a git fetch --tags --force
[ "$rc" = 0 ] || stop 1 "git fetch --tags --force failed"
run 1b git checkout -f "v$version"
[ "$rc" = 0 ] || stop 1 "git checkout -f v$version failed. Is the tag pushed?"

# --- 2. Configure, from a CLEAN build/. ----------------------------------------------------
# A reused build/ keeps its cached SDL3_DIR, often Homebrew's dylib.
rm -rf build
case $target in
  macos-*)        # Never -DCMAKE_OSX_ARCHITECTURES: each Mac builds native for itself.
    run 2 cmake -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$sdl_prefix" \
          -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 ;;
  windows-x86_64) # The generator is named: without it an old CMake builds with 2022.
                  # MultiThreaded links the C runtime statically, as SDL3 was built.
    run 2 cmake -B build -G "Visual Studio 18 2026" -DCMAKE_BUILD_TYPE=Release \
          "-DCMAKE_PREFIX_PATH=$sdl_prefix" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ;;
  *)
    run 2 cmake -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$sdl_prefix" ;;
esac
[ "$rc" = 0 ] || stop 2 "cmake configure failed"
sdl_found "$out" || stop 2 "the configure output has no '-- SDL3 found -- video boards enabled (windowed)' line. A headless binary draws nothing"
if [ -f "$HOME/opt/sdl3-static/.altairsim-sdl3-version" ]; then
  r_sdl=$(tr -d '\r\n' < "$HOME/opt/sdl3-static/.altairsim-sdl3-version")
else
  r_sdl="unknown -- no .altairsim-sdl3-version in ~/opt/sdl3-static"
fi

# --- 3. Build. ------------------------------------------------------------------------------
# The processor count. A bare --parallel runs `make -j` with no limit, and that ran a 3-core
# runner out of memory (PR #817).
ncpu() { nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2; }
if [ $parallel = yes ]; then
  run 3 cmake --build build --config Release --parallel "$(ncpu)"
else
  run 3 cmake --build build --config Release
fi
[ "$rc" = 0 ] || stop 3 "the build failed"

# --- 4. Test: everything but the slow CPU gate and the two tests that need hardware. -------
run 4 ctest --test-dir build -C Release -LE slow -E '^(serial-hw|tnfs-hw)$'
r_tests=$(pass_line "$out") || { r_tests="no pass line"; stop 4 "ctest did not print '100% tests passed'"; }
[ "$rc" = 0 ] || stop 4 "ctest exit status $rc"

# --- 4b. The hardware tests. They WARN; they never stop the release. -----------------------
if [ -n "$serial_a" ]; then
  run 4b env "ALTAIR_SERIAL_A=$serial_a" "ALTAIR_SERIAL_B=$serial_b" \
      ctest --test-dir build -C Release -R '^(serial-hw|tnfs-hw)$' --output-on-failure
  ports=" ($serial_a, $serial_b)"
else
  run 4b ctest --test-dir build -C Release -R '^(serial-hw|tnfs-hw)$' --output-on-failure
  ports=""
fi
r_serial="$(hw_result serial-hw "$out")$ports"
r_tnfs=$(hw_result tnfs-hw "$out")
case $r_serial in
  Passed*) ;;
  *) [ -z "$serial_a" ] || echo "WARNING: serial ports are configured and serial-hw is not Passed. Check the cable, the ports and the serial code." ;;
esac
case $r_tnfs in
  Failed) echo "WARNING: tnfs-hw Failed." ;;
esac

# --- 4c. The CPU exercisers, through the binary that this machine ships. --------------------
# 8080EXM, 8085EXM, ZEXDOC and ZEXALL. They run here and not in CI on the tag, because the
# four build machines are the four compilers that ship, and one of them (the Intel Mac) has
# no CI runner.
if [ $slow = yes ]; then
  # The four exercisers run at the same time, on every machine. --no-parallel is for the
  # compiler, which needs the RAM; an exerciser needs very little.
  run 4c ctest --test-dir build -C Release -L slow -j 4
  r_slow=$(pass_line "$out") || { r_slow="no pass line"; stop 4c "the CPU exercisers did not print '100% tests passed'"; }
  [ "$rc" = 0 ] || stop 4c "ctest -L slow exit status $rc"
  r_slow="$r_slow in $(total_time "$out")"
else
  r_slow="skipped (--skip-slow) -- a dry run, not a release build"
fi

# --- 5. The binary knows what it is. ---------------------------------------------------------
run 5 "./$bin" --version
r_version=$(tr -d '\r' < "$out")
version_ok "$r_version" "$version" || stop 5 "--version is not 'AltairSim $version'. A -N-gsha suffix means the build is not on the tag; (modified) means a dirty tree"

# --- 6. Package, with the manual that CI built. The tag's tree holds it. -------------------
# A macOS package is signed and notarized here, inside build-package.sh (tools/macos-sign.sh).
notary=""
if [ -n "$sign_identity" ]; then
  run 6 tools/build-package.sh --pdf docs/altairsim-manual.pdf --target "$target" \
        --sign "$sign_identity" --notary-profile "$sign_profile" \
        --keychain "$sign_keychain" --keychain-pass-file "$sign_passfile"
  notary=--require-notarized
else
  run 6 tools/build-package.sh --pdf docs/altairsim-manual.pdf --target "$target"
fi
[ "$rc" = 0 ] || stop 6 "build-package.sh refused. Its output says why"
[ -f "$archive" ] || stop 6 "build-package.sh did not write $archive"
case $target in
  macos-*) r_signed=$(signed_line "$out") || { r_signed="no SIGNED line"; stop 6 "tools/macos-sign.sh did not print its SIGNED line"; } ;;
  *)       r_signed="n/a (not macOS)" ;;
esac

# --- 7. Prove the manual true against the ARCHIVE. ------------------------------------------
run 7 tools/verify-package.sh --release $notary "$archive"
r_verify=$(verify_line "$out") || { r_verify="no PASS line"; stop 7 "verify-package.sh did not print its PASS line"; }
[ "$rc" = 0 ] || stop 7 "verify-package.sh exit status $rc"

if command -v shasum > /dev/null 2>&1; then
  hash=$(shasum -a 256 "$archive" | cut -d' ' -f1)
else
  hash=$(sha256sum "$archive" | cut -d' ' -f1)
fi
r_archive="$(basename "$archive")  sha256 $hash"

# --- 8. Deliver. The only credential a worker has is the delivery key. ---------------------
if [ -n "$deliver" ]; then
  run 8 scp "$archive" "$deliver/"
  if [ "$rc" != 0 ]; then
    r_delivered="no -- scp failed"
    stop 8 "scp to $deliver failed"
  fi
  r_delivered="yes -- $deliver"
else
  r_delivered="no -- no --deliver was given"
fi

report
rm -rf "$work"
echo "release-worker: OK"
exit 0
