#!/bin/sh
#
# The coordinator's side of a release build: start tools/release-worker.sh on one build
# machine, PROVE THAT IT STARTED, and read its result. It reads this site's machines from
# distribution.conf (distribution.conf.example names every key).
#
# WHY THIS IS A SCRIPT. How a worker is started over ssh was written by hand for each release,
# and it is where the mistakes were (issue #643, and the trap list that the
# `release-coordinator` skill used to carry):
#
#   - `ssh host 'bash -s' < script` gives the script to its own commands as stdin, and the
#     shell's parser loses its place. The script is COPIED to the box and run as a file.
#   - It is copied OUTSIDE the checkout: step 1 of the worker is `git checkout -f`, which can
#     replace a script inside the checkout while it runs.
#   - A foreground ssh that drops takes the build with it. On macOS and Linux the worker runs
#     under `nohup` on the box and writes its log there.
#   - Windows sshd ends a remote background process when the session ends. There the ssh
#     itself runs under a local `nohup`, with a keep-alive, and the log is local. In 1.3.0 a
#     `Start-Process` wrapper printed `started` and ran nothing.
#   - Bash is not on the ssh PATH of a Windows box. It is called by its short path, which has
#     no space in it: C:\PROGRA~1\Git\bin\bash.exe.
#
# AN EMPTY LOG PROVES NOTHING. `start` returns OK only when the checkout on the box is at the
# new tag AND the worker script is a running process there AND its log names this version.
#
#   usage: tools/release-drive.sh preflight [--conf FILE]
#          tools/release-drive.sh start  <target> X.Y.Z [--skip-slow] [--no-deliver]
#                                        [--unsigned] [--repo DIR] [--conf FILE]
#          tools/release-drive.sh status <target> [--conf FILE]
#          tools/release-drive.sh wait   <target>... [--conf FILE]
#
#     <target>      macos-arm64 | macos-x86_64 | linux-x86_64 | windows-x86_64
#     --skip-slow   Do not run the CPU exercisers. For a dry run only.
#     --no-deliver  Do not copy the archive to the coordinator. For a dry run only.
#     --unsigned    macOS: sign ad hoc, do not notarize. For a dry run only. Without it, a
#                   macOS worker signs with <TARGET>_SIGN_IDENTITY and the three keys after
#                   it, and stops before step 1 when they are not set.
#     --repo        Build in this checkout, not in the one that distribution.conf names. Use
#                   it for a dry run of the coordinator's own target in a second clone.
#     --conf        Read this file. Default: distribution.conf in the repository root.
#
# `preflight` runs before the tag, and proves what a release needs (issues #811 and #816):
#   1. The coordinator. Its checkout has no change to a tracked file. A clone of its HEAD
#      configures, builds, packages with the committed manual, and passes verify-package.sh.
#      The clone is in a temporary folder, so the build/ of this checkout is not touched.
#   2. Each worker, only when the coordinator passed. ssh answers, the checkout is reset
#      (`git reset --hard` and `git clean -fdx`), and an scp from the worker to
#      COORDINATOR_DIST arrives. The scp is the one that step 8 of the worker makes.
#   3. On each Mac, the coordinator too: the signing keychain unlocks with its password file,
#      holds <TARGET>_SIGN_IDENTITY, and Apple answers with the notary profile. Then a copy of
#      /usr/bin/true is signed as tools/macos-sign.sh signs the program, within 60 seconds. A
#      keychain that asks "allow codesign?" passes the first three checks and then makes
#      codesign wait for a click. In 1.4.0 the package step waited 35 minutes for it.
# Each check prints `preflight: OK -- <what>` or `preflight: FAIL -- <what>`. The last line is
# `preflight: OK` or `preflight: FAIL`, and the exit status is 0 only for OK.
#
# A WORKER CHECKOUT IS DISPOSABLE. `start` and `preflight` reset it, and print each path that
# `git clean` removed. The coordinator's checkout is never reset: it is a development tree.
#
# `status` prints the end of the worker's log. Its exit status is 0 when the worker printed
# `release-worker: OK`, 1 when it printed FAIL or is gone with no result, and 2 while it
# still runs.
#
# `wait` checks each target once a minute until every worker has ended, so the coordinator
# does not poll by hand (issue #812). Then it prints the report block of each target under
# `##### <target>`, and ONE last line: `release-drive: wait OK -- all N workers OK`, or
# `release-drive: wait FAIL -- <targets>`. Its exit status is 0 only when every worker printed
# `release-worker: OK`. A worker that is gone with no result line is a FAIL.
#
# The coordinator's own target (COORDINATOR_TARGET) runs here, with no ssh and no delivery:
# its archive is written to dist/ directly. Its checkout must have no change to a tracked
# file, because the worker checks out the tag in it.

set -u

root=$(cd "$(dirname "$0")/.." && pwd)
worker=$root/tools/release-worker.sh
name=altairsim-release-worker
# The same name as a pgrep pattern that does not match the shell that runs the pgrep.
pat='[a]ltairsim-release-worker'
winbash='C:\PROGRA~1\Git\bin\bash.exe'
sshopt="-o BatchMode=yes -o ConnectTimeout=10"

die() { echo "release-drive: FAIL -- $*" >&2; exit 1; }

usage() {
  sed -n '/^#   usage:/,/^# file, because/p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

[ $# -ge 1 ] || usage 1
cmd=$1; shift
case $cmd in
  preflight|start|status|wait) ;;
  -h|--help) usage 0 ;;
  *) echo "release-drive: unknown command: $cmd" >&2; usage 1 ;;
esac

target=""
targets=""
version=""
slow=yes
deliver=yes
unsigned=no
repo_arg=""
conf=$root/distribution.conf

while [ $# -gt 0 ]; do
  case $1 in
    --skip-slow)  slow=no; shift ;;
    --no-deliver) deliver=no; shift ;;
    --unsigned)   unsigned=yes; shift ;;
    --repo)       [ $# -ge 2 ] || die "--repo needs a value"; repo_arg=$2; shift 2 ;;
    --conf)       [ $# -ge 2 ] || die "--conf needs a value"; conf=$2; shift 2 ;;
    -*)           echo "release-drive: unknown argument: $1" >&2; usage 1 ;;
    *)            if [ "$cmd" = preflight ]; then
                    echo "release-drive: preflight takes no target: $1" >&2; usage 1
                  elif [ "$cmd" = wait ]; then targets="$targets $1"
                  elif [ -z "$target" ]; then target=$1
                  elif [ -z "$version" ]; then version=$1
                  else echo "release-drive: unknown argument: $1" >&2; usage 1; fi
                  shift ;;
  esac
done

if [ "$cmd" = preflight ]; then
  :
elif [ "$cmd" = wait ]; then
  [ -n "$targets" ] || { echo "release-drive: give one or more targets." >&2; usage 1; }
else
  [ -n "$target" ] || { echo "release-drive: give a target." >&2; usage 1; }
  targets=$target
fi
for t in $targets; do
  case $t in
    macos-arm64|macos-x86_64|linux-x86_64|windows-x86_64) ;;
    *) echo "release-drive: give a target, not '$t'." >&2; usage 1 ;;
  esac
done

[ -f "$conf" ] || die "$conf is missing. Copy distribution.conf.example and fill it in; do not rebuild the addresses from memory"
# shellcheck disable=SC1090
. "$conf"

# The value of <TARGET>_<KEY> for a target, or empty.
conf_of() {  # $1 = target, $2 = KEY
  key=$(printf '%s' "$1" | tr 'a-z-' 'A-Z_')_$2
  eval "printf '%s' \"\${$key:-}\""
}

# Set the values of one target: host, repo, is_local, log and pidf.
select_target() {  # $1 = target
  target=$1
  host=$(conf_of "$target" SSH)
  repo=${repo_arg:-$(conf_of "$target" REPO)}
  is_local=no
  [ "$target" = "${COORDINATOR_TARGET:-}" ] && is_local=yes
  if [ $is_local = no ] && [ -z "$host" ]; then
    die "$conf has no worker for $target"
  fi
  [ $is_local = no ] || [ -n "$repo" ] || repo=$root
  log=$root/dist/worker-$target.log     # local targets and Windows: the log is here
  pidf=$root/dist/worker-$target.pid
}

# Every target is checked before anything else is done.
for t in $targets; do select_target "$t"; done

mkdir -p "$root/dist"
rscript=/tmp/$name.sh                 # macOS and Linux workers
rlog=/tmp/$name.log

# Is the worker of a target still running? 0 = yes.
running() {  # $1 = target
  t_host=$(conf_of "$1" SSH)
  if [ "$1" = "${COORDINATOR_TARGET:-}" ] || [ "$1" = windows-x86_64 ]; then
    # A local process: the worker itself, or the ssh that holds the Windows worker.
    [ -f "$root/dist/worker-$1.pid" ] && kill -0 "$(cat "$root/dist/worker-$1.pid")" 2> /dev/null
  else
    [ -n "$t_host" ] && ssh $sshopt "$t_host" "pgrep -f '$pat'" > /dev/null 2>&1
  fi
}

# The worker's log, to stdout.
fetch_log() {
  if [ $is_local = yes ] || [ "$target" = windows-x86_64 ]; then
    cat "$log" 2> /dev/null
  else
    ssh $sshopt "$host" "cat $rlog" 2> /dev/null
  fi
}

# `git describe --tags` in the worker's checkout.
describe() {
  if [ $is_local = yes ]; then
    git -C "$repo" describe --tags 2> /dev/null
  else
    ssh $sshopt "$host" "git -C $repo describe --tags" 2> /dev/null | tr -d '\r'
  fi
}

# Print the result line of a log and return 0 for OK, 1 for FAIL, 2 for none yet.
result_of() {
  line=$(printf '%s\n' "$1" | tr -d '\r' | grep -E '^release-worker: (OK|FAIL)' | tail -n 1)
  case $line in
    'release-worker: OK'*)   return 0 ;;
    'release-worker: FAIL'*) return 1 ;;
    *)                       return 2 ;;
  esac
}

# The state of the selected target's worker: 0 = OK, 1 = FAIL, 2 = still runs, 3 = gone with
# no result line. The log is left in $text.
state_of() {
  text=$(fetch_log)
  result_of "$text"; res=$?
  case $res in
    0|1) return $res ;;
  esac
  running "$target" && return 2
  return 3
}

# The script that resets a worker's checkout. It runs ON the worker, in sh or Git Bash, so the
# same commands work on all three. With a destination and a file name it also makes the file
# and copies it there with scp, as step 8 of the worker does. Its name must not match $pat.
reset_script() {
  cat <<'RESET'
#!/bin/sh
set -u
cd "$1" 2> /dev/null || { echo "release-drive-reset: FAIL -- cannot enter the checkout $1"; exit 1; }
git reset -q --hard || { echo "release-drive-reset: FAIL -- git reset --hard failed in $1"; exit 1; }
git clean -fdx || { echo "release-drive-reset: FAIL -- git clean -fdx failed in $1"; exit 1; }
echo "release-drive-reset: reset"
if [ $# -ge 3 ]; then
  f=${TMPDIR:-/tmp}/$3
  echo "release-drive preflight" > "$f"
  if ! scp -o BatchMode=yes -o ConnectTimeout=10 "$f" "$2/" 2>&1; then
    rm -f "${f:?}"
    echo "release-drive-reset: FAIL -- scp to $2 failed"
    exit 1
  fi
  rm -f "${f:?}"
fi
echo "release-drive-reset: OK"
RESET
}

# Reset the checkout of the selected worker. With $1 = a destination and $2 = a file name, also
# copy that file from the worker to the destination. Prints each path that `git clean` removed,
# and returns 0 only when the script on the worker printed its OK line. The worker's output is
# left in $rtext.
reset_worker() {
  rcopy=${TMPDIR:-/tmp}/altairsim-release-reset.sh
  reset_script > "$rcopy"
  rtext=""
  if [ "$target" = windows-x86_64 ]; then
    # The copy goes to the login directory, which is also where Git Bash starts.
    scp $sshopt "$rcopy" "$host:altairsim-release-reset.sh" > /dev/null 2>&1 \
      && rtext=$(ssh $sshopt "$host" "$winbash --login altairsim-release-reset.sh $repo $*" 2>&1)
  else
    scp $sshopt "$rcopy" "$host:/tmp/altairsim-release-reset.sh" > /dev/null 2>&1 \
      && rtext=$(ssh $sshopt "$host" "sh /tmp/altairsim-release-reset.sh $repo $*" 2>&1)
  fi
  rm -f "${rcopy:?}"
  [ -n "$rtext" ] || rtext="release-drive-reset: FAIL -- cannot copy the reset script to $host"
  rtext=$(printf '%s\n' "$rtext" | tr -d '\r')
  printf '%s\n' "$rtext" | grep '^Removing ' | sed "s/^/  $target: /"
  printf '%s\n' "$rtext" | grep '^release-drive-reset: OK$' > /dev/null
}

# ---------------------------------------------------------------------------
if [ "$cmd" = preflight ]; then
  bad=0
  pass() { echo "preflight: OK -- $*"; }
  fail() { echo "preflight: FAIL -- $*"; bad=1; }
  finish() {
    if [ $bad = 0 ]; then echo "preflight: OK"; exit 0; fi
    echo "preflight: FAIL"; exit 1
  }

  # 1. The coordinator. Never reset: a change to a tracked file is a FAIL.
  dirty=$(git -C "$root" status --porcelain --untracked-files=no)
  if [ -n "$dirty" ]; then
    printf '%s\n' "$dirty"
    fail "coordinator: the checkout has changes to tracked files"
    finish
  fi
  pass "coordinator: no change to a tracked file"

  # The signing keychain of one Mac. $1 = the target; runs on this machine when $2 is
  # "local", else over ssh to $2. Prints nothing; the exit status is the answer.
  sign_check() {
    kc=$(conf_of "$1" KEYCHAIN); pf=$(conf_of "$1" KEYCHAIN_PASS_FILE)
    sid=$(conf_of "$1" SIGN_IDENTITY); np=$(conf_of "$1" NOTARY_PROFILE)
    if [ -z "$kc" ] || [ -z "$pf" ] || [ -z "$sid" ] || [ -z "$np" ]; then
      fail "$1: $conf does not set all of ${1}_SIGN_IDENTITY, _NOTARY_PROFILE, _KEYCHAIN and _KEYCHAIN_PASS_FILE (tools/macos-signing-setup.sh prints them)"
      return 1
    fi
    c="security unlock-keychain -p \"\$(cat $pf)\" $kc && security find-identity -v -p codesigning $kc | grep -q $sid && xcrun notarytool history --keychain-profile $np --keychain $kc > /dev/null"
    if [ "$2" = local ]; then sh -c "$c" > /dev/null 2>&1; else ssh $sshopt "$2" "$c" > /dev/null 2>&1; fi \
      && pass "$1: the signing keychain holds the identity, and Apple accepts the notary profile" \
      || { fail "$1: the signing keychain $kc does not unlock, does not hold $sid, or Apple refuses profile $np. Run tools/macos-signing-setup.sh there"; return 1; }
    # A real signature, with the arguments of tools/macos-sign.sh. perl's alarm ends a
    # codesign that waits for an "allow" dialog, so the check fails and does not hang.
    c="f=\$(mktemp -t preflight-sign) && cp /usr/bin/true \$f && security unlock-keychain -p \"\$(cat $pf)\" $kc && perl -e 'alarm 60; exec @ARGV' codesign --force --options runtime --timestamp --keychain $kc --sign $sid \$f; r=\$?; rm -f \$f; exit \$r"
    if [ "$2" = local ]; then sh -c "$c" > /dev/null 2>&1; else ssh $sshopt "$2" "$c" > /dev/null 2>&1; fi \
      && pass "$1: a test codesign with the identity finishes" \
      || fail "$1: a test codesign did not finish in 60 seconds, or it failed. The keychain may ask for permission. On that Mac, run tools/macos-signing-setup.sh, or its security set-key-partition-list line"
  }
  case ${COORDINATOR_TARGET:-} in
    macos-*) sign_check "$COORDINATOR_TARGET" local ;;
  esac

  # A clone of HEAD, built as step 2 of the worker builds it. Each command's output goes to
  # the log; a FAIL prints its last lines.
  plog=$root/dist/preflight-coordinator.log
  : > "$plog"
  step() {  # $1 = what it does, the rest = the command
    what=$1; shift
    echo "+ $*" >> "$plog"
    "$@" >> "$plog" 2>&1 && return 0
    tail -n 15 "$plog"
    fail "coordinator: $what failed. The full output is in $plog"
    return 1
  }
  clone=$(mktemp -d "${TMPDIR:-/tmp}/release-preflight.XXXXXX") || die "mktemp failed"
  # Remove only the folder that mktemp made: a name under the temporary folder, with this
  # prefix. Any other value of $clone is left alone.
  remove_clone() {
    case ${clone:?} in
      "${TMPDIR:-/tmp}"/release-preflight.?*) rm -rf -- "$clone" ;;
      *) echo "release-drive: not removing '$clone': not a preflight folder" >&2 ;;
    esac
  }
  trap remove_clone EXIT
  # The processor count. A bare --parallel runs `make -j` with no limit, and that ran a 3-core
  # runner out of memory (PR #817).
  ncpu() { nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2; }
  src=$clone/src
  case ${COORDINATOR_TARGET:-} in
    macos-*) osx="-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0" ;;
    *)       osx="" ;;
  esac
  # shellcheck disable=SC2086
  step "git clone --local" git clone -q --local --no-checkout "$root" "$src" \
    && step "the checkout of HEAD" git -C "$src" checkout -q --detach "$(git -C "$root" rev-parse HEAD)" \
    && step "the configure" cmake -S "$src" -B "$src/build" -DCMAKE_BUILD_TYPE=Release \
         "-DCMAKE_PREFIX_PATH=$HOME/opt/sdl3-static" $osx \
    && if grep -F -- '-- SDL3 found -- video boards enabled (windowed)' "$plog" > /dev/null; then :
       else fail "coordinator: the configure output has no '-- SDL3 found' line"; false; fi \
    && step "the build" cmake --build "$src/build" --config Release --parallel "$(ncpu)" \
    && step "build-package.sh" "$src/tools/build-package.sh" --pdf "$src/docs/altairsim-manual.pdf" \
         --target "${COORDINATOR_TARGET:-}" "$src/dist" \
    && archive=$(ls "$src"/dist/altairsim-*-"${COORDINATOR_TARGET:-}".tar.gz 2> /dev/null | head -n 1) \
    && if [ -n "$archive" ]; then :
       else fail "coordinator: build-package.sh wrote no archive"; false; fi \
    && step "verify-package.sh" "$src/tools/verify-package.sh" "$archive" \
    && if line=$(grep -E '^verify-package: PASS -- ' "$plog"); then
         pass "coordinator: the package of HEAD builds. $line"
       else fail "coordinator: verify-package.sh printed no PASS line"; fi
  [ $bad = 0 ] || finish

  # 2. The workers: every target but the coordinator's. A release needs all four archives.
  [ -n "${COORDINATOR_USER:-}" ] && [ -n "${COORDINATOR_HOST:-}" ] && [ -n "${COORDINATOR_DIST:-}" ] \
    || die "$conf does not have COORDINATOR_USER, COORDINATOR_HOST and COORDINATOR_DIST"
  case $COORDINATOR_DIST in
    /*) ddir=$COORDINATOR_DIST ;;
    *)  ddir=$HOME/$COORDINATOR_DIST ;;
  esac
  for t in macos-arm64 macos-x86_64 linux-x86_64 windows-x86_64; do
    [ "$t" = "${COORDINATOR_TARGET:-}" ] && continue
    if [ -z "$(conf_of "$t" SSH)" ]; then
      fail "$t: $conf has no worker for it"
      continue
    fi
    select_target "$t"
    if ! ssh $sshopt "$host" "exit 0" > /dev/null 2>&1; then
      fail "$t: ssh $host does not answer"
      continue
    fi
    pass "$t: ssh $host answers"
    case $t in macos-*) sign_check "$t" "$host" ;; esac
    if running "$t"; then
      fail "$t: a worker runs there now"
      continue
    fi
    marker=preflight-$t-$$.txt
    rm -f "${ddir:?}/${marker:?}"
    reset_worker "$COORDINATOR_USER@$COORDINATOR_HOST:$COORDINATOR_DIST" "$marker"; res=$?
    if printf '%s\n' "$rtext" | grep '^release-drive-reset: reset$' > /dev/null; then
      pass "$t: the checkout $repo is reset"
    fi
    if [ $res != 0 ]; then
      printf '%s\n' "$rtext" | grep -v '^Removing ' | tail -n 5
      fail "$t: $(printf '%s\n' "$rtext" | sed -n 's/^release-drive-reset: FAIL -- //p' | tail -n 1)"
    elif [ -f "$ddir/$marker" ]; then
      rm -f "${ddir:?}/${marker:?}"
      pass "$t: an scp from the worker arrives in $COORDINATOR_DIST"
    else
      fail "$t: the scp to $COORDINATOR_USER@$COORDINATOR_HOST:$COORDINATOR_DIST printed no error, and the file is not in $ddir"
    fi
  done
  finish
fi

# ---------------------------------------------------------------------------
if [ "$cmd" = status ]; then
  state_of; res=$?
  printf '%s\n' "$text" | tail -n 25
  case $res in
    0|1) exit $res ;;
    2)   echo "release-drive: $target still runs."; exit 2 ;;
  esac
  die "the worker of $target is not running and its log has no result line"
fi

# ---------------------------------------------------------------------------
if [ "$cmd" = wait ]; then
  pending=$targets
  ok=""
  failed=""
  while :; do
    left=""
    for t in $pending; do
      select_target "$t"
      state_of
      case $? in
        0) ok="$ok $t";         echo "release-drive: $t ended: OK" ;;
        1) failed="$failed $t"; echo "release-drive: $t ended: FAIL" ;;
        2) left="$left $t" ;;
        3) failed="$failed $t"; echo "release-drive: $t ended: FAIL -- gone with no result line" ;;
      esac
    done
    [ -n "$left" ] || break
    pending=$left
    sleep 60
  done

  # The report block runs from its last `target` line to the end of the log. A log with no
  # report block (a worker that died) shows its last 25 lines.
  for t in $targets; do
    select_target "$t"
    text=$(fetch_log | tr -d '\r')
    echo
    echo "##### $t"
    if printf '%s\n' "$text" | grep '^target  ' > /dev/null; then
      printf '%s\n' "$text" | awk '/^target  / { b = "" } { b = b $0 "\n" } END { printf "%s", b }' |
        sed '/^release-worker: /q'
    elif [ -n "$text" ]; then
      printf '%s\n' "$text" | tail -n 25
    else
      echo "(no log)"
    fi
  done
  echo
  n=$(echo $targets | wc -w | tr -d ' ')
  if [ -z "$failed" ]; then
    echo "release-drive: wait OK -- all $n workers OK"
    exit 0
  fi
  echo "release-drive: wait FAIL --$failed"
  exit 1
fi

# ---------------------------------------------------------------------------
# start
printf '%s\n' "$version" | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' > /dev/null \
  || die "give the version as X.Y.Z"

if running "$target"; then
  die "a worker of $target already runs"
fi

# Two targets that are one physical machine build one at a time.
case ",${SHARED_HOST:-}," in
  *",$target,"*)
    for other in $(printf '%s' "${SHARED_HOST:-}" | tr ',' ' '); do
      [ "$other" = "$target" ] && continue
      if running "$other"; then
        die "$other shares a machine with $target (SHARED_HOST) and still runs. Start $target when it is done"
      fi
    done ;;
esac

# The worker's arguments. The values of distribution.conf have no spaces (its format).
args="--version $version --repo $repo"
[ "$(conf_of "$target" PARALLEL)" = no ] && args="$args --no-parallel"
[ -n "$(conf_of "$target" PATH)" ] && args="$args --path $(conf_of "$target" PATH)"
if [ -n "$(conf_of "$target" SERIAL_A)" ] && [ -n "$(conf_of "$target" SERIAL_B)" ]; then
  args="$args --serial-a $(conf_of "$target" SERIAL_A) --serial-b $(conf_of "$target" SERIAL_B)"
fi
[ $slow = yes ] || args="$args --skip-slow"
case $target in
  macos-*)
    if [ $unsigned = yes ]; then
      args="$args --unsigned"
    elif [ -n "$(conf_of "$target" SIGN_IDENTITY)" ]; then
      args="$args --sign $(conf_of "$target" SIGN_IDENTITY)"
      args="$args --notary-profile $(conf_of "$target" NOTARY_PROFILE)"
      args="$args --keychain $(conf_of "$target" KEYCHAIN)"
      args="$args --keychain-pass-file $(conf_of "$target" KEYCHAIN_PASS_FILE)"
    fi ;;  # No identity: the worker stops before step 1 and says what is missing.
esac
if [ $is_local = no ] && [ $deliver = yes ]; then
  [ -n "${COORDINATOR_USER:-}" ] && [ -n "${COORDINATOR_HOST:-}" ] && [ -n "${COORDINATOR_DIST:-}" ] \
    || die "$conf does not have COORDINATOR_USER, COORDINATOR_HOST and COORDINATOR_DIST"
  args="$args --deliver $COORDINATOR_USER@$COORDINATOR_HOST:$COORDINATOR_DIST"
fi

# A worker's checkout is disposable: reset it, so that step 1 of the worker does not stop on a
# change that an earlier run left (issue #811). The coordinator's checkout is never reset.
if [ $is_local = no ]; then
  reset_worker || {
    printf '%s\n' "$rtext" | grep -v '^Removing ' | tail -n 5
    die "cannot reset the checkout $repo on $host"
  }
  echo "release-drive: the checkout $repo on $host is reset."
fi

if [ $is_local = yes ]; then
  copy=${TMPDIR:-/tmp}/$name.sh
  cp "$worker" "$copy" || die "cannot copy the worker script to $copy"
  # shellcheck disable=SC2086
  nohup sh "$copy" $args > "$log" 2>&1 < /dev/null &
  echo $! > "$pidf"
elif [ "$target" = windows-x86_64 ]; then
  # The copy goes to the login directory, which is also where Git Bash starts.
  scp $sshopt "$worker" "$host:$name.sh" > /dev/null \
    || die "cannot copy the worker script to $host"
  # shellcheck disable=SC2086
  nohup ssh $sshopt -o ServerAliveInterval=15 "$host" "$winbash --login $name.sh $args" \
    > "$log" 2>&1 < /dev/null &
  echo $! > "$pidf"
else
  scp $sshopt "$worker" "$host:$rscript" > /dev/null \
    || die "cannot copy the worker script to $host"
  ssh $sshopt "$host" "nohup sh $rscript $args > $rlog 2>&1 < /dev/null &" \
    || die "cannot start the worker on $host"
fi

# Prove it. The fetch and the checkout of step 1 take some seconds, so look for a minute.
tries=0
while [ $tries -lt 20 ]; do
  sleep 3
  tries=$((tries + 1))
  text=$(fetch_log)
  result_of "$text"
  if [ $? = 1 ]; then
    printf '%s\n' "$text" | tail -n 15
    die "the worker of $target stopped"
  fi
  # The first line of the worker names the version, so the log is of THIS run.
  if printf '%s\n' "$text" | grep -E "^release-worker: .*, v$version, in " > /dev/null \
     && [ "$(describe)" = "v$version" ] && running "$target"; then
    echo "release-drive: OK -- $target started. The checkout is at v$version and the worker runs."
    echo "  result:  tools/release-drive.sh status $target"
    exit 0
  fi
done

printf '%s\n' "$text" | tail -n 15
die "$target did not start: after 60 seconds the checkout is at '$(describe)', not v$version, or no worker process is there, or the log does not name v$version"
