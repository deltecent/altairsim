---
name: simplified-english
description: The writing standard for altairsim — rules based on ASD-STE100 Simplified Technical English, plus a word list and the project's own terms. Use when writing, rewriting or reviewing text a user reads (docs/manual, docs/monitor, docs/debugger, QUICK-START, HELP text, the reference emitter), when asked to check a file against STE or plain English, and when writing an issue, PR or Discussion comment.
---

# Simplified English

This is the writing standard for text that people read. It is **based on** ASD-STE100
Simplified Technical English. It is not certified STE. The STE dictionary is copyrighted and
is not reproduced here. The rules below are in our own words, and the word list is our own.

The goal: a reader who is not a native English speaker, or who reads through a translation
tool, gets the same meaning as everyone else, the first time.

## 1. Two strengths

| Strength | Applies to |
|---|---|
| **Strict** | Procedures, numbered steps, tables, warnings and cautions, HELP text. Every rule in §2. |
| **Light** | Explanatory prose in the docs, and every GitHub comment (issue, PR, Discussion). |

**Light** means: no idioms (W8), no made-up situation (W15), short sentences (W1), one name
for each thing (§4), and steps for the reader as a numbered list of commands (W3). Apply the other rules where they
make the text clearer. Do not make a friendly comment stiff to pass a word count.

## 2. The writing rules

Each rule has an ID. A review report cites it.

| ID | Rule |
|---|---|
| **W1** | Keep a sentence to 20 words or fewer in a procedure, and 25 or fewer in a description. A code span or a command counts as one word. |
| **W2** | Give one instruction in each sentence. Two actions go in one sentence only when the reader does them at the same time. |
| **W3** | Write a procedure as numbered steps. Start each step with a command (the imperative): "Type", "Set", "Mount". |
| **W4** | Use the active voice. Use the passive only in a description, and only when the actor is unknown or does not matter. |
| **W5** | Keep one topic in each paragraph, and six sentences or fewer. |
| **W6** | Write a warning or a caution as a command first, then the reason. Not the reason first. |
| **W7** | Do not put more than three nouns in a row. "front-panel address lamps" is the limit. Break a longer one up with "of" or "for". |
| **W8** | Do not use idioms, metaphors, jokes or figurative language. Say the literal fact. |
| **W9** | Do not join a second statement to a sentence with a dash, a semicolon or a colon. Write two sentences. A colon before a list or an example is fine. |
| **W10** | Say what something is or does. Do not describe it by what it is not, when a positive form exists. |
| **W11** | Make every "it", "this" and "that" point at something in the same or the last sentence. If not, repeat the noun. A short name ("the manual", "the board", "the controller") follows the same rule. If two things near it could match, such as "the MITS manuals" near "the manual", use the full name. |
| **W12** | Use each word with one meaning and as one part of speech. Do not use "close" for both "shut" and "near". |
| **W13** | Use the words in §3 and the terms in §4. |
| **W14** | Do not start a sentence with "So", "Then", "And", "But" or "Or". Each sentence must make sense without the one before it. For a result, name the cause ("Because…") or give an example ("For example…"). For a time order in a procedure, use numbered steps (W3). |
| **W15** | Say what the feature does. Do not start with a made-up situation or a made-up reader ("Sometimes you want…", "Imagine…", "today… and tomorrow…"). State the fact, then give an example that has a real file and a real command. |
| **W16** | Make a count agree with the feature. Do not write "one setting" or "a setting or two" for an option that the reader can give any number of times. |

## 3. Word list

Use the word on the right. The list grows: when a review finds a new word that a reader could
misread or need to look up, add a row.

| Not this | This |
|---|---|
| a great deal | much, many |
| allow (a person) | let |
| approximately | about |
| arrangement | (rewrite: name the setup) |
| carry on | continue |
| collide with | be used by … also |
| commence | start |
| dwell on | (drop it, or: look at closely) |
| enable (a person) | let |
| ensure | make sure |
| entitled to | can |
| entirely | (drop it) |
| exactly | (drop it, unless a count or a value is exact) |
| facilitate | help |
| genuinely | (drop it) |
| halt (the machine) | stop. "Halt" reads as the `HLT` instruction. |
| hit (a key, a breakpoint) | press; reach |
| in order to | to |
| in the event that | if |
| indicate | show |
| intercept | get first |
| is able to | can |
| imagine, suppose, say you | (drop it; state the fact, W15) |
| it is necessary to | (a command: "Do X.") |
| negotiable | (drop it; say what can and cannot change) |
| ought to | must, should |
| perform | do |
| prior to | before |
| require | need |
| simply, just | (drop it) |
| sometimes you want | (drop it; say what the feature does, W15) |
| subsequently | then, after |
| sufficient | enough |
| terminate | stop, end |
| utilize | use |
| use (an address, a port) | decode, respond to. A board decodes an address. It does not use one. |
| via | through, with |
| whichever | (rewrite the sentence) |
| worth | (drop it; say why the reader should read on) |

## 4. Project terms

One name for each thing, in every document and every comment.

| Use | Not | Note |
|---|---|---|
| **board** | card | `DESIGN.md` §0.3. "Card" only for the physical 1970s object. |
| **add** a board | fit, install | Matches `BOARDS ADD`. The action you do. |
| a board **plugs into** the bus | connects to, attaches to, sits on | How a board is on the bus, as on real hardware. "Connect" is for a line or an endpoint: a serial board connects to a TCP socket. |
| **remove** a board | pull, unplug | Matches `BOARDS REMOVE`. |
| **`Ctrl-E`** | `^E` | Every control key: `Ctrl-C`, `Ctrl-]`. A reader may not know caret notation. Keep `^E` only inside a quote of what the program prints. |
| **machine** | system, computer | The whole simulated computer. |
| **processor** | CPU (in prose) | The chip. `cpu0` and the CPU board keep their names. |
| **guest** | program (alone) | The software that runs on the machine. |
| **monitor** | prompt, shell | The `altairsim>` prompt. |
| **machine file** | config, TOML file | The `.toml` that describes a machine. |
| **console** | terminal, the host's terminal | The window you started altairsim in, where the monitor runs. A unit connected to it "holds the console". |
| **terminal window** | terminal (alone) | The window that `connect = "terminal"` opens. "Terminal" alone is never the console. |
| **altairsim's windows** | the video window (for `[display]`) | Every window altairsim opens: each video board's window and each terminal window. `SET DISPLAY` sets them all. |
| ***User Manual*** | the manual, this manual | Use the full name in the Monitor and Debugger documents, which are not the *User Manual*. Inside the *User Manual*, "this manual" is fine. |

When a review finds a concept with two names, choose one, add a row, and report it.

## 5. Review a file

A review reports. It does not edit.

**Run the review as one subagent.** A review needs only the file and this skill, not the
session. Start one `Agent` with effort `medium`, and do not give it a `model`. Give it the file
path and tell it to follow §5 of `.claude/skills/simplified-english/SKILL.md` and return the report.
The subagent starts with a small context, so it costs less than a review in a long session.
Do not do this for §6. A writer must have the rules in its own context.

1. Read the whole file.
2. List the named things in the file: documents, boards, commands and settings. Check that
   each one has one name everywhere, and that each short name points at one thing (W11, §4).
3. For each section, decide: strict or light (§1).
4. Report the findings **grouped by rule ID**. Give `file:line` for each one. When a rule
   fails in many places, give the worst five and the count.
5. Show a before and an after for the three worst passages.
6. Report factual errors that you find on the way in a separate list. An unrelated error
   gets a GitHub issue, as `work-task` says. To check a sample's output against the binary,
   use the `check-doc-samples` skill. Do not make up your own replay.
7. Stop.

A rewrite is a separate change, made through `work-task`. Do not leave review notes in the
tree.

## 6. Write or rewrite

- Write to the rules as you go. Do not write first and fix later.
- After a rewrite, check it: count the words in each sentence that looks long (W1), and
  search for dashes (W9) and for the left column of §3. Also search for the short forms of
  the §4 terms and of the document names ("the manual", "this manual", "this document"), and
  check that each one points at one thing (W11). Search for sentences that start with "So",
  "Then", "And", "But" or "Or" (W14). Read the first sentence of each new section: it must say
  what the thing does (W15). Check each count word, such as "one" and "two", against the
  feature (W16).
- Keep the meaning. If a rule forces a choice between clear and correct, choose correct and
  report it.

## 7. What this skill never changes

- `docs/manual/` may not name anything outside the package. `docs-manual.cmake` checks it.
- `docs/manual/ref/` is generated. Fix the wording in the emitter, then run
  `cmake --build build --target docs-reference`.
- A GitHub comment is signed `--AltairSim Claude`, never quotes the maintainer, and uses
  `Fixes #N` or `Refs #N` as `ship-change` says.
- Code comments are out of scope.
