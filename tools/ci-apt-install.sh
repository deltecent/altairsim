#!/usr/bin/env bash
#
# Robust apt install for CI. The GitHub Ubuntu runners' `apt-get update` reaches
# EVERY configured mirror, and when one is slow or unreachable the command HANGS
# rather than failing -- a leg once sat ~24 minutes on such a step with nothing else
# wrong, and it recurred across separate runs and separate workflows (the expect
# install in ci.yml, the poppler-utils install in docs.yml). A plain retry cannot
# rescue a hang; only a timeout can.
#
# So, for the package list given as arguments:
#   1. Try a plain `install` FIRST. The runner image ships fresh-enough apt lists,
#      so the flaky `update` is usually unnecessary -- this skips it entirely in the
#      common case.
#   2. Fall back to `update`+install only if the direct install misses.
#   3. Bound EVERY apt call with `timeout` so a wedged mirror is killed and retried
#      instead of stalling the leg.
#   4. Retry three times, then fail fast and red.
#
# Pair with `timeout-minutes:` on the step as an outer backstop. Invoke as
# `bash tools/ci-apt-install.sh <pkg>...` so no execute bit is required.
set -uo pipefail

pkgs="$*"
if [ -z "$pkgs" ]; then
    echo "usage: ci-apt-install.sh <package>..." >&2
    exit 2
fi

export DEBIAN_FRONTEND=noninteractive

attempt_install() {
    # shellcheck disable=SC2086  # word-splitting $pkgs into separate package args is intended
    sudo timeout 150 apt-get install -y --no-install-recommends $pkgs && return 0
    sudo timeout 150 apt-get update -o Acquire::Retries=3 \
        && sudo timeout 150 apt-get install -y --no-install-recommends $pkgs
}

for attempt in 1 2 3; do
    if attempt_install; then
        echo "installed [$pkgs] on attempt $attempt"
        exit 0
    fi
    echo "::warning::apt install attempt $attempt for [$pkgs] failed or timed out; retrying in 15s"
    sleep 15
done

echo "::error::could not install [$pkgs] after 3 attempts"
exit 1
