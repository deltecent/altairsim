#pragma once
//
// AMD Am9511A Arithmetic Processing Unit (Intel 8231A) -- a CHIP, NOT A CARD.
//
// 16- and 32-bit fixed point, the chip's own 32-bit floating-point format, eleven derived
// functions (SQRT, SIN ... PWR) and the stack commands, all worked on a 16-byte LIFO stack
// reached through two ports. The CompuPro System Support 1 carries one in its math socket.
// Modeled from the Am9511A data sheet and AMD's Am9511A/Am9512 Floating Point Processor
// Manual (reference/AMD 9511A.md) and measurements of two real chips (#908), NOT from any
// one program that drives it.
//
// It knows nothing about S-100. The ports, the stack, END/SVREQ and BUSY are the bus side it
// shares with the Am9512 (chips/am95xx.h); this file is the command set. On the SS-1 CLK is
// the 2 MHz bus clock (pin 49).
//
// ---------------------------------------------------------------------------
//
// The data sheet gives most cycle counts as a range ("FADD 54-368"). The model takes the TOP
// of each range: it is the time a program that times the chip by a delay loop had to allow.
// A short path the data sheet names (FADD with A = 0, SIN of a tiny angle) takes its own count.
//
// ---------------------------------------------------------------------------
// ARITHMETIC
//
//   * Fixed point is two's complement, with the data sheet's overflow rules. The most
//     negative value follows the real chip (#908): SMUL/DMUL return the low half of the
//     product, SMUU/DMUU the high half (7FFF when B is 8000 and A is negative), SDIV/DDIV
//     0, each with overflow.
//   * The four float operations TRUNCATE, as two real chips do (#908, reference/AMD 9511A.md
//     6.1): no guard bits, FMUL a 24-step shift-and-add, FDIV 23 quotient bits when B's
//     mantissa is below A's. FLTD truncates and PUPI pushes 02C90FDA. A result too big or too
//     small keeps its mantissa and its exponent wraps by 128, with the overflow or underflow
//     error, as the data sheet says.
//   * The derived functions follow the Manual's ch.9 algorithms with the steps the real chip
//     takes (reference/AMD 9511A.md 6.1): the exact Chebyshev coefficients cut to 24 bits,
//     SIN/COS reduced by a 23-bit quotient, ASIN through the ATAN series, EXP reduced by an
//     FDIV by LN 2. Every step uses the four operations above. A few results are 1-2 LSB from
//     the real chip; tests/test_am9511a.cpp lists them.
//   * An argument error (SQRT of a negative value, LN of 0, ASIN of 2 ...) stops early with
//     the real chip's result, status and time.
//   * FIXS/FIXD take "the integer portion" of A: the fraction is dropped (toward zero).
//   * A float whose mantissa MSB is 0 is zero (the data sheet's test for CHSF).
//
// WHAT IS NOT MODELED: the scratch values a command leaves in its "lost" stack cells (the
// data sheet does not give them; the ring simply keeps what was there), and the bits of an
// undefined command byte (it does nothing and takes 4 cycles, like NOP, but leaves status).

#include "chips/am95xx.h"

#include <cstdint>
#include <string>

namespace altair {

class Am9511a : public Am95xx {
public:
    explicit Am9511a(std::string name) : Am95xx(std::move(name), 0) {}  // no ERR pin

    // The chip's float format <-> a host double. toFloat truncates to 24 bits and wraps
    // an out-of-range exponent by 128; `err` gets the overflow (0x02) or underflow (0x04)
    // bit when it does.
    static uint32_t toFloat(double x, uint8_t* err = nullptr);
    static double   fromFloat(uint32_t v);

    // The error bits of the status register (data sheet p.7-71); BUSY, SIGN and ZERO are
    // Am95xx's.
    static constexpr uint8_t kDivZero   = 0x10;  // error code 1000
    static constexpr uint8_t kNegArg    = 0x08;  // error code 0100
    static constexpr uint8_t kArgRange  = 0x18;  // error code 1100
    static constexpr uint8_t kUnderflow = 0x04;  // error code XX10
    static constexpr uint8_t kOverflow  = 0x02;  // error code XX01
    static constexpr uint8_t kCarry     = 0x01;

private:
    unsigned execute(uint8_t op) override;
};

}  // namespace altair
