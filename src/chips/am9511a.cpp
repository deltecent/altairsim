#include "chips/am9511a.h"

#include <cmath>
#include <utility>

namespace altair {

namespace {

// ---------------------------------------------------------------------------
// THE CHIP'S FLOAT ARITHMETIC. A value is held as a host double that has at most 24
// significant bits; a double holds each one exactly. The four operations work as the real
// chip does, measured on two chips (issue #908): no guard bits, and the result is
// truncated, not rounded.
//
//   add  aligns the smaller operand with a right shift that drops its low bits, adds or
//        subtracts the mantissas, then normalizes (one right shift on a carry, left shifts
//        after a cancellation).
//   mul  is a 24-step shift-and-add: each step adds B's mantissa when A's bit is 1 and
//        shifts the sum right one place, dropping a bit. One left shift normalizes it.
//   dvd  gives 24 quotient bits when B's mantissa >= A's, else 23 bits and a 0 at the end.
//
// The exponent is not bounded here; toFloat wraps it.
//
// The derived functions below use these four operations and the ROM constants cut to 24
// bits. Their steps were found on the real chip (#908): it leaves intermediate values on
// its stack. SIN, COS, TAN and EXP reduce the argument with a divide by PI/2, PI/4 or
// LN 2: TAN of 01C90FDA and of 01C90FDB (each side of PI/2) both stop with a divide by
// zero on the real chip, and only a quotient of exactly 2.0 does that. SQRT, TAN, LN and
// LOG give the real chip's result in every measured case; SIN, COS, ATAN, ASIN, ACOS, EXP
// and PWR in most, and within 2 LSB in nearly all of the rest.
// ---------------------------------------------------------------------------

// Cut the magnitude to a 24-bit mantissa (toward zero).
double t24(double x) {
    if (x == 0) return 0;
    int e;
    double m = std::frexp(std::fabs(x), &e);  // 0.5 <= m < 1
    double v = std::ldexp(std::floor(std::ldexp(m, 24)), e - 24);
    return x < 0 ? -v : v;
}

// A nonzero value as the chip holds it: |x| = m x 2^(e - 24), 2^23 <= m < 2^24.
struct Parts {
    bool     neg;
    int      e;
    uint64_t m;
};
Parts split(double x) {
    int e;
    double m = std::frexp(std::fabs(x), &e);
    return {x < 0, e, (uint64_t)std::ldexp(m, 24)};
}
double join(bool neg, int e, uint64_t m) {
    double v = std::ldexp((double)m, e - 24);
    return neg ? -v : v;
}

double add(double x, double y) {
    x = t24(x);
    y = t24(y);
    if (y == 0) return x;
    if (x == 0) return y;
    Parts b = split(x), a = split(y);
    if (a.e > b.e || (a.e == b.e && a.m > b.m)) std::swap(a, b);  // b is the larger
    int d = b.e - a.e;
    uint64_t am = d < 24 ? a.m >> d : 0;
    uint64_t m;
    int e = b.e;
    if (a.neg == b.neg) {
        m = b.m + am;
        if (m >> 24) { m >>= 1; ++e; }
    } else {
        m = b.m - am;
        if (m == 0) return 0;
        while (!(m >> 23)) { m <<= 1; --e; }
    }
    return join(b.neg, e, m);
}
double sub(double x, double y) { return add(x, -y); }

double mul(double x, double y) {
    x = t24(x);
    y = t24(y);
    if (x == 0 || y == 0) return 0;
    Parts b = split(x), a = split(y);
    uint64_t acc = 0;
    for (int k = 0; k < 24; ++k) {
        if ((a.m >> k) & 1) acc += b.m;
        acc >>= 1;
    }
    int e = a.e + b.e;
    if (!(acc >> 23)) { acc <<= 1; --e; }
    return join(a.neg != b.neg, e, acc);
}

double dvd(double x, double y) {  // y != 0
    x = t24(x);
    y = t24(y);
    if (x == 0) return 0;
    Parts b = split(x), a = split(y);
    uint64_t q;
    int e;
    if (b.m >= a.m) {
        q = (b.m << 23) / a.m;
        e = b.e - a.e + 1;
    } else {
        q = ((b.m << 23) / a.m) << 1;  // 23 quotient bits
        e = b.e - a.e;
    }
    return join(a.neg != b.neg, e, q);
}

// dvd, cut to 23 significant bits whatever the mantissas. The SIN and COS reduction uses
// this: the quotient the real chip leaves on its stack has 23 bits even where FDIV would
// give 24.
double dvd23(double x, double y) {
    double q = dvd(x, y);
    if (q == 0) return 0;
    Parts p = split(q);
    return join(p.neg, p.e, p.m & ~(uint64_t)1);
}

// A ROM word (the chip's float format) as a value.
double rom(uint32_t v) {
    double m = std::ldexp((double)(v & 0xFFFFFF), (int)(int8_t)((v >> 24) << 1) / 2 - 24);
    return (v & 0x80000000u) ? -m : m;
}

// The constants, as the chip's ROM holds them: cut to 24 bits.
const double kPi       = t24(3.14159265358979323846);
const double kHalfPi   = t24(1.57079632679489661923);
const double kQuarterPi = t24(0.78539816339744830962);
const double kLn2      = t24(0.69314718055994530942);
const double kLog10e   = t24(0.43429448190325182765);

// The Chebyshev coefficients (AMD Floating Point Processor Manual ch.9, reference/AMD
// 9511A.md sec. 6). The ROM holds the exact coefficients cut to 24 bits, not the 8-digit
// decimals the Manual prints: 13 of them differ from the cut decimal in the last bit, and
// only the exact ones give the real chip's results (#908; EXP C0 is 01BA7EF8, the printed
// 1.4569999 cuts to 01BA7EF9).
const double kCsin[]  = {rom(0x02A35D1B), rom(0xFF920DCD), rom(0x7A9563BB), rom(0xF48F38EE),
                         rom(0x6D9F0AFA), rom(0xE5E64E5F)};
const double kCtan[]  = {rom(0x01E29430), rom(0x7DDAA1CD), rom(0x79F894D5), rom(0x768EA6A7),
                         rom(0x72A3DB31), rom(0x6EBC3A5F), rom(0x6AD839F8), rom(0x66F863C6),
                         rom(0x638EAB2B)};
const double kCatan[] = {rom(0x01E1A1B3), rom(0xFDD8DE63), rom(0x7AB6731B), rom(0xF7B50936),
                         rom(0x74C2C3FF), rom(0xF1DBE8B4), rom(0x6F803674), rom(0xEC98FC82),
                         rom(0x69B9BBB5), rom(0xE6E48CE6), rom(0x648E258A), rom(0xE1B26362)};
const double kCexp[]  = {rom(0x01BA7EF8), rom(0xFEFEBB94), rom(0x7BAFB0B0), rom(0xF7A1F7AF),
                         rom(0x72E032C8), rom(0xEDF8653F), rom(0x68E566FA), rom(0xE3B5A0B7)};
const double kCln[]   = {rom(0x00C0BE6C), rom(0x7FAFB0CC), rom(0xFBF12664), rom(0x78DCAA62),
                         rom(0xF5E3294E), rom(0x72F97030), rom(0xF08EA7F7), rom(0x6DA7D5BB),
                         rom(0xEAC99247), rom(0x67F5EE96), rom(0xE597E725)};

// CHEBY (ch.9.3), the Clenshaw form the Manual gives in FORTRAN: C(1..N) there is c[0..n-1].
// The real chip's stack after a derived function holds -A aligned with D, as this order
// gives.
template <size_t N>
double cheby(double x, const double (&c)[N]) {
    double a = 0, b = 0, d = c[N - 1];
    double x2 = 2 * x;  // exact
    for (size_t i = N - 1; i >= 1; --i) {
        a = b;
        b = d;
        d = add(sub(mul(x2, b), a), c[i - 1]);
    }
    return sub(d, a) / 2;  // exact
}
// X * CHEBY(2X^2 - 1): the odd series of SIN, TAN and ATAN.
template <size_t N>
double odd(double x, const double (&c)[N]) {
    return mul(x, cheby(sub(2 * mul(x, x), 1), c));  // ⚠ SIN printed "2X^2 - 0.1"
}

double entier(double x) { return std::floor(x); }  // rounds down: ENTIER(-2.5) = -3

// SIN (ch.9.4) on Y = X / (PI/2), cut to 23 bits. COS is the same on (PI/2 - X): the
// real chip's reduced argument is on the grid of PI/2 - X, not on that of X.
// ⚠ A few reduced arguments on the real chip keep a 24th quotient bit (COS of -1.9 and of
// -5.2, SIN of 3.2); no rule for them is known.
double sinOf(double y) {
    y = t24(y - 4 * entier((y + 1) / 4));
    if (y > 1) y = sub(2, y);
    return odd(y, kCsin);
}
double fsin(double x) { return sinOf(dvd23(x, kHalfPi)); }
double fcos(double x) { return sinOf(dvd23(sub(kHalfPi, x), kHalfPi)); }

// TAN (ch.9.6). ⚠ printed X*2/PI; X/(PI/4) is what closes, and what the command page's
// -PI/4 ... +PI/4 reduction says. Sets *pole when the cotangent branch divides by 0.
double ftan(double x, bool* pole) {
    x = dvd(x, kQuarterPi);
    x = t24(x - 4 * entier((x + 1) / 4));
    double y = x;
    if (y > 1) x = sub(2, x);
    double t = odd(x, kCtan);
    if (y > 1) {
        if (t == 0) { *pole = true; return 0; }
        t = dvd(1, t);
    }
    return t;
}

// SQRT (ch.9.14): split off an even exponent, a minimax first guess on the mantissa, then
// two Newton-Raphson steps. ⚠ the printed FORTRAN divides X, not the mantissa, in the
// Newton steps; the prose (and arithmetic) says the mantissa.
double fsqrt(double x) {
    if (x == 0) return 0;
    int e;
    double m = std::frexp(x, &e);  // the chip's own mantissa and exponent
    if (e & 1) { ++e; m /= 2; }
    double x0 = add(mul(t24(0.68629150), m), t24(0.34314575));
    double x1 = dvd(add(dvd(m, x0), x0), 2);
    double x2 = dvd(add(dvd(m, x1), x1), 2);
    return std::ldexp(x2, e / 2);
}

// ATAN (ch.9.9). The real chip takes the reciprocal at |X| = 1 too: ATAN 1 is 00C90FD8,
// which only that branch gives.
double fatan(double x) {
    double u = x;
    if (std::fabs(u) >= 1) x = dvd(1, x);
    double a = odd(x, kCatan);
    if (u >= 1) a = sub(kHalfPi, a);
    if (u <= -1) a = sub(-kHalfPi, a);
    return a;
}

// ASIN. The real chip does not use the ASIN series the Manual prints (ch.9.7): its stack
// holds the ATAN series' values. ASIN X = ATAN (X / SQRT(1 - X^2)), with 1 - X^2 as
// (1 - |X|)(1 + |X|); where that quotient is 1 or more, the ATAN of its reciprocal
// SQRT(1 - X^2) / |X| is taken from PI/2.
double fasin(double x) {
    double ax = std::fabs(x);
    double q = fsqrt(mul(sub(1, ax), add(1, ax)));
    double r;
    if (dvd(ax, q) >= 1) r = sub(kHalfPi, odd(dvd(q, ax), kCatan));
    else r = odd(dvd(ax, q), kCatan);
    return x < 0 ? -r : r;
}
// ACOS (ch.9.8): PI/2 less the ASIN.
double facos(double x) { return sub(kHalfPi, fasin(x)); }

// EXP (ch.9.10): 2^N times a series on the fraction. The reduction is an FDIV by LN 2.
double fexp(double x) {
    x = dvd(x, kLn2);
    double n = 1 + entier(x);
    return std::ldexp(cheby(t24(2 * (n - x) - 1), kCexp), (int)n);
}

// LN (ch.9.11): a series on the mantissa plus the exponent times LN 2.
double fln(double x) {
    int e;
    double m = std::frexp(x, &e);
    return add(cheby(sub(4 * m, 3), kCln), mul(e - 1, kLn2));
}

// The derived-function command list, by op code.
enum : uint8_t {
    SQRT = 0x01, SIN = 0x02, COS = 0x03, TAN = 0x04, ASIN = 0x05, ACOS = 0x06, ATAN = 0x07,
    LOG = 0x08, LN = 0x09, EXP = 0x0A, PWR = 0x0B,
    FADD = 0x10, FSUB = 0x11, FMUL = 0x12, FDIV = 0x13, CHSF = 0x15, PTOF = 0x17,
    POPF = 0x18, XCHF = 0x19, PUPI = 0x1A, FLTD = 0x1C, FLTS = 0x1D, FIXD = 0x1E, FIXS = 0x1F,
    DADD = 0x2C, DSUB = 0x2D, DMUL = 0x2E, DDIV = 0x2F, CHSD = 0x34, DMUU = 0x36, PTOD = 0x37,
    POPD = 0x38, XCHD = 0x39,
    SADD = 0x6C, SSUB = 0x6D, SMUL = 0x6E, SDIV = 0x6F, CHSS = 0x74, SMUU = 0x76, PTOS = 0x77,
    POPS = 0x78, XCHS = 0x79,
    NOP = 0x00,
};

// SIGN and ZERO for a value of each width.
uint8_t sz16(uint16_t v) { return (v & 0x8000 ? Am9511a::kSign : 0) | (v == 0 ? Am9511a::kZero : 0); }
uint8_t sz32(uint32_t v) { return (v & 0x80000000u ? Am9511a::kSign : 0) | (v == 0 ? Am9511a::kZero : 0); }
uint8_t szF(uint32_t v) {
    return (v & 0x80000000u ? Am9511a::kSign : 0) | (v & 0x800000 ? 0 : Am9511a::kZero);
}

}  // namespace

// ---------------------------------------------------------------------------
// THE FLOAT FORMAT (p.7-71): sign in bit 31, a 7-bit two's complement exponent in bits
// 30-24, a 24-bit mantissa 0.5 <= m < 1 with no hidden bit. Zero is all zeros.
// ---------------------------------------------------------------------------
uint32_t Am9511a::toFloat(double x, uint8_t* err) {
    if (err) *err = 0;
    if (x == 0) return 0;
    int e;
    double m = std::frexp(std::fabs(x), &e);
    uint32_t mant = (uint32_t)std::ldexp(m, 24);  // low bits past 24 are dropped
    if (err && e > 63) *err = kOverflow;
    if (err && e < -64) *err = kUnderflow;
    // "The mantissa is correct and the exponent is offset by 128": the field wraps.
    uint32_t e7 = (uint32_t)(e + 64) & 0x7F;
    e7 = (e7 - 64) & 0x7F;
    return (x < 0 ? 0x80000000u : 0) | (e7 << 24) | mant;
}

double Am9511a::fromFloat(uint32_t v) {
    if (!(v & 0x800000)) return 0;  // mantissa MSB clear: zero
    int e = (int)((v >> 24) & 0x7F);
    if (e & 0x40) e -= 128;
    double m = std::ldexp((double)(v & 0xFFFFFF), e - 24);
    return (v & 0x80000000u) ? -m : m;
}

// ---------------------------------------------------------------------------
// THE COMMANDS (pp.7-72 ... 7-86). Each one sets the whole status register; its cycle count
// is the top of the data sheet's range.
// ---------------------------------------------------------------------------
unsigned Am9511a::execute(uint8_t op) {
    switch (op) {
    case NOP:
        status_ = 0;  // NOP clears the status byte
        return 4;

    // ---- 16-bit fixed point: B op A ----
    case SADD: case SSUB: case SMUL: case SMUU: case SDIV: {
        int32_t a = (int16_t)get16(0), b = (int16_t)get16(1);
        uint16_t ua = (uint16_t)a, ub = (uint16_t)b;
        uint8_t st = 0;
        int32_t r = 0;
        unsigned cyc = 0;
        switch (op) {
        case SADD:
            r = b + a;
            if (ub + ua > 0xFFFF) st |= kCarry;
            if (r != (int16_t)r) st |= kOverflow;
            cyc = 18;
            break;
        case SSUB:
            r = b - a;
            if (ub < ua) st |= kCarry;  // borrow
            if (r != (int16_t)r || a == -32768) st |= kOverflow;
            cyc = 32;
            break;
        // The most negative value (8000H) sets overflow in SMUL, SMUU and SDIV. What R is
        // then was measured on the real chip (#908); the data sheet calls it meaningless.
        case SMUL:  // the low half of the product
            r = b * a;
            if (r != (int16_t)r || a == -32768 || b == -32768) st |= kOverflow;
            cyc = 94;
            break;
        case SMUU:  // the high half; but B = 8000H times a negative A gives 7FFFH
            if (a == -32768 || b == -32768) st |= kOverflow;
            r = (b == -32768 && a < 0 && a != -32768) ? 0x7FFF : (b * a) >> 16;
            cyc = 98;
            break;
        default:  // SDIV
            if (a == 0) { r = b; st |= kDivZero; cyc = 14; break; }
            cyc = 94;
            if (b == -32768 && a < 0 && a != -32768) {
                // Not modeled: the chip gives 8000/FFFF = FFFE, 8000/FFFE = FFFC,
                // 8000/C000 = AAAA, with no rule found. R here is the low 16 bits of B / A.
                r = b / a;
                st |= kOverflow;
            } else if (a == -32768 || b == -32768) {
                r = 0;  // either operand 8000H: R = 0
                st |= kOverflow;
            } else {
                r = b / a;  // no remainder; the quotient is truncated toward zero
            }
            break;
        }
        drop(2);
        set16(0, (uint16_t)r);
        status_ = st | sz16((uint16_t)r);
        return cyc;
    }

    // ---- 32-bit fixed point: B op A ----
    case DADD: case DSUB: case DMUL: case DMUU: case DDIV: {
        const int64_t kMin = INT32_MIN;
        int64_t a = (int32_t)get32(0), b = (int32_t)get32(1);
        uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
        uint8_t st = 0;
        int64_t r = 0;
        unsigned cyc = 0;
        switch (op) {
        case DADD:
            r = b + a;
            if ((uint64_t)ub + ua > 0xFFFFFFFFu) st |= kCarry;
            if (r != (int32_t)r) st |= kOverflow;
            cyc = 22;
            break;
        case DSUB:
            r = b - a;
            if (ub < ua) st |= kCarry;
            if (r != (int32_t)r || a == kMin) st |= kOverflow;
            cyc = 40;
            break;
        // The most negative value as in the 16-bit commands above, measured (#908).
        case DMUL:
            r = b * a;
            if (r != (int32_t)r || a == kMin || b == kMin) st |= kOverflow;
            cyc = 210;
            break;
        case DMUU:
            if (a == kMin || b == kMin) st |= kOverflow;
            r = (b == kMin && a < 0 && a != kMin) ? 0x7FFFFFFF : (b * a) >> 32;
            cyc = 218;
            break;
        default:  // DDIV
            if (a == 0) { r = b; st |= kDivZero; cyc = 18; break; }
            cyc = 210;
            if (b == kMin && a < 0 && a != kMin) {
                r = b / a;  // not modeled, as SDIV; the chip gave 80000000/FFFFFFFE = FFFFFFFC
                st |= kOverflow;
            } else if (a == kMin || b == kMin) {
                r = 0;
                st |= kOverflow;
            } else {
                r = b / a;
            }
            break;
        }
        drop(4);
        set32(0, (uint32_t)r);
        status_ = st | sz32((uint32_t)r);
        return cyc;
    }

    // ---- 32-bit floating point: B op A ----
    case FADD: case FSUB: case FMUL: case FDIV: {
        double a = fromFloat(get32(0)), b = fromFloat(get32(1));
        uint8_t st = 0;
        uint32_t r;
        unsigned cyc;
        if (op == FDIV && a == 0) {
            r = get32(1);  // "R is set equal to B"
            st = kDivZero;
            cyc = 22;
        } else {
            double x = op == FADD ? add(b, a) : op == FSUB ? sub(b, a) : op == FMUL ? mul(b, a) : dvd(b, a);
            r = toFloat(x, &st);
            static const unsigned kTop[] = {368, 370, 168, 184};
            static const unsigned kZeroA[] = {24, 26, 168, 184};
            cyc = a == 0 ? kZeroA[op - FADD] : kTop[op - FADD];
        }
        drop(4);
        set32(0, r);
        status_ = st | szF(r);
        return cyc;
    }

    // ---- the derived functions: TOS = f(TOS); PWR: TOS = B ^ A ----
    case SQRT: case SIN: case COS: case TAN: case ASIN: case ACOS: case ATAN:
    case LOG: case LN: case EXP: case PWR: {
        double a = fromFloat(get32(0));
        uint8_t err = 0;
        bool keep = false;  // an argument error leaves the operand in place
        double r = 0;
        unsigned cyc = 0;
        switch (op) {
        case SQRT:
            if (a < 0) {
                // The real chip stops at once and leaves the mantissa with exponent 0 and the
                // sign: SQRT -4 = 80800000, SQRT -3 = 80C00000.
                err = kNegArg;
                keep = true;
                set32(0, 0x80000000u | (get32(0) & 0xFFFFFF));
                cyc = 20;
            } else if (a == 0) {
                cyc = 20;
            } else {
                r = fsqrt(a);
                cyc = 870;
            }
            break;
        case SIN:
            if (std::fabs(a) <= std::ldexp(1.0, -12)) { r = a; cyc = 30; }  // sin A = A
            else { r = fsin(a); cyc = 4808; }
            break;
        case COS:
            r = fcos(a);
            cyc = 4878;
            break;
        case TAN:
            if (std::fabs(a) <= std::ldexp(1.0, -12)) { r = a; cyc = 30; break; }
            {
                bool pole = false;
                r = ftan(a, &pole);
                // At a pole the cotangent step's FDIV divides 1 by 0, and the real chip ends
                // there: R = B = 1, divide-by-zero.
                if (pole) { err = kDivZero; r = 1; }
            }
            cyc = 5886;
            break;
        case ASIN:
        case ACOS:
            if (std::fabs(a) > 1) { err = kArgRange; keep = true; cyc = 140; break; }
            cyc = op == ASIN ? 7938 : 8284;
            r = op == ASIN ? fasin(a) : facos(a);
            break;
        case ATAN:
            r = fatan(a);
            cyc = 6536;
            break;
        case LN:
        case LOG:
            if (a <= 0) { err = kNegArg; keep = true; cyc = 20; break; }
            r = fln(a);
            if (op == LOG) r = mul(r, kLog10e);  // ch.9.12
            cyc = op == LN ? 6956 : 7132;
            break;
        case EXP:
            if (std::fabs(a) > 32) { err = kArgRange; keep = true; cyc = 34; break; }
            r = fexp(a);
            cyc = 4878;
            break;
        default: {  // PWR: B ^ A = EXP(A * LN B) (ch.9.13)
            double b = fromFloat(get32(1));
            uint32_t rb = get32(1);
            drop(4);  // A is consumed either way; R replaces B
            if (b <= 0) { err = kNegArg; set32(0, rb); status_ = err | szF(rb); return 20; }
            double y = mul(a, fln(b));
            if (std::fabs(y) > 32) {
                // The real chip stops after the LN and the FMUL and leaves A x LN B.
                uint32_t ry = toFloat(y);
                set32(0, ry);
                status_ = kArgRange | szF(ry);
                return 6956 + 168;
            }
            uint32_t rv = toFloat(fexp(y), &err);
            set32(0, rv);
            status_ = err | szF(rv);
            return 12032;
        }
        }
        uint32_t rv = get32(0);
        if (!keep) {
            uint8_t range = 0;
            rv = toFloat(r, &range);
            err |= range;
            set32(0, rv);
        }
        status_ = err | szF(rv);
        return cyc;
    }

    // ---- conversions ----
    case FIXS: case FIXD: {
        uint32_t a = get32(0);
        double t = std::trunc(fromFloat(a));  // the integer portion of A
        double lim = op == FIXS ? 32768.0 : 2147483648.0;
        if (std::fabs(t) >= lim) {  // larger than 15 [31] bits: A is not changed
            status_ = kOverflow | szF(a);
            return 20;  // the real chip stops at once
        }
        drop(4);
        if (op == FIXS) {
            lift(2);
            set16(0, (uint16_t)(int16_t)t);
            status_ = sz16(get16(0));
            return 214;
        }
        lift(4);
        set32(0, (uint32_t)(int32_t)t);
        status_ = sz32(get32(0));
        return 336;
    }
    case FLTS: {
        double a = (int16_t)get16(0);
        drop(2);
        lift(4);
        set32(0, toFloat(a));
        status_ = szF(get32(0));
        return 156;
    }
    case FLTD: {
        set32(0, toFloat((int32_t)get32(0)));  // truncated when it has more than 24 bits
        status_ = szF(get32(0));
        return 342;
    }

    // ---- change sign ----
    case CHSS: {
        uint16_t a = get16(0);
        if (a == 0x8000) status_ = kOverflow | sz16(a);  // no positive equivalent
        else { set16(0, (uint16_t)-a); status_ = sz16(get16(0)); }
        return 24;
    }
    case CHSD: {
        uint32_t a = get32(0);
        if (a == 0x80000000u) status_ = kOverflow | sz32(a);
        else { set32(0, 0u - a); status_ = sz32(get32(0)); }
        return 28;
    }
    case CHSF: {
        uint32_t a = get32(0);
        if (a & 0x800000) set32(0, a ^ 0x80000000u);  // zero (mantissa MSB 0): no change
        status_ = szF(get32(0));
        return 20;
    }

    // ---- stack manipulation ----
    case PTOS: { uint16_t a = get16(0); lift(2); set16(0, a); status_ = sz16(a); return 16; }
    case PTOD: { uint32_t a = get32(0); lift(4); set32(0, a); status_ = sz32(a); return 20; }
    case PTOF: { uint32_t a = get32(0); lift(4); set32(0, a); status_ = szF(a); return 20; }
    case POPS: drop(2); status_ = sz16(get16(0)); return 10;
    case POPD: drop(4); status_ = sz32(get32(0)); return 12;
    case POPF: drop(4); status_ = szF(get32(0)); return 12;
    case XCHS: { uint16_t a = get16(0); set16(0, get16(1)); set16(1, a); status_ = sz16(get16(0)); return 18; }
    case XCHD: { uint32_t a = get32(0); set32(0, get32(1)); set32(1, a); status_ = sz32(get32(0)); return 26; }
    case XCHF: { uint32_t a = get32(0); set32(0, get32(1)); set32(1, a); status_ = szF(get32(0)); return 26; }
    case PUPI: lift(4); set32(0, toFloat(kPi)); status_ = szF(get32(0)); return 16;

    default:
        return 4;  // "results are undefined": nothing is done
    }
}

}  // namespace altair
