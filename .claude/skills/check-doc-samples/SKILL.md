---
name: check-doc-samples
description: Check the sample output in a doc against the binary — replay each `altairsim>` monitor transcript (a debugger session, REGS, BREAK, DUMP, DISASM, WHO, SHOW output) with --exec and diff it, and replay guest output over --mcp. Use when asked to check, verify or refresh the examples or sample output in a doc, when a simplified-english review checks facts, and before you report that a doc's sample output is wrong.
---

# Check a doc's sample output

A doc's sample output must be what the binary prints. This skill replays each sample and
reports the differences. **It reports. It does not edit.**

## Which method checks which sample

| Sample | Replay with | Why |
|---|---|---|
| `altairsim>` monitor transcript | `check-block.sh` (below). It uses `--exec`. | `--exec` runs **after** the machine's `startup`, as the reader has it. Its output is the real monitor text. |
| Guest output that needs typing (`A>`, BASIC `OK`) | `--mcp`, with the `altairsim` skill (§5) | A pipe cannot type at a guest. |
| `$ altairsim …` shell line in `docs/manual/` | `tools/verify-package.sh`, at release | Do not repeat that check here. |

**Do not compare an MCP register line with a monitor transcript.** MCP does not run
`startup`, so the machine starts in a different state. On `altmon`, MCP gives `HL=0000 I0`
at `FBA5`, and the monitor gives `HL=F81F I1`. The doc was correct. A review that used MCP
reported a false error.

**Do not pipe commands into the monitor.** Stdin is the guest's keyboard. A `startup` that
runs the machine (ALTMON's `RUN F800`) reads the commands as keys, and the run hangs.
`check-block.sh` sets stdin to `/dev/null` for this reason.

## 1. List the samples

```sh
.claude/skills/check-doc-samples/check-block.sh --list docs/debugger/debugging.md
```

This prints `file:line` for each fenced block that has an `altairsim>` prompt. The line is the
opening fence. Read each block and the text above it. A block that is only a list of commands,
with no output, has nothing to check.

## 2. Find the machine and the state

- **The machine.** Use the machine that the text names: "on the `altmon` machine", "the
  default machine's DBL PROM". A Z80 sample needs a Z80 machine, such as `z80`. If the
  text names no machine, use `default`.
- **Earlier commands.** A block often continues from an earlier block in the same section. For
  example, a bare `DISASM` continues from the last `DISASM`, and `STEP 20` follows a `BREAK`
  and a `RUN`. Give the earlier commands as setup commands (§3).
- **Prepared state.** A `DISASM` or `DUMP` listing shows its own bytes. Put the bytes in first
  with a `DEPOSIT` setup command. A file that a block names by a relative path needs
  `DIR=<folder>`. For example, `SYMBOLS LOAD ALTMON.PRN` needs `DIR=roms/ALTMON`.
- **A command that writes a file** (`SNAPSHOT`, `TRACE ON <file>`, `SET CONSOLE log=`) needs
  nothing extra. The script replays each block in a new temporary folder, so the file lands
  there. If a replay changes the tree, the script stops with exit 3 and shows `git status`.
- **Unknown state.** A `REGS` line from a moment that the text does not describe cannot be
  replayed. Report the block as **not checkable**, and give the reason. Do not guess a state
  and do not change the sample to make it match.

## 3. Replay and compare

```sh
S=.claude/skills/check-doc-samples/check-block.sh
$S docs/debugger/debugging.md 147 default
$S docs/debugger/debugging.md 857 altmon "BREAK FBA5" "RUN F800" DISASM
$S docs/debugger/debugging.md 426 z80 "DEPOSIT 100 ED B0 CB 27 18 FE 10 FC DD 7E 05"
DIR=roms/ALTMON $S docs/debugger/debugging.md 437 altmon
```

The arguments are the doc, the line of the opening fence, the machine, and then the setup
commands. The script runs the setup commands and then the block's commands, each with its own
`-x`. It compares the output from the block's first command, so the output of `startup` and of
the setup commands is not compared. It prints `OK file:line`, or a diff: `<` is the doc and `>`
is the binary. It stops a run after 20 seconds, because macOS has no `timeout`. A `RUN` that
never stops (no breakpoint, and a guest that waits for a key) ends there.

The script ignores these differences, because they are not errors:

- the doc's notes after a command: `REGS            (on a Z80 machine)`
- blank lines, and spaces at the end of a line
- the `[console -- ^E returns to the monitor]` line that each `RUN` prints
- output after a `...` line at the end of a block

**Every other difference is a finding.** Read the diff before you report it. Check that the
machine and the setup were correct. A wrong setup gives a diff that is not a finding.

## 4. Samples the script cannot check

A version string: the doc must say `AltairSim X.Y.Z`, and the binary prints its own version.
A `...` line in the middle of a block. Output from a guest that you must type at (§5). Report
these blocks by `file:line`, with the method that you used or the reason that you could not
check them.

## 5. Guest samples

1. Load the `altairsim` skill, and start the machine with `--mcp`.
2. Boot the machine with `run {from: <addr>, until: …}`.
3. Give one `run {input: "CMD\r", until: …}` for each command in the sample.

The traps:

- MCP does not run `startup`. You boot the machine yourself.
- The `monitor` tool's `RUN` only sets the PC. Use the `run` tool to run the machine.
- Compare only the guest's text. Register lines and cycle counts come from a different
  starting state (see the table above).

## 6. Report

1. List each mismatch as `file:line`, with the doc text and the binary text.
2. List the blocks that you could not check, and the reason for each.
3. Say which method checked each block that passed (`check-block.sh` or `--mcp`).
4. Stop.

A fix is a separate change, made through `work-task`. To fix a transcript, paste the replay
output into the doc. Do not change numbers in a transcript by hand.
