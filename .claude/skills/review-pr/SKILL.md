---
name: review-pr
description: How a pull request from another contributor (author not deltecent) is reviewed — read it, check the feature is needed, build and run it, prove its tests actually test the change, review it against the house rules, then either fix easy problems on their branch (only when the branch can be pushed to: maintainerCanModify true and the fork not an organization's) or post a review comment. Merge needs green CI AND the maintainer's approval. Use when asked to review, look at or merge PR #N, or for any PR not authored by deltecent.
---

# Review a contributor's PR

Our own changes go through `work-task` and `ship-change`; an *issue* goes through
`review-issue`. **This skill is for a PR whose author is not `deltecent`.** The PR that merges is always **theirs** — never open our own PR
in its place.

The rules, set by the maintainer:

- **Easy problems I fix myself, on their branch** — but only when GitHub lets me push to it.
  Substantial problems get a review comment, and the contributor does the work.
- **I post review comments myself**, signed `--AltairSim Claude`. No draft for approval.
- **Merge needs green CI *and* the maintainer's explicit approval.** No merge on green here.
- **Their commits are kept**, mine go on top. Never squash, never rewrite theirs.
- **I run their code**, and prove their tests test the change.

## 1. Read it

```sh
gh pr view <N> --json author,title,body,files,commits,headRefName,headRepositoryOwner,maintainerCanModify
gh pr diff <N>
gh pr checks <N>
gh api users/<headRepositoryOwner.login> --jq .type
```

- **Record whether I can push to their branch, now, before any work.** I can only when
  both are true:
  - `maintainerCanModify` is `true`: the contributor left "Allow edits by maintainers" on.
  - The owner type is `User`. GitHub does not let maintainers push to a fork that belongs
    to an `Organization`, even when `maintainerCanModify` reads `true`. The PR page says so.

  **If I cannot push, this review ends in a comment, whatever the findings: skip steps 6
  and 8, and every finding, easy or not, goes into the step 7 review.** Never fix a PR I
  cannot push to — no branch on origin, no PR of our own.
- Read the linked issues and the `DESIGN.md` sections it touches.
- **Did CI run?** A first-time contributor's workflow runs wait for approval. Report that to
  the maintainer; do not approve the run yourself.

## 2. Is it needed?

For a feature, this is `work-task` step 1a applied to their PR:

- What is the real need behind it?
- **Can altairsim already do it, and is that documented?** Then the PR isn't needed: point
  them to the section or `HELP` entry.
- Can it do it, but the docs don't say so? Then it's a documentation gap, not a feature.
- Was it declined before? Search closed issues and `DESIGN.md`.
- Does it fit the design? A feature that gives hardware a behavior it never had is a no.

A PR that isn't needed is a finding for step 7, however good its code is.

## 3. Check it out and run it

In a worktree, so the main checkout stays on its own branch:

```sh
git fetch origin master
git worktree add --detach .claude/worktrees/pr-<N> origin/master
cd .claude/worktrees/pr-<N> && gh pr checkout <N> -b pr-<N>
cmake -B build -DWERROR=on && cmake --build build -j
./build/altair_tests <the suites it touches>
```

`gh pr checkout` records their fork as the branch's push remote. The local branch is
`pr-<N>`, but theirs is `headRefName`, so step 8 pushes to their branch by name. **Quote
the pass lines.** A new warning is a finding: CI builds with `-Werror` on all three
toolchains.

## 4. Do their tests test the change?

A negative control. Keep their test files and take out everything else they changed:

```sh
base=$(git merge-base origin/master HEAD)
git checkout "$base" -- <their non-test files>
cmake --build build -j && ./build/altair_tests <their suites>    # MUST FAIL
git checkout HEAD -- <their non-test files>                      # restore
```

**If their tests still pass without their change, they don't test it.** That is a finding.
Also check the tests against `work-task`'s rules. None of these is allowed:

- a loosened assertion
- a skipped or disabled test
- an input chosen to avoid the bug
- a retry or sleep hiding a race
- known-bad output recorded as expected

## 5. Review against the house rules

- **Scoped to one thing.** Unrelated changes belong in their own PR.
- **Docs in the same PR:**
  - the manual in `docs/manual/`, and `ORDER` for a new chapter
  - the manual names nothing outside the package
  - `docs/devguide/`
  - `DESIGN.md` when the design moved
  - `docs/manual/ref/` regenerated, never hand-edited
  - a changelog line for a user-visible change
- **"Board", not "card".**
- **A bug is fixed, not documented.** A new "be careful to…" warning is a finding.
- **Code reads like the code around it** — naming, comment density, idiom.
- **No closing keyword on someone else's issue.** `Fixes/Closes/Resolves #N` closes the issue
  on merge. It is fine for an issue opened by `deltecent` or by the PR's own author; for anyone
  else's, it must be `Refs #N`.

**An unrelated bug found along the way gets its own issue**, as in `work-task` — not a
request to the contributor.

## 6. Easy or substantial?

**Only when step 1 found I can push to their branch.** It is a judgement call:

- **Easy** — local and mechanical, with no guess about what they meant: missing docs, a
  naming fix, a missing or weak test, a warning, a small bug in their own new code.
- **Substantial** — changes their design or approach, rewrites most of it, a feature that
  isn't needed, a conflict with master, or any fix where I'd have to guess their intent.

Mixed? Anything substantial makes the whole PR substantial: comment on all of it, easy items
included.

## 7. Comment

```sh
gh pr review <N> --comment --body-file <file>
```

Name each problem with `file:line`, what is wrong, and what is wanted, so it can be fixed
without a round trip. Thank them for the work in one line, no more. Never quote the
maintainer. Sign it `--AltairSim Claude`. Write it to the `simplified-english` light rules.

Then report to the maintainer and stop. When the contributor pushes again, start over at
step 1.

## 8. Fix it — easy, and step 1 found I can push

1. In the step 3 worktree, on `pr-<N>`, make the fixes. Run the tests and the step 4
   negative control again.
2. **Stop for review**, as in `work-task` step 6: what I changed on top of theirs, and the
   pass lines.
3. **Commit only on approval**, as my own commit on top of theirs (`ship-change` step 1:
   staged by name, `type(scope): summary`, no attribution).
4. **Check `maintainerCanModify` again** — the contributor can turn it off at any time — then
   push to their branch by name:

   ```sh
   gh pr view <N> --json maintainerCanModify
   git push "$(git config branch.pr-<N>.pushremote)" HEAD:<headRefName>
   ```

   The push goes to someone else's repository, so it prompts for the maintainer's yes. If the
   push is refused, or `maintainerCanModify` is now false, post the fixes as a step 7 comment
   instead.
5. Comment on the PR: what I changed, and why, signed `--AltairSim Claude`.

## 9. CI, then the maintainer

Poll `gh pr checks <N>` every 20 seconds until every leg the run started has finished — all
three platforms for a core change, Linux only otherwise (`tools/ci-changed-code.sh`).

- **Red:** find out whose it is. A failure in their change goes into a step 7 comment, or is
  fixed as an easy item. Never re-run a red job hoping it goes green.
- **Green:** report it and **wait for the maintainer's explicit approval to merge.** Then:

  ```sh
  gh pr merge <N> --merge
  ```

  Never `--auto`, never `--squash`: their commits are kept as they are. Do not pass
  `--delete-branch` — their branch lives in their fork.

## 10. After the merge

- **Comment on each linked issue,** signed `--AltairSim Claude`. Start with
  `Fixed by #<N> (merged <sha>).` only when the PR fixes all of the issue. Otherwise start with
  `#<N> is merged (<sha>).`, then say what the PR did for the issue and what is still open.
  **Never close someone else's issue.** The person who opened it does.
- Clean up: `git worktree remove .claude/worktrees/pr-<N>`, then `git branch -D pr-<N>`.
- Sync: `git switch master && git pull --ff-only`.
