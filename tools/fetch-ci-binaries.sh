#!/bin/sh
#
# Wait for CI, then put the three platform binaries it built into ./artifacts.
#
#   usage: tools/fetch-ci-binaries.sh [PR-number | run-id]
#
#          no argument -> the newest CI run on the CURRENT branch
#
# WHY THIS IS A LOCAL PULL AND NOT A CI PUSH. The obvious shape -- "have the workflow drop
# the binaries on my machine when it goes green" -- cannot exist. A GitHub Actions runner
# is a throwaway VM on GitHub's network with no route to this laptop, and the only way it
# could reach one is if we opened a door for it. So the artifacts are pulled, and the
# thing doing the pulling is the thing that was already sitting here waiting for CI.
#
# THE POINT OF WAITING. `gh run watch --exit-status` fails when CI fails, so this script
# cannot hand you binaries from a red run -- and the download is a natural gate before
# `gh pr merge`: if it printed three files, the code that produced them passed on three
# platforms. It does not merge for you. Deciding to merge stays a person's job.
#
# ./artifacts IS SCRATCH. It is .gitignore'd, it is overwritten on every fetch, and
# nothing in the build or the test suite reads from it. These are the binaries CI built,
# kept around to run or hand to someone -- they are not a build output of this tree.

set -e

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$REPO_ROOT/artifacts"

command -v gh >/dev/null 2>&1 || {
    echo "fetch-ci-binaries: needs the GitHub CLI (gh) on PATH" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Which run? An argument is either a PR number or a run id. They are told apart by
# SIZE, not by syntax: run ids are 11-digit-ish counters and PR numbers are small.
# Anything under a million is a PR, which this repo will not outgrow.
# ---------------------------------------------------------------------------
run=""
case "$1" in
    "")
        branch=$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)
        ;;
    *[!0-9]*)
        echo "fetch-ci-binaries: '$1' is neither a PR number nor a run id" >&2
        exit 1
        ;;
    *)
        if [ "$1" -ge 1000000 ]; then
            run="$1"
        else
            branch=$(gh pr view "$1" --json headRefName -q .headRefName)
        fi
        ;;
esac

if [ -z "$run" ]; then
    echo "fetch-ci-binaries: looking for a CI run on '$branch'"
    run=$(gh run list --workflow=ci.yml --branch "$branch" --limit 1 \
              --json databaseId -q '.[0].databaseId')
    [ -n "$run" ] || {
        echo "fetch-ci-binaries: no CI run on '$branch' -- has it been pushed?" >&2
        exit 1
    }
fi

# Blocks while the run is in flight and exits nonzero if it ends red, so a failed CI
# stops us here rather than leaving stale binaries lying around looking current.
gh run watch "$run" --exit-status

# A fresh directory every time: a leftover binary from an older run is worse than no
# binary at all, because it looks exactly like a current one.
rm -rf "$OUT"
gh run download "$run" --dir "$OUT"

# THE EXECUTABLE BIT DOES NOT SURVIVE THE ROUND TRIP. upload-artifact zips without POSIX
# modes, so everything arrives 0644 and the Unix binaries will not run until we say so.
find "$OUT" -type f ! -name '*.exe' -exec chmod +x {} +

echo
echo "CI binaries from run $run:"
find "$OUT" -type f | sed "s|^$REPO_ROOT/|  |"
