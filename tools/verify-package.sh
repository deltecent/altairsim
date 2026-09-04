#!/bin/sh
#
# PROVE THE MANUAL IS TRUE AGAINST THE ACTUAL SHIPPED PACKAGE.
#
# tools/build-package.sh assembles the archive we hand people and STOPS -- it never runs the
# manual's own commands against it, so a chapter can tell a reader to type something that does
# not work in the zip and nothing catches it. That is not hypothetical: the manual promised
# "CP/M in one command" while a published archive carried no media at all (docs/package.map
# and issue #308 record it). This script is the missing half. It takes the finished archive,
# extracts it somewhere `git rev-parse` FAILS -- no repository in sight, exactly the layout a
# user has -- and types the manual's own fenced `$ altairsim ...` commands at the binary that
# is actually in the package, asserting each reaches the prompt the manual says it will.
#
# WHY A SEPARATE SCRIPT, and not a step inside build-package.sh or a ctest case:
#   * build-package.sh's job is to ASSEMBLE AND STOP (like build-checksums.sh). Verifying a
#     finished artifact is a different job, and must be re-runnable against an already-built
#     archive without a rebuild -- which folding it into the packager would forbid.
#   * ctest runs BEFORE packaging (DISTRIBUTION.md 4.2 step 4) and proves the BINARY, not the
#     archive. The archive only exists at release time, and only then do the PDFs (docs.yml)
#     and the windowed-SDL binary that build-package.sh's own gates require exist. So this is
#     a release-time check, wired into DISTRIBUTION.md 4.2 after build-package.sh, not CI.
#
# WHERE THE COMMANDS AND THE PROMPTS COME FROM. The commands are read from the manual SOURCE
# (docs/manual/*.md in this checkout -- the package ships only the rendered PDF), token-expanded
# from docs/package.map with the SAME substitution build-package.sh uses, so the two can never
# disagree about what {{MACHINE_CPM}} means. Extraction is open-ended (any fenced one-liner is a
# candidate); DISPATCH is a small closed table, and a documented example this table does not
# know is a LOUD FAILURE ("add it here"), never a silent skip. That asymmetry is the point: a
# new `$ altairsim {{MACHINE_NEWTHING}}` in a chapter cannot slip past unverified.
#
# THREE OF THE FIVE NEED A PTY, NOT A PIPE, which is not a preference -- a pipe hands the guest
# keystrokes it never asked for and, for the Sol-20, the output never reaches stdout at all
# (it paints the VDM-1's video RAM). Those three reuse the existing acceptance `.exp` scripts
# UNCHANGED, pointed at the extracted package's own files: tests/acceptance/{basic1,diskbasic,
# trek80}.exp already take <binary> and a dir/toml, so proving the SHIPPED file boots is a
# matter of handing them the shipped file.
#
# `expect` IS OPTIONAL, exactly as it is for the acceptance suite. CMakeLists gates every `.exp`
# test behind find_program(expect) and SKIPS them where it is absent -- which is Windows, whose
# Git Bash ships no expect. So this does the same: without expect the two PIPED commands (CP/M,
# 4K BASIC) still run everywhere, and the three pty commands are SKIPPED WITH A LOUD NOTICE
# rather than failing the leg. A skip is reported, never silent -- an unverified command must
# look unverified.
#
# NOT `set -e`. This harness RUNS commands that are expected to fail when the package is wrong
# -- a failed boot is a finding, not a script error -- so failures are handled explicitly and
# tallied, and the script's own exit status is the count of findings.
#
#   usage: tools/verify-package.sh [--keep] <archive>
#
#     --keep     Leave the scratch extraction directory instead of deleting it, to debug a
#                failure by hand (the message names the directory).
#     <archive>  dist/altairsim-X.Y.Z-<target>.tar.gz or .zip -- what build-package.sh wrote.

set -u

root=$(cd "$(dirname "$0")/.." && pwd)
map=$root/docs/package.map
manual=$root/docs/manual

keep=0
archive=""
while [ $# -gt 0 ]; do
  case $1 in
    --keep) keep=1; shift ;;
    -h|--help) echo "usage: tools/verify-package.sh [--keep] <archive>"; exit 0 ;;
    -*) echo "verify-package: unknown option $1" >&2; exit 1 ;;
    *)  [ -z "$archive" ] || { echo "verify-package: only one archive, got '$archive' and '$1'" >&2; exit 1; }
        archive=$1; shift ;;
  esac
done

[ -n "$archive" ] || { echo "verify-package: no archive given. usage: tools/verify-package.sh [--keep] <archive>" >&2; exit 1; }
[ -f "$archive" ] || { echo "verify-package: $archive does not exist" >&2; exit 1; }
[ -f "$map" ]     || { echo "verify-package: $map is missing -- run from a checkout of the repo" >&2; exit 1; }

have_expect=0
if command -v expect > /dev/null 2>&1; then have_expect=1; fi

archive=$(cd "$(dirname "$archive")" && pwd)/$(basename "$archive")

# ---------------------------------------------------------------------------
# 1. EXTRACT, OUTSIDE THE REPOSITORY. mktemp under TMPDIR, never under $root, so the extracted
#    tree is a place `git rev-parse` fails -- the same "no repo in sight" property the examples
#    acceptance test relies on, now proven against the REAL archive rather than a copy. Running
#    the PACKAGED binary (not build/altairsim) also proves the copy kept the exec bit and did
#    not corrupt it.
# ---------------------------------------------------------------------------
scratch=$(mktemp -d "${TMPDIR:-/tmp}/verify-package.XXXXXX") || {
  echo "verify-package: could not make a scratch directory" >&2; exit 1; }

cleanup() {
  if [ "$keep" -eq 1 ]; then echo "verify-package: kept scratch dir $scratch" >&2
  else rm -rf "$scratch"; fi
}
trap cleanup EXIT INT TERM

extract_ok=1
case $archive in
  *.tar.gz|*.tgz) tar xzf "$archive" -C "$scratch" || extract_ok=0 ;;
  *.zip)
    # unzip first; fall back to a REAL bsdtar (libarchive), mirroring build-package.sh's
    # zip-CREATION asymmetry on the extraction side. Git Bash's GNU tar cannot unzip, so on
    # Windows reach System32\tar.exe by absolute path exactly as the packager does.
    if command -v unzip > /dev/null 2>&1; then
      unzip -q "$archive" -d "$scratch" || extract_ok=0
    else
      bsdtar=""
      if tar --version 2>/dev/null | grep -qi bsdtar; then
        bsdtar=tar
      else
        for t in "$(cygpath -u "${SYSTEMROOT:-}" 2>/dev/null)/System32/tar.exe" \
                 /c/Windows/System32/tar.exe; do
          if [ -x "$t" ] && "$t" --version 2>/dev/null | grep -qi bsdtar; then bsdtar=$t; break; fi
        done
      fi
      [ -n "$bsdtar" ] || { echo "verify-package: no unzip and no bsdtar -- cannot extract $archive" >&2; exit 1; }
      ( cd "$scratch" && "$bsdtar" -xf "$archive" ) || extract_ok=0
    fi ;;
  *) echo "verify-package: unknown archive extension: $archive (want .tar.gz or .zip)" >&2; exit 1 ;;
esac
[ "$extract_ok" -eq 1 ] || { echo "verify-package: extracting $archive failed" >&2; exit 1; }

# The archive holds exactly one top-level directory, altairsim-X.Y.Z-<target>/.
base=""
for d in "$scratch"/*/; do
  [ -d "$d" ] || continue
  [ -z "$base" ] || { echo "verify-package: archive has more than one top-level directory -- $archive" >&2; exit 1; }
  base=${d%/}
done
[ -n "$base" ] || { echo "verify-package: archive extracted to nothing -- $archive" >&2; exit 1; }

if   [ -x "$base/altairsim" ];     then bin=$base/altairsim
elif [ -x "$base/altairsim.exe" ]; then bin=$base/altairsim.exe
elif [ -f "$base/altairsim.exe" ]; then bin=$base/altairsim.exe   # Windows: no exec bit to check
else echo "verify-package: no altairsim binary in the package ($base)" >&2; exit 1
fi

# ---------------------------------------------------------------------------
# 2. THE TOKEN TABLE, built from docs/package.map into a sed script of `s|{{TOKEN}}|value|g`
#    lines -- the SAME token regex build-package.sh's expand() uses (tools/build-package.sh
#    ~line 309), so a path the manual writes as a token expands here to the path that is
#    actually in the zip, and the two scripts cannot drift on it.
# ---------------------------------------------------------------------------
sedscript=$scratch/.tokens.sed
sed -n 's/^\([A-Z_][A-Z0-9_]*\)[[:blank:]]*=[[:blank:]]*\(.*[^[:blank:]]\)[[:blank:]]*$/s|{{\1}}|\2|g/p' "$map" > "$sedscript"
expand() { printf '%s' "$1" | sed -f "$sedscript"; }

# ---------------------------------------------------------------------------
# 3. EXTRACT CANDIDATE COMMANDS from the manual's fenced blocks. A block is a runnable
#    candidate IFF it holds exactly one `$ altairsim <arg>` line whose arg does not start with
#    `-`, optionally preceded in the same block by ONE `$ cp -R`, `$ cd`, or `$ ls` setup line
#    and no other `$` command. That keeps the walkthrough one-liners (and their `cp -R` / `cd`
#    pairs) and drops the machines/running/package comparison TABLES, which always stack two or
#    three `$ altairsim` lines with trailing prose in one block. awk emits, per surviving block,
#    a tab-separated record:  FILE <tab> LINE <tab> SETUP-LINE-or-empty <tab> ALTAIRSIM-LINE
# ---------------------------------------------------------------------------
records=$scratch/.records
awk '
  FNR==1 { inblock=0 }
  /^```/ {
    if (inblock) {
      ok=1; ac=0; setupn=0; acmd=""; setup=""
      for (i=1; i<=n; i++) {
        l=cmd[i]
        if (l ~ /^\$ altairsim /)                                    { ac++; acmd=l }
        else if (l ~ /^\$ cp -R / || l ~ /^\$ cd / || l ~ /^\$ ls /) { setupn++; setup=l }
        else                                                         { ok=0 }
      }
      if (ok==1 && ac==1 && setupn<=1) {
        arg=acmd; sub(/^\$ altairsim */,"",arg)
        if (arg !~ /^-/) print FILENAME "\t" bstart "\t" setup "\t" acmd
      }
      inblock=0
    } else { inblock=1; bstart=FNR; n=0 }
    next
  }
  inblock && /^\$ / { cmd[++n]=$0 }
' "$manual"/*.md > "$records"

# ---------------------------------------------------------------------------
# 4/5. RESOLVE, DISPATCH, RUN, ASSERT.
# ---------------------------------------------------------------------------
seen=$scratch/.seen
: > "$seen"
total=0
passed=0
failed=0
skipped=0

# assert_piped <label> <captured-output>  (required strings: newline-list on stdin)
assert_piped() {
  _label=$1; _out=$2; _missing=""
  while IFS= read -r want; do
    [ -n "$want" ] || continue
    if ! printf '%s' "$_out" | grep -F -q "$want"; then _missing="$_missing
    missing: $want"; fi
  done
  if [ -n "$_missing" ]; then
    echo "verify-package: FAIL -- $_label did not reach its documented prompt:" >&2
    printf '%s\n' "$_missing" >&2
    return 1
  fi
  return 0
}

# run_piped <rundir> <tomlarg> <keysfile>  -> PIPED_OUT. Boot flat-out, feed keys, watchdog-
# kill: the guest does not quit on stdin EOF, but the banner and first DIR line land in the
# first second at the free-running default.
run_piped() {
  _dir=$1; _toml=$2; _keys=$3
  _outf=$scratch/.out
  ( cd "$_dir" && "$bin" "$_toml" < "$_keys" > "$_outf" 2>&1 ) &
  _pid=$!
  ( sleep 30; kill "$_pid" 2>/dev/null ) &
  _wd=$!
  wait "$_pid" 2>/dev/null
  kill "$_wd" 2>/dev/null
  PIPED_OUT=$(cat "$_outf" 2>/dev/null)
  rm -f "$_outf"
}

# run_expect <cwd-or-empty> <exp> <arg...>  -> 0 pass / 1 fail. The pty drivers self-terminate.
run_expect() {
  _cwd=$1; _exp=$2; shift 2
  _log=$scratch/.exp
  if [ -n "$_cwd" ]; then ( cd "$_cwd" && expect -f "$root/tests/acceptance/$_exp" "$@" ) > "$_log" 2>&1 </dev/null
  else                    expect -f "$root/tests/acceptance/$_exp" "$@" > "$_log" 2>&1 </dev/null
  fi
  _rc=$?
  if [ "$_rc" -ne 0 ]; then
    echo "verify-package: FAIL -- $_exp against the package did not pass:" >&2
    sed 's/^/    /' "$_log" >&2
  fi
  rm -f "$_log"
  return "$_rc"
}

# dispatch <driver> <target> <detail> <human-cmd>
#   piped drivers: target=rundir, detail=toml arg relative to it
#   pty drivers:   target=cwd/dir/toml-path, detail=""
dispatch() {
  _key=$1; _target=$2; _detail=$3; _cmd=$4
  _dk="$_key|$_target|$_detail"
  if grep -Fxq "$_dk" "$seen" 2>/dev/null; then echo "  (already handled: $_cmd)"; return 0; fi
  echo "$_dk" >> "$seen"
  # The pty drivers need expect; where it is absent (Windows) SKIP them loudly instead of
  # failing the leg -- the same posture CMakeLists takes, gating every .exp test on expect.
  case $_key in
    BASIC1|DISKBASIC|SOL)
      if [ "$have_expect" -eq 0 ]; then
        skipped=$((skipped + 1))
        echo "  SKIP (no expect on this box; pty command NOT verified): $_cmd"
        return 0
      fi ;;
  esac
  total=$((total + 1))
  echo "  RUN  $_cmd"
  _ok=0
  case $_key in
    CPM)
      run_piped "$_target" "$_detail" "$root/tests/acceptance/cpm-dir.keys"
      if assert_piped "CP/M ($_cmd)" "$PIPED_OUT" <<EOF
56K CP/M 2.2b v2.3
For Altair 8" Floppy
A>
A: L80      COM
EOF
      then _ok=1; fi ;;
    HDSK)
      run_piped "$_target" "$_detail" "$root/tests/acceptance/hdsk-dir.keys"
      if assert_piped "HDSK CP/M ($_cmd)" "$PIPED_OUT" <<EOF
HDBL 2.00
48K CP/M 2.2b v1.6
For MITS 88-HDSK
A0>
A: BOOT     ASM
EOF
      then _ok=1; fi ;;
    BASIC)
      run_piped "$_target" "$_detail" "$root/tests/acceptance/basic4k.keys"
      if assert_piped "4K BASIC ($_cmd)" "$PIPED_OUT" <<EOF
ALTAIR BASIC
OK
42
TAPE OK
EOF
      then _ok=1; fi ;;
    BASIC1)    if run_expect "$_target" basic1.exp    "$bin";           then _ok=1; fi ;;
    DISKBASIC) if run_expect ""        diskbasic.exp "$bin" "$_target"; then _ok=1; fi ;;
    SOL)       if run_expect ""        trek80.exp    "$bin" "$_target"; then _ok=1; fi ;;
  esac
  if [ "$_ok" -eq 1 ]; then passed=$((passed + 1)); echo "  PASS $_cmd"
  else failed=$((failed + 1)); echo "  ---- FAIL $_cmd"; fi
}

echo "verify-package: running the manual's documented commands against"
echo "                $archive"
echo

# Iterate the work-list by INDEX, pulling each record with sed. A streaming `while read` loop
# shares its input descriptor with the children spawned in the body (the backgrounded simulator,
# expect), which disturb it and silently drop and reorder commands; reading each line
# independently shares nothing with any child. The list is tiny, so O(n^2) is free.
nrec=$(awk 'END{print NR}' "$records")
rec_i=0
while [ "$rec_i" -lt "$nrec" ]; do
  rec_i=$((rec_i + 1))
  record=$(sed -n "${rec_i}p" "$records")
  [ -n "$record" ] || continue
  file=$(printf  '%s' "$record" | cut -f1)
  line=$(printf  '%s' "$record" | cut -f2)
  setup=$(printf '%s' "$record" | cut -f3)
  acmd=$(printf  '%s' "$record" | cut -f4)
  [ -n "${acmd:-}" ] || continue
  rel=${file#"$root"/}
  arg=${acmd#\$ altairsim }
  argx=$(expand "$arg")

  case $argx in
    *'{{'*'}}'*)
      echo "verify-package: FAIL -- $rel:$line has an UNEXPANDED TOKEN: $argx" >&2
      echo "  Add it to docs/package.map." >&2
      failed=$((failed + 1)); continue ;;
  esac

  # Only PATH-shaped args are packaged-media examples. A bare word ($ altairsim basic4k) or an
  # empty arg ($ altairsim) is a built-in launch -- log and skip, do not dispatch.
  is_path=0
  case $argx in */*|*.toml) is_path=1 ;; esac
  if [ "$is_path" -eq 0 ]; then
    echo "  SKIP (built-in, not packaged media): $rel:$line  \$ altairsim ${arg:-<default>}"
    continue
  fi

  # Setup + where the command runs from:
  #   cp -R A B : copy inside the scratch tree (guarded against a second chapter re-copying),
  #               run from base with the copied path -- proves the "copy the folder" step and
  #               never touches the archive under dist/.
  #   cd D      : run from base/D with the bare filename.
  #   ls / none : run from base with the expanded path.
  rundir=$base
  toml_run=$argx
  case $setup in
    '$ cp -R '*)
      _src=$(printf '%s' "$setup" | awk '{print $4}')
      _dst=$(printf '%s' "$setup" | awk '{print $5}')
      [ -e "$base/$_dst" ] || cp -R "$base/$_src" "$base/$_dst" ;;
    '$ cd '*)
      _cd=$(printf '%s' "$setup" | awk '{print $3}')
      rundir=$base/$_cd
      toml_run=$(basename "$argx") ;;
  esac

  bn=$(basename "$toml_run")
  case $bn in
    cpm22-buffered.toml) dispatch CPM   "$rundir" "$toml_run" "\$ altairsim $arg" ;;
    hdsk.toml)           dispatch HDSK  "$rundir" "$toml_run" "\$ altairsim $arg" ;;
    basic4k.toml)        dispatch BASIC "$rundir" "$toml_run" "\$ altairsim $arg" ;;
    basic1.toml)
      _cwd=$(cd "$rundir/$(dirname "$toml_run")" && pwd)
      dispatch BASIC1 "$_cwd" "" "\$ altairsim $arg" ;;
    diskbasic.toml)
      _dir=$(cd "$rundir/$(dirname "$toml_run")" && pwd)
      dispatch DISKBASIC "$_dir" "" "\$ altairsim $arg" ;;
    trek80.toml)
      _tp="$rundir/$toml_run"
      dispatch SOL "$_tp" "" "\$ altairsim $arg" ;;
    *)
      echo "verify-package: FAIL -- $rel:$line runs a documented example this check does not know:" >&2
      echo "    \$ altairsim $arg   ->   $argx" >&2
      echo "  Add a dispatch case for $bn to tools/verify-package.sh." >&2
      failed=$((failed + 1)) ;;
  esac
done

echo
# A claimed test that runs nothing is worse than no test (issue #308's whole point), so an
# empty extraction is itself a failure -- the extractor or the manual's fence layout changed.
if [ "$((total + failed + skipped))" -eq 0 ]; then
  echo "verify-package: FAIL -- extracted NO runnable commands from docs/manual/*.md." >&2
  echo "  The fence layout or the extractor changed; this check is not proving anything." >&2
  exit 2
fi

if [ "$skipped" -ne 0 ]; then
  echo "verify-package: NOTE -- $skipped pty command(s) were NOT verified (no expect on this box)." >&2
  echo "  Install expect to verify BASIC 1.0, disk BASIC and the Sol-20 too." >&2
fi

if [ "$failed" -ne 0 ]; then
  echo "verify-package: FAIL -- $failed of $((passed + failed)) manual commands did not reach their documented prompt." >&2
  exit 1
fi

echo "verify-package: PASS -- $passed/$passed manual commands reached their documented prompt (skipped $skipped)."
exit 0
