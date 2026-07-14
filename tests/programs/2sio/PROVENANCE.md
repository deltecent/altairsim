# 88-2SIO echo test programs

Two period 8080 test programs for the MITS 88-2SIO, used by
`tests/acceptance/2sio-echo.exp`. They are tiny, they are authoritative, and between
them they cover **both** ways a guest can drive this card.

## Where they came from

Downloaded 2026-07-14 from Mike Douglas's Altair software archive, unmodified:

    https://deramp.com/downloads/altair/software/utilities/other/

| file | what it is |
|---|---|
| `ECHO.ASM` / `.HEX` / `.PRN` | 2SIO echo, **POLLED**: spin on RDRF, read, echo. |
| `ECHOINT.ASM` / `.HEX` / `.PRN` | 2SIO echo, **RECEIVE INTERRUPTS**: ISR at 0038h. |

`.HEX` is what the test loads; `.ASM` and `.PRN` are kept because a test program you
cannot read is a test program you cannot debug.

## Why BOTH, and why they earn their place

They are the two halves of the card, and **they fail differently** — which is the whole
point of having both:

- **`ECHO`** programs the 6850 with `15h` (no interrupts) and polls the status register.
  A polled guest **self-paces**: it reads each character in its own foreground loop, so
  it can never be handed one it was not ready for.

- **`ECHOINT`** programs it with `95h` — **8N1, receive interrupts on** — the *same
  control byte MITS PS2 writes* — enables 8080 interrupts, and then does nothing but
  `NOP` in a loop. Every character it echoes has to travel the entire interrupt chain:
  the 6850 raises IRQ with nobody touching a register, the card drives pin 73, the 8080
  acknowledges, and the ISR at 0038h reads and echoes. **If the receiver only advanced
  when the guest looked at a register, this program would echo nothing, for ever.**

`ECHOINT` needs **no 88-VI/RTC**. Its ISR is at `0038h` = RST 7, and with no
vector-interrupt board in the machine nothing drives the bus during the acknowledge, so
it floats high to `FFh` — which the 8080 executes as `RST 7`. That is the real Altair's
behavior and `Bus::intAck()` reproduces it, so the test runs on the plain `default`
machine with one jumper moved (`SET sio0:a interrupt=int`) and proves the floating-bus
path into the bargain.

## The bug they stand guard over (2026-07-14)

`baud = 0` was proposed as the UART default, by analogy with `clock_hz = 0`. It hangs
every interrupt-driven console, and the mailbox arithmetic that proves why is in
`src/chips/mc6850.h` (see `baud_`) and DESIGN.md §7. **The baud rate is the only flow
control this card has.** These two programs are the cheapest way to notice if that ever
stops being true — `ECHO` keeps working when the receiver is broken, and `ECHOINT` does not.
