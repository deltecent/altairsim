# The paper CLI — annotated session transcripts

**Status:** Step 0. **Reviewed and signed off before implementation starts.** These become the acceptance script.

> **Why this document exists** (`docs/roadmap.md`, Step 0):
>
> The board API can be validated on paper by hand-tracing. A CLI cannot — it is judged by whether it is *pleasant to actually use*, and you only learn that by using it. Reading these transcripts is how you discover that a verb is wrong, a default is annoying, or output is unreadable, **at the cost of editing Markdown rather than refactoring a command dispatcher.**
>
> So: every byte of output below is written out in full. No `<dump goes here>` hand-waving — unreadable output is precisely the defect being hunted, and you cannot see it in an ellipsis.

Every command used here is drawn from `DESIGN.md` §10. Where a transcript **needed** something §10 does not define, that is recorded as a **finding** (F1…F12) rather than quietly invented. The findings are the deliverable; the transcripts are the vehicle.

**Grounding.** Transcripts 1 and 4 use the **real** DBL 4.1 boot PROM (`dbl.bin`, 256 bytes, disassembled by Martin Eberhard in `dbl.prn`); the hex and the disassembly below are the actual bytes, not plausible ones. Transcript 1's CP/M sign-on is the real string from `BIOS.ASM:363`. Transcript 3's program is hand-assembled and verified by hand below.

---

## Conventions these transcripts assume — and which §10 does not yet state

Writing the transcripts forced these into the open. Each is **F3** unless noted.

| Convention | Assumed here |
|---|---|
| Prompt | `altairsim> ` |
| Number base | **Addresses, ports, and data are hex** (`0x` prefix optional and accepted). **Counts, sizes, and rates are decimal** — `SET sio2a:a BAUD=9600` is 9600 bps, `STEP 20` is twenty steps. |
| Ranges | `100-1FF` (inclusive) or `100:20` (start + count). |
| Case | Commands and property names case-insensitive; file paths and endpoints case-preserving. |
| Script comments | `;` to end of line, in `-c` scripts. |

That table is a **finding, not a decision**: §10 defines none of it, and `BREAK 100` is ambiguous until it does.

---

## Transcript 1 — cold-boot CP/M to `A0>`

**Milestone 3.** The everyday path. If this is clumsy, everything is clumsy.

```
$ altairsim machines/cpm.toml
```
> **F4.** §10 only ever mentions `altairsim -c script.cmd` and `altairsim --mcp`. **The command-line grammar is undefined** — there is no stated way to say "start with this machine." Proposed: `altairsim [config.toml] [-c script.cmd] [--mcp | --mcp-port N]`.

```
altairsim 0.1.0 — Altair 8800 / S-100 simulator
Loaded machines/cpm.toml — machine "cpm-dev"

  id       type      decode              status
  ------------------------------------------------------------------
  cpu0     88-cpu    —                   8080 @ 2.000 MHz
  ram0     ram       mem 0000-FEFF       64256 bytes
  turnkey  rom       mem FF00-FFFF       256 bytes, phantom=read
                                           <- roms/dbl.bin
  sio2a    88-2sio   io  10-13           interrupt=int
    :a                                     console
    :b                                     (not connected)
  fdc      88-dcdd   io  08-0A           4 drives
    :0                                     disks/CPM22-8MB-56K-SIM.DSK
                                             8978432 bytes, fdc8mb, rw
    :1                                     (empty)

running startup commands from machines/cpm.toml:
  GO FF00

56K CP/M 2.2b v1.0
For Altair 8Mb Virtual Drive

A0>
```

> **There is no `BOOT` command, and that is deliberate** (`DESIGN.md` §10.0). The 8080 starts at `0000`; the DBL PROM lives at `FF00`. SIMH bridges that by synthesizing a bootstrap internally — **fabricated hardware, forbidden by §0.1.** The real machine bridges it with a **power-on jump** on the turnkey board, which we may model later once the 88-TURNKEY manual is in hand (§17).
>
> Until then the honest answer is the one an operator gives: **`GO FF00`**. And the machine config carries that keystroke so you don't retype it every session:
> ```toml
> [machine]
> sense   = 0x00
> startup = [ "GO FF00" ]     # monitor commands, run once the backplane exists
> ```
> The payoff is bigger than convenience: **the config language and the script language are now one language.** Anything you can type, a config can do — which is most of what F9 below needs.

> **F6 — a real gap, and it blocks the milestone-3 boot.** The DBL PROM does this at `FF22`:
> ```
> FF22  DB FF     IN   0FFH      ; read the SENSE switches
> FF24  E6 10     ANI  10H       ; stop-bit select for the 2SIO
> ```
> It **literally reads port 0xFF at boot** to choose the 2SIO's stop-bit configuration. `DESIGN.md` §1 says "sense switches at port 0xFF are a config value" — but **`docs/config.md` had no key for them and §10 had no `SET` target for them.** Now fixed: a `[machine] sense` key, plus `SHOW MACHINE` (**F10**, which did not exist either):
> ```
> altairsim> SHOW MACHINE
>   name      cpm-dev
>   clock     2000000 Hz
>   sense     00        (port 0xFF — front-panel sense switches)
>   startup   GO FF00
> ```

> ### ⚠️ F6 WAS NOT FIXED, AND IT SAID IT WAS FOR MONTHS. (Patrick, 2026-07-12)
>
> Read the "fix" above again. It added a `[machine] sense` **key**, a `SHOW MACHINE` **line**, and
> a `CONFIG SAVE` **line** — and it wired the value **to nothing at all**. No board decoded port
> `0xFF`. So `IN 0FFH` fell through to the floating bus and returned `0xFF`, forever, whatever you
> configured, and **DBL's `ANI 10H` was testing a pulled-up wire instead of a switch**. The bit came
> back set every time and the machine ran 8N1 by luck.
>
> Every symptom a working feature has, this had. It parsed. It round-tripped. It printed. The one
> thing it did not do was the thing it was for, and *the transcript's own boot worked*, which is
> how it survived.
>
> **The failure was not the missing wire. It was believing `DESIGN.md` §1** — "sense switches at
> port 0xFF are a **config value**" — which is the sentence that made a config key look like a
> complete answer. It is not a config value. It is eight toggle switches on a card, and the card is
> the Display/Control board.
>
> **Actually fixed 2026-07-12:** the panel is a board (`fp`), it decodes `IN 0FFH`, and
> `Machine::sense` is **deleted** — the field, the key, the `SHOW` line and the `CONFIG SAVE` line.
> `[machine] sense` is now a load **error** that hands you the `[[board]]` block replacing it. See
> `docs/boards/mits-frontpanel.md`, and `tests/test_frontpanel.cpp`, which keeps a tripwire on the
> floating case so a machine with no panel still honestly returns `0xFF`.
>
> The lesson is cheaper to write down than to relearn: **a config key is not a feature.** Nothing
> here tested that the guest could *read* the switches, and nothing in the plumbing could have.

```
A0>DIR
A0>: AFORMAT COM : ASM     COM : ASM21   COM : BIOS    ASM : BIOS    HEX
A0>: BOOT    ASM : CPM48   COM : DDT     COM : DUMP    ASM : DUMP    COM
A0>: ED      COM : FINDBAD COM : FORMAT8M COM: IOBYTE  TXT : IPBYTE  BAK
A0>: L80     COM : LOAD    COM : LS      COM : M80     COM : MAKE    SUB
A0>: MBASIC  COM : MBASIC45 COM: MOVCPM8M COM: NSWP    COM : PIP     COM
A0>: R       COM : SEDIT   COM : SEDIT   DOC : STARDUST COM: STAT    COM
A0>: SUBMIT  COM : SYSGEN  COM : W       COM : WM      COM : WM      HLP
A0>: XSUB    COM
A0>
```

> That directory is **real** — extracted from `CPM22-8MB-56K-SIM.DSK` by walking its actual CP/M directory. Note `R.COM` and `W.COM` sitting right there: the AltairZ80 host-transfer utilities. **They will not work under `altairsim`** (`DESIGN.md` §12), and a user will absolutely try them. They should fail with an explanation, not a hang:
> ```
> A0>R FOO.TXT
> ```
> …hits `OUT 0FEH`, which no board decodes. **F12:** an unclaimed I/O port should be *observable*, not silent. `SET BUS CONTENTION` covers two boards claiming one port; nothing covers **zero** boards claiming one. Proposed: `SET BUS UNCLAIMED=WARN|ERROR|SILENT`, defaulting to `WARN`:
> ```
> warning: OUT 0FE <- 01 at PC=0113: no board decodes port 0xFE. reads float to 0xFF.
>          (AltairZ80's SIMH pseudo-device is not implemented — see DESIGN.md §12.)
> ```
> That one line turns a mystifying hang into a self-explaining refusal, and it costs nothing.

Getting back to the monitor, and shutting down:

```
[Ctrl-E]
altairsim> STOP
stopped at PC=C4D3 (bios+0x0F3), 41,207,884 T-states elapsed

altairsim> QUIT
fdc:0 has unwritten changes — sync to disks/CPM22-8MB-56K-SIM.DSK? [Y/n] y
synced. bye.
$
```

> **F1 — there is no way to leave the program in §10.** This is not a nitpick: a disk image with a dirty write-back buffer needs a defined shutdown, and the porting notes (`§ BIOS track-buffer trap`) say the buffer may still be holding a directory update. Leaving must flush, and must *say* it flushed.
>
> *Resolved: **`QUIT`**, and only QUIT — there is no `EXIT`. Two words for one action is two things to learn and buys nothing, and EXIT was the only thing standing between EXAMINE and its natural `EX`.*
>
> **F2 — there is no `HELP`**, either. A monitor with ~40 commands and no `HELP` is a monitor you read the source to use.

---

## Transcript 2 — build a machine, hit a port collision, fix it

**Milestone 1.** This is the transcript that tests whether contention-as-a-feature (§4.6) actually pays off in practice.

```
$ altairsim
altairsim 0.1.0 — Altair 8800 / S-100 simulator
No machine loaded. Empty backplane.

altairsim> BOARD TYPES
  type       description                          properties
  ----------------------------------------------------------------------------
  88-cpu     MITS 88-CPU processor card           cpu, clock
  ram        Static RAM                           base, size, phantom, banks,
                                                  bank_size, live_bank
  rom        ROM / PROM card                      base, size, mount, phantom
  88-2sio    MITS 88-2SIO dual serial (2x 6850)   port, interrupt
                                                    per unit: baud, connect
  88-dcdd    MITS 88-DCDD 8" floppy controller    port, drives, interrupt
                                                    per unit: mount, media, readonly

altairsim> BOARD ADD 88-cpu cpu0 CPU=8080
cpu0: 88-cpu added. 8080 @ 2.000 MHz.

altairsim> BOARD ADD ram ram0 BASE=0000 SIZE=10000
ram0: ram added. mem 0000-FFFF (65536 bytes).

altairsim> BOARD ADD 88-2sio sio2a PORT=10
sio2a: 88-2sio added. io 10-13.

altairsim> CONNECT sio2a:a console
sio2a:a -> console.
```

Now the mistake — a second 2SIO, and the base address is wrong:

```
altairsim> BOARD ADD 88-2sio sio2b PORT=12
sio2b: 88-2sio added. io 12-15.

warning: I/O contention. ports 12-13 are claimed by 2 boards:
           sio2b  io 12-15  (88-2sio, channel A)
           sio2a  io 10-13  (88-2sio, channel B)
         reads return the wire-AND of both drivers. see SHOW BUS IO.
```

> **This is the moment the design is trying to buy**, and it lands: the collision is reported *at the instant it is created*, naming both boards and the exact overlapping ports — not discovered three hours later as "BASIC prints garbage on channel B." Note it also names the *channels*, not just the boards, which is what you actually need in order to fix it.

```
altairsim> SHOW BUS IO
  port  board    unit  direction  what
  --------------------------------------------------------------
  10    sio2a    :a    in/out     6850 control (w) / status (r)
  11    sio2a    :a    in/out     6850 data
  12    sio2a    :b    in/out     6850 control (w) / status (r)   ** CONTENDED
        sio2b    :a    in/out     6850 control (w) / status (r)   **
  13    sio2a    :b    in/out     6850 data                       ** CONTENDED
        sio2b    :a    in/out     6850 data                       **
  14    sio2b    :b    in/out     6850 control (w) / status (r)
  15    sio2b    :b    in/out     6850 data

  2 contended ports. SET BUS CONTENTION=WARN|ERROR|SILENT (currently WARN).

altairsim> WHO IO 12
  port 12 is claimed by 2 boards:
    sio2a  88-2sio  base=10, channel B control/status  (10-13)
    sio2b  88-2sio  base=12, channel A control/status  (12-15)
  a 88-2sio occupies 4 consecutive ports from `port`. these overlap.
  fix: SET sio2b PORT=14   (or any base >= 14)
```

> The last line — the monitor *suggesting the fix* — is the difference between a diagnostic and a tool. It can do it because `properties()` tells it `port` is the board's base and the decode is 4 wide. It costs nothing and it is exactly the thing you want at 1 a.m.

```
altairsim> SET sio2b PORT=14
error: `port` is a config-time property and the machine is running.
       STOP the machine, or POWER it down, before changing it.

altairsim> STOP
already stopped (machine has never run).
```

> **Bug in my own transcript, and worth keeping.** The machine has never been started, yet `SET` rejected the change as "running." "Running" and "configured" are different states, and §5's `runtime` flag conflates them. **F11:** a board needs three states, not two — *unconfigured* (freely settable), *configured but halted* (settable, takes effect on next reset), *running* (config-time properties locked). `SHOW <id>` must display which. As written, you cannot build a machine interactively at all, which is the entire point of this transcript.

Assuming that is fixed:

```
altairsim> SET sio2b PORT=14
sio2b: port 12-15 -> 14-17. contention cleared.

altairsim> SHOW BUS IO
  port  board    unit  direction  what
  --------------------------------------------------------------
  10    sio2a    :a    in/out     6850 control (w) / status (r)
  11    sio2a    :a    in/out     6850 data
  12    sio2a    :b    in/out     6850 control (w) / status (r)
  13    sio2a    :b    in/out     6850 data
  14    sio2b    :a    in/out     6850 control (w) / status (r)
  15    sio2b    :a    in/out     6850 data
  16    sio2b    :b    in/out     6850 control (w) / status (r)
  17    sio2b    :b    in/out     6850 data

  no contention.

altairsim> SHOW sio2b
  sio2b — 88-2sio (MITS dual serial, 2x Motorola 6850 ACIA)

  property    value     units  runtime?  legal
  ---------------------------------------------------------------------
  port        14        hex    no        any 4-port boundary
  interrupt   none             no        none | int | vi0..vi7

  unit :a
    baud      9600      bps    yes       61..19200
    connect   (none)           yes       console | socket:* | serial:* | file:* | null
  unit :b
    baud      9600      bps    yes       61..19200
    connect   (none)           yes       console | socket:* | serial:* | file:* | null

altairsim> CONNECT sio2b:a socket:2323
sio2b:a -> socket:2323 (listening; no client yet).

altairsim> CONFIG SAVE machines/twosio.toml
wrote machines/twosio.toml (5 boards, 2 connections).
```

> `SHOW` is **fully generic** — it knows nothing about baud rates. Everything in that table came from `Board::properties()`, which is also what generated the `BOARD TYPES` listing, the tab completion, the TOML round-trip, and the MCP schema. That is §5 paying off, and the transcript confirms the shape is right.

---

## Transcript 3 — the debug loop

**Milestone 1.** Where the hours actually go, and therefore the most important transcript in this file.

The program: print `HELLO` on 2SIO channel A, polled. Hand-assembled, and **it has a bug**, which is the point.

```asm
        ORG  0100H
SIOCTL  EQU  10H              ; 6850 ch. A control (w) / status (r)
SIODAT  EQU  11H              ; 6850 ch. A data
        MVI  A,03H            ; 6850 master reset
        OUT  SIOCTL
        MVI  A,11H            ; /16, 8 data bits, 2 stop, no interrupts
        OUT  SIOCTL
        LXI  H,MSG
LOOP:   MOV  A,M
        ORA  A
        JZ   DONE
TXWAIT: IN   SIOCTL
        ANI  01H              ; <-- BUG: 01 is RDRF. TDRE is 02.
        JZ   TXWAIT
        MOV  A,M
        OUT  SIODAT
        INX  H
        JMP  LOOP
DONE:   HLT
MSG:    DB   'HELLO',0DH,0AH,0
```

```
altairsim> LOAD hello.bin AT 0100
loaded hello.bin: 39 bytes -> 0100-0126 (binary).

altairsim> GO 0100
```
…and it hangs. Nothing prints. So:

```
[Ctrl-E]
altairsim> STOP
stopped at PC=0114 (JZ 0110), 1,284,553 T-states elapsed

altairsim> DISASM 0110 6
  0110  DB 10        IN   10
  0112  E6 01        ANI  01
  0114  CA 10 01     JZ   0110
  0117  7E           MOV  A,M
  0118  D3 11        OUT  11
  011A  23           INX  H

altairsim> BREAK 0112
breakpoint 1 set at 0112.

altairsim> GO
breakpoint 1 at 0112.

altairsim> STEP
  0112  E6 01     ANI  01     ->  A=00  F=.Z.P.  (was A=02)

altairsim> REGS
  A=00  F=01000110  (.Z...P.)      SP=0000
  B=00  C=00   BC=0000
  D=00  E=00   DE=0000
  H=01  L=1F   HL=011F  (M=48 'H')
  PC=0114       T=1284561
```

> There it is. `IN 10` returned **`02`** — TDRE set, RDRF clear, exactly right for a 6850 with nothing typed and the transmitter idle. The mask `ANI 01` tests **RDRF**, so it waits for a *received* character that will never come. The program is polling the wrong bit.
>
> `(was A=02)` in the `STEP` output is what makes this a five-second diagnosis instead of a five-minute one — the pre-value is gone by the time you print the registers, so `STEP` has to show it or you re-run to find it.

Patch the byte and continue:

```
altairsim> DEPOSIT 0113 02
0113: 01 -> 02

altairsim> DISASM 0112 1
  0112  E6 02        ANI  02

altairsim> NOBREAK
1 breakpoint cleared.

altairsim> GO 0100
HELLO
halted at PC=011F (HLT), 1,284,672 T-states elapsed.
```

> **F5.** `NOBREAK` cleared everything, silently. §10 gives `BREAK` and `NOBREAK` and **no way to list breakpoints** — but MCP has a `breakpoints` tool (§11), so the capability exists and only the monitor lacks it. Proposed: bare `BREAK` lists; `NOBREAK <n>` clears one; `NOBREAK` clears all *and says how many*, as above.
>
> **F8.** §10.2 refers to `EXAMINE` as one of the through-the-bus commands, but §10's command list never defines it. Either add it (as an alias for a 1-byte `DUMP`) or strike it from §10.2 — right now the design contradicts itself.

Verifying the patch didn't just get lucky:

```
altairsim> DUMP 0100-0126
  0100  3E 03 D3 10 3E 11 D3 10  21 1F 01 7E B7 CA 1E 01   >...>...!..~....
  0110  DB 10 E6 02 CA 10 01 7E  D3 11 23 C3 0B 01 76 48   .......~..#...vH
  0120  45 4C 4C 4F 0D 0A 00                               ELLO...

altairsim> SAVE hello-fixed.bin 0100-0126
wrote hello-fixed.bin: 39 bytes (binary).
```

---

## Transcript 4 — the DBL PROM, `RAW`, and a phantomed-out board

**Milestone 4.** This is the transcript that tests §10.2 — the through-the-bus vs behind-the-bus distinction — and it is the subtlest thing in the CLI.

```
altairsim> BOARD ADD ram ram0 BASE=0000 SIZE=10000
ram0: ram added. mem 0000-FFFF (65536 bytes).

altairsim> BOARD ADD rom turnkey BASE=FF00 SIZE=0100 PHANTOM=read
turnkey: rom added. mem FF00-FFFF (256 bytes).
         asserts PHANTOM* (pin 67) on read cycles in FF00-FFFF.
         ram0 honors PHANTOM* and will disable itself for those reads.

altairsim> LOAD roms/dbl.bin AT 0000 RAW turnkey
loaded roms/dbl.bin: 256 bytes -> turnkey[0000-00FF] (binary, behind the bus).
```

> **Note the address.** `AT 0000 RAW turnkey` — **board-local offset zero**, not `FF00`. That is the corrected §10.2: `RAW` addresses a board's own store, which is what makes it work uniformly for a banked RAM board whose store is bigger than the address space. An earlier draft wrote `LOAD dbl.bin AT 0xFF00 RAW turnkey`, mixing a bus address with a behind-the-bus command; that would have been a real trap.
>
> The `RAW` is *not optional* here: the ROM board correctly refuses a bus write, so loading its image **must** bypass the bus. Try it the obvious way and you get told so:
> ```
> altairsim> LOAD roms/dbl.bin AT FF00
> error: 256 writes to FF00-FFFF rejected: turnkey is a ROM board.
>        to load its image, address the board directly:  LOAD roms/dbl.bin AT 0000 RAW turnkey
> ```

Now read it back *through the bus*, which is what the CPU will see:

```
altairsim> DUMP FF00-FF2F
  FF00  21 13 FF 11 00 2C 0E EB  7E 12 23 13 0D C2 08 FF   !....,..~.#.....   [turnkey, phantom]
  FF10  C3 00 2C F3 AF D3 22 2F  D3 23 3E 2C D3 22 3E 03   ..,..."/.#>,.">.   [turnkey, phantom]
  FF20  D3 10 DB FF E6 10 0F 0F  C6 10 D3 10 31 79 2D AF   ............1y-.   [turnkey, phantom]

altairsim> DISASM FF00 12
  FF00  21 13 FF     LXI  H,FF13        ; source: the code to relocate
  FF03  11 00 2C     LXI  D,2C00        ; destination: RUNLOC
  FF06  0E EB        MVI  C,EB          ; 235 bytes
  FF08  7E           MOV  A,M
  FF09  12           STAX D
  FF0A  23           INX  H
  FF0B  13           INX  D
  FF0C  0D           DCR  C
  FF0D  C2 08 FF     JNZ  FF08
  FF10  C3 00 2C     JMP  2C00          ; run from RAM
  FF13  F3           DI
  FF14  AF           XRA  A
```

> Those are the **real bytes of the real DBL 4.1 PROM**, and the disassembly matches Martin Eberhard's listing line for line. The PROM's first act is to copy *itself* into RAM at `2C00` and jump there — because the EPROM was too slow to execute from directly. A nice thing to see the simulator render correctly on day one.
>
> The `[turnkey, phantom]` annotation on every line is §10.2's promise kept: the bytes came from the ROM's phantom overlay, **not** from the RAM that also decodes `FF00-FFFF`. Without that tag, this dump is a lie by omission.

The payoff — proving the RAM underneath is still there, and still writable:

```
altairsim> DEPOSIT FF00 DE AD BE EF
FF00: 4 bytes written. (phantom=read: writes fell through to ram0.)

altairsim> DUMP FF00-FF0F
  FF00  21 13 FF 11 00 2C 0E EB  7E 12 23 13 0D C2 08 FF   !....,..~.#.....   [turnkey, phantom]

altairsim> DUMP 0000-000F RAW ram0
  0000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................

altairsim> DUMP FF00-FF0F RAW ram0
  FF00  DE AD BE EF 00 00 00 00  00 00 00 00 00 00 00 00   ................
```

> **This is the whole point of the memory model, on one screen.** The write went *through* the phantom to the RAM; the read came *from* the phantom. The bus view and the board view disagree, and they are *supposed* to — and the CLI can show you both, so the disagreement is legible instead of maddening. If a user can produce those three dumps, PHANTOM is not going to confuse them again.

```
altairsim> WHO FF00
  address FF00 is registered by 2 boards:
    turnkey  rom  FF00-FFFF  enabled, asserts PHANTOM* on reads
    ram0     ram  0000-FFFF  enabled, honors PHANTOM*

  read  FF00:  turnkey pulls PHANTOM* (pin 67) -> ram0 disables itself -> turnkey answers.
  write FF00:  turnkey does not pull PHANTOM* on writes -> ram0 is live -> ram0 takes it.

  this is not contention. only one board drives either cycle.
```

> **This is the corrected §4.2, and the wording is the whole point.** The bus does not "pick a winner." The ROM *pulls a signal* and the RAM *turns itself off* — so "writes fall through" stops being a special rule and becomes the obvious consequence of a ROM that only asserts PHANTOM\* on reads. `SHOW BUS CONTENTION` stays quiet because, on any given cycle, exactly one board is really driving.
>
> Strap the RAM the other way and the simulator hands you the bug the backplane would have:
> ```
> altairsim> SET ram0 HONORS_PHANTOM=OFF
> ram0: honors_phantom off. warning: ram0 will now drive FF00-FFFF against turnkey.
>
> altairsim> DUMP FF00-FF0F
> warning: contention at FF00-FF0F: turnkey and ram0 both drive. returning wire-AND.
>   FF00  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
> ```
> Which is *exactly* what you would see on the bench, for exactly the same reason.

**The boot ROM that switches itself out** (§4.2.1) — the case that motivated all of this:

```
altairsim> SHOW BUS MAP
  range        board    type  state
  ----------------------------------------------------------------
  0000-FFFF    ram0     ram   enabled, honors PHANTOM*
  FF00-FFFF    turnkey  rom   enabled, asserts PHANTOM* on reads

; ... the boot code runs, and as its last act writes to the ROM board's port,
; switching itself out so the RAM underneath becomes visible ...

altairsim> SHOW BUS MAP
  range        board    type  state
  ----------------------------------------------------------------
  0000-FFFF    ram0     ram   enabled, honors PHANTOM*
  FF00-FFFF    turnkey  rom   ** DISABLED **  (guest wrote 01 -> port FE at PC=2C71)

altairsim> DUMP FF00-FF0F
  FF00  DE AD BE EF 00 00 00 00  00 00 00 00 00 00 00 00   ................
```

> The ROM is gone and the RAM is showing through — and `SHOW BUS MAP` says **who disabled it and from what PC**, which is the difference between a five-minute diagnosis and an afternoon. A board that is present but decoding nothing is otherwise completely invisible.
>
> **And this is where `POWER` vs `RESET` earns its keep** (§6). `POWER` must re-enable the ROM or the machine boots exactly once and never again. Whether the front-panel `RESET` also re-enables it is a **board strap**, and it is the classic source of "boots from power-on, dead from the reset button" — because a warm reset that leaves the ROM switched out drops the CPU onto RAM at 0000, where it executes garbage.
> ```
> altairsim> RESET
> reset (bus). turnkey: stays DISABLED (strap: poc_only) — CPU will start in RAM.
>              warning: no boot ROM is enabled. GO 0 will execute RAM contents.
>
> altairsim> POWER
> power-on clear. turnkey: re-enabled. ram0: contents cleared.
> ```

---

## Transcript 5 — the same machine, driven from MCP

**Milestone 1.** The test here is whether MCP is genuinely a first-class interface (§11) or a wrapper around a text CLI.

Bring up transcript 3's machine and run 4K MITS BASIC on it — acceptance test 1.

```jsonc
// -> board_list()
{"boards": [
  {"id": "cpu0",  "type": "88-cpu",  "cpu": "8080", "clock": 2000000},
  {"id": "ram0",  "type": "ram",     "base": 0, "size": 65536},
  {"id": "sio2a", "type": "88-2sio", "port": 16, "interrupt": "int",
   "units": {"a": {"baud": 9600, "connect": "console"},
             "b": {"baud": 9600, "connect": null}}}
]}
```

> Note `"port": 16` — **JSON has no hex.** The monitor shows `10`, MCP says `16`, and they are the same port. That is fine, but it must be *stated*, or the first person to write an MCP test will file a bug. **F3** again, from the other side.

```jsonc
// -> board_set({"id": "sio2a", "unit": "a", "key": "baud", "value": 300})
{"ok": true, "id": "sio2a", "unit": "a", "baud": 300, "previous": 9600}

// -> mem_load({"file": "basic4k.bin", "at": 0})
{"ok": true, "bytes": 4096, "range": [0, 4095], "format": "binary"}

// -> run({"max_steps": 5000000, "idle_threshold": 1000})
{"reason": "idle", "steps": 182446, "t_states": 1093221,
 "output": "MEMORY SIZE? "}
```

> **`"reason": "idle"` is the single most valuable thing in this document.** BASIC is sitting in a polling loop on the 6850 status port; it will never halt and never hit a breakpoint. Without idle detection (§8) this call burns five million steps and returns `max_steps`, and every automated test is slow and ambiguous. With it, the call returns in milliseconds *and tells you the machine is parked at a prompt.* The porting notes are right that this is what makes automation work.

```jsonc
// -> send({"text": "\r"})
{"ok": true, "queued": 1}

// -> expect({"pattern": "OK", "max_steps": 5000000, "idle_threshold": 1000})
{"found": true, "steps": 44102, "t_states": 264612,
 "output": "MEMORY SIZE? \r\nTERMINAL WIDTH? \r\nWANT SIN? \r\n4K BASIC\r\nOK\r\n"}

// -> send({"text": "PRINT 2+2\r"})
{"ok": true, "queued": 10}

// -> expect({"pattern": "^ 4", "max_steps": 1000000, "idle_threshold": 1000})
{"found": true, "steps": 8817, "t_states": 52902, "output": " 4 \r\nOK\r\n"}
```

**Acceptance test 1, met.** And the failure path is what makes `expect` worth having:

```jsonc
// -> expect({"pattern": "SYNTAX ERROR", "max_steps": 1000000, "idle_threshold": 1000})
{"found": false, "reason": "idle", "steps": 1204, "t_states": 7224,
 "output_tail": " 4 \r\nOK\r\n"}
```

> It returns **`output_tail` on failure**. Anyone who has debugged a test harness that says only `expected "X", timed out` knows why that one field is non-negotiable.

The debug tools, structured rather than scraped:

```jsonc
// -> regs()
{"a": 0, "f": {"s":false,"z":true,"ac":false,"p":true,"cy":false},
 "bc": 0, "de": 0, "hl": 287, "sp": 8192, "pc": 276, "t_states": 1093221}

// -> disasm({"at": 272, "count": 3})
{"lines": [
  {"addr": 272, "bytes": "DB 10",    "text": "IN 10"},
  {"addr": 274, "bytes": "E6 02",    "text": "ANI 02"},
  {"addr": 276, "bytes": "CA 10 01", "text": "JZ 0110"}]}

// -> bus_io()
{"ports": [
  {"port": 16, "board": "sio2a", "unit": "a", "dir": "inout", "what": "6850 control/status"},
  {"port": 17, "board": "sio2a", "unit": "a", "dir": "inout", "what": "6850 data"},
  {"port": 18, "board": "sio2a", "unit": "b", "dir": "inout", "what": "6850 control/status"},
  {"port": 19, "board": "sio2a", "unit": "b", "dir": "inout", "what": "6850 data"}],
 "contention": []}
```

> `bus_io()` returns the **same facts** as `SHOW BUS IO`, from the same `Machine` API, without the ASCII table. That is the §11 claim — one engine, two front ends — and it holds up here. The monitor's version is not the source of truth that MCP parses; both render one model.

### But there is a hole, and it is in the CI story

**F9 — the acceptance tests cannot actually be run as specified.**

`docs/roadmap.md` says every milestone has *"a written acceptance test that runs headless in CI on all four platforms via `altairsim -c script.cmd`."* But acceptance test 1 is *"BASIC answers `PRINT 2+2`"* — and answering that requires `send` and `expect`, which **exist only in MCP** (§11). §10's command language has no way to type at the guest or wait for output. So:

- the CI gate as written is **unmeetable with `-c script.cmd`**, and
- the two interfaces are **not** at parity, which contradicts §11's central claim.

Two ways out, and the second is better:

1. Run CI through MCP with a JSON driver script. Works, but now the monitor is a second-class citizen and `-c script.cmd` is decorative.
2. **Give the monitor `SEND`, `EXPECT`, and `RUN` as real commands** (with an exit status), so a `.cmd` script is a genuine test script:
   ```
   ; acceptance/m1-basic.cmd — exits nonzero on failure
   CONFIG LOAD machines/basic.toml
   LOAD basic4k.bin AT 0000
   RUN IDLE=1000
   SEND "\r"
   EXPECT "OK" TIMEOUT=5000000
   SEND "PRINT 2+2\r"
   EXPECT " 4" TIMEOUT=1000000
   ECHO "acceptance test 1: PASS"
   EXIT 0
   ```
   This costs almost nothing — the engine is already there for MCP — and it makes §11's "one `Machine` API, two front ends" true rather than aspirational. **Recommended.**

---

## Findings — the punch list

The transcripts did their job: none of these were visible in `DESIGN.md`, and all of them would have been discovered by writing code instead.

| # | Finding | Severity |
|---|---|---|
| **F1** | **No `EXIT`/`QUIT` command exists in §10.** No defined shutdown — and a dirty disk write-back buffer needs one. | **blocking** |
| **F2** | No `HELP`. | should fix |
| **F3** | **Number base, range grammar, and case rules are undefined.** `BREAK 100` is ambiguous. JSON-vs-monitor hex/decimal must be stated. | **blocking** |
| **F4** | **Command-line invocation grammar undefined** — no stated way to start with a machine file. | **blocking** |
| **F5** | No way to **list** breakpoints in the monitor, though MCP has `breakpoints`. | should fix |
| **F6** | **Sense switches (port 0xFF) had no config key and no `SET` target** — but the DBL PROM reads them at `FF22` to configure the 2SIO. Would have blocked the milestone-3 boot. ~~**✅ Fixed:** `[machine] sense`, plus `SHOW MACHINE`.~~ **THAT FIX WAS A FICTION** — the key parsed into a byte that nothing put on the bus, so `IN 0FFH` read the floating bus (`0xFF`) regardless. **Actually fixed 2026-07-12:** the front panel is a *board* (`fp`) and it decodes the port. `[machine] sense` is deleted. See the ⚠️ note above. | ~~blocking~~ ~~fixed~~ **fixed for real** |
| **F7** | **`BOOT` had no honest semantics.** The SIMH answer (synthesize a bootstrap) is forbidden by §0.1; the real mechanism is a turnkey board's **power-on jump**, which needs a manual we don't have. **✅ Fixed by removing `BOOT` entirely** (§10.0): the config file carries `startup = ["GO FF00"]` — the operator's keystroke, written down, inventing nothing. | ~~blocking~~ **fixed** |
| **F8** | §10.2 references `EXAMINE`; §10 never defines it. The design contradicts itself. | should fix |
| **F9** | **The CI acceptance gate is unmeetable as specified.** `send`/`expect` exist only in MCP, so `altairsim -c script.cmd` cannot run acceptance test 1. Proposed: `SEND`/`EXPECT`/`RUN`/`ECHO`/`EXIT n` in the monitor. *Mostly unblocked by §10.0's `startup` list, which already makes config and script one language — this is the remaining half.* | **blocking** |
| **F10** | No `SHOW MACHINE` / `SHOW VERSION`. Nowhere to see the clock, the sense switches, or the startup commands. **✅ Fixed** alongside F6. | ~~should fix~~ **fixed** |
| **F11** | **§5's `runtime` flag conflates "configured" with "running."** A board needs three states, or you cannot build a machine interactively — which breaks transcript 2 entirely. | **blocking** |
| **F12** | An I/O port claimed by **zero** boards is silent. §4.6 covers two claimants but not none. Proposed `SET BUS UNCLAIMED=WARN` — it is what turns a `R.COM` hang into an explanation. | should fix |

### Resolved: how the CPU reaches `0xFF00` — and why there is no `BOOT`

**Asked and answered** (Patrick, 2026-07-11). The real mechanism is a **power-on jump**: a turnkey/PROM board forces the processor to the PROM address after reset. Simulators typically don't model it — they come up at a prompt and the operator types `G FF00`.

Two consequences, both now in the design:

1. **`BOOT` is deleted** (§10.0). It could only ever have been dishonest: SIMH's version synthesizes a bootstrap the hardware never had, which §0.1 forbids outright. `GO FF00` is what the operator actually does, so that is what the monitor offers.
2. **Machine configs carry a `startup` command list** — Patrick's suggestion, and it is strictly better than `BOOT`. `startup = ["GO FF00"]` automates the keystroke while inventing nothing, and it makes **the config language and the script language the same language**, which is most of what F9 needs. `SHOW MACHINE` prints it; `CONFIG SAVE` round-trips it.

**Still deferred, and it needs a manual (§17): modeling power-on jump as a real board property.** That is the honest long-run model for a project that calls itself a hardware bench, and it costs nothing to support *if* the `Board` interface lets a board claim an `OpFetch` cycle the way the 88-VI claims an `IntAck` (§4.4) — which would be one more piece of evidence that the bus model is right. **The 88-TURNKEY manual would settle it.** Nothing in milestones 1–3 is blocked on it.
