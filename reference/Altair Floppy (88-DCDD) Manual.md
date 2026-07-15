# Altair Floppy Disk (88-DCDD) — Emulation Reference

Source: [Altair Floppy (88-DCDD) Manual.pdf](#)

This reference distills the programmer-visible behavior of the MITS Altair 88-DCDD
floppy disk controller from the *Altair Floppy Disk Drive and Controller Hardware
Documentation* (MITS, ©1976, reprinted April 1977). The bulk of that manual is
kit-assembly and power-supply wiring; the emulation-relevant material is the
"Altair Disk Controller I/O Information" and "Disk Test Programs" sections, plus
the block diagrams. All register/port facts below are taken from those pages.

> Note on the source: this printing is a *Preliminary Documentation Release*. It
> states that the "complete Theory of Operation" would be shipped later. As a
> result the manual contains **no bootstrap/boot-loader listing** — only disk
> *test* programs (which double as reference drivers). See "Boot loader" below.

---

## 1. System architecture

The 88-DCDD is a two-board S-100 controller plus an external drive cabinet:

| Element | Role |
|---|---|
| **Controller Board #1** | All **input** to the Altair bus: address/I-O select, sector/index circuit, read circuit, status output. |
| **Controller Board #2** | All **output** from the Altair bus: disk enable + drive select, write circuit, disk-function (control) circuit. |
| **Interconnect cable** | 18-pair flat cable, 37-pin (one male, one female end). Connects controller to drive and daisy-chains drive→drive. |
| **Disk Buffer board** (in drive cabinet) | Line drivers/receivers for the long cable; holds the **drive-address jumpers** (1 of 16) and the disk-enable / 5-second timer logic; drivers for multi-system chaining. |
| **Drive** | Pertec **FD-400** 8-inch mechanism. |

The controller decodes three consecutive I/O port addresses. The drive itself is
selected by a 4-bit address set with jumper wires on the Buffer board (each drive
has a unique address 0–15; the serial-number sticker records it).

---

## 2. Media geometry and timing

| Property | Value | Notes |
|---|---|---|
| Drive mechanism | Pertec FD-400, 8-inch | |
| Sectoring | **Hard-sectored** | 32 sector holes + 1 index hole. **Not IBM compatible.** |
| Sectors per track | **32** (numbered 0–31) | Sector number appears in the sector-position register. |
| Tracks | **77** (numbered 0–76) | Step-out reaches track 0; step-in reaches track 76. Track 0 = outermost. |
| Bytes per sector | **137 data bytes max, including the SYNC byte** | A 138th byte (`000`) is written to fill the remainder of the sector. |
| Byte cell time | **32 µs** | `ENWD` (write) and `NRDA` (read) both recur every 32 µs. |
| Sector-true pulse width | **30 µs** | `SR0` is asserted (low) for 30 µs at the start of each sector. |
| Head-current split | Tracks **43–76** are the inner "high current switch" zone | Set control bit `HCS` (D6) on writes to these tracks. |

Timing landmarks within a sector (all relative to sector-true `SR0`):

- Write data first requested **280 µs** after `SR0` goes true.
- Read data first available **140 µs** after `SR0` goes true.
- On a write, trim-erase turns on **200 µs** after Write-Enable; `ENWD` first goes
  true **280 µs** from start of sector.
- After the sector's data is written, trim-erase is disabled **475 µs** later.

---

## 3. I/O port map

The controller occupies three ports. The manual gives them in **octal**; hex is
added here for convenience.

| Octal | Hex | Direction | Function |
|---|---|---|---|
| `010` | `0x08` | **OUT** | Drive select / enable (and disk-control clear). Latches. |
| `010` | `0x08` | **IN** | Disk **status** register. |
| `011` | `0x09` | **OUT** | Disk **control** (function) register. |
| `011` | `0x09` | **IN** | Sector-position register. |
| `012` | `0x0A` | **OUT** | **Write** data. |
| `012` | `0x0A` | **IN** | **Read** data. |

The controller is inert until enabled: an `OUT 010` with a matching drive address
(and D7=0) selects the drive **and** enables the controller.

**Enable interlocks** — the drive/controller *cannot* be enabled if:
- the drive door is open,
- the drive power is off, or
- the interconnect cable is not connected between controller and drive.

Disk control is additionally cleared automatically by opening the drive door or
turning drive power off (the Buffer board's 5-second timer / enable logic).

---

## 4. Drive-select / enable register — `OUT 010` (0x08)

Latched output. Selects 1 of 16 drives and enables the controller.

| Bit | Name | Meaning |
|---|---|---|
| D0 | Addr LSB | 4-bit drive address (0–15). Selects the drive whose jumper address matches, |
| D1 | Addr | and enables the controller. The address is set by 4 jumper wires on the |
| D2 | Addr | Disk Buffer PC card in the drive. |
| D3 | Addr MSB | |
| D4 | — | Not used, don't care. |
| D5 | — | Not used, don't care. |
| D6 | — | Not used, don't care. |
| D7 | **Clear** | If **1**, clears/disables disk control (D0–D6 don't care). Disk control is also cleared by opening the door or removing drive power. |

Typical enable of drive 0: `MVI A,000` / `OUT 010`.
Disable: `MVI A,200` (D7=1) / `OUT 010`.

---

## 5. Disk status register — `IN 010` (0x08)

**Active-low: a flag is TRUE when its bit = 0, FALSE when the bit = 1.**
All bits read FALSE (1) if the drive/controller is not enabled, and all FALSE if
there is no disk in the drive. Also delivers a valid `INTE` (D5) from the Altair
bus when the controller is enabled.

| Bit | Name | True (=0) meaning |
|---|---|---|
| D0 | **ENWD** — Enter New Write Data | Write circuit is ready for the next byte. Recurs every 32 µs; first occurs 280 µs after sector-true (when write-enabled). Reset by outputting a byte to the write-data channel (`OUT 012`). |
| D1 | **MH** — Move Head | Head movement is allowed (step IN/OUT). Goes false 10 ms, true 1 ms, false 20 ms around a step command (so ~10 ms/step). Goes false 40 ms after head load. Goes false during Write and for 475 µs after Write (to finish trim-erase). |
| D2 | **HS** — Head Status | Head is loaded and settled — true 40 ms after head load (or after a step with head already loaded). Also **enables the sector-position channel** when true. |
| D3 | — | Not used, = 0. |
| D4 | — | Not used, = 0. |
| D5 | **INTE** | Interrupt enabled (reflects Altair bus INTE). |
| D6 | **TRACK 0** | Head is on the outermost track (track 0). |
| D7 | **NRDA** — New Read Data Available | Read circuit has a byte ready on the read-data channel (`IN 012`). After the SYNC bit is detected, recurs every 32 µs; reset by an input from channel `012`. The byte containing the SYNC bit is the first byte read from the disk. |

Emulator note: because flags are active-low, code polls with `ANA mask` /
`JNZ` (jump while the tested bit is still 1 = false) and falls through when the
bit reads 0 = true.

---

## 6. Disk control (function) register — `OUT 011` (0x09)

Only effective while drive + controller are enabled. A **logic 1** on a data line
asserts that function:

| Bit | Name | Function when set (=1) |
|---|---|---|
| D0 | **Step IN** | Step head in one position toward a **higher-numbered** track. |
| D1 | **Step OUT** | Step head out one position toward a **lower-numbered** track. |
| D2 | **Head Load** | Load head onto disk; **enables sector-position status**. |
| D3 | **Head Unload** | Remove head from disk. May be issued immediately after Write-Enable — the write/trim-erase circuits keep the head loaded until finished. |
| D4 | **IE** — Interrupt Enable | Enable interrupt to occur when `SR0` (sector-true) is true. |
| D5 | **ID** — Interrupt Disable | Disable interrupt circuit (also disabled by clearing disk control). |
| D6 | **HCS** — Head Current Switch | Must be set when writing with the head on tracks **43–76** (reduces head current / improves inner-track resolution). Auto-reset at end of writing a sector. |
| D7 | **Write Enable** | Initiates the write sequence (see §8). |

The step lines are edge/level commands; consult status bit MH (D1) before each
step (allowed ~every 10 ms), and HS (D2) before reading/writing.

---

## 7. Sector-position register — `IN 011` (0x09)

Valid only with drive + controller enabled, head loaded, and **40 ms after the
head is loaded** (i.e. once HS is true — head load enables this channel).

| Bit | Name | Meaning |
|---|---|---|
| D0 | **SR0** — Sector True | **True when = 0**, asserted for **30 µs**. Marks the start of the current sector. Begin a write as close as possible to when SR0 goes true; write data is requested 280 µs after, read data available 140 µs after. |
| D1 | SR1 | Sector number, LSB |
| D2 | SR2 | Sector number |
| D3 | SR3 | Sector number |
| D4 | SR4 | Sector number |
| D5 | SR5 | Sector number, MSB |

Bits D1–D5 hold the **current sector number 0–31 in binary** (SR1 = LSB). The
manual tabulates it explicitly; it is a straight 5-bit binary count:

| Sector # | D5 SR5 | D4 SR4 | D3 SR3 | D2 SR2 | D1 SR1 |
|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 | 0 |
| 1 | 0 | 0 | 0 | 0 | 1 |
| 2 | 0 | 0 | 0 | 1 | 0 |
| 3 | 0 | 0 | 0 | 1 | 1 |
| … | | | | | |
| 31 | 1 | 1 | 1 | 1 | 1 |

D6, D7 are not part of the sector number in this register.

Software waits for D0=0 (sector true) and reads D1–D5 to find "which sector am I
at now"; to access sector *N* it polls until the sector number equals *N*.

---

## 8. Write data channel — `OUT 012` (0x0A), and the write sequence

A byte is written by outputting it to `012` **in response to the ENWD status
request** (status D0 = 0). The write sequence (control D7 = Write Enable):

1. Disk selected and enabled, head loaded (sector status active).
2. Software waits for **sector-true** (`SR0`) of the desired sector, then sets
   Write-Enable.
3. **200 µs** after Write-Enable, trim-erase turns on automatically. **280 µs**
   after start of sector, `ENWD` goes true; software writes the **sync byte**.
4. The **first byte written must have its MSB (D7) = 1 (the SYNC bit)**; the MSB
   is written to the medium first.
5. `ENWD` recurs every **32 µs**. Maximum **137** data bytes per sector
   (including the SYNC byte).
6. The last (138th) byte written must be **`000`**; it is written for the
   remainder of the sector. Ignore `ENWD` from that point to end of sector.
7. At end of sector the write circuit is disabled automatically; trim-erase is
   disabled 475 µs later.

Notes: (a) the write circuit keeps writing the last byte output to `012` to the
end of the sector; (b) the head may be unloaded any time during the write cycle
if no further read/write is expected — Write-Enable holds the head loaded for the
time needed for writing and trim-erase.

---

## 9. Read data channel — `IN 012` (0x0A), and the read sequence

A byte is read by inputting from `012` **in response to the NRDA status flag**
(status D7 = 0). The **first byte read is the one containing the SYNC bit**; after
SYNC detection, `NRDA` recurs every 32 µs and is reset by each input from `012`.

Typical read of a sector: enable drive, load head, wait for the target sector,
then loop {poll NRDA (status D7); `IN 012`; store} until the byte count for the
sector is exhausted.

---

## 10. Head / step / seek command reference (summary)

| Operation | How |
|---|---|
| Enable drive N | `MVI A,N` (N=0..15, D7=0) ; `OUT 010`. |
| Disable | `MVI A,200` ; `OUT 010` (or open door / cut power). |
| Load head | set control D2 ; `OUT 011`. HS (status D2) true 40 ms later. |
| Unload head | set control D3 ; `OUT 011`. |
| Step in (toward track 76) | wait MH (status D1) true ; set control D0 ; `OUT 011`. |
| Step out (toward track 0) | wait MH true ; set control D1 ; `OUT 011`. Track 0 reached = status D6 true. |
| Seek to track 0 (recalibrate) | step out repeatedly (up to 77 times) until TRACK 0 (status D6) true. |
| Wait for sector | poll `IN 011`; SR0 (D0) = 0 = sector true; D1–D5 = sector number. |
| Write byte | on ENWD (status D0=0), `OUT 012`. |
| Read byte | on NRDA (status D7=0), `IN 012`. |

Stepping cadence: MH allows a step roughly every 10 ms. After a head load, allow
40 ms before relying on HS / sector data.

---

## 11. Emulation quirks and gotchas

- **Ports are octal in the manual**: `010/011/012` = `0x08/0x09/0x0A`. Do not
  confuse with decimal 10/11/12.
- **Status is active-low.** True = bit 0, False = bit 1. When *not* enabled and
  when *no disk is present*, the entire status byte reads `0xFF` (all false).
- **Sector position is only valid ~40 ms after head load** (once HS is true);
  head-load enables that channel.
- **SYNC bit convention**: the first written byte's D7 = 1 marks sync, MSB-first
  onto the medium; the first *read* byte is the sync-containing byte. The 138th
  byte of a written sector is a `000` fill.
- **HCS (control D6) is required on writes to tracks 43–76** and auto-resets per
  sector.
- **Disk control auto-clears** on door-open / power-off — an emulator should drop
  the enabled state on those events.
- **Interrupts**: control D4 (IE) / D5 (ID) gate a sector-start interrupt (fires
  on `SR0` true when enabled); status D5 (INTE) reflects bus interrupt-enable.
  The controller board also has an interrupt-vector option (VI7 / PINTE) on the
  schematic for use with the 88-VI board.
- **Write timing is real flow control**: `ENWD`/`NRDA` pace the CPU at one byte
  per 32 µs; software that ignores the flags corrupts the sector. The write
  circuit fills the rest of a sector with the last byte output, so a driver must
  output `000` before the sector ends.

---

## 12. Reference / test programs in the manual

The manual ships disk **test** programs (reprinted from *Computer Notes*,
April 1976) that also serve as reference drivers. Two short ones are listed in
clean octal:

**Preliminary test** (single-step; verifies enable, head-load, sector, status),
origin `000,000` (octal):

```
Addr  Octal  Instruction        Comment
000   076    MVI A              \
001   000      (drive addr 0)    | enable drive/controller
002   323    OUT                 |
003   010      (channel 010)    /
004   076    MVI A              \
005   004      (head-load, D2=1) | load head
006   323    OUT                 |
007   011      (channel 011)    /
010   333    IN                 } sector position (channel 011)
011   011
012   333    IN                 } disk status     (channel 010)
013   010
```
After the last two inputs, status should read (data lights):
`D0 ENND on; D1 MH off; D2 HS off; D5 INTE on if front-panel INTE off;
D6 TRACK 0 off if head on track 0; D7 NRDA flickering (read circuit OK)`.

**Individual-function test** (step head in, etc.) — outputs a control pattern
taken from the sense switches to channel `011`:

```
000   076    MVI A
001   000      (drive addr)
002   323    OUT
003   010      (disk enable channel)
004   333    IN
005   377      (from sense switches, port 0377 = 0xFF)
006   323    OUT
007   011      (disk control channel)
```
Set sense switch 8 up (others down) to exercise one function; change the switch
pattern to drive other control bits.

The manual also contains longer **Read/Write/Output** and **Stepping** test
programs (with flowcharts and full octal listings, manual pp. 121–125). Key
parameters, as documented:

- Read/Write test: writes sector 0 of the current track, reads it back into
  memory (read buffer at `001,236` octal), then outputs it to a terminal
  (output channel `001`). Number of write bytes is taken from the sense switches
  (max `220` octal). Write data pattern: 1st byte `377` (D7=1 sync), then
  descending `data, data-1, …, 001, 000`. Sense = `000` stops. Drive address
  lives at locations `000,001` / `000,150`; output device addresses at
  `000,133` (status) and `000,141` (data).
- Stepping test: steps the head out 77 times to track 0, then in 77 times to
  track 76, repeating. Drive address at `001,001`. Head stays unloaded while
  stepping.

These listings are not transcribed digit-for-digit here (the scan is marginal);
consult the PDF pages if an exact opcode stream is needed.

---

## 13. Boot loader

**This preliminary manual does not contain a bootstrap/boot-loader listing.** It
explicitly defers the "complete Theory of Operation," Operators Manual, and DOS
documentation to a later release. The only executable listings present are the
disk *test* programs above. The Altair disk bootstrap (the short loader that
reads track 0 / sector 0 and jumps to it — normally toggled into front-panel
memory or supplied with Disk BASIC / DOS) must be sourced from the Disk Extended
BASIC / Altair DOS documentation, not from this hardware manual. (Per project
rule, no boot-loader bytes are invented here.)

---

## 14. Block-diagram signal list (for reference)

Controller↔drive signals carried on the interconnect (from the block diagrams):
`DISK PWR / DOOR OPEN`, `HEAD CURRENT SW`, `TRIM ERASE`, `WRITE ENABLE`,
`WRITE DATA`, `STEP IN`, `STEP OUT`, `HEAD LOAD`, `INDEX`, `TRACK 0`,
`READ DATA`, plus 4 drive-address lines (`DA-A..DA-D`) and `DISK ENABLE`.

Drive power rails (FD-400 + buffer): +24 V (drive), +5 V (buffer & electronics),
−5 V (electronics), from a transformer/bridge supply in the drive cabinet. Front-
panel indicators: POWER, DISK ENABLE, HEAD LOAD.
