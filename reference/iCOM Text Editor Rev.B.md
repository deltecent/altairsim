# iCOM 8080 Text Editor (Revision B) — FDOS line/character editor

Source: [iCOM Text Editor Rev.B.pdf](#), iCOM (a division of Pertec Computer
Corporation, Canoga Park, CA), "8080 TEXT EDITOR", **Revision B, June 1977**. Scanned
image PDF (~20 pages of content, no text layer), from the deramp.com iCOM floppy
software archive (`.../altair/software/icom_floppy/FDOS/`).

The iCOM Text Editor is the source-text editor of the iCOM floppy software ecosystem
(EDOS → FDOS-II → FDOS-III operating systems; the Text Editor, Macro Assembler,
Relocating Assembler, and DEBBI BASIC are the tools that run on it). It is a
character/line buffer editor in the TECO family: single-letter commands typed at an
`@` prompt, terminated by a doubled ESCAPE (echoed `$$`), operating on a memory
workspace read from and written to floppy-disk files. It is invoked from the FDOS-II
command mode with input and output filenames. The iCOM FD3712/FD3812 floppy hardware
is documented separately in `reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md`.

## Note on Revision B vs. Revision A

This is Revision B (June 1977). The style guide references a Rev A distillation
(`reference/iCOM Text Editor.md`), but **no such file currently exists in the tree**,
and this Rev B PDF contains no changelog, revision-history page, or "changed in this
revision" markers. **Therefore what differs from Rev A cannot be determined from this
document alone** — it is not stated here, and no Rev A source was available to diff
against. This should not be read as "nothing changed"; the difference is simply
unknown from the material at hand.

## 1. Invocation and startup

From the **FDOS-II command mode**, type:

```
EDIT,INPUT-FILENAME,OUTPUT-FILENAME(Cr)
```

Two assumptions are made at load time:

- a. The text to be edited resides in a file on a floppy in the drive.
- b. There is sufficient room on the output diskette for the output file.

On load the editor prints its banner and prompt:

```
ICOM 8080 TEXT EDITOR VER X.X
@
```

The `@` prompt means the editor is ready for command input.

## 2. Model: workspace, buffer pointer, lines

- **Workspace** — a memory buffer holding the text. Input comes from one of two
  sources: the system console device, or the specified floppy input file. The edit
  cycle is: read text from the input file into the buffer → issue edit commands to
  modify it → output the modified text to the output file.
- **Buffer pointer** — locates the point in the workspace where the next operation
  acts. It **always resides between two characters** (never on one). After an Append
  it sits at the beginning of the workspace; after inserting/deleting it sits at the
  edit location (to the right of the last character inserted).
- **Lines and characters** — the workspace is classified into CHARACTERS and LINES.
  A CHARACTER is a single ASCII byte. A LINE is the span between two LINE FEED
  characters. CARRIAGE RETURN (Cr) and LINE FEED (Lf) are treated as ordinary,
  manipulable characters; removing a Lf merges two lines into one.

## 3. Command syntax and termination

- Commands are **single letters** typed in response to the `@` prompt. Arguments may
  precede or follow the letter depending on the command.
- A command is **terminated and executed by two ESCAPE (ALT MODE) characters**, each
  echoed as a dollar sign — shown throughout as `$$`. **CARRIAGE RETURN is data, not
  a terminator** — do not use it in place of ESCAPE.
  - ESCAPE = command terminator.
  - LINE FEED = internal line terminator. The editor **automatically supplies a LINE
    FEED whenever a CARRIAGE RETURN is typed**, so the Lf need not be entered
    manually.
- **BREAK** terminates execution of a running command and returns the editor to input
  (command) mode; the editor then reprints `@`.

### 3.1 The `n` argument

Many commands take a leading numeric count `n`:

- Permitted range **−254 to +255**; a value outside the range is evaluated **modulo
  256**.
- If `n` is absent it defaults to **positive 1**.
- Sign selects direction (forward `+` / backward `−`) for pointer-motion and
  delete/type commands. `0` has command-specific meaning (often "to beginning of
  current line" or "no motion").

## 4. Command set

Commands fall into four groups: **Input**, **Buffer-Pointer Manipulation**, **Data
Manipulation**, and **Output**. `$$` denotes the doubled ESCAPE terminator.

| Cmd | Format | Group | Effect |
|-----|--------|-------|--------|
| **A** | `A$$` | Input | **Append.** Read text from the input file, appending to the workspace until one of: end of file, an end-of-file character (CTL-Z) is read, workspace full, or 50 lines read. The editor prints `@` at the left margin when loading completes; it supplies its own end-of-file at true EOF and puts only actual text characters into the workspace. Repeatable, but for long files issue only 3–4 Appends at a time to avoid exceeding memory; use `255P$$` to flush the workspace to the output file and clear it for further Appends. |
| **I** | `Itext$$` | Input | **Insert.** Insert the literal `text` argument into the workspace at the buffer pointer; the pointer ends to the right of the last inserted character. Argument may be any length up to remaining workspace and any characters except ESCAPE/ALT MODE/BREAK. Include Cr/Lf explicitly in the text to add line breaks. If the insertion exceeds available space the editor echoes the BELL character; terminate the command and store the text with the Punch command. |
| **B** | `B$$` | Pointer | **Beginning of workspace.** Move the buffer pointer to the start of the workspace. |
| **L** | `nL$$` | Pointer | **Line.** Move the pointer `n` lines backward/forward (sign selects direction, default `+`). `n=0` moves to the beginning of the current line. If `n` exceeds the lines available, the pointer stops at the workspace beginning/end. (LINE FEED is not typed; the editor supplies it with Cr.) |
| **M** | `nM$$` | Pointer | **Move.** Move the pointer forward/backward `n` characters (sign selects direction). If `n` exceeds characters available, stops at beginning/end. `n=0` leaves the pointer in place. |
| **Z** | `Z$$` | Pointer | **End of workspace.** Move the pointer to the end of the workspace; used prior to appending text to the end. |
| **S** | `Stext$$` | Data | **Search.** Search forward from the current pointer for the character string `text`; on a match the pointer is left immediately after the matched string. If the end of the workspace is reached first, prints **`CANNOT FIND`**. Search argument limited to **16 characters**. |
| **C** | `Carg$text$$` | Data | **Change.** Search out `arg` (up to 16 chars; only the first 16 are recognized if longer) and replace it with the second string. After the search the pointer sits after the found string; the argument is deleted and the replacement inserted ahead of the pointer. The two strings are separated by a single ESCAPE (`$`). |
| **D** | `nD$$` | Data | **Delete.** Delete `n` characters from the workspace; sign selects direction relative to the pointer. `n=0` deletes nothing / no pointer movement. |
| **K** | `nK$$` | Data | **Kill.** Line-level delete (the line analogue of D). `n=0` deletes from the pointer back to the first preceding LINE FEED. If `n` exceeds the lines available, deletes to the workspace beginning/end and leaves the pointer there. `+` is assumed when no sign is given. With the pointer mid-line, e.g. `2K` deletes the remainder of the current line plus the following line. |
| **P** | `nP$$` | Output | **Punch.** Write `n` lines from the workspace to the **output file**. |
| **T** | `nT$$` | Output | **Type.** Output `n` lines to the **system console**, beginning at the pointer. Positive `n` = following lines; negative `n` = preceding lines; `n=0` = from the previous LINE FEED to the pointer. If `n` exceeds existing lines, only the existing lines are typed. |
| **E** | `E$$` | Output | **End.** Write the entire workspace to the output file, copy any remaining input-file text to the output file, close the output file, then **load and execute FDOS-II**. This ends the edit session. |

## 5. Command strings (chaining)

Any number of commands may be chained and are executed left-to-right as individual
commands. **Three commands — C, S, and I — take a text argument and must be separated
from the following command by an ESCAPE (`$`)** so the editor can tell argument text
from the next command letter.

- Valid: `@B2L4K3DISYSTEMS$$` → Beginning; move down 2 lines; delete 4 lines; delete 3
  characters; insert `SYSTEMS`.
- Wrong: `ISYSTEMS3L4D$$` → inserts the literal string `SYSTEMS3L4D`.
- Right: `ISYSTEMS$3L4D$$` → inserts `SYSTEMS`, then moves 3 lines and deletes 4
  characters. (The single `$` closes the Insert argument; the final `$$` terminates
  the string.)

## 6. Command iterations (repeat)

Bracket a command string with `<` and `>` and prefix a count to auto-repeat it:

```
n<command string>$$
```

Example: `4<ISYSTEMS.(Cr)(Lf)>B4T$$` inserts `SYSTEMS.` + CRLF four times, then moves
to the workspace beginning and types the first four lines. Iterations may be
**nested up to eight deep**; deeper nesting prints **`ITERATION STACK FAULT`** and the
offending command must be re-entered with the error condition removed.

## 7. Tabs

The horizontal-tab character (**CTRL-I**) may be used anywhere a space could appear.
**Tab stops are every eight positions** (columns 0, 8, 16, …). Used to produce
readable assembly-source listings, e.g.
`ILABEL(tab)MOV(tab)A,B(tab);(sp)COMMENTS$$` lays the fields out on 8-column stops.

## 8. Error / status messages

| Message | Meaning |
|---------|---------|
| `CANNOT FIND` | Search (S) or Change (C) string not found: it does not exist in the workspace, or the pointer resides past its occurrence (search runs forward only). *(The worked example text shows it as `CANNOT FINE` — a scan/typo in the manual; the true message is `CANNOT FIND`, per §2-11.)* |
| `ITERATION STACK FAULT` | Command iterations nested more than eight deep; the command must be re-entered. |
| BELL (audible) | Emitted on Insert (I) when the inserted text exceeds available workspace; terminate the command and Punch the workspace to the output file. |

## 9. Worked-example notes (from the manual)

The manual illustrates every command against the sample phrase
`ICOM MANUFACTURES MICROPERIPHERALS FOR ALL MICROPROCESSOR SYSTEMS`. Selected
patterns:

- **Search then insert:** `B$$` (rewind) → `Stext$$` (position) → `Itext$$`.
- **Change + reprint:** `CMANUFACTURES$MAKES$$` then `0L3T$$` to reposition to line
  start and type 3 lines to verify.
- **Kill a whole line:** move to it with `nL`, then `1K$$`; verify with `B$$` `3T$$`.

## Not distilled here

The scan also contains: title/notice pages, table of contents, Appendix A (a prose
restatement of the command groups), and Appendix B (a one-line-per-command summary
table with page references) — both appendices duplicate the command set captured in
§4 above and add no new behavior. Trailing pages of the ~25-page PDF are blank.
