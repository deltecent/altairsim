# 88-2SIO — MITS Dual Serial Interface

**Status: BUILT, 2026-07-11.** `src/boards/mits-2sio.cpp`, `tests/test_sio2.cpp`. **The 6850 itself moved out to `src/chips/mc6850.cpp` on 2026-07-12** (DESIGN.md §7.8) — a chip is not a card, and the next card with a 6850 on it inherits the DCD latch and the CTS-inhibits-TDRE rule already right. What is left in `mits-2sio.cpp` is what the *card* does: where the ports are, where the IRQ is jumpered, which chip answers which address.

**The proof vehicle** — a fully-modeled 2SIO exercises every interface in the design (console, TCP socket, host serial, interrupts, multi-unit boards, multiple instances), which is why it is the only peripheral in milestone 1.

**It runs ALTMON.** `altairsim altmon`, then `CONSOLE`: Mike Douglas's 1K monitor PROM prints its banner, takes commands, and dumps memory — through this card, over a real 6850, on the real bus. That is not a test we wrote; it is a program someone wrote for real hardware, and it either works or it does not.

```
$ altairsim altmon
startup> CONSOLE F800

ALTMON 1.3
*DUMP F800 F80F
F800 3E 03 D3 10 D3 12 3E 11 D3 10 D3 12 31 00 C0 CD  >.....>.....1...
```

Those bytes are ALTMON's own first sixteen, and they are `MVI A,3 / OUT 10 / OUT 12 / MVI A,11 / OUT 10 / OUT 12` — the ROM reading its own 2SIO initialization back to us *through the card it initializes*.

> **A trap, from ALTMON's own listing.** Its `ahex` routine reads exactly four hex digits and **aborts to the prompt on any byte below `'0'`** — so a space is not a separator, it is a *cancel*. `DF800F80F`, not `D F800 F80F`. The spaces in ALTMON's own printed command summary are typography, not grammar. This cost twenty minutes and is exactly the kind of thing DESIGN.md §0.1 sends you to the source for.

## The real hardware

Two independent **Motorola 6850 ACIA**s on one S-100 card. Base address is jumper-selectable; channel A sits at BA+0/BA+1 and channel B at BA+2/BA+3. Baud rate is a jumper/clock on the board, not software-settable. Each ACIA's interrupt request can be jumpered to the S-100 interrupt lines.

Default base **0x10**, so: channel A = 0x10 (control write / status read) + 0x11 (data), channel B = 0x12 + 0x13.

## Sources

Motorola 6850 datasheet. `roms/ALTMON/ALTMON.ASM` — Mike Douglas's monitor, whose equates (`CONS equ 10h`, `COND equ 11h`, `TBE equ 2`, `RDA equ 1`) confirm the base port, the register layout **and the true-sense status bits**, and whose first four instructions are the canonical initialization sequence:

```asm
monit   mvi  a,3          ;reset 6850 uart
        out  CONS
        out  CONS+2       ;2nd 2SIO port as well
        mvi  a,11h        ;8N2
        out  CONS
        out  CONS+2
```

`roms/DBL/DBL.ASM` (the boot PROM's own 2SIO equates). Both are in this repository — first-hand period artifacts, which is what §0.1 asks for.

### The data sheet cross-check (2026-07-12)

`src/chips/mc6850.cpp` was written before the data sheet was in hand, from the manual and from what ALTMON does. `reference/6850.pdf` (Motorola MC6850/68A50/68B50, pages 4-527…4-535) has now been read against it, line by line. What it **confirmed**: the status and control bit maps (Table 1), all eight rows of the word-select table, the RTS decode (`low` unless CR6=1 and CR5=0; break on `11`), `/CTS` inhibiting TDRE, the entire `/DCD` latch — edge-triggered interrupt, two-step status-then-data clear, follow-the-pin afterwards, receiver dead and RDRF forced empty while the pin is high — and RIE arming RDRF **and** OVRN **and** DCD together.

What it **corrected**, and the data sheet won every time (§0.1):

1. **A master reset does not zero the control register**, and it *latches* — see the two notes under the control register above. The old code swallowed the master-reset byte whole and zeroed `control_`, which threw away any other bits the guest wrote in the same `OUT`, and left TDRE *set* on a chip that a real 6850 would be holding down. `tests/test_sio2.cpp` asserted that wrong behaviour explicitly (*"master reset leaves TDRE set"*); the assertion has been inverted and now cites the sheet.
2. **The divide ratio is a known divergence** — see Limitations.
3. **The RESET switch cannot touch a 6850, because there is no pin for it to touch.** The chip's 24 pins are Vss, RxData, RxCLK, TxCLK, RTS, TxData, IRQ, CS0–2, RS, Vcc, R/W, E, D0–D7, DCD and CTS, and that is all of them — no RESET. Reset is internal power-on logic only, *"released by means of the bus-programmed master reset."* So the S-100 `RESET*` line reaches this card's address decoding and **nothing else**. `Sio2Board::reset()` used to reset both ACIAs on a bus reset, and it was destructive twice over: it threw away the guest's word format and interrupt enables, and it **ate a character out of the receive register** — on a card where the real thing would have preserved every bit of it. `Reset::Bus` is now a no-op on the chips. See Reset, below, and DESIGN.md §6.1.

## Register reference

### Control register (write) — the Python prototype ignored this entirely; don't.

| Bits | Meaning |
|---|---|
| 0–1 | Clock divide: `00` = ÷1, `01` = ÷16, `10` = ÷64, **`11` = master reset** |
| 2–4 | Word select: data bits / parity / stop bits (8 standard combinations) |
| 5–6 | Transmit control: `00` RTS low + **TIE off**; `01` RTS low + **TIE on** (transmit interrupt enable); `10` RTS high + TIE off; `11` RTS low + transmit break |
| 7 | **RIE** — receive interrupt enable |

Period software writes `0x03` (master reset) then `0x11` (÷16, 8N2). An **interrupt-driven** driver sets bit 7 and/or the `01` transmit-control field — that is the path milestone 1 must prove.

**Why it is always *two* writes, and never one.** The divide field is not a pulse, it is a **latch**: `11` sits there *holding the chip in reset* until a second write selects a real ratio. And a chip held in reset has its transmitter inhibited — the data sheet lists the reset condition alongside `/CTS` as a thing that suppresses TDRE. So a guest that master-resets and then polls for TDRE without programming the word format **waits forever**, on the real chip and on this one. That is why ALTMON's `MVI A,3 / OUT 10h` is only half an initialization sequence, and why every 6850 driver ever written does two `OUT`s.

**A master reset does not clear the rest of the byte it arrived in.** *"Master reset does not affect other Control Register bits"* — so `0x83` (reset **and** arm the receive interrupt, in one `OUT`, which is legal) latches RIE and resets; it does not throw the interrupt enable away. The reset is a thing the write *requests*, not a thing the write *is*.

### Status register (read) — **true sense**

| Bit | Name |
|---|---|
| 0 | **RDRF** — receive data register full |
| 1 | **TDRE** — transmit data register empty |
| 2 | DCD |
| 3 | CTS |
| 4 | FE |
| 5 | OVRN |
| 6 | PE |
| 7 | **IRQ** — this ACIA's interrupt request |

> **Trap:** the 88-SIO's status bits are **inverted**, the 88-2SIO's are not. A machine with both boards has both conventions live at once. Do not share code between them without thinking.

## How it is simulated

- Decodes I/O reads and writes on BA+0 … BA+3, **and no memory address at all.**
- **Two units** (`a`, `b`), each an independent 6850 with its own `ByteStream`, its own baud jumper, its own interrupt strap and its own transform chain. They share *nothing*: modeling them as one object with an index would be modeling the PCB rather than the chips on it.
- **Multiple board instances** at different base ports: `sio0` at 0x10, `sio1` at 0x14.
- Bit 7 (IRQ) reflects the **chip's own** request, jumper or no jumper — it is a pin, and it does not care what you soldered to it. Where that request *goes* is the unit's `interrupt` property (`none | int | vi0..vi7`), which is the **wire**. With `interrupt = int` and no VI board present, the `IntAck` cycle finds nobody driving the bus, the CPU reads a floating `0xFF`, and executes **RST 7**. A `vi*` strap with no 88-VI in the machine correctly does *nothing* — exactly as a wire to an empty slot would.
- **TDRE is a DEADLINE driven from the `Clock`**, at the configured baud rate and the word format the guest actually selected — see below.

### Properties

The board has one. **Everything else belongs to a unit**, because everything else is genuinely per-chip: `SHOW sio0` prints all three tables.

| Property | Scope | Notes |
|---|---|---|
| `port` | board | Base address. Hex — it is on the wire. |
| `baud` | unit | `SET sio0:a BAUD=9600`. Decimal — it never is. |
| `interrupt` | unit | `none \| int \| vi0..vi7` |
| `dcd` | unit | **`ground` (default) \| `wired`** — where the 6850's `/DCD` pin goes. |
| `cts` | unit | **`ground` (default) \| `wired`** — likewise `/CTS`. Wired, it **gates the transmitter**. |
| `connect` | unit | Endpoint. `CONNECT` sets it. |
| `lines` | unit | **Read-only.** The live pin state: `DCD CTS RTS brk` (capitals = asserted). A pin is not a jumper, so it has no setter, and `SET` says so. |
**There is no `data_bits`, `parity` or `stop_bits` property on this card, and there must not be.** Unlike the 88-SIO, the word format here is **not a jumper** — it is the 6850's control register, and the **guest** writes it (`0x11` = ÷16, 8N2). A property would be a second place to set it, and the two would disagree the moment software touched the chip. It is still *line coding* and it still reaches the wire: on a real serial port the card programs the host port from the bits the guest wrote (`ByteStream::setParams`), so a guest that selects 7E1 reconfigures the cable to 7E1.

**There is no transform chain on this card either, and that is deliberate.** `upper`, `strip7in`, `strip7out`, `crlf`, `echo`, `bell` and `bsdel` are the **console's** (`SET CONSOLE UPPER=ON`, DESIGN.md §7.2). A 6850's line is **8-bit clean, whatever is plugged into it** — because the thing plugged into it may be a socket carrying XMODEM, and a filter there corrupts the transfer silently. Only a terminal may rewrite a byte, and this card is not a terminal; it is a hole in the back of the machine.

### MITS BASIC's high bit, on this card

The 88-SIO's story (`docs/boards/mits-88sio.md`) applies here **unchanged, and the control register proves it**: BASIC programs the ACIA for **8N2** — *eight* data bits — so the chip legitimately puts bit 7 of `...'E'|0x80` on the wire. The card is not lying and there is nothing to fix in it. `SET CONSOLE STRIP7OUT=ON` is the Teletype that ignores the eighth bit; the line stays 8-bit clean for everything that is not one.

In a config file that is `[board.unit.a]`, and `CONFIG SAVE` round-trips all of it (`lines` excepted — you cannot save a pin).

```toml
[[board]]
type = "2sio"
id   = "sio0"
port = 10

  [board.unit.a]
  connect = "console"
  baud    = 9600

  [board.unit.b]
  connect = "serial:/dev/tty.usbserial-AL009KFH"
  baud    = 300
  dcd     = "wired"    # believe the far end
  cts     = "wired"    # ...and let it stop our transmitter
```

### The modem control lines — and the strap lives on the CARD

This is the PHANTOM\* lesson again: **the read/write distinction lived on the honoring board.** The 2SIO manual's hardwire table gives CTS, DCD and RTS each their own **jumper pads** — whether the 6850's pin reaches the connector or is strapped to ground is a fact about *the card*, not about whatever is plugged into it, and period installers grounded them constantly.

So it is a **unit property, not a stream behavior**, and the default is `ground`: the pin is tied asserted on the card, the far end is never asked, and **every config that existed before this landed keeps working untouched.**

It also dissolves the "what if there is no real serial port" question. There is no unconnected case to handle: an unplugged unit is a `NullStream`, which asserts everything, and a card strapped to `ground` never even looks. No board grows a "what if nothing is plugged in" branch — which is exactly what §7.1 demands.

**What each endpoint drives** (`true` = asserted, always; the `/DCD` and `/CTS` inversions live in the chip, which is the only thing with those pins):

| Endpoint | DCD | CTS | RTS out |
|---|---|---|---|
| `null`, `console` | asserted | asserted | ignored |
| `loopback` | = our DTR | = our RTS | fed back — *a loopback plug crosses the pins, so it is the one endpoint that can test modem control with no hardware* |
| `socket:PORT` | **a client is connected** | send buffer has room | ignored |
| `serial:/dev/tty…` | **the real pin** | **the real pin** | **the real pin** |

**A telnet client connecting *is* carrier appearing**, and hanging up *is* carrier dropping. That is what every terminal server ever built did, and it is what will let a PMMI work over a socket without the board learning what TCP is.

### The card programs the wire (there is only one baud rate)

`CONNECT sio0:b serial:/dev/tty.usbserial-XXXX` opens the port and then **the card immediately programs it**: `baud` from the strap, and the frame (8N1, 7E2, …) from the **word-select bits the guest wrote into the control register** — because those bits *are* what goes on the wire. A guest that reconfigures the chip for 7E1 reconfigures the cable for 7E1, exactly as it would on the bench.

> **There is no second, independent baud rate on the endpoint, and the plan's "two baud rates" section is struck** (Patrick, 2026-07-12: *"do we need emulated character timing with a real serial port attached? The real serial port is the limiting factor."*). A card strapped for 300 driving a terminal set to 9600 does not give you a fast link on real hardware — it gives you garbage. A second baud rate could only ever configure the garbage.
>
> The emulated character timing **stays**, and it is not double-counting: it is the *same* duration the real port takes, not an extra one. It has to stay because **the guest can measure it** — see the Mike Douglas BIOS, below.

If the host cannot do the strapped rate (an FTDI cable and 76800 baud), the card **says so** through `Board::drainLog()` and goes on pacing the guest at what it is jumpered to. What must never happen is the silent version.

**There is no `flow = rtscts` setting, either.** Hardware flow control in `termios` means the *OS driver* owns RTS and CTS — and the 6850 owns those pins: RTS is control bits 5–6, and CTS gates TDRE. Two owners for one pin is a bug that only shows up under load. The port is opened with flow control off and the chip drives the pins itself, which is the only arrangement in which `cts=wired` can mean anything at all.

### Reset

**Two different resets live on this card, and calling them both "master reset" is how you end up reading the wrong page of the data sheet.**

**Three things get called a reset here, and only two of them can touch this card. Keeping them apart is the whole of it.**

| | who does it | what it does |
|---|---|---|
| **Master reset** | the **guest**, writing `11` into the divide field | The only thing that resets a 6850. It does **not** clear the control register — *"Master reset does not affect other Control Register bits"* — it latches the whole byte it rode in on (write `0x83` and RIE survives), and it **holds the chip down** until a second control write. Modeled to the letter; see the two notes under the control register. |
| **Bus reset** (`Reset::Bus`, the RESET switch) | the **backplane**, `RESET*` | **Nothing.** The 6850 has no RESET pin. The control register, the word format, RTS, the interrupt enables and any character in the receive register all survive it. |
| **Power-on-clear** (`Reset::PowerOn`, POC\*) | the **power supply** | `Mc6850::powerOn`: zeroes the control register, clears RDRF, empties the receiver, leaves the transmitter ready, asserts RTS. |

- **Neither bus signal unplugs the `ByteStream`.** A guest that reset its UART and found the console gone would be a baffling thing to debug.

> **`Reset::Bus` being a no-op is not laziness — it is the hardware** (see the cross-check under Sources, and DESIGN.md §6.1). This card used to reset both ACIAs on `RESET*`, which lost the guest's configuration *and destroyed a received byte*. `tests/test_sio2.cpp` now pins it: hit RESET with a character in the receiver and RIE armed, and both are still there afterwards.
>
> The one thing we do **not** model literally is power-on-clear's internals. A real 6850 comes up held in an internal reset that only the guest's first master reset releases; we skip the holding — nothing can observe it that does not also program the chip — and simply come up in a known good state at once, so the machine is usable the moment it is switched on.

## Quirks reproduced (and what breaks if you don't)

**TDRE is a deadline, not a flag.** The Python prototype hardwired `TDRE = 1` (always ready to send). That is not merely an approximation:

> The Mike Douglas Altair CP/M BIOS **infers the line speed by timing how long TDRE stays clear**. It counts ~21 µs ticks while polling; if the 16-bit counter's high byte reaches ≥ 11, it concludes **110 baud Teletype** and sets `sndNull`, so a NULL is sent after every CR.

Hardwire TDRE and you **silently change what the guest decides to do**. So a write to the data register records `now + charTime` and the status bit is a *comparison against the clock*, not a stored flag. `tests/test_sio2.cpp` checks that TDRE is still clear one T-state early and set one T-state later.

**The character time follows the word format**, and falls out of the control register the guest wrote — 8N2 is 11 bits on the wire, 7E1 is 10. A guest that configures a Teletype gets a Teletype's timing, and nothing in the code was ever told what a Teletype is.

**Honor the control register.** Master reset (`0x03`) must actually reset — ALTMON's very first two instructions are `MVI A,3 / OUT 10h`, so if that write does nothing, every machine that starts with a master reset starts wrong. The interrupt-enable bits must actually enable interrupts; that is acceptance test 4, and it caught a real bug (above).

**`/CTS` INHIBITS TDRE. It does not merely report.** The data sheet: *"In the high state, the Transmit Data Register Empty bit is inhibited."* And since the transmit interrupt is *derived* from TDRE, a negated CTS inhibits that too — so a card whose far end is not clear-to-send does not spin the guest through an interrupt handler it has nowhere to transmit into. A model that only set status bit 3 would look right and would never actually stop the transmitter, which is the entire function of the pin.

The same bit carries **endpoint backpressure**: a full TCP send buffer or a full serial driver buffer is physically the same situation as a modem holding CTS low, so it lands in the same place and the guest simply *waits*. That keeps §7.1's rule intact — we never manufacture data loss the transport does not have.

**`/DCD` is three behaviors, and only the first is obvious.** The data sheet again:

> *"The DCD input inhibits and initializes the receiver section of the ACIA when high. A low-to-high transition of DCD initiates an interrupt to the MPU to indicate the occurrence of a loss of carrier when the Receive Interrupt Enable bit is set… It remains high after the DCD input is returned low until cleared by first reading the Status Register and then the Data Register, or until a master reset occurs."*

1. **The status bit is LATCHED on the edge** and survives the pin coming back. A guest that was not looking when the line dropped still finds out — which is the whole point.
2. **It raises an INTERRUPT** (a *receive* interrupt: RIE arms RDRF, OVRN **and** DCD). That is how a modem program sitting in a `HLT` learns the call ended.
3. **While the pin is high the receiver is DEAD** — inhibited *and initialized*, and *"Data Carrier Detect being high also causes RDRF to indicate empty."*

Number 3 would never have been guessed. A modem program that checked RDRF after the call dropped would, on a card that merely set a bit, go on cheerfully reading garbage out of the receive register.

**The clear is two steps — status, then data — and the data read that clears the latch is the same read that takes the character.** There is only one data register. (A test here originally asserted the received byte *survived* the acknowledge sequence; the test was wrong, not the chip.) If the pin is still high afterwards, the interrupt clears but **the bit stays set and thereafter follows the pin**. And a master reset does *not* put the carrier back: *"clears the Status Register (except for external conditions on CTS and DCD)"* — a reset button on the front panel does not dial the phone.

## Limitations

- Baud is a board jumper on real hardware, so `baud` is a property rather than a register. Software cannot change it, which is correct.
- **The clock divide ratio (CR1:CR0 = ÷1 / ÷16 / ÷64) is decoded but not honored.** We act on `11` (master reset) and ignore the other three: `baud` is the rate *on the wire*, so a guest that reprograms ÷16 to ÷64 gets no change where a real card would slow down fourfold. This is a **divergence from the data sheet**, kept deliberately, because closing it needs a fact the data sheet does not have. The 6850 divides a clock it is *given*, and what the 88-2SIO gives it is a **jumper**; to honor the ratio, `baud` would have to stop meaning "the line rate" and start meaning "the crystal", and the conversion between the two is in the 2SIO manual's strapping table, not in the chip's data sheet. Guessing at it would be exactly the invented number §0.1 forbids. No period software has been observed to care — a driver picks a ratio once, at init, and the jumper was cut to match.
- **The 6850 has no DTR pin and no RI pin.** It has `/CTS`, `/DCD` and `RTS`, and that is all — so **this card cannot hang up a phone and cannot hear one ring**, and no amount of wanting it to changes what is soldered to the chip. The `ByteStream` layer carries DTR and RI because the **PMMI** has both (DTR *is* its hang-up, and it counts ring bursts to answer), and a `socket:` endpoint implements the hangup — but nothing in the machine drives DTR today. The data sheet notes RTS *"can also be used for Data Terminal Ready"*, i.e. a card **may** wire it that way; the 88-2SIO's hardwire table is not in front of us, and §0.1 says **ask, do not reason**. So it is not modeled.
- **A `socket:` endpoint auto-answers; RI is always false.** Ringing would mean *not* answering until the card raises DTR — and since no card in the machine has a DTR pin, every socket would sit there ringing forever, unanswerable. The PMMI is the card that will make a ring mean something.
- Parity and framing errors are not synthesized. They report **line noise**, and there is no line: a `ByteStream` delivers the byte that was sent or it delivers nothing. Synthesizing them would mean inventing a noise model, which would mean inventing a probability — the kind of number §0.1 says to *ask* about rather than make up. (A **real serial port** genuinely has framing and parity errors, and that is a real hardware event a `HostSerialStream` could one day report — from the place that actually knows, which is not here.)

### OVRN is never set, and that is a correction

**An earlier version of this document said OVRN was modeled. It was, it was wrong, and it has been removed.** The reasoning is worth keeping, because the mistake is an easy one:

A real 6850 overruns because a serial line is **free-running** — the sender clocks bits down the wire whether or not the receiver is keeping up, and a character that arrives while the last one is still unread is lost. So the first implementation pulled a byte off the stream every character-time regardless of `RDRF`, raised OVRN, and dropped it.

**It lost data immediately.** ALTMON echoes the full command name as you type (`D` → `DUMP `), and while it was busy transmitting those five characters it was not reading the receiver — so the address typed after the `D` went on the floor and the dump silently did nothing.

The bug was not the pacing. **It was believing a `ByteStream` is a serial line.** It is not: it is a buffered, flow-controlled source — a pipe, a socket, an OS keyboard queue — and it will happily hold the byte until we take it. Inventing an overrun from it does not reproduce a hardware behaviour; it *manufactures* data loss the host transport does not have, and it breaks transfers that would have worked on the real thing.

So the receiver is still **paced at the baud rate** — that part is real, it is what stops a guest reading faster than the line allows, and it is the same clock TDRE is timed against — but it only ever takes a byte when the register is free. Status bit 5 is therefore always zero.

If a **host serial port** endpoint ever lands, an overrun there is a genuine hardware event and the stream can report one — from the place that actually knows, which is not this board.

## Verification (milestone 1 acceptance)

| | | |
|---|---|---|
| 1 | 4K MITS BASIC answers `PRINT 2+2` on the console | **not yet** — ALTMON runs instead, which proves the same path |
| 2 | Same session over a **TCP socket** | **DONE** — `socket:` lands; `tests/test_lines.cpp` runs a real client over real TCP |
| 3 | Same over a **host serial port** | **DONE, ON REAL HARDWARE** — `tests/serialtest.cpp`, below |
| 4 | An **interrupt-driven** console echo, with **no VI board** | **DONE** — `tests/test_sio2.cpp` |
| 5 | **Two 2SIO boards at once**, four channels, independently configured | **DONE** — the base port is a jumper, and two cards coexist |
| 6 | The card raises an interrupt **while nobody is asking it anything** | **DONE** — the guest halts; the card's own deadline wakes it |

### The modem-control acceptance is a CABLE, and it is tested with one

Everything else in this project is provable with a `ScriptedStream` and a `MemoryMedia`, and should be. **Whether RTS on one card actually raises CTS on another is a question about a cable**, and the only honest way to answer it is to put a volt down one.

So `tests/serialtest.cpp` is real hardware, opt-in, and it skips loudly when the hardware is absent — *a hardware test that quietly passes with no hardware is a green tick that means nothing.* Two USB serial adapters, a null modem between them (Patrick, 2026-07-12):

```
ALTAIR_SERIAL_A=/dev/tty.usbserial-AL009KFH \
ALTAIR_SERIAL_B=/dev/tty.usbserial-AB0NW409 ctest -L hw
```

A null modem crosses `A RTS → B CTS` and `A DTR → B DSR + B DCD`. So **the far end raising DTR is a carrier appearing at the card** — not an analogy: to a 6850 strapped `dcd=wired`, it is indistinguishable from a modem. The test drives one end by hand and puts a **real 2SIO in a real backplane** on the other, and asserts, across the wire:

- the guest's `OUT 11h` comes out of the cable as bytes;
- a byte typed into the cable raises RDRF **and pulls pin 73**;
- the far end dropping **RTS** inhibits TDRE — *the emulated transmitter stops because a physical pin went low*;
- the far end dropping **DTR** is a carrier loss: **latched**, interrupting, and cleared only by status-then-data.

It passed on the first run, which is suspicious enough to be worth checking — so it was also run with the far end pointed at a **different, unconnected port**, where it collapses into twelve failures. The green run is real.

### Test 4 is the one that mattered, and it found a real bug

It is the acceptance test the whole interrupt design stands on: the 6850 raises IRQ because a character arrived, the jumper takes it to `pINT`, the 8080 acknowledges at an instruction boundary, **nobody claims the `IntAck` cycle** because there is no 88-VI card in the machine, the bus floats to `0xFF`, and `0xFF` is `RST 7`. The vector is not *chosen* by anything — it is what an empty backplane reads as.

The first implementation **failed it**, and the failure is worth recording:

> The ACIA only took a character off the line when the guest **read a register**. But an interrupt-driven driver *never reads the status port* — not reading it is the entire point of being interrupt-driven. So `RDRF` was never set, IRQ never rose, and the interrupt never fired. The operator could type forever and nothing would happen.

A 6850's receive shift register fills **on the chip's own clock** and owes the CPU nothing. The interface has to be able to say so. Every *polled* test in the suite passed throughout — only this one could have caught it.

### Test 6 is where that fix turned out to be half a fix

**The card's free-running work was real. Putting it inside `assertsInt()` was not.**

The receiver was advanced inside that query for exactly one reason: being asked was the only thing that ever woke the card up. The bus called `assertsInt()` on every instruction, and the card quietly used the poll as its clock. It worked, and it was the wrong shape — no backplane interrogates a card for its interrupt status (**DESIGN.md §4.4.1**, and Patrick, who said so). A card **pulls pin 73 and holds it**.

Take the poll away and the hole is obvious:

> The transmit interrupt is jumpered. The guest writes a character and **halts**. TDRE goes true when the character has finished going out — and at that moment **nobody is touching the chip.** No bus cycle runs. No register is read. The CPU is parked in a `HLT` waiting for exactly this interrupt.
>
> If the only way the card can act is to *be asked*, and the only thing that would ask is the CPU that is halted waiting for it, **the machine is dead.**

That is an entirely ordinary driver, and the old run loop **declared it finished** — two thousand T-states before the interrupt it was waiting for. So the card now owns its own clock:

- **`Mc6850::nextEdge()`** — the next moment this chip's IRQ pin could move *with nobody touching it*. The card sets a `Clock` deadline for it (**§7.5**) and wakes itself. On a quiet line with an idle transmitter the answer is **"never"**, and no timer is set at all — which is the commonest state in the machine, and precisely the one the poll was paying full price for.
- **`pump()`** — for the thing a deadline cannot predict: a human touching a key. Nothing in emulated time saw that coming, so no timer could have been set for it.

Both, and they are not alternatives — they answer different questions. It is the same function on this card (`Sio2Board::refresh()`), and it is the answer to *"event queue, or periodic timer?"*: **both, and you already had the second one.**

`assertsInt()` is now `const` and **pure**: it reads two pins and ORs them, filtered by what is actually soldered to the wire. It does no work. The card tells the backplane when the pin moves (`Board::intChanged()`), and forgetting to is caught by `Bus::setVerify(true)`, which this suite runs with permanently on.
