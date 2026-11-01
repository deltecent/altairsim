---
name: ship-merge
context: fork
effort: low
description: The mechanical tail of ship-change, run in a fresh subagent after the maintainer approves a PR — wait for CI, merge on green, comment on the related issues, and watch the master run and the PDF commit. Started only by ship-change, with the PR number, the branch and the related issues as its arguments.
---

# Merge an approved PR: CI, merge, issues

## Running as a subagent

This skill runs in its own subagent (`context: fork`). It starts without the session's
conversation. Everything it knows comes from its arguments and this file. It cannot ask the
maintainer anything during the run.

**Arguments:** the PR number, the branch, and each related issue with its author and its
keyword: `Fixes #N` for `deltecent`, `Refs #N` for anyone else. If one is missing, stop and
report it. If the PR's author is not `deltecent`, stop: `review-pr` owns that merge.

**On a red, cancelled or stalled leg, stop and report.** Do not fix anything. Do not re-run
anything. A fix is a new change, and the main session owns it.

## 1. Wait for CI

Start ONE command with `run_in_background`:

```sh
sleep 120; perl -e 'alarm 480; exec @ARGV' gh pr checks <N> --watch --interval 20 --fail-fast
```

The command waits 2 minutes. Then `gh` checks every 20 seconds until CI is done or a leg fails.
The `alarm` ends `gh` after 8 more minutes, for a limit of 10 minutes. The limit is in the command
because a tool timeout does not apply to a background command. `perl` sets the limit because
macOS has no `timeout` command.

End your turn after you start the command. Its exit wakes this subagent. Do not poll.

- **Exit 0:** every leg passed.
- **Exit 142:** the limit ended the wait, so CI has stalled. Report it. Do not repeat the command.
- **Any other exit:** read the legs with `gh pr checks <N>`.

A core change runs Linux, macOS and Windows. Any other change runs Linux only
(`tools/ci-changed-code.sh` decides, and the `Classify the change` log says which). A leg that
never started is not a missing check.

**Red or cancelled:** a cancelled leg, or a leg with `steps=0`, is not green. Get the failing
step and its log excerpt with `gh run view <id> --log-failed`, and report them. Do not merge.

## 2. Merge on green

No further approval is needed when every leg that ran is green.

```sh
gh pr merge <N> --merge --delete-branch
```

**Never `--auto`:** there is no branch protection, so it merges at once, before CI.

## 3. Comment on each related issue

Post `Fixed by #<PR> (merged <sha>)`, or what is still left, on every related issue, whether or
not the merge closed it. Sign the comment `--AltairSim Claude`. Never quote the maintainer.
Never close an issue yourself. Only a `deltecent` issue closes, and only through `Fixes #N`.
Write to the `simplified-english` light rules.

## 4. Watch `master`

Start each watch with `run_in_background`, as in step 1. Each watch has a limit of 10 minutes
and no `sleep`. Exit 142 means a stall. Do not repeat a watch.

- **A code merge:** `CI` on `master` builds Linux, macOS and Windows. Find the run, then watch
  it:

  ```sh
  gh run list --workflow=ci.yml --branch master -L 1
  perl -e 'alarm 600; exec @ARGV' gh run watch <id> --interval 20 --exit-status
  ```

  Read every leg's result. Report a red leg with its log excerpt.
- **A change under `docs/`:** the `Documents` workflow pushes a `Rebuild the PDFs for <sha>`
  commit a minute or two after the merge. Find it with `gh run list --workflow=docs.yml -L 1`,
  and watch it the same way.

## 5. Report

Return the merge commit, the PDF commit if there was one, the result of each leg, and the
comments posted. Say what was not done, and why. The main session does the local sync.
