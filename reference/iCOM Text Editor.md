# iCOM 8080 Text Editor (Revision A) — FDOS-II character/line editor

Source: [iCOM Text Editor.pdf](#), "TEXT EDITOR", Revision A, January 1977, iCOM / a
division of Pertec Computer Corporation. Provenance: deramp.com iCOM floppy software
archive (`.../altair/software/icom_floppy/FDOS/`). This distillation is written from the
scanned manual's OCR text layer; the scan itself is not committed.

The iCOM 8080 Text Editor is the interactive text-file editor that runs under the iCOM
FDOS-II floppy operating system (the EDOS → FDOS-II → FDOS-III lineage). It reads text
from a floppy disk input file into an in-memory workspace, applies single-letter editing
commands, and writes the result to a floppy disk output file. Alongside it, the Macro
Assembler, Relocating Assembler, and DEBBI BASIC are the other FDOS tools. The iCOM
FD3712/FD3812 floppy hardware is documented separately in
`reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md`.

**This is the Revision A manual.** A separate Revision B manual exists and is distilled in
its own file; where behavior differs, Rev B is authoritative for later software.

## 1. Invocation and operation under FDOS-II

The editor assumes (a) the text to edit is in a file on a floppy in the drive, and (b)
there is room on the output diskette for the output file. From the **FDOS-II command
mode**, invoke it as:

```
T,INPUT-FILENAME,OUTPUT-FILENAME(Cr)
```

(`T` is the FDOS command that loads the Text Editor, with the input and output filenames
as arguments.) The editor prints:

```
ICOM 8080 TEXT EDITOR VER X.X
```

and then the prompt character **`@`**. It is now ready for command input.

The editor operates on input from one of two sources: the **system console device**, or
the **specified floppy disk input file**. The normal edit cycle is:

1. Read text from the input file into the workspace (`A`).
2. Issue edit commands to modify the text.
3. Write modified text to the output file (`P`, or `E` to finish).

## 2. Text buffer (workspace) model

- Input is stored in a memory buffer called the **workspace**.
- The **buffer pointer** marks the point at which operations occur. It **always resides
  between two characters** (never on a character). Inserting a character leaves the pointer
  immediately after the inserted character.
- After an `A` (Append), the buffer pointer sits at the **beginning** of the workspace.
  Deleting or inserting moves the pointer to the edit location.
- Content is classified as **CHARACTERS** and **LINES**. A CHARACTER is a single ASCII
  character; a LINE is the span between two LINE FEED characters. CARRIAGE RETURN and LINE
  FEED are themselves ordinary characters that can be manipulated like any other.

### Command syntax, terminator, and BREAK

- Commands are single letters typed at the `@` prompt; arguments may precede or follow.
- Commands are terminated **and executed** by typing **two ESCAPE / ALT MODE characters**,
  each echoed as a dollar sign (`$$`).
- **Do not** use CARRIAGE RETURN as the terminator — CR is treated as *data* by the editor.
  ESCAPE is the command terminator; LINE FEED is the internal line terminator. The editor
  automatically supplies a LINE FEED whenever a CARRIAGE RETURN is typed, so LF need not be
  entered manually.
- The **BREAK** character terminates execution of a running command and returns to command
  input mode.

### The `n` argument

Where a command takes a repeat/count argument `n`, the permitted range is **−254 to +255**.
Values outside that range are evaluated **modulo 256**. If `n` is not present it defaults to
**+1**. A sign of `+` is assumed when `−` is not given. An `n` of `0` generally means "no
movement / current line" per the individual command.

## 3. Command set

Commands group into: Input, Buffer Pointer Manipulation, Data Manipulation, and Output.
`$$` below denotes the two-ESCAPE terminator (echoed as `$$`); a single `$` inside a
command separates argument fields.

| Cmd | Group | Format | Effect |
|-----|-------|--------|--------|
| **A** | Input | `A$$` | Append: read text from the input file into the workspace. Stops at end-of-file, an end-of-file character (CTL-Z), workspace full, or **50 lines read** (50 lines per `A`). Repeatable until the whole file is read; recommend only 3–4 Appends at a time on long files to avoid exceeding memory. |
| **I** | Input | `Istring$$` | Insert: insert `string` at the buffer pointer; pointer ends to the right of the last inserted character. Argument may be any length that fits the free workspace, of any characters **except** ESCAPE, ALT MODE, or BREAK. If the insert exceeds free space, the editor echoes the **BELL** and the text should be written out with `P` before continuing. |
| **B** | Pointer | `B$$` | Beginning: move the buffer pointer to the start of the workspace. |
| **L** | Pointer | `nL$$` | Line: move the pointer `n` lines backward/forward (sign default `+`). `0L` moves to the **beginning of the current line**. If `n` exceeds the lines available, the pointer stops at the end/beginning of the workspace. (LINE FEED is not used here; the editor supplies LF for CR.) |
| **M** | Pointer | `nM$$` | Move: move the pointer `n` **characters** forward/backward per the sign. If `n` exceeds available characters, stops at the end/beginning. `0M` leaves the pointer in place. |
| **Z** | Pointer | `Z$$` | End of workspace: move the pointer to the end of the workspace (used before appending text to the end). |
| **S** | Data | `Stext$$` | Search: from the current pointer, search forward for the string `text` (limited to **16 characters**). On a match the pointer is placed immediately after the matched string. If not found before end of workspace, prints **`CANNOT FIND`**. |
| **C** | Data | `Csearch$text$$` | Change: search out `search` (up to 16 chars) and replace it with `text`. If the search string is >16 chars only the first 16 are used; the entire replacement `text` (any length) replaces the matched string. Deletes the search argument, inserts the replacement ahead of the pointer. |
| **D** | Data | `nD$$` | Delete: delete `n` **characters** from the workspace; sign selects direction from the pointer. `0D` causes no movement/deletion. |
| **K** | Data | `nK$$` | Kill: like `D` but for **lines** — delete `n` lines. `0K` deletes from the pointer back to the previous line feed. If the pointer is mid-line and `n=2` (`2K`), the remainder of the current line and the following line are deleted. If `n` exceeds available lines, deletes to the end/beginning and leaves the pointer there. `+` assumed without `−`. |
| **P** | Output | `nP$$` | Punch: write `n` lines from the workspace to the **output file**. (`255P$$` is the idiom to flush the workspace to the output file, clearing it for more Appends.) |
| **T** | Output | `nT$$` | Type: output `n` lines to the **system console**, starting where the pointer resides. Positive `n` types following lines; `−n` types preceding lines; `0T` types from the previous LINE FEED to the current pointer. If `n` exceeds available lines only the existing lines are typed. |
| **E** | Output | `E$$` | End: write the entire workspace to the output file, copy any remaining input-file text to the output file, close the output file, then **load and execute FDOS-II** (exit the editor). |

## 4. Command strings, iteration, and tabs

**Command strings** — any number of commands may be chained and are executed left-to-right
as individual commands. The three commands that take a text argument — **C, S, and I** —
must be separated from following commands by the ESCAPE (`$`) character so the editor can
tell the argument text from the next command.

- Example: `@B2L4K3DISYSTEMS$$` = go to beginning, down 2 lines, delete 4 lines, delete 3
  characters, insert `SYSTEMS`.
- Wrong: `ISYSTEMS3L4D$$` places the literal text `SYSTEMS3L4D` in the workspace.
- Correct: `ISYSTEMS$3L4D$$` inserts `SYSTEMS`, then moves 3 lines and deletes 4 characters.

**Command iterations** — a command string can be auto-repeated `n` times by bracketing it
with `<` and `>`:

```
n<command string>$$
```

Example: `@4<ISYSTEMS.(Cr)>B4T$$` inserts `SYSTEMS.`+CRLF four times, then goes to the
beginning and types the first four lines. Iterations may be **nested up to eight deep**;
deeper nesting prints **`ITERATION STACK FAULT`** and the command must be re-entered.

**Tabs** — the horizontal tab character (**CTRL-I**) may be used wherever a space could
appear. Tab stops are located every **eight** positions, producing readable listings.

## 5. Error and status messages

| Message / signal | Meaning |
|------------------|---------|
| `CANNOT FIND` | `S` (or `C`) search string not located between the pointer and end of workspace (absent, or the pointer is already past its occurrence — use `B` and retry). |
| BELL (echoed) | An `I` insert exceeded free workspace; terminate the command and write text out with `P`. |
| `ITERATION STACK FAULT` | Command iterations nested more than eight deep; re-enter the command. |

## 6. Not distilled here

The scan also contains the title/notice page, table of contents, and the two appendices
(Appendix A "Text Editor Commands" narrative descriptions, and Appendix B "Command Summary"
index) — all of which restate the command set captured above. No schematics, listings, or
parts lists are present in this manual.
