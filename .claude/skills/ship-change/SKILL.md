---
name: ship-change
description: How a reviewed altairsim change is shipped — commit after the maintainer approves, open the PR only after a second approval, then start ship-merge, which waits for CI, merges on green and comments on the related issues. Use when told to commit, open the PR, push, ship or merge. Every step here waits for its own approval; work-task comes first.
---

# Ship a reviewed change: commit, PR, merge, issues

**Each gate is one explicit yes from the maintainer, for that step only.** An approval to
commit is not an approval to open a PR. If you are unsure whether you have one, you do not:
ask.

## 1. Commit — after the maintainer approves the review

**Before you stage: does this change what a package holder gets?** If the diff touches
`docs/manual/`, `docs/monitor/`, `docs/debugger/`, `examples/` or
`docs/package.map`, then `docs/changelog/changelog.md` `## Unreleased` must already say what
someone can do now that they could not do in the last release. **A new document in the archive
and an example that now ships both count** — "it is only documentation" is how this gets missed,
and it has been. CI refuses the PR otherwise (the `Changelog entry` job); the way past it is a
real entry, or a `no changelog` label when a human decides one is not wanted. An entry for a
fix names what the simulator did wrong (see `work-task`), not only what the guest does now. A release removes
the `## Unreleased` header; if it is not there, the entry adds it, above the newest `## X.Y.Z`.

- Stage exactly the files that were reviewed, by name. Never `git add -A` or `git add .`.
- The message follows the repo's style: `type(scope): summary`, then a body that says why.
- **No `Co-Authored-By`, no AI attribution, no quote of the maintainer.**
- **An issue it fixes: check who opened it first** (`gh issue view <N> --json author`).
  Opened by `deltecent` → `Fixes #N`, and the merge closes it. Opened by anyone else →
  `Refs #N`: `Fixes`, `Closes` and `Resolves` all close the issue on merge, and it is not ours
  to close.
- If more changes are wanted, go back to `work-task` step 3. Every new change is reviewed
  before it is committed.

## 2. Wait for approval to open the PR

Report the commits on the branch (`git log --oneline master..`) and stop. More commits may be
wanted first; each goes through review and step 1.

## 3. Open the PR — after approval

```sh
git push -u origin <branch>
gh pr create --base master --title "<type(scope): summary>" --body-file <file>
```

The body gives a summary, the verification done (pass lines), and for each related issue
`Fixes #N` if `deltecent` opened it or `Refs #N` if anyone else did — the same check as the
commit message. No attribution. Any comment you post on it ends with
`--AltairSim Claude`.

## 4. Start `ship-merge`

After the PR is open, run the `ship-merge` skill. It runs in a fresh subagent, so CI, the merge
and the issue comments do not send this conversation with each step. Give it these arguments:

- the PR number and the branch
- each related issue, with its author and its keyword: `Fixes #N` or `Refs #N`

It waits for CI, merges on green, comments on the issues, and watches the `master` run and the
PDF commit. It stops and reports a red, cancelled or stalled leg.

**On a stop report:** do not merge. A fix for red CI is a new change: `work-task` step 3, review,
commit approval. Before you run `ship-merge`, say so if the change leans on anything CI does not
check, such as a platform not tested by hand or a window check that a person has not done.

## 5. Close out, so the session can be cleared

**The merge is not the end of the work. Finish these before you say the task is done, so the
maintainer can `/clear` without losing anything.** Say when they are all done.

- **Sync:** `git switch master && git pull --ff-only`.
- **Confirm the branch is gone** both locally and on origin (`git fetch --prune`), and that
  `git status` is clean. A leftover branch or a stray edit is the thing a `/clear` hides.
- If `TODO.md` tracks the item, update it. It is untracked, so it needs no branch or PR.
- **Write back what was learned.** A non-obvious trace, a trap paid for once, or a procedure
  that is now a skill goes into a memory note (and its one-line pointer in `MEMORY.md`) or
  into `docs/devguide/` when the fact is about how the code is built. Nothing durable should
  exist only in the transcript.
- **Report the final state in the reply**: the merge commit, the PDF commit if there was one,
  what was written back, and anything deliberately left undone.

## A PR from another contributor

If the PR's author is not `deltecent`, this skill does not apply: use `review-pr`. Its merge
needs green CI **and** the maintainer's approval.
