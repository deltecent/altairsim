# Zilog Z80 CPU

Source: [Zilog Z80.pdf](#)

The Zilog Z80 CPU (Zilog User Manual UM0080 / UM008011-0816) is an 8-bit
microprocessor that is a strict binary superset of the Intel 8080A: every 8080
opcode runs unchanged (those opcodes are shaded in the manual's op-code
tables), and the Z80 adds the alternate register bank, the IX/IY index
registers, the CB/ED/DD/FD prefix groups, relative jumps, block move/search/IO,
and three interrupt modes. In S-100 / Altair systems the Z80 drops into an
8080 socket-compatible CPU card and is treated as an 8080-superset CPU. This
file distills the register/flag model, addressing modes, the complete opcode
maps (with byte length, T-states and flags), the interrupt system and reset
behavior — everything an emulator core or disassembler reopens the manual for.

Note on undocumented flags: this manual (UM0080) explicitly labels flag-register
bits 5 and 3 as "X = Not Used" (Table 21/22) and does **not** describe the
real-silicon behavior where bits 5/3 copy result bits 5/3 (the "YF/XF" or
"F5/F3" undocumented flags). Those are recorded below as the manual states them
(unused); an emulator targeting ZEXALL must implement the real copy behavior,
which this manual does not specify.

## Register Set

The programmer-visible state is 208 bits: a main register set, a duplicate
("alternate", primed) set, and six special-purpose registers.

### Main / alternate register file

| Main | Alt | Width | Notes |
|---|---|---|---|
| A | A' | 8 | Accumulator |
| F | F' | 8 | Flag register (see below) |
| B, C | B', C' | 8 each | pair **BC** |
| D, E | D', E' | 8 each | pair **DE** |
| H, L | H', L' | 8 each | pair **HL** (default 16-bit memory pointer) |

BC/DE/HL usable individually as 8-bit or in pairs as 16-bit. `AF` groups the
accumulator with its flags.

### Special-purpose registers

| Reg | Width | Purpose |
|---|---|---|
| PC | 16 | Program Counter — address of the instruction being fetched |
| SP | 16 | Stack Pointer — top of the external LIFO stack |
| IX | 16 | Index register (base for `(IX+d)` indexed addressing) |
| IY | 16 | Index register (base for `(IY+d)` indexed addressing) |
| I  | 8 | Interrupt vector page — high 8 bits of the Mode 2 vector address |
| R  | 8 | Memory refresh counter — low 7 bits auto-increment after each opcode fetch; bit 7 holds its programmed value. Programmer may load/read it (`LD R,A` / `LD A,R`) but it is normally left to hardware |

### Bank-swap semantics

- `EX AF,AF'` (08h): swap `AF` with `AF'` only.
- `EXX` (D9h): swap `BC`/`DE`/`HL` with `BC'`/`DE'`/`HL'` (does **not** touch AF).
- These are the only ways to reach the alternate set; a single one-byte exchange
  is used for fast interrupt/context switching. IX, IY, I, R, SP, PC have no
  alternate copy.

## Flag Register (F)

Table 21 / Table 22 bit layout:

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|
| Flag | S | Z | X | H | X | P/V | N | C |
| Name | Sign | Zero | (unused) | Half-carry | (unused) | Parity/Overflow | Add/Subtract | Carry |

- **S (bit 7)** — copy of bit 7 of the result (sign; 1 = negative in two's complement). `IN r,(C)` also sets S from the input byte's bit 7.
- **Z (bit 6)** — set when the result is 0. For compare/search, set when A equals the memory byte. For `BIT b,s`, Z = complement of the tested bit. For the I/O block ops and `IN r,(C)`, reflects B=0 / zero byte.
- **bit 5** — manual: "Not Used" (X). (Real silicon: copy of result bit 5.)
- **H (bit 4)** — half-carry: carry/borrow between bit 3 and bit 4 of an 8-bit op. Used only by DAA; cannot be tested by conditional jumps.
- **bit 3** — manual: "Not Used" (X). (Real silicon: copy of result bit 3.)
- **P/V (bit 2)** — dual use: **parity** for logical/rotate/shift ops (P=1 when the number of set bits is even) vs **overflow** for arithmetic (V=1 when the signed result exceeds +127 or is below −128). Also: for block transfer/search (LDI/LDIR/LDD/LDDR/CPI/CPIR/CPD/CPDR) it monitors BC — cleared to 0 when BC decrements to 0, else 1. For `LD A,I` / `LD A,R` it is loaded with IFF2. For `IN r,(C)` it reflects input-byte parity.
- **N (bit 1)** — add/subtract flag: 0 after an ADD-type op, 1 after a SUB-type op. Read by DAA. Cannot be tested.
- **C (bit 0)** — carry/borrow; also the bit shifted out by rotates/shifts; reset by AND/OR/XOR; set by SCF; complemented by CCF.

Which flag P vs V: **V (overflow)** on 8-bit and 16-bit ADD/ADC/SUB/SBC/NEG/INC/DEC/CP; **P (parity)** on AND/OR/XOR/rotate-shift/`IN r,(C)`; P/V = BC≠0 on block ops; P/V = IFF2 on `LD A,I`/`LD A,R`. Only C, P/V, Z, S are testable by conditional JP/CALL/RET; H and N are not.

### Flag-column notation used in the opcode tables below

| Symbol | Meaning |
|---|---|
| `*` | affected according to the result |
| `-` | not affected |
| `0` | reset to 0 |
| `1` | set to 1 |
| `P` | P/V = parity of result |
| `V` | P/V = overflow of result |
| `IFF2` | P/V loaded from IFF2 |

## Addressing Modes

| Mode | Form | Description |
|---|---|---|
| Immediate | `LD A,n` | operand byte follows the opcode |
| Immediate extended | `LD HL,nn` | two operand bytes (low, then high) follow the opcode |
| Modified page zero | `RST p` | 1-byte call to one of eight page-0 addresses (00,08,10,18,20,28,30,38h) |
| Relative | `JR e` | 1 signed displacement byte added to (PC of following instruction); reach −126..+129 from the opcode |
| Extended | `JP nn`, `LD A,(nn)` | full 16-bit address in the instruction (low byte first) |
| Indexed | `(IX+d)`, `(IY+d)` | signed displacement `d` added to IX/IY forms the pointer; the displacement byte always follows the (2-byte DD/FD) opcode |
| Register | `LD C,B` | operand register selected by bits in the opcode |
| Implied | ADD (A implied) | opcode implies a register (usually A) |
| Register indirect | `(HL)`, `(BC)`, `(DE)`, `(SP)` | a register pair holds the memory pointer |
| Bit | `BIT b,r` | 3 opcode bits select which of bits 0–7 is operated on |

Addressing modes combine (e.g., `LD (IX+d),n` = indexed destination + immediate source).

## Timing model

Instructions are 1–6 machine (M) cycles; each M cycle is 3–6 clock/T states
(the M1 opcode-fetch is 4 T; memory read/write M cycles are 3 T; extendable by
WAIT). I/O cycles insert one automatic WAIT state. Interrupt-acknowledge M1 is
lengthened by 2 automatic WAIT states. The manual quotes execution time at 4 MHz
(1 T = 250 ns). The T-state counts in the tables below are the totals from the
per-instruction descriptions.

---

## Opcode Maps

Convention below: `n` = immediate byte, `nn` = immediate word (low byte first in
memory), `d` = signed index displacement, `e` = relative displacement byte
(stored as e−2 of the assembler offset). Multi-byte opcodes are shown MSB-first
as fetched. 8080-compatible opcodes are noted where useful.

### 8-Bit Load Group (Table 6)

`LD dst,src`. Register encoding r: **B=0 C=1 D=2 E=3 H=4 L=5 (HL)=6 A=7**.
All 8-bit loads leave flags **unaffected** except `LD A,I` and `LD A,R`.

| Instruction | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| LD r,r' | 40h + 8·dst + src (e.g. LD C,B=48) | 1 | 4 | - - - - - - |
| LD r,n | 06h + 8·r ; n | 2 | 7 | - - - - - - |
| LD r,(HL) | 46h + 8·r | 1 | 7 | - - - - - - |
| LD r,(IX+d) | DD 46+8·r d | 3 | 19 | - - - - - - |
| LD r,(IY+d) | FD 46+8·r d | 3 | 19 | - - - - - - |
| LD (HL),r | 70h + r | 1 | 7 | - - - - - - |
| LD (IX+d),r | DD 70+r d | 3 | 19 | - - - - - - |
| LD (IY+d),r | FD 70+r d | 3 | 19 | - - - - - - |
| LD (HL),n | 36 n | 2 | 10 | - - - - - - |
| LD (IX+d),n | DD 36 d n | 4 | 19 | - - - - - - |
| LD (IY+d),n | FD 36 d n | 4 | 19 | - - - - - - |
| LD A,(BC) | 0A | 1 | 7 | - - - - - - |
| LD A,(DE) | 1A | 1 | 7 | - - - - - - |
| LD A,(nn) | 3A nn | 3 | 13 | - - - - - - |
| LD (BC),A | 02 | 1 | 7 | - - - - - - |
| LD (DE),A | 12 | 1 | 7 | - - - - - - |
| LD (nn),A | 32 nn | 3 | 13 | - - - - - - |
| LD A,I | ED 57 | 2 | 9 | * * 0 IFF2 0 - |
| LD A,R | ED 5F | 2 | 9 | * * 0 IFF2 0 - |
| LD I,A | ED 47 | 2 | 9 | - - - - - - |
| LD R,A | ED 4F | 2 | 9 | - - - - - - |

### 16-Bit Load Group (Table 7)

`dd` = BC/DE/HL/SP encoded 00/01/10/11 (bits 5-4). No flags affected.

| Instruction | Opcode | Bytes | T | Flags |
|---|---|---|---|---|
| LD dd,nn | 01+16·dd ; nn (01/11/21/31) | 3 | 10 | - - - - - - |
| LD IX,nn | DD 21 nn | 4 | 14 | - - - - - - |
| LD IY,nn | FD 21 nn | 4 | 14 | - - - - - - |
| LD HL,(nn) | 2A nn | 3 | 16 | - - - - - - |
| LD dd,(nn) | ED 4B+16·dd nn (BC=4B,DE=5B,HL=6B,SP=7B) | 4 | 20 | - - - - - - |
| LD IX,(nn) | DD 2A nn | 4 | 20 | - - - - - - |
| LD IY,(nn) | FD 2A nn | 4 | 20 | - - - - - - |
| LD (nn),HL | 22 nn | 3 | 16 | - - - - - - |
| LD (nn),dd | ED 43+16·dd nn (BC=43,DE=53,HL=63,SP=73) | 4 | 20 | - - - - - - |
| LD (nn),IX | DD 22 nn | 4 | 20 | - - - - - - |
| LD (nn),IY | FD 22 nn | 4 | 20 | - - - - - - |
| LD SP,HL | F9 | 1 | 6 | - - - - - - |
| LD SP,IX | DD F9 | 2 | 10 | - - - - - - |
| LD SP,IY | FD F9 | 2 | 10 | - - - - - - |
| PUSH qq | C5+16·qq (BC=C5,DE=D5,HL=E5,AF=F5) | 1 | 11 | - - - - - - |
| PUSH IX | DD E5 | 2 | 15 | - - - - - - |
| PUSH IY | FD E5 | 2 | 15 | - - - - - - |
| POP qq | C1+16·qq (BC=C1,DE=D1,HL=E1,AF=F1) | 1 | 10 | - - - - - - |
| POP IX | DD E1 | 2 | 14 | - - - - - - |
| POP IY | FD E1 | 2 | 14 | - - - - - - |

`qq` = BC/DE/HL/AF. High byte is pushed first / popped last.

### Exchange Group (Table 8)

| Instruction | Opcode | Bytes | T | Flags |
|---|---|---|---|---|
| EX DE,HL | EB | 1 | 4 | - - - - - - |
| EX AF,AF' | 08 | 1 | 4 | - - - - - - (swaps flags too) |
| EXX | D9 | 1 | 4 | - - - - - - |
| EX (SP),HL | E3 | 1 | 19 | - - - - - - |
| EX (SP),IX | DD E3 | 2 | 23 | - - - - - - |
| EX (SP),IY | FD E3 | 2 | 23 | - - - - - - |

### Block Transfer Group (Table 9)

HL = source, DE = destination, BC = byte counter.

| Instruction | Opcode | Bytes | T | Op | Flags (S Z H P/V N C) |
|---|---|---|---|---|---|
| LDI | ED A0 | 2 | 16 | (DE)←(HL); HL++,DE++,BC-- | - - 0 * 0 - ; P/V=(BC≠0) |
| LDIR | ED B0 | 2 | 21 (BC≠0) / 16 (BC=0) | repeat LDI until BC=0 | - - 0 0 0 - |
| LDD | ED A8 | 2 | 16 | (DE)←(HL); HL--,DE--,BC-- | - - 0 * 0 - ; P/V=(BC≠0) |
| LDDR | ED B8 | 2 | 21 / 16 | repeat LDD until BC=0 | - - 0 0 0 - |

### Block Search Group (Table 10)

Compare A against (HL); HL is auto-inc/dec, BC decremented.

| Instruction | Opcode | Bytes | T | Op | Flags |
|---|---|---|---|---|---|
| CPI | ED A1 | 2 | 16 | cmp A,(HL); HL++,BC-- | * * * * 1 - ; Z=match, P/V=(BC≠0) |
| CPIR | ED B1 | 2 | 21 / 16 | repeat until match or BC=0 | * * * * 1 - |
| CPD | ED A9 | 2 | 16 | cmp A,(HL); HL--,BC-- | * * * * 1 - ; Z=match, P/V=(BC≠0) |
| CPDR | ED B9 | 2 | 21 / 16 | repeat until match or BC=0 | * * * * 1 - |

### 8-Bit Arithmetic & Logic (Table 11)

Source `s` = A/B/C/D/E/H/L / (HL) / (IX+d) / (IY+d) / n. Accumulator is the
implied destination (except INC/DEC which act on any r/(HL)/(IX+d)/(IY+d)).
Register column encodes A=…7 B=…0 C=…1 D=…2 E=…3 H=…4 L=…5 in the low 3 bits.

Base opcodes (source = register r, add r in low 3 bits):

| Op | reg base | (HL) | (IX+d) | (IY+d) | imm n | Flags (S Z H P/V N C) |
|---|---|---|---|---|---|---|
| ADD A,s | 80+r | 86 | DD 86 d | FD 86 d | C6 n | * * * V 0 * |
| ADC A,s | 88+r | 8E | DD 8E d | FD 8E d | CE n | * * * V 0 * |
| SUB s | 90+r | 96 | DD 96 d | FD 96 d | D6 n | * * * V 1 * |
| SBC A,s | 98+r | 9E | DD 9E d | FD 9E d | DE n | * * * V 1 * |
| AND s | A0+r | A6 | DD A6 d | FD A6 d | E6 n | * * 1 P 0 0 |
| XOR s | A8+r | AE | DD AE d | FD AE d | EE n | * * 0 P 0 0 |
| OR s | B0+r | B6 | DD B6 d | FD B6 d | F6 n | * * 0 P 0 0 |
| CP s | B8+r | BE | DD BE d | FD BE d | FE n | * * * V 1 * (A unchanged) |

T-states: register form 4; immediate 7; (HL) 7; (IX+d)/(IY+d) 19.

INC / DEC (destination `m`):

| Op | reg | (HL) | (IX+d) | (IY+d) | Flags |
|---|---|---|---|---|---|
| INC m | 04+8·r (A=3C,B=04,C=0C,D=14,E=1C,H=24,L=2C) | 34 | DD 34 d | FD 34 d | * * * V 0 - (C unchanged) |
| DEC m | 05+8·r (A=3D,B=05,C=0D,D=15,E=1D,H=25,L=2D) | 35 | DD 35 d | FD 35 d | * * * V 1 - (C unchanged) |

T-states: INC/DEC r = 4; (HL) = 11; (IX+d)/(IY+d) = 23.

### General-Purpose Arithmetic & CPU Control (Table 12 / Table 20)

| Instruction | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| DAA | 27 | 1 | 4 | * * * P - * |
| CPL | 2F | 1 | 4 | - - 1 - 1 - |
| NEG | ED 44 | 2 | 8 | * * * V 1 * |
| CCF | 3F | 1 | 4 | - - * - 0 * (H←old C; C←~C) |
| SCF | 37 | 1 | 4 | - - 0 - 0 1 |
| NOP | 00 | 1 | 4 | - - - - - - |
| HALT | 76 | 1 | 4 | - - - - - - (NOPs until interrupt) |
| DI | F3 | 1 | 4 | - - - - - - (IFF1=IFF2=0) |
| EI | FB | 1 | 4 | - - - - - - (IFF1=IFF2=1, delayed 1 instr) |
| IM 0 | ED 46 | 2 | 8 | - - - - - - (8080A mode) |
| IM 1 | ED 56 | 2 | 8 | - - - - - - (RST 0038h) |
| IM 2 | ED 5E | 2 | 8 | - - - - - - (I:vector) |

### 16-Bit Arithmetic (Table 13)

`ss` = BC/DE/HL/SP (00/01/10/11). ADD leaves S,Z,P/V unaffected; ADC/SBC affect
all flags.

| Instruction | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| ADD HL,ss | 09+16·ss (BC=09,DE=19,HL=29,SP=39) | 1 | 11 | - - * - 0 * |
| ADC HL,ss | ED 4A+16·ss (BC=4A,DE=5A,HL=6A,SP=7A) | 2 | 15 | * * * V 0 * |
| SBC HL,ss | ED 42+16·ss (BC=42,DE=52,HL=62,SP=72) | 2 | 15 | * * * V 1 * |
| ADD IX,pp | DD 09+16·pp (pp=BC/DE/IX/SP → 09/19/29/39) | 2 | 15 | - - * - 0 * |
| ADD IY,rr | FD 09+16·rr (rr=BC/DE/IY/SP → 09/19/29/39) | 2 | 15 | - - * - 0 * |
| INC ss | 03+16·ss (BC=03,DE=13,HL=23,SP=33) | 1 | 6 | - - - - - - |
| INC IX | DD 23 | 2 | 10 | - - - - - - |
| INC IY | FD 23 | 2 | 10 | - - - - - - |
| DEC ss | 0B+16·ss (BC=0B,DE=1B,HL=2B,SP=3B) | 1 | 6 | - - - - - - |
| DEC IX | DD 2B | 2 | 10 | - - - - - - |
| DEC IY | FD 2B | 2 | 10 | - - - - - - |

(The manual prints DEC ss low bytes as 0B/1B/2B/3B; its Table 13 shows "DB/1B/2B/3B" with a typo in the BC entry — the correct BC opcode is 0Bh.)

### Rotate & Shift (Figure 39)

The four accumulator-only rotates are single-byte 8080-style and affect only
H,N,C (S,Z,P/V unaffected):

| Instruction | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| RLCA | 07 | 1 | 4 | - - 0 - 0 * |
| RRCA | 0F | 1 | 4 | - - 0 - 0 * |
| RLA | 17 | 1 | 4 | - - 0 - 0 * |
| RRA | 1F | 1 | 4 | - - 0 - 0 * |

The general (CB-prefix) rotates/shifts operate on r/(HL)/(IX+d)/(IY+d) and set
S,Z,P(parity),H=0,N=0,C=shifted-out bit. Encoding: `CB (base + r)` with
**base**: RLC=00, RRC=08, RL=10, RR=18, SLA=20, SRA=28, SLL(undoc)=30, SRL=38,
and r = B0 C1 D2 E3 H4 L5 (HL)6 A7.

| Op | reg form | (HL) | (IX+d) | (IY+d) | Flags |
|---|---|---|---|---|---|
| RLC r | CB 00+r | CB 06 | DD CB d 06 | FD CB d 06 | * * 0 P 0 * |
| RRC r | CB 08+r | CB 0E | DD CB d 0E | FD CB d 0E | * * 0 P 0 * |
| RL r  | CB 10+r | CB 16 | DD CB d 16 | FD CB d 16 | * * 0 P 0 * |
| RR r  | CB 18+r | CB 1E | DD CB d 1E | FD CB d 1E | * * 0 P 0 * |
| SLA r | CB 20+r | CB 26 | DD CB d 26 | FD CB d 26 | * * 0 P 0 * |
| SRA r | CB 28+r | CB 2E | DD CB d 2E | FD CB d 2E | * * 0 P 0 * |
| SRL r | CB 38+r | CB 3E | DD CB d 3E | FD CB d 3E | * * 0 P 0 * |

T-states: reg = 8; (HL) = 15; (IX+d)/(IY+d) = 23.

(Base 30h = SLL/SLI is undocumented; the manual's Figure 39 leaves that row
blank and shows only the ED 6F/ED 67 entries for the 4-bit BCD digit rotates
below.)

BCD digit rotates through (HL):

| Instruction | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| RLD | ED 6F | 2 | 18 | * * 0 P 0 - |
| RRD | ED 67 | 2 | 18 | * * 0 P 0 - |

### Bit Set/Reset/Test (Table 14)

240 instructions, all CB- (or DD CB / FD CB) prefixed. Systematic encoding
(r = B0 C1 D2 E3 H4 L5 (HL)6 A7, b = bit 0–7):

- **BIT b,r** = `CB (40h + 8·b + r)` — tests bit; Z = complement of the bit.
- **RES b,r** = `CB (80h + 8·b + r)` — reset bit to 0.
- **SET b,r** = `CB (C0h + 8·b + r)` — set bit to 1.

Indexed forms insert the displacement between the CB prefix and the op byte:
- **BIT b,(IX+d)** = `DD CB d (46h + 8·b)`, **BIT b,(IY+d)** = `FD CB d (46h + 8·b)`
- **RES b,(IX+d)** = `DD CB d (86h + 8·b)` (and FD for IY)
- **SET b,(IX+d)** = `DD CB d (C6h + 8·b)` (and FD for IY)

| Group | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| BIT b,r | CB 40+8b+r | 2 | 8 | ? * 1 ? 0 - (Z=~bit; S,P/V undefined) |
| BIT b,(HL) | CB 46+8b | 2 | 12 | ? * 1 ? 0 - |
| BIT b,(IX+d) | DD CB d 46+8b | 4 | 20 | ? * 1 ? 0 - |
| BIT b,(IY+d) | FD CB d 46+8b | 4 | 20 | ? * 1 ? 0 - |
| SET b,r | CB C0+8b+r | 2 | 8 | - - - - - - |
| SET b,(HL) | CB C6+8b | 2 | 15 | - - - - - - |
| SET b,(IX+d) | DD CB d C6+8b | 4 | 23 | - - - - - - |
| SET b,(IY+d) | FD CB d C6+8b | 4 | 23 | - - - - - - |
| RES b,r | CB 80+8b+r | 2 | 8 | - - - - - - |
| RES b,(HL) | CB 86+8b | 2 | 15 | - - - - - - |
| RES b,(IX+d) | DD CB d 86+8b | 4 | 23 | - - - - - - |
| RES b,(IY+d) | FD CB d 86+8b | 4 | 23 | - - - - - - |

Example concrete opcodes: BIT 0,B = CB40; BIT 7,A = CB7F; RES 0,B = CB80;
RES 7,A = CBBF; SET 0,B = CBC0; SET 7,A = CBFF; BIT 0,(IX+0) = DD CB 00 46.

### Jump Group (Table 15)

Condition `cc` (JP/CALL/RET): NZ=0, Z=1, NC=2, C=3, PO=4, PE=5, P=6, M=7
(encoded in bits 5-3 of the opcode).

| Instruction | Opcode | Bytes | T | Notes |
|---|---|---|---|---|
| JP nn | C3 nn | 3 | 10 | unconditional |
| JP cc,nn | C2+8·cc nn (NZ=C2,Z=CA,NC=D2,C=DA,PO=E2,PE=EA,P=F2,M=FA) | 3 | 10 | never varies |
| JR e | 18 e−2 | 2 | 12 | relative |
| JR C,e | 38 e−2 | 2 | 12 (taken) / 7 (not) | |
| JR NC,e | 30 e−2 | 2 | 12 / 7 | |
| JR Z,e | 28 e−2 | 2 | 12 / 7 | |
| JR NZ,e | 20 e−2 | 2 | 12 / 7 | |
| JP (HL) | E9 | 1 | 4 | PC←HL |
| JP (IX) | DD E9 | 2 | 8 | PC←IX |
| JP (IY) | FD E9 | 2 | 8 | PC←IY |
| DJNZ e | 10 e−2 | 2 | 13 (B≠0) / 8 (B=0) | B--, jump if B≠0 |

Jumps/calls/returns do not affect flags.

### Call & Return Group (Table 15)

| Instruction | Opcode | Bytes | T | Notes |
|---|---|---|---|---|
| CALL nn | CD nn | 3 | 17 | push PC, jump |
| CALL cc,nn | C4+8·cc nn (NZ=C4,Z=CC,NC=D4,C=DC,PO=E4,PE=EC,P=F4,M=FC) | 3 | 17 (taken) / 10 (not) | |
| RET | C9 | 1 | 10 | pop PC |
| RET cc | C0+8·cc (NZ=C0,Z=C8,NC=D0,C=D8,PO=E0,PE=E8,P=F0,M=F8) | 1 | 11 (taken) / 5 (not) | |
| RETI | ED 4D | 2 | 14 | return from INT; signals Z80 peripherals |
| RETN | ED 45 | 2 | 14 | return from NMI; IFF1←IFF2 |

### Restart Group (Table 17)

Single-byte page-0 calls, `RST p` = `C7 + p` where p = 00,08,10,18,20,28,30,38h.

| Instruction | Opcode | Target | Bytes | T |
|---|---|---|---|---|
| RST 00h | C7 | 0000h | 1 | 11 |
| RST 08h | CF | 0008h | 1 | 11 |
| RST 10h | D7 | 0010h | 1 | 11 |
| RST 18h | DF | 0018h | 1 | 11 |
| RST 20h | E7 | 0020h | 1 | 11 |
| RST 28h | EF | 0028h | 1 | 11 |
| RST 30h | F7 | 0030h | 1 | 11 |
| RST 38h | FF | 0038h | 1 | 11 |

### Input Group (Table 18)

| Instruction | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| IN A,(n) | DB n | 2 | 11 | - - - - - - (A7-0 = n, A15-8 = A) |
| IN r,(C) | ED 40+8·r (B=40,C=48,D=50,E=58,H=60,L=68,A=78) | 2 | 12 | * * 0 P 0 - |
| INI | ED A2 | 2 | 16 | ? * ? ? 1 - ; B--; Z=(B=0) |
| INIR | ED B2 | 2 | 21 / 16 | ? 1 ? ? 1 - (repeat until B=0) |
| IND | ED AA | 2 | 16 | ? * ? ? 1 - ; B--; Z=(B=0) |
| INDR | ED BA | 2 | 21 / 16 | ? 1 ? ? 1 - (repeat until B=0) |

`IN r,(C)`: `IN F,(C)` = ED 70 exists (updates flags only, undocumented dest).
INI/IND: (HL)←port(C), B--, HL++ (INI) / HL-- (IND).

### Output Group (Table 19)

| Instruction | Opcode | Bytes | T | Flags (S Z H P/V N C) |
|---|---|---|---|---|
| OUT (n),A | D3 n | 2 | 11 | - - - - - - (A7-0 = n, A15-8 = A) |
| OUT (C),r | ED 41+8·r (B=41,C=49,D=51,E=59,H=61,L=69,A=79) | 2 | 12 | - - - - - - |
| OUTI | ED A3 | 2 | 16 | ? * ? ? 1 - ; B--; Z=(B=0) |
| OTIR | ED B3 | 2 | 21 / 16 | ? 1 ? ? 1 - (repeat until B=0) |
| OUTD | ED AB | 2 | 16 | ? * ? ? 1 - ; B--; Z=(B=0) |
| OTDR | ED BB | 2 | 21 / 16 | ? 1 ? ? 1 - (repeat until B=0) |

OUTI/OUTD: port(C)←(HL), B--, HL++ (OUTI) / HL-- (OUTD). `OUT (C),0` = ED 71
(undocumented, outputs 0). Note the manual's Table 19 mislabels the `OUT (C),r`
register row bit0 (`ED 79` for A) and the block-op text ("11OUT") due to OCR/type
issues; the systematic ED 41+8·r encoding above is correct.

---

## Prefix Map Summary

| Prefix | Group |
|---|---|
| (none) | 8080-superset base opcodes: 8-bit/16-bit loads, arithmetic, JP/CALL/RET/RST, accumulator rotates, IN/OUT (n) |
| **CB** | rotate/shift on r/(HL); BIT/RES/SET b,r/(HL) |
| **ED** | block LD/CP/IN/OUT (LDIR…OTDR); 16-bit `LD (nn),dd`/`LD dd,(nn)`; ADC/SBC HL,ss; `LD A,I`/`LD A,R`/`LD I,A`/`LD R,A`; NEG; IM 0/1/2; RETI/RETN; RLD/RRD; `IN r,(C)`/`OUT (C),r` |
| **DD** | IX form: `(IX+d)` variants of the base group, IX 16-bit ops |
| **FD** | IY form: `(IY+d)` variants of the base group, IY 16-bit ops |
| **DD CB d op** | bit/rotate/shift on `(IX+d)` (displacement precedes the final op byte) |
| **FD CB d op** | bit/rotate/shift on `(IY+d)` |

Undocumented but real (not covered by this manual): DD/FD access to IXH/IXL/IYH/
IYL 8-bit halves; ED base "duplicate" NEG/IM/RETN opcodes; SLL (CB 30–37);
the F5/F3 flag copies. Implement these only from a source that documents them.

## Interrupt System

Two interrupt inputs and two enable flip-flops:

- **IFF1** — the master enable; when 0, maskable INT is ignored.
- **IFF2** — a temporary store for IFF1's value across an NMI; its state is
  copied into the P/V flag by `LD A,I` / `LD A,R` (so software can read the
  pre-NMI interrupt-enable state).

| Action | IFF1 | IFF2 | Effect |
|---|---|---|---|
| CPU reset | 0 | 0 | maskable INT disabled |
| DI | 0 | 0 | maskable INT disabled |
| EI | 1 | 1 | enabled — but the enable takes effect *after the instruction following EI* (lets a RET complete first) |
| Accept INT | 0 | 0 | both cleared on acknowledge; re-enable with EI |
| LD A,I / LD A,R | – | – | P/V ← IFF2 |
| Accept NMI | 0 | IFF2 unchanged | IFF1→0 (IFF2 keeps old IFF1) |
| RETN | ←IFF2 | – | IFF1 ← IFF2 (restore) |

For all cases except immediately after an NMI, IFF1 and IFF2 are equal.

### NMI (non-maskable, pin NMI, negative-edge)

Cannot be masked. On acknowledge: the CPU ignores the fetched instruction,
pushes PC, sets IFF1=0 (IFF2 preserved), and jumps to fixed address **0066h**.
`RETN` at the end restores IFF1 from IFF2. Higher priority than INT.

### Maskable INT — three modes

- **Mode 0** (8080A-compatible; default after reset). The interrupting device
  places an opcode (typically an RST) on the data bus; the CPU executes it.
  Two extra WAIT states added; timing = the executed instruction + 2 T.
- **Mode 1** (`IM 1`). CPU executes a restart to fixed address **0038h**
  (like RST 38h). 13 T-states.
- **Mode 2** (`IM 2`). Vectored indirect. 16-bit pointer = **I register (high
  byte)** : **byte supplied by the device (low byte, LSB forced to 0)**. The CPU
  reads the 2-byte service-routine address from that table entry (even
  addresses) and CALLs it. 19 T-states. Table may be relocated anywhere in RAM.

`EI` delays the enable by one instruction; a pending INT is not accepted until
after the instruction following `EI` (important so `EI; RET` completes the RET).

## Reset Behavior

`RESET` (pin, active Low, held ≥3 clock cycles) forces:

| State | Value after reset |
|---|---|
| PC | 0000h |
| I register | 00h |
| R register | 00h |
| IFF1, IFF2 | 0 (interrupts disabled) |
| Interrupt mode | Mode 0 |
| Address/data bus | high-impedance during reset; control outputs inactive |

The manual specifies PC, I, R cleared, interrupt FF reset, and IM 0 set; it does
**not** define SP or the main/alternate registers at reset (on real silicon SP
and AF power up to FFFFh, but this manual does not state that — treat SP/AF as
undefined per the manual).

## Execution-time reference

The per-instruction descriptions quote execution time in microseconds at a
4 MHz clock (1 T = 0.25 µs), e.g. an instruction of "2 M cycles, 7(4,3) T
states" = 1.75 µs. Scale linearly for the S-100 2 MHz Z80 card (1 T = 0.5 µs).
