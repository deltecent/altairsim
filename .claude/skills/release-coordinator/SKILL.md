---
name: release-coordinator
effort: high
description: Cut an altairsim release from the coordinator — bump the version, curate the changelog, tag the CI PDF commit, open the draft GitHub release, drive the build machines over ssh, collect the four archives, checksum, upload and publish. Use when asked to cut, tag, ship or publish a release, or to run DISTRIBUTION.md §5. Runs only on the machine holding the GitHub credentials.
---

# Cut a release, from the coordinator

**The coordinator is the only machine with credentials.** It tags, pushes, runs every `gh`
command, and collects the archives in its repo's `dist/`. The build machines hold no GitHub
credentials: they clone the public repo over anonymous https, build, and `scp` one archive
here. The coordinator is also a build machine for its own target.

**Read `distribution.conf` first** (repo root, gitignored; `distribution.conf.example` names
every key). It says which target this box builds (`COORDINATOR_TARGET`), how to reach each
worker (`<TARGET>_SSH`, `_REPO`, `_PARALLEL`, `_PATH`), their serial ports, and which targets
share one physical machine (`SHARED_HOST`, empty when none do). If it is missing, STOP and ask for it — do not
reconstruct addresses from memory or old docs.

The four targets: `macos-arm64`, `macos-x86_64`, `windows-x86_64`, `linux-x86_64`.

Steps 1–4 can be done ahead of build day. **Ask before every step that pushes, tags or talks
to GitHub** — those are outward-facing and hard to undo.

## 1. Bump the version and write the changelog

- `project(altairsim VERSION X.Y.Z …)` in `CMakeLists.txt` is the only place the number
  lives; everything else derives from it or from `git describe`.
- In the same commit, add the `## X.Y.Z` section to `docs/changelog/changelog.md`. **Curate,
  do not rename.** A released section is a short themed narrative in the voice of the sections
  already there (a handful of entries). `## Unreleased` is a longer working scratch that also
  carries things already shipped. Build the section from `git log --merges <prevtag>..HEAD`,
  group it into themes, and drop anything already released: `git merge-base --is-ancestor
  <sha> <prevtag>` true means it shipped. Do not trust `git log --grep` for this. Then
  **remove the `## Unreleased` header and everything under it.** A release leaves no
  `## Unreleased` header, not an empty one either: the next change that needs an entry adds
  the header again. If the header is not there (nothing package-facing changed since the last
  release), there is nothing to remove.
- It goes through the `work-task` and `ship-change` skills, as every change here: review, then
  commit approval, then PR approval, then merge on green CI.

## 1a. Sweep the docs

Per-change work checks only the docs a diff touches, so a stale sample can sit on `master`
until now. Before the version bump:

- Replay every doc that has `altairsim>` transcripts with the `check-doc-samples` skill.
- Load every TOML example in the manual, as the design-docs-drift rule says.
- **Report** what is stale. Do not edit. Each fix goes through `work-task` and `ship-change`
  and merges before step 1.

## 2. Wait for CI's PDFs, and tag THAT commit

`docs.yml` rebuilds the documents on master and commits them as *"Rebuild the PDFs for
`<sha>`"*. **Tag that commit, not the merge** — otherwise the tagged tree carries a stale
manual, changelog, monitor or debugger PDF. Never build the PDFs locally: a different pandoc
is a different document.

## 2a. Preflight, before the tag

Run it with this checkout at the commit that you will tag:

```sh
tools/release-drive.sh preflight
```

- **The coordinator first.** The checkout must have no change to a tracked file. A clone of
  HEAD in a temporary folder is configured, built and packaged, and `verify-package.sh` runs
  the manual's commands on the archive. This checkout and its `build/` are not touched.
- **Then each worker.** ssh must answer. The checkout on the box is reset
  (`git reset --hard`, `git clean -fdx`), and the command prints each path that it removed.
  The worker then copies a small file to `COORDINATOR_DIST` with `scp`, as its step 8 does. In
  1.3.4 a delivery key failed only after a full build (issue #816).
- **CHECK the last line, `preflight: OK`.** **STOP on `preflight: FAIL`.** The
  `preflight: FAIL -- <item>` lines above it name each problem. Tag only after
  `preflight: OK`.

## 3. Tag and push

```sh
git tag vX.Y.Z <pdf-rebuild-sha> && git push origin vX.Y.Z
```

The tag starts no CI run. The CPU exercisers (8080EXM, 8085EXM, ZEXDOC, ZEXALL) run on the four
build machines in step 5, through the binaries that ship. `cpu-exerciser-release.yml` is manual
only (`gh workflow run`), for a check on the CI runners outside a release.

## 4. Open the draft

```sh
gh release create vX.Y.Z --draft --notes-file <notes>
```

## 5. Build on all four machines

Each machine runs `tools/release-worker.sh` (the **`release-worker`** skill gives the reason
for each of its checks). **Start it with `tools/release-drive.sh`, one command for each
target. Do not write your own ssh command or your own worker script** — that is where the
1.3.0 mistakes were (issue #643).

```sh
tools/release-drive.sh start macos-arm64    X.Y.Z
tools/release-drive.sh start macos-x86_64   X.Y.Z
tools/release-drive.sh start linux-x86_64   X.Y.Z
tools/release-drive.sh start windows-x86_64 X.Y.Z
```

The driver reads `distribution.conf`, copies the worker script to the box (outside its
checkout), gives it the site values and the version, and starts it detached. For
`COORDINATOR_TARGET` it runs the script here: no ssh and no delivery, the archive lands in
`dist/`. **This checkout must have no change to a tracked file**, because the worker checks
out the tag in it. On a worker box, the driver first resets the checkout, as `preflight` does.

- **CHECK `release-drive: OK -- <target> started`.** The driver prints it only when the
  checkout on the box is at `vX.Y.Z`, the worker is a running process, and its log names the
  version. **STOP on `release-drive: FAIL`.** An empty log is not a build in progress: in 1.3.0
  a start that printed `started` ran nothing, and nobody saw it for about 10 minutes.
- **The two Macs sign and notarize their own packages** with the `<TARGET>_SIGN_…` values in
  `distribution.conf` (DISTRIBUTION.md §4.3). `preflight` checks each Mac's signing keychain
  and makes a test signature that must finish in 60 seconds. A keychain that asks "allow
  codesign?" fails there, not in packaging. A Mac with no signing values stops before step 1. Do not pass `--unsigned` for a release:
  it is for a dry run only, and its package needs the `xattr` step that the manual no longer
  gives.
- **Any order, and at the same time**, but the driver refuses a target in `SHARED_HOST` while
  another target of that list runs. They are one physical machine.
- **Wait for the results with one command, run in the background.** Do not poll by hand.

  ```sh
  tools/release-drive.sh wait macos-arm64 macos-x86_64 linux-x86_64 windows-x86_64
  ```

  Give it each target that you started. It checks them once a minute until every worker has
  ended. A build with the exercisers takes 10 to 30 minutes for each box. Then it prints each
  report block under `##### <target>`, and one last line. **CHECK
  `release-drive: wait OK -- all N workers OK`.** **STOP on `release-drive: wait FAIL`.** It
  names the targets that failed.
- A target in `SHARED_HOST` starts only after its partner has ended. Start it then, and give
  it its own `wait`.
- `tools/release-drive.sh status <target>` reads one worker's log once. It exits 2 while the
  worker runs, 0 on `release-worker: OK`, and 1 on `release-worker: FAIL at step N`.
- The Windows log is `dist/worker-windows-x86_64.log` here, and the ssh that holds the worker
  runs on this machine: do not end it. The macOS and Linux workers run on their boxes and
  survive a dropped link.

The `wait` output holds each worker's report block. Any FAIL is a STOP for the release: do not upload a set
with a hole in it. `serial-hw` and `tnfs-hw` results are warnings — put them in front of the
person, do not hold the release for them. The `exercisers` line of each report is the CPU
gate for that platform.

### While the builds run

- **Merge only the fixes that this release needs**, from the tag to the publish. A re-tag
  ships every merge since the first tag.
- **The coordinator's own build uses this checkout.** Do not switch a branch and do not edit a
  file here until its worker has ended. Merge a PR on GitHub with
  `gh pr merge <N> --merge --delete-branch`, not in this checkout.

### Re-spinning for a doc-only fix

While the release is **still a draft**, a doc fix after tagging is shipped by force-moving the
tag so the archives still stamp a clean `X.Y.Z`: `git tag -f vX.Y.Z <sha>; git push --force
origin vX.Y.Z`, then rebuild on every box. Never move a published tag.

## 6. Verify, checksum, upload, publish

1. **Verify the archives.** The workers already did: step 7 of each worker runs
   `tools/verify-package.sh --release` on its archive, on its own platform. In the `wait`
   output, read the `verify` and `signed` line of each `#####` block.
   - **CHECK** that each `verify` line is `verify-package: PASS -- …`, and that each macOS
     `signed` line ends `notarized (Accepted)`. **STOP** on any other value.
   - **Do not write a boot script or run the checks again.** The `release-verify` skill says
     what the script checks.
   - Give the window checklist of `release-verify` to a person.
2. **Checksum:** `tools/build-checksums.sh --version X.Y.Z`. Give `--version`: `dist/` keeps the
   archives of earlier releases, and with no version the script stops and asks for one. It
   refuses unless all four archives of X.Y.Z are in `dist/`, writes `dist/SHA256SUMS`, and
   checks it before returning. **CHECK `build-checksums: OK -- all four archives verify`.**
3. **Upload all five:**
   ```sh
   gh release upload vX.Y.Z dist/altairsim-X.Y.Z-*.tar.gz dist/altairsim-X.Y.Z-*.zip dist/SHA256SUMS
   ```
4. **Publish:** `gh release edit vX.Y.Z --draft=false`.

That is the end of the release here. The website's download copy is refreshed **on the web
server itself**, from the published GitHub release — not from this machine.

## The two traps that shaped this

- **Build at the tag, not the merge.** v0.2.0's first binaries reported
  `AltairSim 0.2.0 (v0.1.0-86-g59dbba8)`: CI built the merge, an ancestor of the tag, and
  `git describe` found the previous tag. Every worker checks `--version` before packaging.
- **The manual in the package is the one CI built.** A local pandoc makes a different PDF.
  `--pdf docs/altairsim-manual.pdf` hands in the tagged tree's copy.
