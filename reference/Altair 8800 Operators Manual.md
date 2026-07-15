# Altair 8800 Operator's Manual

Source: [Altair 8800 Operators Manual.pdf](#)

> MITS, Inc., 1975. Reference extract for the **altairsim** simulator. This file
> captures everything in the manual relevant to emulating the Altair 8800 and its
> front panel: switches, LEDs, status bits, operating procedures, the bus, the
> memory map, I/O conventions, toggle-in programs, and the full 8080 instruction
> set with octal codes. Marketing copy, order forms, warranty, and the number-
> system tutorial in Part 1 are omitted. Octal and binary values are preserved
> exactly as printed (binary is grouped in the octal-friendly `xx xxx xxx`
> form the manual uses). Page numbers refer to the printed manual.

---

## 1. Machine Organization (Part 2)

Block diagram (Fig 2-1): a **CLOCK** feeds the **CPU**; the CPU connects to
**MEMORY**, **INPUT**, and **OUTPUT** via a 16-bit **ADDRESS BUS** and an 8-bit
**DATA BUS**.

- **CPU**: Intel 8080, 40-pin. Executes a complete instruction cycle in ~2 µs
  (about 30,000 six-instruction addition programs per second). 78 machine
  instructions; >200 variants when register/pair fields are counted.
- **Clock**: 2 MHz crystal-controlled. Crystal control keeps the clock from
  exceeding the CPU's maximum permissible speed. (Note for altairsim: the sim's
  free-running default is a simulator policy, not hardware — hardware is 2 MHz.)
- **Memory**: external to the CPU, expandable to **65,536** 8-bit words. Basic
  RAM board holds up to eight 256×4-bit RAMs. Any memory works if bus loading
  ≤ 50 TTL loads and the bus is driven by standard TTL loads.
- **Address bus**: 16 lines (A15–A0). Addresses transferred as two sequential
  8-bit bytes.
- **Data bus**: 8 lines (D7–D0), parallel.
- **I/O**: services up to **256 input** and **256 output** devices. Interrupts
  can be enabled/ignored under program control.
- **DMA**: a Direct Memory Access Controller can take the address and data lines
  from the CPU for block transfers (memory-to-memory or memory-to-device).

### CPU signals (Fig 2-2)

- Inputs / clocks: `Ø1`, `Ø2`, `READY`, `INT`, `RESET`, `HOLD`.
- Status/handshake outputs: `INTE`, `HLDA`, `DBIN`, `SYNC`, `WR`, `WAIT`.
- Data pins `D7–D0`; address drivers `A15–A0` (16).
- Internal: Timing & Control, Instruction Decode, Instruction Register,
  Register array (Temp W/Z, B/C, D/E, H/L, SP, PC), incrementer/decrementer,
  address latch (16), ALU (8), accumulator + accumulator latch (8), decimal
  arithmetic, flag register (5), I/O buffer & latch.

The Timing and Control system drives these front-panel status indicators:
`HOLD, WAIT, INTE, STACK, OUT, IN(P), M1, MEMR, HLTA, WO, INT` (p.21).

### Registers (Fig 2-3)

Seven 8-bit working registers plus the status/flag register, arranged in pairs:

| Pair | High | Low |
|------|------|-----|
| B    | B    | C   |
| D    | D    | E   |
| H    | H    | L   |
| PSW  | *(Status Bit Register)* | A (Accumulator) |

- **Accumulator (A)**: primary result register; target of most arithmetic/logic.
- **Program Counter (PC)**: 16-bit, address of next instruction; auto-advances;
  accessible via JMP/CALL/RETURN.
- **Stack Pointer (SP)**: 16-bit; points at the stack (a reserved memory region).
  PUSH/POP and subroutine CALL/RETURN use it. Loaded via LXI.
- In register-pair addressing, **H is the most-significant 8 bits, L the least.**

### Status Bit Register — the 5 flags (p.23)

| Flag | Set (=1) when |
|------|---------------|
| **Carry (C)**    | A carry out occurred (add, subtract, rotate, some logic). Reset if no carry. |
| **Auxiliary Carry (AC)** | Carry out of bit 3 of the result. Affected only by DAA (per manual). |
| **Sign (S)**     | Result is minus — reflects bit 7 (MSB). 0 = plus. (8-bit signed range −128..+127.) |
| **Zero (Z)**     | Result is zero; reset to 0 when result > zero. |
| **Parity (P)**   | Set for an **even** number of 1 bits; reset for odd. |

### PSW byte layout (pushed by PUSH PSW, p.50–51)

The flags byte (register pair PSW, high byte) has this fixed bit format:

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Contents | Sign | Zero | 0 | Aux Carry | 0 | Parity | 1 | Carry |

Example: Carry=1, all others 0 → `00 000 011`.

---

## 2. Front Panel — Switches (Part 3, p.29–30)

The panel has **25 toggle switches** and **36 indicator/status LEDs**. Routine
256-word operation needs only 15 switches and 16 LEDs.

| Switch | Positions | Function |
|--------|-----------|----------|
| **ON/OFF** | ON / OFF | ON applies power. **OFF cuts power AND erases memory.** |
| **STOP / RUN** | momentary up/down | STOP halts program execution; RUN begins execution. |
| **SINGLE STEP** | momentary | Executes one machine-language instruction per actuation. One instruction may take up to 5 machine cycles. |
| **EXAMINE / EXAMINE NEXT** | momentary | EXAMINE displays the contents of the address preset in the ADDRESS switches on the DATA LEDs. EXAMINE NEXT shows the next sequential address's contents; repeat to walk memory. |
| **DEPOSIT / DEPOSIT NEXT** | momentary | DEPOSIT writes the 8 DATA switches into the currently-addressed location. DEPOSIT NEXT advances to the next sequential address first, then writes. Data switches may be changed before actuating. |
| **RESET / CLR** | momentary | RESET sets the Program Counter to address 0 (`0 000 000 000 000 000`) — a fast way back to the program's first step. CLR is a CLEAR command for external I/O equipment. |
| **PROTECT / UNPROTECT** | momentary | PROTECT prevents memory contents from being changed; UNPROTECT allows alteration. |
| **AUX (×2)** | — | Two auxiliary switches, **not connected** in the basic machine; reserved for future peripherals. |

### DATA / ADDRESS switches

- **DATA switches** = the switches designated **7–0** (8 bits).
- **ADDRESS switches** = the switches designated **15–0** (16 bits).
- Switch **UP = 1 bit**, switch **DOWN = 0 bit**.
- On a basic 256-word machine, ADDRESS switches **8–15 are unused** and should be
  set to 0 when entering an address.

> Note for altairsim: this original manual documents only the two unconnected
> AUX switches. On later Altair hardware the high address switches (A8–A15)
> double as sense switches read at an input port. altairsim implements sense on
> the front-panel card at port FF — see the project's front-panel memory, not
> this manual.

---

## 3. Front Panel — Indicator LEDs (p.31)

> When the machine is **stopped**, a glowing LED = 1 bit / active status; dark =
> 0 bit / inactive. **While running, LED indications may be erroneous.**

| LED group | Meaning |
|-----------|---------|
| **ADDRESS (A15–A0)** | Bit pattern = the memory address being examined or loaded. |
| **DATA (D7–D0)** | Bit pattern = the data in the specified memory address. |
| **INTE** | Glowing = an interrupt has been enabled. |
| **PROT** | Glowing = memory is protected. |
| **WAIT** | Glowing = the CPU is in a WAIT state. |
| **HLDA** | Glowing = a HOLD has been acknowledged. |

---

## 4. Status LEDs (p.32)

> A glowing LED = active status for the designated condition.

| LED | Definition |
|-----|------------|
| **MEMR** | The memory bus will be used for memory read data. |
| **INP** | The address bus holds the address of an input device; input data should be placed on the data bus when the data bus is in the input mode. |
| **M1** | The CPU is processing the **first machine cycle** of an instruction. |
| **OUT** | The address bus holds an output device address; the data bus will contain output data when the CPU is ready. |
| **HLTA** | A HALT instruction has been executed and acknowledged. |
| **STACK** | The address bus holds the Stack Pointer's pushdown-stack address. |
| **WO** | The current machine cycle is a WRITE (memory) or OUTPUT function. Otherwise a READ (memory) or INPUT operation occurs. (WO is active-low in meaning: "write/output when this condition applies.") |
| **INT** | An interrupt request has been acknowledged. |

These 8 status bits are the 8080 status word latched at the start of each
machine cycle (SYNC). altairsim's bus/CPU layer should present them on the panel
exactly as above.

---

## 5. Memory, Addressing, and Memory Map

### Memory map convention (p.39)

Machine-language operation requires the programmer to track memory by hand.
A typical 256-word map: programs in the first 100 (decimal) words, subroutines
in the second 100 words, data in the remaining 56 words. Blocks are arbitrary;
the point is a consistent organization. RST vectors live in the lower 64 words.

### Addressing modes (p.40)

1. **Direct** — the two bytes following the opcode supply the address (STA, LDA,
   SHLD, LHLD, JMP, CALL, conditional jumps/calls). Byte 2 = low, byte 3 = high.
2. **Register-pair** — a pair holds the address. H/L for most instructions
   (H = high 8 bits, L = low 8 bits). STAX/LDAX allow B/C or D/E to hold the
   address.
3. **Stack-pointer** — PUSH/POP only. The programmer must reserve the stack and
   load SP via LXI.
4. **Immediate** — data is part of the instruction stream (loaded with the
   program). No memory-map bookkeeping needed.
5. **Subroutine stack addressing** — CALL auto-PUSHes the return address; RETURN
   POPs it.

### Stack behavior

- **PUSH**: first register (high) → (SP−1); second register (low) → (SP−2);
  then SP ← SP−2.
- **POP**: second register (low) ← (SP); first register (high) ← (SP+1);
  then SP ← SP+2.

---

## 6. I/O Addressing Conventions

- **IN** (`11 011 011`, octal **333**): byte 2 = device number (0–255). Loads one
  byte from the device into A. Status bits unaffected.
- **OUT** (`11 010 011`, octal **323**): byte 2 = device number. Sends A to the
  device. Status bits unaffected.
- Up to 256 input and 256 output device addresses.
- The **INP** status LED marks an input machine cycle; **OUT** marks an output
  cycle; **WO** distinguishes write/output from read/input.

---

## 7. Operating Procedures

### 7.1 Loading a program (p.36)

1. Actuate **RESET** — PC ← address 0.
2. Set the first byte on **DATA switches 7–0**; actuate **DEPOSIT** (writes to 0).
3. Set the next byte; actuate **DEPOSIT NEXT** (auto-advances to address 1, writes).
4. Repeat step 3 for each subsequent byte; the address auto-increments.

### 7.2 Loading data at a chosen address (p.38)

- Sequential: set ADDRESS switches to the first data address, actuate **EXAMINE**,
  set DATA switches, **DEPOSIT**; then **DEPOSIT NEXT** for each following address.
- Non-sequential: for each address, set ADDRESS switches → **EXAMINE**, then set
  DATA switches → **DEPOSIT**.

### 7.3 Running

- Actuate **RESET**, then **RUN**. Wait, then actuate **STOP**.
- To read a result: set ADDRESS switches to the result address, actuate
  **EXAMINE**; result appears on the DATA LEDs.

### 7.4 Single-step debugging (p.41)

- **SINGLE STEP** advances one *machine cycle* (not a full instruction), letting
  you watch the 8 STATUS LEDs to catch illegal entries and bad program flow.

### 7.5 Proofreading (p.41)

- RESET (or set address + EXAMINE) to the start; check DATA LEDs against byte 0;
  **EXAMINE NEXT** through the program. To fix a byte: set DATA switches,
  **DEPOSIT**, continue with **EXAMINE NEXT**.

### 7.6 NOP padding (p.41)

- Scatter **NOP** (`000`) through a program; later replace with a needed
  instruction during proofreading. Reserve enough NOPs for a multi-byte
  instruction (e.g. 3 NOPs to make room for an LDA).

---

## 8. Toggle-In Programs

### 8.1 Sample addition program (p.34–37)

Adds the byte at address 128 (`10 000 000`) to the byte at 129 (`10 000 001`),
stores the sum at 130 (`10 000 010`), then jumps back to 0 to loop.

| Step | Mnemonic | Binary | Octal |
|------|----------|--------|-------|
| 0  | LDA        | `00 111 010` | 072 |
| 1  | (addr low) | `10 000 000` | 200 |
| 2  | (addr high)| `00 000 000` | 000 |
| 3  | MOV (A→B)  | `01 000 111` | 107 |
| 4  | LDA        | `00 111 010` | 072 |
| 5  | (addr low) | `10 000 001` | 201 |
| 6  | (addr high)| `00 000 000` | 000 |
| 7  | ADD (B+A)  | `10 000 000` | 200 |
| 8  | STA        | `00 110 010` | 062 |
| 9  | (addr low) | `10 000 010` | 202 |
| 10 | (addr high)| `00 000 000` | 000 |
| 11 | JMP        | `11 000 011` | 303 |
| 12 | (addr low) | `00 000 000` | 000 |
| 13 | (addr high)| `00 000 000` | 000 |

Load sequence: RESET, DEPOSIT step 0, then DEPOSIT NEXT for steps 1–13. Then
load operands at 128/129 and run (RESET, RUN, STOP); EXAMINE 130 for the result.

### 8.2 Sample binary-multiply program (p.38a)

Multiplier in A, multiplicand in D/E, result stored at locations 100,101 (octal).
Addresses and octal object code exactly as printed:

| Mnemonic | Addr (octal) | Octal code | Comment |
|----------|--------------|------------|---------|
| MVI A | 000 / 001 | 076 / 002 | Multiplier → A |
| MVI D | 002 / 003 | 026 / 003 | Multiplicand → D,E |
| MVI E | 004 / 005 | 036 / 000 | |
| LXI H | 006 / 007 / 010 | 041 / 000 / 000 | Clear H,L (partial product) |
| MVI B | 011 / 012 | 006 / 010 | Iteration count → B |
| DAD H | 013 | 051 | Shift partial product left into carry |
| RAL   | 014 | 027 | Rotate multiplier bit into carry |
| JNC   | 015 / 016 / 017 | 322 / 023 / 000 | Test multiplier at carry |
| DAD D | 020 | 031 | Add multiplicand to partial product if carry=1 |
| ACI   | 021 / 022 | 316 / 000 | |
| DCR B | 023 | 005 | Decrement iteration counter |
| JNZ   | 024 / 025 / 026 | 302 / 013 / 000 | Check iterations |
| SHLD  | 027 / 030 / 031 | 042 / 100 / 000 | Store answer at 100,101 |
| JMP   | 032 / 033 / 034 | 303 / 000 / 000 | Restart |

---

## 9. Instruction Set (Part 4 + Appendix)

Register field encodings (`SSS`, `DDD`, `reg`):

| Register | Bits |
|----------|------|
| B | 000 |
| C | 001 |
| D | 010 |
| E | 011 |
| H | 100 |
| L | 101 |
| Memory (M, via H/L) | 110 |
| A (Accumulator) | 111 |

Register-pair field (`rp`):

| Pair | Bits |
|------|------|
| B & C | 00 |
| D & E | 01 |
| H & L | 10 |
| SP (or PSW for PUSH/POP) | 11 |

> M (110) means the memory byte addressed by H/L. For MOV, source and
> destination cannot both be 110. For STAX/LDAX only B/C (X=0) and D/E (X=1)
> are valid.

### 9.A Command instructions

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags | Notes |
|----------|-------|--------|--------|-------|-------|-------|
| IN  | 2 | 3 | `11 011 011` | 333 | — | byte2 = device #; device→A |
| OUT | 2 | 3 | `11 010 011` | 323 | — | byte2 = device #; A→device |
| EI  | 1 | 1 | `11 111 011` | 373 | — | Set interrupt flip-flop |
| DI  | 1 | 1 | `11 110 011` | 363 | — | Reset interrupt flip-flop |
| HLT | 1 | 1 | `01 110 110` | 166 | — | PC→next, stop until interrupt. (After DI, only power-cycle restarts.) |
| RST | 1 | 3 | `11 (exp) 111` | 3(exp)7 | — | PUSH PC; jump to `00 000 000 00 (exp) 000`, exp = 000..111 (lower 64 words). |
| CMC | 1 | 1 | `00 111 111` | 077 | Carry | Complement carry |
| STC | 1 | 1 | `00 110 111` | 067 | Carry | Set carry = 1 |
| NOP | 1 | 1 | `00 000 000` | 000 | — | No operation |

### 9.B Single-register instructions

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags |
|----------|-------|--------|--------|-------|-------|
| INR | 1 | 3 | `00 DDD 100` | 0(DDD)4 | Z, S, P, AC |
| DCR | 1 | 3 | `00 DDD 101` | 0(DDD)5 | Z, S, P, AC |
| CMA | 1 | 1 | `00 101 111` | 057 | — | Complement A |
| DAA | 1 | 1 | `00 100 111` | 047 | Z, S, P, C, AC |

DAA: if low nibble > 9 or AC=1, add 6 to low nibble; if high nibble > 9 or C=1
(after step 1), add 6 to high nibble.

### 9.C Register-pair instructions

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags |
|----------|-------|--------|--------|-------|-------|
| PUSH | 1 | 3 | `11 (rp)0 101` | 3(rp)5 | — |
| POP  | 1 | 3 | `11 (rp)0 001` | 3(rp)1 | — (PSW: all, if rp=PSW) |
| DAD  | 1 | 3 | `00 (rp)1 001` | 0(rp)1 | Carry |
| INX  | 1 | 1 | `00 (rp)0 011` | 0(rp)3 | — |
| DCX  | 1 | 1 | `00 (rp)1 011` | 0(rp)3 | — |
| XCHG | 1 | 1 | `11 101 011` | 353 | — | H/L ↔ D/E |
| XTHL | 1 | 5 | `11 100 011` | 343 | — | L↔(SP), H↔(SP+1) |
| SPHL | 1 | 1 | `11 111 001` | 371 | — | SP ← H/L |

> Note: the Appendix prints INX and DCX both as octal `0(rp)3`; they differ in
> the binary (bit 3): INX `00 (rp)0 011`, DCX `00 (rp)1 011`. Trust the binary.

### 9.D Rotate-accumulator instructions

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags | Operation |
|----------|-------|--------|--------|-------|-------|-----------|
| RLC | 1 | 1 | `00 000 111` | 007 | Carry | A left; bit7→bit0 and →Carry |
| RRC | 1 | 1 | `00 001 111` | 017 | Carry | A right; bit0→bit7 and →Carry |
| RAL | 1 | 1 | `00 010 111` | 027 | Carry | A left through Carry; bit7→Carry, Carry→bit0 |
| RAR | 1 | 1 | `00 011 111` | 037 | Carry | A right through Carry; bit0→Carry, Carry→bit7 |

### 9.E Data-transfer instructions

**Move / indirect:**

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags |
|----------|-------|--------|--------|-------|-------|
| MOV  | 1 | 1 or 2 | `01 DDD SSS` | 1(DDD)(SSS) | — |
| STAX | 1 | 2 | `00 0X0 010` | 0(X)2 | — | A→(BC) if X=0, (DE) if X=1 |
| LDAX | 1 | 2 | `00 0X1 010` | 0(X)2 | — | (BC)→A if X=0, (DE)→A if X=1 |

> STAX/LDAX share octal `0(X)2`; they differ in bit 3 of the binary (STAX
> `00 0X0 010`, LDAX `00 0X1 010`). Trust the binary.

**Register/Memory → Accumulator (arithmetic & logic), all `10 xxx SSS`:**

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags | Operation |
|----------|-------|--------|--------|-------|-------|-----------|
| ADD | 1 | 1 | `10 000 SSS` | 20(SSS) | C,S,Z,P,AC | A ← A + reg |
| ADC | 1 | 1 | `10 001 SSS` | 21(SSS) | C,S,Z,P,AC | A ← A + reg + Carry |
| SUB | 1 | 1 | `10 010 SSS` | 22(SSS) | C,S,Z,P,AC | A ← A − reg (two's comp; Carry=1 means borrow) |
| SBB | 1 | 1 | `10 011 SSS` | 23(SSS) | C,S,Z,P,AC | A ← A − (reg + Carry) |
| ANA | 1 | 1 | `10 100 SSS` | 24(SSS) | C,Z,S,P | A ← A AND reg; Carry reset to 0 |
| XRA | 1 | 1 | `10 101 SSS` | 25(SSS) | C,S,Z,P | A ← A XOR reg; Carry reset to 0 |
| ORA | 1 | 1 | `10 110 SSS` | 26(SSS) | C,Z,S,P | A ← A OR reg; Carry reset to 0 |
| CMP | 1 | 1 | `10 111 SSS` | 27(SSS) | C,S,Z,P | Compare (A − reg), operands unchanged. Z=1 if equal; Carry set if reg > A. Carry sense reverses if signs differ. |

**Direct addressing (3 bytes: op, low, high):**

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags |
|----------|-------|--------|--------|-------|-------|
| STA  | 3 | 4 | `00 110 010` | 062 | — | (addr) ← A |
| LDA  | 3 | 4 | `00 111 010` | 072 | — | A ← (addr) |
| SHLD | 3 | 5 | `00 100 010` | 042 | — | (addr) ← L, (addr+1) ← H |
| LHLD | 3 | 5 | `00 101 010` | 052 | — | L ← (addr), H ← (addr+1) |

### 9.F Immediate instructions

| Mnemonic | Bytes | Cycles | Binary | Octal | Flags | Operation |
|----------|-------|--------|--------|-------|-------|-----------|
| LXI | 3 | 3 | `00 (rp)0 001` | 0(rp)1 | — | byte2→low reg, byte3→high reg (reversed for SP: byte2=low, byte3=high of SP) |
| MVI | 2 | 2 or 3 | `00 SSS 110` | 0(SSS)6 | — | reg/M ← byte2 |
| ADI | 2 | 2 | `11 000 110` | 306 | C,S,Z,P,AC | A ← A + data |
| ACI | 2 | 2 | `11 001 110` | 316 | C,S,Z,P,AC | A ← A + data + Carry |
| SUI | 2 | 2 | `11 010 110` | 326 | C,S,Z,P,AC | A ← A − data (Carry=1 = borrow) |
| SBI | 2 | 2 | `11 011 110` | 336 | C,S,Z,P,AC | A ← A − (data + Carry) |
| ANI | 2 | 2 | `11 100 110` | 346 | C,S,Z,P | A ← A AND data; Carry reset |
| XRI | 2 | 2 | `11 101 110` | 356 | C,S,Z,P | A ← A XOR data; Carry reset |
| ORI | 2 | 2 | `11 110 110` | 366 | C,S,Z,P | A ← A OR data; Carry reset |
| CPI | 2 | 2 | `11 111 110` | 376 | C,Z,S,P,AC | Compare A − data; operands unchanged |

> LXI: byte 1 field bits 4–5 select the pair. Example `00 010 001` = LXI D:
> byte2→D, byte3→E.

### 9.G Branching instructions

**Jumps** (3 bytes except PCHL; byte2 = low addr, byte3 = high addr):

| Mnemonic | Bytes | Cycles | Binary | Octal | Condition |
|----------|-------|--------|--------|-------|-----------|
| PCHL | 1 | 1 | `11 101 001` | 351 | Unconditional; PC ← H/L |
| JMP  | 3 | 3 | `11 000 011` | 303 | Unconditional |
| JC   | 3 | 3 | `11 011 010` | 332 | Carry = 1 |
| JNC  | 3 | 3 | `11 010 010` | 322 | Carry = 0 |
| JZ   | 3 | 3 | `11 001 010` | 312 | Zero = 1 |
| JNZ  | 3 | 3 | `11 000 010` | 302 | Zero = 0 |
| JM   | 3 | 3 | `11 111 010` | 372 | Sign = 1 (minus) |
| JP   | 3 | 3 | `11 110 010` | 362 | Sign = 0 (positive) |
| JPE  | 3 | 3 | `11 101 010` | 352 | Parity = 1 (even) |
| JPO  | 3 | 3 | `11 100 010` | 342 | Parity = 0 (odd) |

**Calls** (3 bytes; byte2 = low, byte3 = high):

| Mnemonic | Bytes | Cycles | Binary | Octal | Condition |
|----------|-------|--------|--------|-------|-----------|
| CALL | 3 | 5 | `11 001 101` | 315 | Unconditional |
| CC   | 3 | 3 or 5 | `11 011 100` | 334 | Carry = 1 |
| CNC  | 3 | 3 or 5 | `11 010 100` | 324 | Carry = 0 |
| CZ   | 3 | 3 or 5 | `11 001 100` | 314 | Zero = 1 |
| CNZ  | 3 | 3 or 5 | `11 000 100` | 304 | Zero = 0 |
| CM   | 3 | 3 or 5 | `11 111 100` | 374 | Sign = 1 (minus) |
| CP   | 3 | 3 or 5 | `11 110 100` | 364 | Sign = 0 (plus) |
| CPE  | 3 | 3 or 5 | `11 101 100` | 354 | Parity = 1 (even) |
| CPO  | 3 | 3 or 5 | `11 100 100` | 344 | Parity = 0 (odd) |

**Returns** (1 byte; last bit = 1 unconditional, 0 conditional):

| Mnemonic | Bytes | Cycles | Binary | Octal | Condition |
|----------|-------|--------|--------|-------|-----------|
| RET | 1 | 3 | `11 001 001` | 311 | Unconditional |
| RC  | 1 | 1 or 3 | `11 011 000` | 330 | Carry = 1 |
| RNC | 1 | 1 or 3 | `11 010 000` | 320 | Carry = 0 |
| RZ  | 1 | 1 or 3 | `11 001 000` | 310 | Zero = 1 |
| RNZ | 1 | 1 or 3 | `11 000 000` | 300 | Zero = 0 |
| RM  | 1 | 1 or 3 | `11 111 000` | 370 | Sign = 1 (minus) |
| RP  | 1 | 1 or 3 | `11 110 000` | 360 | Sign = 0 (plus) |
| RPE | 1 | 1 or 3 | `11 101 000` | 350 | Parity = 1 (even) |
| RPO | 1 | 1 or 3 | `11 100 000` | 340 | Parity = 0 (odd) |

CALL auto-PUSHes the return address (address of the next sequential
instruction); RET/conditional-return POPs it into PC.

---

## 10. Quick Fact Sheet for the Simulator

- Word size 8 bits; address space 16 bits (0–65535); basic machine 256 words.
- Switch UP = 1, DOWN = 0. RESET → PC=0. OFF erases memory.
- DEPOSIT writes the DATA switches at the current address; DEPOSIT NEXT
  pre-increments the address.
- EXAMINE shows [ADDRESS switches]; EXAMINE NEXT pre-increments.
- SINGLE STEP = one machine cycle (up to 5 per instruction).
- Panel LEDs are only reliable when stopped.
- Status word bits (glowing = active): MEMR, INP, M1, OUT, HLTA, STACK, WO, INT.
- Indicator LEDs: INTE (interrupts enabled), PROT (protected), WAIT, HLDA.
- I/O: IN = octal 333, OUT = octal 323, byte2 = device 0–255.
- RST n vectors to `00 000 000 00 nnn 000` (0..7 × 8) in the low 64 words.
- H = high byte, L = low byte of a 16-bit register-pair address.
- Clock is 2 MHz crystal (hardware). 78 instructions; octal codes above.
