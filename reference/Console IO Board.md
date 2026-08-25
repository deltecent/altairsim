# S100Computers Console IO Board

Source (fetched 2026-08-16, John Monahan's S-100 archive):
- Board page: [Console IO Board.htm](http://www.s100computers.com/My%20System%20Pages/Console%20IO%20Board/Console%20IO%20Board.htm)

A modern S-100 **console** board built around a **Parallax Propeller** (P8X32A): the Propeller
takes a PS/2 keyboard in and drives a VGA display out, and presents to the S-100 bus as an
ordinary **two-port, polled "status + data" console** — the same shape as an SD Systems 8024 or
any classic polled UART, with **no chip register map** (there is no UART; the Propeller *is* the
terminal). This is the console the Dual SD / SBC-Z80 CP/M 3 machines use.

**Licensing:** no licence stated. John Monahan's hobbyist/educational S-100 project. Nothing is
redistributed here — text distillation citing the public page. (Record the licence; never gate.)

This board is emulated as a preset of the **chip-less strap-serial engine** (`src/boards/strapserial.h`,
the same engine behind the `io4` board): a single-channel subtype `propio` presetting the straps
below. Everything the board does is "read a status bit, read/write a data byte," which the strap
engine already models; the board's real hardware is *itself* fully jumper-configurable, so a subtype
presetting one documented convention (all straps still overridable) is a faithful model, not an
invented one.

## Port map

Two I/O ports, selected by on-board switches **SW2** (status) and **SW3** (data):

| Port | Read | Write |
|------|------|-------|
| STATUS | status byte (keyboard-ready + output-busy bits, positions jumpered) | — (no control register) |
| DATA   | keyboard character (read clears the keyboard-ready flag) | display character (write sets the output-busy flag) |

- **Monitor default: STATUS = `00H`, DATA = `01H`.** The board's own test software uses
  **`14H`/`15H`**. Any base is selectable.

## Status bits and polarity (jumper-configurable; the worked-example convention)

The board page stresses that *which* status bit carries each signal, and each bit's polarity, are
set by jumpers **P74–P77** so the board "will splice into almost any S-100 system" unchanged. The
page's worked example (matching the **SD Systems 8024** convention its shipped monitor assumes):

| Signal | Bit | Polarity | Meaning |
|--------|-----|----------|---------|
| Keyboard/RX ready | **bit 1** (`AND 02H`) | active **high** (1 = a key is waiting) | DATA read returns it and clears the flag |
| Output/TX ready   | **bit 2** (`AND 04H`) | active **high** (1 = ready, 0 = busy) | write DATA when set |

P77 selects RX-ready polarity ("If your hardware uses a 0 as a character-ready flag, jumper P77
1-3 & 2-4"); P75/P76 select the output-status bit and polarity.

The board's example driver (its authoritative port/bit convention):

```
INPUT:  IN   A,(00H)     ; read keyboard status
        AND  02H         ; bit 1 = keyboard data ready
        JP   Z,INPUT     ; loop while not ready
        IN   A,(01H)     ; get keyboard data (clears the ready flag)

OUTPUT: IN   A,(00H)     ; read console status
        AND  04H         ; bit 2 = output ready (0 = busy)
        JP   Z,OUTPUT    ; loop while busy
        LD   A,C
        OUT  (01H),A     ; send display character
```

## Handshake (from the circuit description)

- **Keyboard read:** when the Propeller latches a key it pulses P24 low, loading U47 (74LS374) and
  setting `DATA_IN_BUSY` (the keyboard-ready flag). The S-100 read of the DATA port brings
  `INPUT_ENABLE*` low, which resets U44B and lowers `DATA_IN_BUSY` — reading the data port clears
  the ready flag.
- **Display write:** the S-100 write of the DATA port latches the character into U46 (74LS374) and
  clocks U44A, setting `DATA_OUT_BUSY`; the Propeller clears it once it consumes the character.

## Notes for the emulation

- Model as a single-channel strap-serial subtype (`propio`) presetting: `status_port=0x00`,
  `data_port=0x01`, `dav=1`, `tbmt=2`, `inverter_gate=off` (both bits active high). All remain
  overridable via properties — the real board is jumperable, so a machine whose jumpers differ (or
  the `14H/15H` test bases) is one property change away. Polled TX/RX console only; no interrupts
  (the strap-serial engine has none).
