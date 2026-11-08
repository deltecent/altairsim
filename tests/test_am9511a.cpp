#include "test.h"

#include "chips/am9511a.h"
#include "core/statefile.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace altair;

// The Am9511A (chips/am9511a.h) driven through its two ports, with no board and no bus.
// Every expected value comes from reference/AMD 9511A.md: the data sheet's command pages,
// the worked encodings, the SS-1 manual's test program, and the ch.9 error limits.

namespace {

// A chip and its CLK count. Each command runs to its end before the next access, as PAUSE
// makes it on a real bus.
struct Apu {
    Am9511a  c{"apu"};
    uint64_t now = 1000;

    void push16(uint16_t v) { c.writeData((uint8_t)v, now); c.writeData((uint8_t)(v >> 8), now); }
    void push32(uint32_t v) { for (int i = 0; i < 4; ++i) c.writeData((uint8_t)(v >> 8 * i), now); }
    void pushF(double x) { push32(Am9511a::toFloat(x)); }
    uint16_t pop16() {
        uint16_t v = (uint16_t)(c.readData(now) << 8);
        return (uint16_t)(v | c.readData(now));
    }
    uint32_t pop32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = v << 8 | c.readData(now);
        return v;
    }
    double popF() { return Am9511a::fromFloat(pop32()); }
    // Write a command, run it to its end, and read the status.
    uint8_t run(uint8_t cmd) {
        c.writeCommand(cmd, now);
        now = c.busyUntil();
        return c.readStatus(now);
    }
    unsigned cycles(uint8_t cmd) {
        uint64_t t0 = now;
        c.writeCommand(cmd, now);
        now = c.busyUntil();
        return (unsigned)(now - t0);
    }
};

double rel(double got, double want) { return std::fabs(got - want) / std::fabs(want); }

// The worst error of a derived function over a range, against host math at the operand the
// chip actually received (the 24-bit rounded input).
double worst(uint8_t cmd, double lo, double hi, int n, double (*f)(double), bool relative) {
    double w = 0;
    for (int i = 0; i <= n; ++i) {
        Apu a;
        double x = Am9511a::fromFloat(Am9511a::toFloat(lo + (hi - lo) * i / n));
        a.pushF(x);
        a.run(cmd);
        double want = f(x), got = a.popF();
        double e = relative ? (want == 0 ? std::fabs(got) : rel(got, want)) : std::fabs(got - want);
        if (e > w) w = e;
    }
    return w;
}

// RESULTS FROM REAL CHIPS (issue #908). Each row ran on a CompuPro System Support 1 with an
// AMD 9511A, through a test program that pushes the operands (B first, A last), writes the
// command, polls until BUSY is 0, then reads the status and pops the result (MS byte first).
// Two boards with two different chips gave the same status and result for the first 108 rows
// (the 60 fixed and 48 random cases of #908); the rest are from the second board only.
//
// Left out: SDIV and DDIV of the most negative value by a negative divisor other than the
// most negative value. The chip returns 8000/FFFF = FFFE, 8000/FFFE = FFFC, 8000/C000 = AAAA
// and 80000000/FFFFFFFE = FFFFFFFC, with no rule found; the data sheet says R is
// meaningless.
//
// MATCH says how close the simulator must come:
//   '=' the same status and result, bit for bit: 507 of the 550 rows, every command.
//   '2' the same status and a result 1 or 2 LSB from the real one: 35 rows of SIN, COS,
//       ATAN, ASIN, ACOS, EXP and PWR. The difference is in some step of the series that
//       the chip's stack does not show.
//   'R' the same status and a result within 1e-6. Eight SIN and COS rows where the real
//       chip's reduced argument has a 24th quotient bit, not the 23 it has in all other
//       cases; with 24 bits the simulator gives the real result for seven of them. No rule
//       for the 24th bit is known. SIN of -2.80 (82B33333) is 11 LSB low either way.
struct RealCase {
    uint8_t     cmd;
    const char* ops;     // hex, pushed in this order; 4 digits = 16 bits, 8 digits = 32 bits
    uint8_t     status;
    const char* result;  // the bytes popped, MS byte first
    char        match;   // '=', '2' or 'R', above
    const char* name;
};
const RealCase kReal[] = {
    {0x10, "01800000 69800000", 0x00, "01800000", '=', "FADD 1.0 + 1/2 LSB (tie, even base)"},
    {0x10, "01800001 69800000", 0x00, "01800001", '=', "FADD 1+LSB + 1/2 LSB (tie, odd base)"},
    {0x10, "01800000 69C00000", 0x00, "01800000", '=', "FADD 1.0 + 3/4 LSB"},
    {0x10, "01800000 68800000", 0x00, "01800000", '=', "FADD 1.0 + 1/4 LSB"},
    {0x10, "01800001 68800000", 0x00, "01800001", '=', "FADD 1+LSB + 1/4 LSB"},
    {0x10, "81800001 E9800000", 0x40, "81800001", '=', "FADD -(1+LSB) + -(1/2 LSB) (neg tie)"},
    {0x10, "81800000 E9C00000", 0x40, "81800000", '=', "FADD -1.0 + -(3/4 LSB)"},
    {0x11, "01800000 68800000", 0x00, "01800000", '=', "FSUB 1.0 - 2^-25 (tie after renorm)"},
    {0x11, "01800000 69800000", 0x00, "01800000", '=', "FSUB 1.0 - 2^-24 (exact)"},
    {0x11, "01800000 67C00000", 0x00, "01800000", '=', "FSUB 1.0 - 3*2^-27"},
    {0x12, "02C00000 01800001", 0x00, "02C00000", '=', "FMUL 3 * (1+2^-23) (tie, odd base)"},
    {0x12, "02C00000 01800003", 0x00, "02C00004", '=', "FMUL 3 * (1+3*2^-23) (tie, even base)"},
    {0x12, "02C00000 01800005", 0x00, "02C00006", '=', "FMUL 3 * (1+5*2^-23) (tie, odd base)"},
    {0x13, "01800000 02C00000", 0x00, "7FAAAAAA", '=', "FDIV 1 / 3"},
    {0x13, "01800000 03E00000", 0x00, "7E924924", '=', "FDIV 1 / 7"},
    {0x13, "02800000 02C00000", 0x00, "00AAAAAA", '=', "FDIV 2 / 3"},
    {0x13, "01800000 00000000", 0x10, "01800000", '=', "FDIV 1 / 0"},
    {0x12, "3F800000 3F800000", 0x02, "7D800000", '=', "FMUL overflow 2^62 * 2^62"},
    {0x12, "45800000 45800000", 0x04, "09800000", '=', "FMUL underflow 2^-60 * 2^-60"},
    {0x1C, "00003039", 0x00, "0EC0E400", '=', "FLTD 12345"},
    {0x1D, "3039", 0x00, "0EC0E400", '=', "FLTS 12345"},
    {0x1C, "FFFFFD5A", 0x40, "8AA98000", '=', "FLTD -678"},
    {0x1E, "07F78000", 0x00, "0000007B", '=', "FIXD 123.75"},
    {0x1E, "02A00000", 0x00, "00000002", '=', "FIXD 2.5"},
    {0x1E, "82A00000", 0x40, "FFFFFFFE", '=', "FIXD -2.5"},
    {0x1F, "02E00000", 0x00, "0003", '=', "FIXS 3.5"},
    {0x1F, "82E00000", 0x40, "FFFD", '=', "FIXS -3.5"},
    {0x1F, "109C4000", 0x02, "109C", '=', "FIXS 40000.0 (overflow)"},
    {0x6D, "0001 0002", 0x41, "FFFF", '=', "SSUB 1 - 2"},
    {0x6E, "0100 0100", 0x22, "0000", '=', "SMUL 0100 * 0100 (overflow)"},
    {0x76, "0100 0100", 0x00, "0001", '=', "SMUU 0100 * 0100"},
    {0x6E, "8000 0002", 0x22, "0000", '=', "SMUL 8000 * 0002 (most negative)"},
    {0x6F, "0007 0000", 0x10, "0007", '=', "SDIV 7 / 0"},
    {0x6F, "FFF9 0002", 0x40, "FFFD", '=', "SDIV -7 / 2"},
    {0x74, "8000", 0x42, "8000", '=', "CHSS 8000"},
    {0x2C, "7FFFFFFF 00000001", 0x42, "80000000", '=', "DADD 7FFFFFFF + 1"},
    {0x2E, "00010000 00010000", 0x22, "00000000", '=', "DMUL 10000 * 10000 (overflow)"},
    {0x36, "00010000 00010000", 0x00, "00000001", '=', "DMUU 10000 * 10000"},
    {0x2F, "000F4240 00000007", 0x00, "00022E09", '=', "DDIV 1000000 / 7"},
    {0x2F, "FFF0BDC0 00000007", 0x40, "FFFDD1F7", '=', "DDIV -1000000 / 7"},
    {0x01, "02800000", 0x00, "01B504F3", '=', "SQRT 2"},
    {0x01, "00800000", 0x00, "00B504F3", '=', "SQRT 0.5"},
    {0x01, "81800000", 0x48, "80800000", '=', "SQRT -1 (error)"},
    {0x02, "01800000", 0x00, "00D76AA2", '=', "SIN 1"},
    {0x02, "00800000", 0x00, "7FF5773E", '=', "SIN 0.5"},
    {0x03, "01800000", 0x00, "008A513E", '=', "COS 1"},
    {0x04, "00800000", 0x00, "008BDA79", '=', "TAN 0.5"},
    {0x05, "00800000", 0x00, "00860A91", '=', "ASIN 0.5"},
    {0x06, "00800000", 0x00, "01860A92", '=', "ACOS 0.5"},
    {0x05, "02800000", 0x18, "02800000", '=', "ASIN 2 (error)"},
    {0x07, "01800000", 0x00, "00C90FD8", '=', "ATAN 1"},
    {0x08, "02800000", 0x00, "7F9A2099", '=', "LOG 2"},
    {0x09, "02800000", 0x00, "00B17217", '=', "LN 2"},
    {0x09, "00800000", 0x40, "80B17215", '=', "LN 0.5"},
    {0x0A, "01800000", 0x00, "02ADF855", '=', "EXP 1"},
    {0x0A, "81800000", 0x00, "7FBC5AB0", '=', "EXP -1"},
    {0x0B, "02800000 00800000", 0x00, "01B504F3", '=', "PWR 2^0.5"},
    {0x0B, "04A00000 02C00000", 0x00, "0AF9FFF2", '=', "PWR 10^3"},
    {0x1A, "01800000", 0x00, "02C90FDA", '=', "PUPI (after a dummy push)"},
    {0x00, "81800000", 0x00, "81800000", '=', "NOP (status after)"},
    {0x10, "FDFE8F9D 75C757A0", 0x40, "FDFDC846", '=', "FADD rand 0 FDFE8F9D 75C757A0"},
    {0x10, "F79A2F4B 7FFDA85E", 0x00, "7FFD0E2F", '=', "FADD rand 1 F79A2F4B 7FFDA85E"},
    {0x10, "82AB7C44 6A85881F", 0x40, "82AB7C44", '=', "FADD rand 2 82AB7C44 6A85881F"},
    {0x10, "FAD8064E 7ADD0725", 0x00, "75A01AE0", '=', "FADD rand 3 FAD8064E 7ADD0725"},
    {0x10, "059493BB 03BC4491", 0x00, "05C3A4DF", '=', "FADD rand 4 059493BB 03BC4491"},
    {0x10, "E680A908 7EDB30FC", 0x00, "7EDB30FC", '=', "FADD rand 5 E680A908 7EDB30FC"},
    {0x10, "7CF2BF03 7AC9CC3C", 0x00, "7D929909", '=', "FADD rand 6 7CF2BF03 7AC9CC3C"},
    {0x10, "6799B3AA 7CD5C580", 0x00, "7CD5C584", '=', "FADD rand 7 6799B3AA 7CD5C580"},
    {0x10, "FBC6FCEE 79EF3B7D", 0x40, "FB8B2E0F", '=', "FADD rand 8 FBC6FCEE 79EF3B7D"},
    {0x10, "72C2002D 80DB2D2B", 0x40, "80DB2A23", '=', "FADD rand 9 72C2002D 80DB2D2B"},
    {0x10, "7EE97467 76E381C6", 0x00, "7EEA57E8", '=', "FADD rand 10 7EE97467 76E381C6"},
    {0x10, "E9EFE4EF 81BBEB7D", 0x40, "81BBEB7D", '=', "FADD rand 11 E9EFE4EF 81BBEB7D"},
    {0x11, "7EC3C7A0 E4F491EC", 0x00, "7EC3C7A0", '=', "FSUB rand 0 7EC3C7A0 E4F491EC"},
    {0x11, "77CABFD2 86CA8B07", 0x00, "06CA8C9C", '=', "FSUB rand 1 77CABFD2 86CA8B07"},
    {0x11, "FFBE1FB7 FF813AB9", 0x40, "FDF393F8", '=', "FSUB rand 2 FFBE1FB7 FF813AB9"},
    {0x11, "74EF11E2 04B22677", 0x40, "84B22588", '=', "FSUB rand 3 74EF11E2 04B22677"},
    {0x11, "7AFB3006 6E8A5913", 0x00, "7AFB2761", '=', "FSUB rand 4 7AFB3006 6E8A5913"},
    {0x11, "EDD0B8E3 FCCD82A1", 0x00, "7CCD8100", '=', "FSUB rand 5 EDD0B8E3 FCCD82A1"},
    {0x11, "00D17D8F FFFC797F", 0x00, "01A7DD27", '=', "FSUB rand 6 00D17D8F FFFC797F"},
    {0x11, "F3BB29AF FDE0615C", 0x00, "7DE03292", '=', "FSUB rand 7 F3BB29AF FDE0615C"},
    {0x11, "86BA7513 F7C7F2F8", 0x40, "86BA7384", '=', "FSUB rand 8 86BA7513 F7C7F2F8"},
    {0x11, "6EEDC35B 86B404CB", 0x00, "06B404CB", '=', "FSUB rand 9 6EEDC35B 86B404CB"},
    {0x11, "FAA53808 7ACCE251", 0x40, "FBB90D2C", '=', "FSUB rand 10 FAA53808 7ACCE251"},
    {0x11, "73F80364 80A4315C", 0x00, "00A4391C", '=', "FSUB rand 11 73F80364 80A4315C"},
    {0x12, "04DB7B8C 7DE9E68B", 0x00, "01C8891A", '=', "FMUL rand 0 04DB7B8C 7DE9E68B"},
    {0x12, "86D7FDF0 7ECC98F6", 0x40, "84AC9F69", '=', "FMUL rand 1 86D7FDF0 7ECC98F6"},
    {0x12, "85C03613 049DCB4E", 0x40, "88ECF39E", '=', "FMUL rand 2 85C03613 049DCB4E"},
    {0x12, "06B7FD30 7DED6B1E", 0x00, "03AAA261", '=', "FMUL rand 3 06B7FD30 7DED6B1E"},
    {0x12, "7AFBA184 02C2A413", 0x00, "7CBF51AC", '=', "FMUL rand 4 7AFBA184 02C2A413"},
    {0x12, "FDABF0D5 FACE1893", 0x00, "778A6C4C", '=', "FMUL rand 5 FDABF0D5 FACE1893"},
    {0x12, "028E4F73 7AFD973B", 0x00, "7C8CF896", '=', "FMUL rand 6 028E4F73 7AFD973B"},
    {0x12, "7EA4203E 82ED4D6D", 0x40, "8098237C", '=', "FMUL rand 7 7EA4203E 82ED4D6D"},
    {0x12, "7BC83947 82D9C337", 0x40, "FDAA513B", '=', "FMUL rand 8 7BC83947 82D9C337"},
    {0x12, "01C8957C 7D985EA1", 0x00, "7DEEC5CC", '=', "FMUL rand 9 01C8957C 7D985EA1"},
    {0x12, "7BE08449 05BCF203", 0x00, "00A5B565", '=', "FMUL rand 10 7BE08449 05BCF203"},
    {0x12, "7BBFBAB2 8097B5C7", 0x40, "FAE33E86", '=', "FMUL rand 11 7BBFBAB2 8097B5C7"},
    {0x13, "8588A826 7ABAE464", 0x40, "8BBB3060", '=', "FDIV rand 0 8588A826 7ABAE464"},
    {0x13, "01F809C2 848AB25B", 0x40, "FEE4E89F", '=', "FDIV rand 1 01F809C2 848AB25B"},
    {0x13, "80829087 06BF132D", 0x40, "FAAEEDCC", '=', "FDIV rand 2 80829087 06BF132D"},
    {0x13, "82DE8C40 05A1B1D9", 0x40, "FEB02C19", '=', "FDIV rand 3 82DE8C40 05A1B1D9"},
    {0x13, "7CC2EF8A FA895492", 0x40, "83B5B10B", '=', "FDIV rand 4 7CC2EF8A FA895492"},
    {0x13, "00DECB5C 02BB5FD0", 0x00, "7F983244", '=', "FDIV rand 5 00DECB5C 02BB5FD0"},
    {0x13, "FAAFE40E 7F986A13", 0x40, "FC93B746", '=', "FDIV rand 6 FAAFE40E 7F986A13"},
    {0x13, "05ED45E9 7AA12ACA", 0x00, "0CBC719F", '=', "FDIV rand 7 05ED45E9 7AA12ACA"},
    {0x13, "84E9ECAE 00E4AC6A", 0x40, "8582F073", '=', "FDIV rand 8 84E9ECAE 00E4AC6A"},
    {0x13, "01DE95C9 85E1CC1B", 0x40, "FCFC5BAE", '=', "FDIV rand 9 01DE95C9 85E1CC1B"},
    {0x13, "06F723E7 80B52405", 0x40, "87AEA330", '=', "FDIV rand 10 06F723E7 80B52405"},
    {0x13, "FADE93EB FFDDFBB7", 0x00, "7C8057C3", '=', "FDIV rand 11 FADE93EB FFDDFBB7"},
    {0x01, "82800000", 0x48, "80800000", '=', "SQRT -2"},
    {0x01, "82C00000", 0x48, "80C00000", '=', "SQRT -3"},
    {0x01, "83800000", 0x48, "80800000", '=', "SQRT -4"},
    {0x01, "84800000", 0x48, "80800000", '=', "SQRT -8"},
    {0x01, "81C00000", 0x48, "80C00000", '=', "SQRT -1.5"},
    {0x01, "80800000", 0x48, "80800000", '=', "SQRT -0.5"},
    {0x01, "FF800000", 0x48, "80800000", '=', "SQRT -0.25"},
    {0x01, "FE800000", 0x48, "80800000", '=', "SQRT -0.125"},
    {0x01, "02C00000", 0x00, "01DDB3D7", '=', "SQRT 3"},
    {0x01, "00000000", 0x20, "00000000", '=', "SQRT 0"},
    {0x09, "81800000", 0x48, "81800000", '=', "LN -1"},
    {0x09, "00000000", 0x28, "00000000", '=', "LN 0"},
    {0x08, "00000000", 0x28, "00000000", '=', "LOG 0"},
    {0x08, "82800000", 0x48, "82800000", '=', "LOG -2"},
    {0x0A, "06A00000", 0x18, "06A00000", '=', "EXP 40"},
    {0x0A, "86A00000", 0x58, "86A00000", '=', "EXP -40"},
    {0x06, "02800000", 0x18, "02800000", '=', "ACOS 2"},
    {0x05, "81C00000", 0x58, "81C00000", '=', "ASIN -1.5"},
    {0x0B, "82800000 02800000", 0x48, "82800000", '=', "PWR (-2)^2"},
    {0x0B, "00000000 02800000", 0x28, "00000000", '=', "PWR 0^2"},
    {0x0B, "02800000 07C80000", 0x18, "078AA121", '=', "PWR 2^100"},
    {0x0B, "02C00000 02800000", 0x00, "048FFFFB", '=', "PWR 3^2"},
    {0x1E, "20800000", 0x02, "20800000", '=', "FIXD 2^31"},
    {0x1F, "90800000", 0x42, "9080", '=', "FIXS -32768"},
    {0x1F, "10800000", 0x02, "1080", '=', "FIXS 32768"},
    {0x04, "01C90FDB", 0x10, "01800000", '=', "TAN 1.5707964 (past pi/2)"},
    {0x04, "01C90FDA", 0x10, "01800000", '=', "TAN 1.5707963 (below pi/2)"},
    {0x6E, "8000 0001", 0x42, "8000", '=', "SMUL 8000 * 0001"},
    {0x6E, "8000 0003", 0x42, "8000", '=', "SMUL 8000 * 0003"},
    {0x6E, "8000 FFFF", 0x42, "8000", '=', "SMUL 8000 * FFFF"},
    {0x6E, "0002 8000", 0x22, "0000", '=', "SMUL 0002 * 8000"},
    {0x6E, "8000 8000", 0x22, "0000", '=', "SMUL 8000 * 8000"},
    {0x76, "8000 0002", 0x42, "FFFF", '=', "SMUU 8000 * 0002"},
    {0x76, "8000 FFFF", 0x02, "7FFF", '=', "SMUU 8000 * FFFF"},
    {0x6F, "8000 0002", 0x22, "0000", '=', "SDIV 8000 / 0002"},
    {0x6D, "0000 8000", 0x43, "8000", '=', "SSUB 0000 - 8000"},
    {0x2E, "80000000 00000002", 0x22, "00000000", '=', "DMUL 80000000 * 2"},
    {0x2E, "80000000 00000003", 0x42, "80000000", '=', "DMUL 80000000 * 3"},
    {0x36, "80000000 00000002", 0x42, "FFFFFFFF", '=', "DMUU 80000000 * 2"},
    {0x34, "80000000", 0x42, "80000000", '=', "CHSD 80000000"},
    {0x1C, "01000003", 0x00, "19800001", '=', "FLTD 01000003"},
    {0x1C, "01000001", 0x00, "19800000", '=', "FLTD 01000001"},
    {0x1C, "7FFFFFFF", 0x00, "1FFFFFFF", '=', "FLTD 7FFFFFFF"},
    {0x1C, "FEFFFFFD", 0x40, "99800001", '=', "FLTD FEFFFFFD"},
    {0x1C, "80000000", 0x40, "A0800000", '=', "FLTD 80000000"},
    {0x10, "01800000 81800000", 0x20, "00000000", '=', "FADD 1 + -1"},
    {0x10, "00000000 01800000", 0x00, "01800000", '=', "FADD 0 + 1"},
    {0x13, "00000000 02C00000", 0x20, "00000000", '=', "FDIV 0 / 3"},
    {0x12, "00FFFFFF 00FFFFFF", 0x00, "00FFFFFE", '=', "FMUL FFFFFF * FFFFFF"},
    {0x13, "01800000 00FFFFFF", 0x00, "01800000", '=', "FDIV 1 / FFFFFF"},
    {0x11, "01800000 00FFFFFF", 0x00, "6A800000", '=', "FSUB 1 - 0.99999994"},
    {0x01, "7D955DF0", 0x00, "7F8A4577", '=', "SQRT rand 0 7D955DF0"},
    {0x01, "06B1E45E", 0x00, "03D566E2", '=', "SQRT rand 1 06B1E45E"},
    {0x01, "7ADA4E06", 0x00, "7DEC66F9", '=', "SQRT rand 2 7ADA4E06"},
    {0x01, "05F7A5CB", 0x00, "03B20AB7", '=', "SQRT rand 3 05F7A5CB"},
    {0x02, "01843C9A", 0x00, "00DBE03E", '=', "SIN rand 0 01843C9A"},
    {0x02, "01BC5A66", 0x00, "00FEBD3E", '=', "SIN rand 1 01BC5A66"},
    {0x02, "82E4EFA7", 0x00, "7FD8029E", '=', "SIN rand 2 82E4EFA7"},
    {0x02, "7FAEE8DB", 0x00, "7FAB86F9", '2', "SIN rand 3 7FAEE8DB"},
    {0x03, "00C57B77", 0x00, "00B78860", '=', "COS rand 0 00C57B77"},
    {0x03, "FEEBD980", 0x00, "00F93D64", '=', "COS rand 1 FEEBD980"},
    {0x03, "FDBB7B7F", 0x00, "00FEED98", '2', "COS rand 2 FDBB7B7F"},
    {0x03, "FF9C442F", 0x00, "00F42B3C", '2', "COS rand 3 FF9C442F"},
    {0x04, "80E93B4B", 0x40, "81A5042C", '=', "TAN rand 0 80E93B4B"},
    {0x04, "7EE1B96B", 0x00, "7EE573EE", '=', "TAN rand 1 7EE1B96B"},
    {0x04, "FD9EFDA8", 0x40, "FD9F4F9D", '=', "TAN rand 2 FD9EFDA8"},
    {0x04, "7DA904EC", 0x00, "7DA9676C", '=', "TAN rand 3 7DA904EC"},
    {0x05, "FDD7ADE4", 0x40, "FDD81476", '=', "ASIN rand 0 FDD7ADE4"},
    {0x05, "7F8F6CF3", 0x00, "7F915EF3", '=', "ASIN rand 1 7F8F6CF3"},
    {0x05, "00BE36EE", 0x00, "00D66BA2", '2', "ASIN rand 2 00BE36EE"},
    {0x05, "FEEEC259", 0x40, "FEF0FA2A", '=', "ASIN rand 3 FEEEC259"},
    {0x06, "FEBC4396", 0x00, "01E0BAC4", '=', "ACOS rand 0 FEBC4396"},
    {0x06, "FFA2F828", 0x00, "01F2868E", '=', "ACOS rand 1 FFA2F828"},
    {0x06, "7EF0EDAA", 0x00, "01AAA92F", '=', "ACOS rand 2 7EF0EDAA"},
    {0x06, "80E19C8B", 0x00, "02A98FA1", '=', "ACOS rand 3 80E19C8B"},
    {0x07, "81990A47", 0x40, "80DFCF9C", '=', "ATAN rand 0 81990A47"},
    {0x07, "82B85269", 0x40, "819E48F5", '=', "ATAN rand 1 82B85269"},
    {0x07, "7EE09594", 0x00, "7EDD1575", '=', "ATAN rand 2 7EE09594"},
    {0x07, "FFE6D59F", 0x40, "FFD8DCC5", '=', "ATAN rand 3 FFE6D59F"},
    {0x08, "0297B724", 0x00, "7FBFEC49", '=', "LOG rand 0 0297B724"},
    {0x08, "7CFA8CD3", 0x40, "819B52D3", '=', "LOG rand 1 7CFA8CD3"},
    {0x08, "7DA7DC0F", 0x40, "818B0E9D", '=', "LOG rand 2 7DA7DC0F"},
    {0x08, "7BBC2A2B", 0x40, "81D1C5E3", '=', "LOG rand 3 7BBC2A2B"},
    {0x09, "01C2EF06", 0x00, "7FD75CB3", '=', "LN rand 0 01C2EF06"},
    {0x09, "05E80F85", 0x00, "02D7860C", '=', "LN rand 1 05E80F85"},
    {0x09, "0690FB6D", 0x00, "02E5C7BB", '=', "LN rand 2 0690FB6D"},
    {0x09, "7FC3E3C6", 0x40, "80F5F530", '=', "LN rand 3 7FC3E3C6"},
    {0x0A, "7DAB9DAD", 0x00, "018B3030", '2', "EXP rand 0 7DAB9DAD"},
    {0x0A, "02EF0CFE", 0x00, "06A7948D", '=', "EXP rand 1 02EF0CFE"},
    {0x0A, "0394D16E", 0x00, "07D149CC", '=', "EXP rand 2 0394D16E"},
    {0x0A, "7D9AE072", 0x00, "018A0E22", '2', "EXP rand 3 7D9AE072"},
    {0x0B, "0388D616 02FBF2E1", 0x00, "09987C4A", '=', "PWR rand 0 0388D616 02FBF2E1"},
    {0x0B, "7F9AAF3C 01851DED", 0x00, "7F93751B", '=', "PWR rand 1 7F9AAF3C 01851DED"},
    {0x0B, "7FDACF19 00A14CE9", 0x00, "0095D59A", '=', "PWR rand 2 7FDACF19 00A14CE9"},
    {0x0B, "00DD0798 7E8729EA", 0x00, "00FB15A6", '2', "PWR rand 3 00DD0798 7E8729EA"},
    {0x76, "8000 0001", 0x42, "FFFF", '=', "SMUU 8000*0001"},
    {0x76, "8000 0003", 0x42, "FFFE", '=', "SMUU 8000*0003"},
    {0x76, "8000 FFFE", 0x02, "7FFF", '=', "SMUU 8000*FFFE"},
    {0x76, "8000 7FFF", 0x42, "C000", '=', "SMUU 8000*7FFF"},
    {0x76, "8000 8000", 0x02, "4000", '=', "SMUU 8000*8000"},
    {0x76, "FFFF 8000", 0x22, "0000", '=', "SMUU FFFF*8000"},
    {0x76, "0001 8000", 0x42, "FFFF", '=', "SMUU 0001*8000"},
    {0x76, "7FFF 8000", 0x42, "C000", '=', "SMUU 7FFF*8000"},
    {0x76, "C000 0002", 0x40, "FFFF", '=', "SMUU C000*0002"},
    {0x76, "4000 0002", 0x20, "0000", '=', "SMUU 4000*0002"},
    {0x76, "FFFF FFFF", 0x20, "0000", '=', "SMUU FFFF*FFFF"},
    {0x76, "8001 FFFF", 0x20, "0000", '=', "SMUU 8001*FFFF"},
    {0x6F, "8000 0001", 0x22, "0000", '=', "SDIV 8000/0001"},
    {0x6F, "8000 0003", 0x22, "0000", '=', "SDIV 8000/0003"},
    {0x6F, "8000 8000", 0x22, "0000", '=', "SDIV 8000/8000"},
    {0x6F, "FFFF 8000", 0x22, "0000", '=', "SDIV FFFF/8000"},
    {0x6F, "0004 8000", 0x22, "0000", '=', "SDIV 0004/8000"},
    {0x6F, "8000 4000", 0x22, "0000", '=', "SDIV 8000/4000"},
    {0x6F, "C000 0002", 0x40, "E000", '=', "SDIV C000/0002"},
    {0x6F, "8001 0002", 0x40, "C001", '=', "SDIV 8001/0002"},
    {0x6F, "7FFF FFFF", 0x40, "8001", '=', "SDIV 7FFF/FFFF"},
    {0x2F, "80000000 00000001", 0x22, "00000000", '=', "DDIV 80000000/00000001"},
    {0x2F, "80000000 00000002", 0x22, "00000000", '=', "DDIV 80000000/00000002"},
    {0x2F, "00000004 80000000", 0x22, "00000000", '=', "DDIV 00000004/80000000"},
    {0x2F, "C0000000 00000002", 0x40, "E0000000", '=', "DDIV C0000000/00000002"},
    {0x36, "80000000 FFFFFFFF", 0x02, "7FFFFFFF", '=', "DMUU 80000000*FFFFFFFF"},
    {0x36, "80000000 00000001", 0x42, "FFFFFFFF", '=', "DMUU 80000000*00000001"},
    {0x36, "80000000 00000003", 0x42, "FFFFFFFE", '=', "DMUU 80000000*00000003"},
    {0x36, "FFFFFFFF 80000000", 0x22, "00000000", '=', "DMUU FFFFFFFF*80000000"},
    {0x6E, "0001 8000", 0x42, "8000", '=', "SMUL 0001*8000"},
    {0x03, "03A45E3D", 0x00, "7FD2B1FE", '2', "COS worst 03A45E3D"},
    {0x03, "03A45D3D", 0x00, "7FD2A36A", '=', "COS worst 03A45D3D"},
    {0x03, "03A45F3D", 0x00, "7FD2C096", '=', "COS worst 03A45F3D"},
    {0x03, "83A45E3D", 0x00, "7FD2B1E8", '=', "COS worst 83A45E3D"},
    {0x03, "83A45D3D", 0x00, "7FD2A352", 'R', "COS worst 83A45D3D"},
    {0x03, "83A45F3D", 0x00, "7FD2C07E", 'R', "COS worst 83A45F3D"},
    {0x03, "039CBA4F", 0x00, "7EBCB7B0", '=', "COS worst 039CBA4F"},
    {0x03, "039CB94F", 0x00, "7EBC983B", '2', "COS worst 039CB94F"},
    {0x03, "039CBB4F", 0x00, "7EBCD725", '2', "COS worst 039CBB4F"},
    {0x05, "00FF9DB2", 0x00, "01C20CD8", '=', "ASIN worst 00FF9DB2"},
    {0x05, "00FF9CB2", 0x00, "01C203BD", '=', "ASIN worst 00FF9CB2"},
    {0x05, "00FF9EB2", 0x00, "01C21600", '=', "ASIN worst 00FF9EB2"},
    {0x05, "80FF9DB2", 0x40, "81C20CD8", '=', "ASIN worst 80FF9DB2"},
    {0x05, "80FF9CB2", 0x40, "81C203BD", '=', "ASIN worst 80FF9CB2"},
    {0x05, "80FF9EB2", 0x40, "81C21600", '=', "ASIN worst 80FF9EB2"},
    {0x05, "00FF1A9F", 0x00, "01BE5972", '=', "ASIN worst 00FF1A9F"},
    {0x05, "00FF199F", 0x00, "01BE5378", '=', "ASIN worst 00FF199F"},
    {0x05, "00FF1B9F", 0x00, "01BE5F6F", '=', "ASIN worst 00FF1B9F"},
    {0x06, "00FF9DB2", 0x00, "7CE06040", '=', "ACOS worst 00FF9DB2"},
    {0x06, "00FF9CB2", 0x00, "7CE183A0", '=', "ACOS worst 00FF9CB2"},
    {0x06, "00FF9EB2", 0x00, "7CDF3B40", '=', "ACOS worst 00FF9EB2"},
    {0x06, "80FF9DB2", 0x00, "02C58E59", '=', "ACOS worst 80FF9DB2"},
    {0x06, "80FF9CB2", 0x00, "02C589CB", '=', "ACOS worst 80FF9CB2"},
    {0x06, "80FF9EB2", 0x00, "02C592ED", '=', "ACOS worst 80FF9EB2"},
    {0x06, "80FF1A9F", 0x00, "02C3B4A6", '=', "ACOS worst 80FF1A9F"},
    {0x06, "80FF199F", 0x00, "02C3B1A9", '=', "ACOS worst 80FF199F"},
    {0x06, "80FF1B9F", 0x00, "02C3B7A4", '=', "ACOS worst 80FF1B9F"},
    {0x06, "80FED916", 0x00, "02C2FCF1", '=', "ACOS worst 80FED916"},
    {0x06, "80FED816", 0x00, "02C2FA4E", '=', "ACOS worst 80FED816"},
    {0x06, "80FEDA16", 0x00, "02C2FF95", '=', "ACOS worst 80FEDA16"},
    {0x09, "7FEAAF76", 0x40, "80C7B33A", '=', "LN worst 7FEAAF76"},
    {0x09, "7FEAAE76", 0x40, "80C7B454", '=', "LN worst 7FEAAE76"},
    {0x09, "7FEAB076", 0x40, "80C7B224", '=', "LN worst 7FEAB076"},
    {0x09, "7FBEC2D6", 0x40, "80FCBFDE", '=', "LN worst 7FBEC2D6"},
    {0x09, "7FBEC1D6", 0x40, "80FCC136", '=', "LN worst 7FBEC1D6"},
    {0x09, "7FBEC3D6", 0x40, "80FCBE88", '=', "LN worst 7FBEC3D6"},
    {0x09, "7FFAEE6F", 0x40, "80B690AA", '=', "LN worst 7FFAEE6F"},
    {0x09, "7FFAED6F", 0x40, "80B691B0", '=', "LN worst 7FFAED6F"},
    {0x09, "7FFAEF6F", 0x40, "80B68FA6", '=', "LN worst 7FFAEF6F"},
    {0x08, "03ECFF97", 0x00, "00DE9DC8", '=', "LOG worst 03ECFF97"},
    {0x08, "03ECFE97", 0x00, "00DE9D52", '=', "LOG worst 03ECFE97"},
    {0x08, "03ED0097", 0x00, "00DE9E42", '=', "LOG worst 03ED0097"},
    {0x08, "03F1978D", 0x00, "00E0C02A", '=', "LOG worst 03F1978D"},
    {0x08, "03F1968D", 0x00, "00E0BFB6", '=', "LOG worst 03F1968D"},
    {0x08, "03F1988D", 0x00, "00E0C0A2", '=', "LOG worst 03F1988D"},
    {0x08, "03FF5F6F", 0x00, "00E6EB10", '=', "LOG worst 03FF5F6F"},
    {0x08, "03FF5E6F", 0x00, "00E6EAA2", '=', "LOG worst 03FF5E6F"},
    {0x08, "03FF606F", 0x00, "00E6EB82", '=', "LOG worst 03FF606F"},
    {0x0A, "85F3C6A7", 0x00, "55836F26", '=', "EXP worst 85F3C6A7"},
    {0x0A, "85F3C5A7", 0x00, "55837F9D", '=', "EXP worst 85F3C5A7"},
    {0x0A, "85F3C7A7", 0x00, "55835EC8", '=', "EXP worst 85F3C7A7"},
    {0x0A, "85FEF9DB", 0x00, "5381A516", '=', "EXP worst 85FEF9DB"},
    {0x0A, "85FEF8DB", 0x00, "5381B553", '=', "EXP worst 85FEF8DB"},
    {0x0A, "85FEFADB", 0x00, "538194F0", '2', "EXP worst 85FEFADB"},
    {0x0A, "85EE45A1", 0x00, "5682C277", '=', "EXP worst 85EE45A1"},
    {0x0A, "85EE44A1", 0x00, "5682D2C1", '=', "EXP worst 85EE44A1"},
    {0x0A, "85EE46A1", 0x00, "5682B217", '=', "EXP worst 85EE46A1"},
    {0x0A, "04800000", 0x00, "0CBA4F5A", '=', "EXP 8"},
    {0x0A, "05800000", 0x00, "18879769", '=', "EXP 16"},
    {0x0A, "05C00000", 0x00, "23C55C12", '=', "EXP 24"},
    {0x0A, "05F80000", 0x00, "2DD35BCB", '=', "EXP 31"},
    {0x0A, "84800000", 0x00, "75AFE102", '=', "EXP -8"},
    {0x0A, "85800000", 0x00, "69F1AACB", '=', "EXP -16"},
    {0x0A, "85C00000", 0x00, "5EA6082B", '=', "EXP -24"},
    {0x0A, "85F80000", 0x00, "549B08FC", '=', "EXP -31"},
    {0x0A, "05FF3333", 0x00, "2F81F6E3", '=', "EXP 31.9"},
    {0x0A, "85FF3333", 0x00, "52FC216D", '=', "EXP -31.9"},
    {0x05, "00FD70A3", 0x00, "01B6F1DD", '=', "ASIN 0.99"},
    {0x06, "00FD70A3", 0x00, "7E90EFE8", '=', "ACOS 0.99"},
    {0x05, "00FFBE76", 0x00, "01C35645", '=', "ASIN 0.999"},
    {0x06, "00FFBE76", 0x00, "7CB732A0", '=', "ACOS 0.999"},
    {0x05, "00FFF972", 0x00, "01C74067", '=', "ASIN 0.9999"},
    {0x06, "00FFF972", 0x00, "7AE7B980", '=', "ACOS 0.9999"},
    {0x05, "80FD70A3", 0x40, "81B6F1DD", '=', "ASIN -0.99"},
    {0x06, "80FD70A3", 0x00, "02C000DB", '=', "ACOS -0.99"},
    {0x05, "80FFBE76", 0x40, "81C35645", '=', "ASIN -0.999"},
    {0x06, "80FFBE76", 0x00, "02C6330F", '=', "ACOS -0.999"},
    {0x05, "00FFFFFF", 0x00, "01C8FFDA", '=', "ASIN max"},
    {0x06, "00FFFFFF", 0x00, "76800000", '=', "ACOS max"},
    {0x03, "03C00000", 0x00, "00F5CDBA", '2', "COS 6.0"},
    {0x03, "03C8F5C2", 0x00, "00FFFFAC", '=', "COS 6.28"},
    {0x03, "83C00000", 0x00, "00F5CDB6", '=', "COS -6.0"},
    {0x03, "02C00000", 0x40, "80FD7024", '=', "COS 3.0"},
    {0x03, "03800000", 0x40, "80A7552C", '=', "COS 4.0"},
    {0x03, "03A00000", 0x00, "7F913C27", '=', "COS 5.0"},
    {0x02, "7E800000", 0x00, "7DFF5572", '2', "SIN sweep 1"},
    {0x03, "7E800000", 0x00, "00FE00AC", '2', "COS sweep 1"},
    {0x05, "7D9D89D8", 0x00, "7D9DB1B7", '=', "ASIN sweep 1"},
    {0x06, "7D9D89D8", 0x00, "01BF34BF", '=', "ACOS sweep 1"},
    {0x0A, "81A00000", 0x00, "7F92B0C2", '=', "EXP sweep 1"},
    {0x09, "00A00000", 0x40, "FFF0A44E", '=', "LN sweep 1"},
    {0x02, "7F800000", 0x00, "7EFD5772", '=', "SIN sweep 2"},
    {0x03, "7F800000", 0x00, "00F80AA6", '=', "COS sweep 2"},
    {0x05, "7E9D89D8", 0x00, "7E9E2AA6", '=', "ASIN sweep 2"},
    {0x06, "7E9D89D8", 0x00, "01B54A86", '=', "ACOS sweep 2"},
    {0x09, "00C00000", 0x40, "FF934B0E", '=', "LN sweep 2"},
    {0x02, "7FC00000", 0x00, "7FBB8811", '=', "SIN sweep 3"},
    {0x03, "7FC00000", 0x00, "00EE35BE", '=', "COS sweep 3"},
    {0x05, "7EEC4EC4", 0x00, "7EEE74FE", '=', "ASIN sweep 3"},
    {0x06, "7EEC4EC4", 0x00, "01AB413B", '=', "ACOS sweep 3"},
    {0x0A, "80C00000", 0x00, "7FF1DA05", '=', "EXP sweep 3"},
    {0x09, "00E00000", 0x40, "FE88BC70", '=', "LN sweep 3"},
    {0x03, "00800000", 0x00, "00E0A93E", '=', "COS sweep 4"},
    {0x05, "7F9D89D8", 0x00, "7FA022F5", '=', "ASIN sweep 4"},
    {0x06, "7F9D89D8", 0x00, "01A1071D", '=', "ACOS sweep 4"},
    {0x0A, "80800000", 0x00, "009B4598", '=', "EXP sweep 4"},
    {0x09, "01800000", 0x00, "69840000", '=', "LN sweep 4"},
    {0x02, "00A00000", 0x00, "0095C8ED", '=', "SIN sweep 5"},
    {0x03, "00A00000", 0x00, "00CF9B44", '=', "COS sweep 5"},
    {0x05, "7FC4EC4E", 0x00, "7FCA220E", '=', "ASIN sweep 5"},
    {0x06, "7FC4EC4E", 0x00, "01968757", '=', "ACOS sweep 5"},
    {0x0A, "FF800000", 0x00, "00C75F7B", '=', "EXP sweep 5"},
    {0x09, "01900000", 0x00, "7DF1383B", '=', "LN sweep 5"},
    {0x02, "00C00000", 0x00, "00AE7FDF", '2', "SIN sweep 6"},
    {0x03, "00C00000", 0x00, "00BB4FF4", '=', "COS sweep 6"},
    {0x05, "7FEC4EC4", 0x00, "7FF59EFC", '=', "ASIN sweep 6"},
    {0x06, "7FEC4EC4", 0x00, "018BA81B", '=', "ACOS sweep 6"},
    {0x0A, "00000000", 0x00, "01800000", '=', "EXP sweep 6"},
    {0x09, "01A00000", 0x00, "7EE47FBF", '=', "LN sweep 6"},
    {0x02, "00E00000", 0x00, "00C47DB8", '=', "SIN sweep 7"},
    {0x03, "00E00000", 0x00, "00A4185C", '=', "COS sweep 7"},
    {0x05, "0089D89D", 0x00, "00919070", '=', "ASIN sweep 7"},
    {0x06, "0089D89D", 0x00, "018047A2", '=', "ACOS sweep 7"},
    {0x0A, "7F800000", 0x00, "01A45AF2", '=', "EXP sweep 7"},
    {0x09, "01B00000", 0x00, "7FA30C5E", '=', "LN sweep 7"},
    {0x05, "009D89D8", 0x00, "00A9B219", '=', "ASIN sweep 8"},
    {0x06, "009D89D8", 0x00, "00E86D9C", '=', "ACOS sweep 8"},
    {0x0A, "00800000", 0x00, "01D3094C", '=', "EXP sweep 8"},
    {0x09, "01C00000", 0x00, "7FCF991E", '=', "LN sweep 8"},
    {0x02, "01900000", 0x00, "00E6FB02", '2', "SIN sweep 9"},
    {0x03, "01900000", 0x00, "7FDCC328", '2', "COS sweep 9"},
    {0x05, "00B13B13", 0x00, "00C3C236", '=', "ASIN sweep 9"},
    {0x06, "00B13B13", 0x00, "00CE5D7E", '=', "ACOS sweep 9"},
    {0x0A, "00C00000", 0x00, "02877CEE", '=', "EXP sweep 9"},
    {0x09, "01D00000", 0x00, "7FF8947A", '=', "LN sweep 9"},
    {0x02, "01A00000", 0x00, "00F2F0A8", '=', "SIN sweep 10"},
    {0x03, "01A00000", 0x00, "7FA171EB", '=', "COS sweep 10"},
    {0x05, "00C4EC4E", 0x00, "00E0ACC6", '2', "ASIN sweep 10"},
    {0x06, "00C4EC4E", 0x00, "00B172EE", '2', "ACOS sweep 10"},
    {0x09, "01E00000", 0x00, "008F42FA", '=', "LN sweep 10"},
    {0x02, "01B00000", 0x00, "00FB1BCE", '=', "SIN sweep 11"},
    {0x03, "01B00000", 0x00, "7EC7377E", '=', "COS sweep 11"},
    {0x05, "00D89D89", 0x00, "01811DF2", '=', "ASIN sweep 11"},
    {0x06, "00D89D89", 0x00, "008FE3D0", '=', "ACOS sweep 11"},
    {0x0A, "01A00000", 0x00, "02DF61C7", '=', "EXP sweep 11"},
    {0x09, "01F00000", 0x00, "00A0EC7E", '=', "LN sweep 11"},
    {0x02, "01C00000", 0x00, "00FF5BD6", '2', "SIN sweep 12"},
    {0x03, "01C00000", 0x00, "7D90DE9E", '=', "COS sweep 12"},
    {0x05, "00EC4EC4", 0x00, "01968756", '=', "ASIN sweep 12"},
    {0x06, "00EC4EC4", 0x00, "7FCA2210", '=', "ACOS sweep 12"},
    {0x0A, "01C00000", 0x00, "038F6A01", '=', "EXP sweep 12"},
    {0x0B, "02800000 04A00000", 0x00, "0AFFFFF4", '=', "PWR 02800000^04A00000"},
    {0x0B, "02C00000 01800000", 0x00, "02BFFFFC", '=', "PWR 02C00000^01800000"},
    {0x0B, "04A00000 00800000", 0x00, "02CA62BE", '=', "PWR 04A00000^00800000"},
    {0x02, "7F97903B", 0x00, "7F955C07", '=', "SIN rand3 0"},
    {0x02, "00E10007", 0x00, "00C52174", '2', "SIN rand3 1"},
    {0x02, "81BF27D4", 0x40, "80FF3BD4", '=', "SIN rand3 2"},
    {0x02, "80ADC6C4", 0x40, "80A0BC1D", '=', "SIN rand3 3"},
    {0x02, "FEC9F32F", 0x40, "FEC8A4B0", '=', "SIN rand3 4"},
    {0x02, "01A1BCD1", 0x00, "00F40370", '=', "SIN rand3 5"},
    {0x03, "FDCB65DC", 0x00, "00FEBD10", '2', "COS rand3 0"},
    {0x03, "82924B8F", 0x40, "80A7D9C6", 'R', "COS rand3 1"},
    {0x03, "FF90ADC9", 0x00, "00F5D8E0", '=', "COS rand3 2"},
    {0x03, "81EE443D", 0x40, "FF92BB53", '=', "COS rand3 3"},
    {0x03, "7DCA6FFD", 0x00, "00FEC01A", '=', "COS rand3 4"},
    {0x03, "82F687E6", 0x40, "80C210B0", '=', "COS rand3 5"},
    {0x05, "80AF7D71", 0x40, "80C15B68", '=', "ASIN rand3 0"},
    {0x05, "00E12F4E", 0x00, "01899C36", '=', "ASIN rand3 1"},
    {0x05, "FFC300D2", 0x40, "FFC80E0F", '=', "ASIN rand3 2"},
    {0x05, "7DAF042D", 0x00, "7DAF3AE2", '=', "ASIN rand3 3"},
    {0x05, "FD93DBE5", 0x40, "FD93FCD9", '=', "ASIN rand3 4"},
    {0x05, "7FEE14B8", 0x00, "7FF79F3A", '=', "ASIN rand3 5"},
    {0x06, "7DEBB2AA", 0x00, "01BA4C50", '=', "ACOS rand3 0"},
    {0x06, "7FFD4215", 0x00, "0186D4D6", '=', "ACOS rand3 1"},
    {0x06, "00E378D7", 0x00, "7FF40420", '=', "ACOS rand3 2"},
    {0x06, "FEF0AA76", 0x00, "01E76DE1", '=', "ACOS rand3 3"},
    {0x06, "7DBC973C", 0x00, "01BD421F", '=', "ACOS rand3 4"},
    {0x06, "7FF0EB95", 0x00, "018A5A73", '=', "ACOS rand3 5"},
    {0x0A, "FFE787EF", 0x00, "00A2DF65", '=', "EXP rand3 0"},
    {0x0A, "84CE1CF3", 0x00, "6EAAACAD", '=', "EXP rand3 1"},
    {0x0A, "03E5B47A", 0x00, "0BA3D4E9", '=', "EXP rand3 2"},
    {0x0A, "80A77F76", 0x00, "00851262", '=', "EXP rand3 3"},
    {0x0A, "04BB94CB", 0x00, "11F12B7E", '=', "EXP rand3 4"},
    {0x0A, "FEF1249F", 0x00, "00CA4967", '=', "EXP rand3 5"},
    {0x09, "00DEA20F", 0x40, "FE8F0114", '=', "LN rand3 0"},
    {0x09, "03CC419A", 0x00, "01ED440C", '=', "LN rand3 1"},
    {0x09, "7FEA6652", 0x40, "80C80310", '=', "LN rand3 2"},
    {0x09, "03E70FA3", 0x00, "01FD0C90", '=', "LN rand3 3"},
    {0x09, "7FD66EA4", 0x40, "80DECE02", '=', "LN rand3 4"},
    {0x09, "7AB85C96", 0x40, "838F96D2", '=', "LN rand3 5"},
    {0x08, "04F1225D", 0x00, "0196CD3F", '=', "LOG rand3 0"},
    {0x08, "06C896BA", 0x00, "01D9A1AC", '=', "LOG rand3 1"},
    {0x08, "04E61156", 0x00, "019430AD", '=', "LOG rand3 2"},
    {0x08, "01D6D8EE", 0x00, "7EE65170", '=', "LOG rand3 3"},
    {0x08, "7BA5742D", 0x40, "81D8EC5B", '=', "LOG rand3 4"},
    {0x08, "7BCBEC58", 0x40, "81CD4D5A", '=', "LOG rand3 5"},
    {0x03, "81CCCCCC", 0x40, "FBEF3310", '=', "COS neg -1.60"},
    {0x03, "01CCCCCC", 0x40, "FBEF33CA", '=', "COS pos 1.60"},
    {0x03, "81F33333", 0x40, "FFA58628", 'R', "COS neg -1.90"},
    {0x03, "01F33333", 0x40, "FFA58636", '=', "COS pos 1.90"},
    {0x03, "828CCCCC", 0x40, "8096A7FB", '=', "COS neg -2.20"},
    {0x03, "028CCCCC", 0x40, "8096A7FF", '=', "COS pos 2.20"},
    {0x03, "82A00000", 0x40, "80CD17B4", '=', "COS neg -2.50"},
    {0x03, "02A00000", 0x40, "80CD17BE", '=', "COS pos 2.50"},
    {0x03, "82B33333", 0x40, "80F13578", '=', "COS neg -2.80"},
    {0x03, "02B33333", 0x40, "80F1357A", '=', "COS pos 2.80"},
    {0x03, "82C66666", 0x40, "80FFC752", '=', "COS neg -3.10"},
    {0x03, "02C66666", 0x40, "80FFC754", '2', "COS pos 3.10"},
    {0x03, "82D99999", 0x40, "80F78018", '=', "COS neg -3.40"},
    {0x03, "02D99999", 0x40, "80F78018", '2', "COS pos 3.40"},
    {0x03, "82ECCCCC", 0x40, "80D91D1C", '=', "COS neg -3.70"},
    {0x03, "02ECCCCC", 0x40, "80D91D14", '=', "COS pos 3.70"},
    {0x03, "83800000", 0x40, "80A75536", '=', "COS neg -4.00"},
    {0x03, "83899999", 0x40, "FFCD35A6", '=', "COS neg -4.30"},
    {0x03, "03899999", 0x40, "FFCD3590", '=', "COS pos 4.30"},
    {0x03, "83933333", 0x40, "FDE5B084", '=', "COS neg -4.60"},
    {0x03, "03933333", 0x40, "FDE5B020", '=', "COS pos 4.60"},
    {0x03, "839CCCCC", 0x00, "7EBEFCD7", '=', "COS neg -4.90"},
    {0x03, "039CCCCC", 0x00, "7EBEFD09", '=', "COS pos 4.90"},
    {0x03, "83A66666", 0x00, "7FEFE156", 'R', "COS neg -5.20"},
    {0x03, "03A66666", 0x00, "7FEFE16C", '=', "COS pos 5.20"},
    {0x03, "83B00000", 0x00, "00B56B5C", '=', "COS neg -5.50"},
    {0x03, "03B00000", 0x00, "00B56B66", '=', "COS pos 5.50"},
    {0x03, "83B99999", 0x00, "00E2B162", '=', "COS neg -5.80"},
    {0x03, "03B99999", 0x00, "00E2B168", '=', "COS pos 5.80"},
    {0x03, "83C33333", 0x00, "00FBB77A", 'R', "COS neg -6.10"},
    {0x03, "03C33333", 0x00, "00FBB77A", '=', "COS pos 6.10"},
    {0x02, "01CCCCCC", 0x00, "00FFE410", '2', "SIN pos 1.60"},
    {0x02, "81CCCCCC", 0x40, "80FFE410", '2', "SIN neg -1.60"},
    {0x02, "02800000", 0x00, "00E8C7B8", '=', "SIN pos 2.00"},
    {0x02, "82800000", 0x40, "80E8C7BA", '2', "SIN neg -2.00"},
    {0x02, "02999999", 0x00, "00ACEB28", '=', "SIN pos 2.40"},
    {0x02, "82999999", 0x40, "80ACEB28", '=', "SIN neg -2.40"},
    {0x02, "02B33333", 0x00, "7FAB8392", '2', "SIN pos 2.80"},
    {0x02, "82B33333", 0x40, "FFAB839E", 'R', "SIN neg -2.80"},
    {0x02, "02CCCCCC", 0x40, "FCEF1958", 'R', "SIN pos 3.20"},
    {0x02, "82CCCCCC", 0x00, "7CEF18F4", '=', "SIN neg -3.20"},
    {0x02, "02E66666", 0x40, "FFE29200", '=', "SIN pos 3.60"},
    {0x02, "82E66666", 0x00, "7FE29200", '=', "SIN neg -3.60"},
    {0x02, "03800000", 0x40, "80C1BDC6", '=', "SIN pos 4.00"},
    {0x02, "83800000", 0x00, "00C1BDC6", '=', "SIN neg -4.00"},
    {0x02, "038CCCCC", 0x40, "80F39C2C", '=', "SIN pos 4.40"},
    {0x02, "838CCCCC", 0x00, "00F39C2C", '=', "SIN neg -4.40"},
    {0x02, "03999999", 0x40, "80FF04A6", '=', "SIN pos 4.80"},
    {0x02, "83999999", 0x00, "00FF04A6", '=', "SIN neg -4.80"},
    {0x02, "03A66666", 0x40, "80E22A1A", '=', "SIN pos 5.20"},
    {0x02, "83A66666", 0x00, "00E22A1A", '=', "SIN neg -5.20"},
    {0x02, "03B33333", 0x40, "80A19AB2", '=', "SIN pos 5.60"},
    {0x02, "83B33333", 0x00, "00A19AB2", '=', "SIN neg -5.60"},
    {0x02, "03C00000", 0x40, "FF8F0F99", '=', "SIN pos 6.00"},
    {0x02, "83C00000", 0x00, "7F8F0F99", '=', "SIN neg -6.00"},
    {0x02, "7F90AF52", 0x00, "7F8EC44F", '=', "SIN small 0.28259"},
    {0x02, "01BCC462", 0x00, "00FED1EA", '2', "SIN small 1.47474"},
    {0x02, "00C1040F", 0x00, "00AF3DCD", '=', "SIN small 0.75397"},
    {0x02, "7FAD091D", 0x00, "7FA9C2AA", '=', "SIN small 0.33796"},
    {0x02, "7DFF7EC2", 0x00, "7DFED538", '=', "SIN small 0.12475"},
    {0x02, "7FF235E3", 0x00, "7FE946DE", '=', "SIN small 0.47307"},
    {0x02, "009885C8", 0x00, "008FA86F", '=', "SIN small 0.59579"},
    {0x02, "0185C76B", 0x00, "00DD7090", '2', "SIN small 1.04515"},
    {0x02, "7DCDBC27", 0x00, "7DCD639C", '=', "SIN small 0.10046"},
    {0x02, "00A2F2D5", 0x00, "00982A82", '=', "SIN small 0.63652"},
    {0x02, "00AA32EA", 0x00, "009DEF4C", '=', "SIN small 0.66484"},
    {0x02, "7FB64CCC", 0x00, "7FB278F3", '=', "SIN small 0.35605"},
    {0x02, "00EB20B5", 0x00, "00CB6FA6", '=', "SIN small 0.91847"},
    {0x02, "7FE16ED3", 0x00, "7FDA3824", '=', "SIN small 0.44030"},
    {0x02, "01ADB750", 0x00, "00FA2E16", '=', "SIN small 1.35716"},
    {0x02, "019AA4C7", 0x00, "00EF59B6", '=', "SIN small 1.20815"},
    {0x07, "7F999999", 0x00, "7F9539D2", '=', "ATAN 0.30"},
    {0x07, "7FB33333", 0x00, "7FAC60A4", '=', "ATAN 0.35"},
    {0x07, "7FCCCCCC", 0x00, "7FC2D1BA", '=', "ATAN 0.40"},
    {0x07, "7FE66666", 0x00, "7FD8804E", '=', "ATAN 0.45"},
    {0x07, "00800000", 0x00, "7FED6338", '=', "ATAN 0.50"},
    {0x07, "008CCCCC", 0x00, "0080BA54", '=', "ATAN 0.55"},
    {0x07, "00999999", 0x00, "008A58EE", '=', "ATAN 0.60"},
    {0x07, "00A66666", 0x00, "00938D53", '2', "ATAN 0.65"},
    {0x07, "00B33333", 0x00, "009C588A", '=', "ATAN 0.70"},
    {0x07, "00C00000", 0x00, "00A4BC7E", '=', "ATAN 0.75"},
    {0x07, "00CCCCCC", 0x00, "00ACBBD3", '=', "ATAN 0.80"},
    {0x07, "00D99999", 0x00, "00B459BA", '=', "ATAN 0.85"},
    {0x07, "00E66666", 0x00, "00BB99C6", '=', "ATAN 0.90"},
    {0x07, "00F33333", 0x00, "00C27FD1", '2', "ATAN 0.95"},
    {0x07, "01999999", 0x00, "00E04556", '=', "ATAN 1.2"},
    {0x07, "01C00000", 0x00, "00FB985E", '=', "ATAN 1.5"},
    {0x07, "02800000", 0x00, "018DB70C", '=', "ATAN 2.0"},
    {0x07, "02C00000", 0x00, "019FE0BB", '=', "ATAN 3.0"},
    {0x07, "80C00000", 0x40, "80A4BC7E", '=', "ATAN -0.75"},
    {0x07, "81C00000", 0x40, "80FB985E", '=', "ATAN -1.5"},
    {0x0A, "03C23515", 0x00, "09D81EA4", '=', "EXP r4 6.0690"},
    {0x0A, "058432C0", 0x00, "18E52935", '=', "EXP r4 16.5248"},
    {0x0A, "058777B8", 0x00, "19AC6C71", '=', "EXP r4 16.9335"},
    {0x0A, "02853B64", 0x00, "04804BBA", '=', "EXP r4 2.0818"},
    {0x0A, "8585A877", 0x00, "68EE4809", '=', "EXP r4 -16.7073"},
    {0x0A, "84C0BF69", 0x00, "6FC4C0BE", '=', "EXP r4 -12.0467"},
    {0x0A, "858F19F5", 0x00, "67926072", '=', "EXP r4 -17.8877"},
    {0x0A, "0591B8EC", 0x00, "1B9B524E", '=', "EXP r4 18.2153"},
    {0x0A, "838E2078", 0x00, "7AC0FB62", '=', "EXP r4 -4.4415"},
    {0x0A, "858FDEDB", 0x00, "6784F579", '=', "EXP r4 -17.9838"},
    {0x03, "80F6872B", 0x00, "00923100", '=', "COS 80F6872B"},
    {0x03, "80EC8B43", 0x00, "009A462D", '=', "COS 80EC8B43"},
    {0x03, "80FEF9DB", 0x00, "008B2D93", '=', "COS 80FEF9DB"},
    {0x03, "80FAE147", 0x00, "008E990D", '=', "COS 80FAE147"},
    {0x03, "80F4FDF3", 0x00, "0093731E", '=', "COS 80F4FDF3"},
    {0x03, "80D851EB", 0x00, "00A9EA51", '=', "COS 80D851EB"},
    {0x03, "00F6872B", 0x00, "009230F8", '=', "COS 00F6872B"},
    {0x03, "00EC8B43", 0x00, "009A4625", '=', "COS 00EC8B43"},
    {0x03, "00FEF9DB", 0x00, "008B2D8C", '=', "COS 00FEF9DB"},
    {0x03, "00FAE147", 0x00, "008E9907", '=', "COS 00FAE147"},
    {0x03, "808CCCCC", 0x00, "00DA3F0A", '=', "COS neg -0.55"},
    {0x03, "80A66666", 0x00, "00CBCC28", '=', "COS neg -0.65"},
    {0x03, "80C00000", 0x00, "00BB4FF8", '2', "COS neg -0.75"},
    {0x03, "80D99999", 0x00, "00A8F4A8", '=', "COS neg -0.85"},
    {0x03, "80F33333", 0x00, "0094E932", '=', "COS neg -0.95"},
    {0x03, "81866666", 0x00, "7FFEC1A6", '2', "COS neg -1.05"},
    {0x03, "81933333", 0x00, "7FD12546", '=', "COS neg -1.15"},
    {0x03, "81A00000", 0x00, "7FA171EE", '=', "COS neg -1.25"},
    {0x03, "81ACCCCC", 0x00, "7EE0435A", '2', "COS neg -1.35"},
    {0x03, "81B99999", 0x00, "7DF6CA3C", '=', "COS neg -1.45"},
};

}  // namespace

void test_am9511a() {
    SECTION("Am9511A -- the float format, from the Manual's worked encodings");
    {
        CHECK(Am9511a::toFloat(5) == 0x03A00000, "5 = 03A00000");
        CHECK(Am9511a::toFloat(6) == 0x03C00000, "6 = 03C00000");
        CHECK(Am9511a::toFloat(123) == 0x07F60000, "123 = 07F60000");
        CHECK(Am9511a::toFloat(456) == 0x09E40000, "456 = 09E40000");
        CHECK(Am9511a::toFloat(12345) == 0x0EC0E400, "12345 = 0EC0E400");
        CHECK(Am9511a::toFloat(-678) == 0x8AA98000, "-678 = 8AA98000");
        CHECK(Am9511a::toFloat(100.5) == 0x07C90000, "100.5 = 0.11001001 x 2^7");
        CHECK(Am9511a::toFloat(0) == 0, "zero is all zeros");
        CHECK(Am9511a::toFloat(0.25) == 0x7F800000, "a negative exponent is two's complement");
        CHECK(Am9511a::fromFloat(0x0EC0E400) == 12345, "and back");
        CHECK(Am9511a::fromFloat(0x05400000) == 0, "a mantissa MSB of 0 reads as zero");
    }

    SECTION("Am9511A -- the SS-1 manual's test program: 3000H + 4000H by SADD");
    {
        Apu a;
        for (uint8_t b : {0x00, 0x30, 0x00, 0x40}) a.c.writeData(b, a.now);  // TBL1
        a.run(0x6C);
        uint8_t hi = a.c.readData(a.now), lo = a.c.readData(a.now);
        CHECK(hi == 0x70 && lo == 0x00, "pops 70H then 00H, MS byte first (TBL2)");
    }

    SECTION("Am9511A -- the stack is a 16-byte ring");
    {
        Apu a;
        a.push32(0x11111111); a.push32(0x22222222); a.push32(0x33333333);
        a.push32(0x44444444); a.push32(0x55555555);  // a fifth overwrites the oldest
        CHECK(a.pop32() == 0x55555555 && a.pop32() == 0x44444444, "LIFO");
        CHECK(a.pop32() == 0x33333333 && a.pop32() == 0x22222222, "four 32-bit entries");
        CHECK(a.pop32() == 0x55555555, "a pop past empty re-reads old data: the first is gone");

        Apu b;
        b.push16(0x1234);
        b.c.writeData(0xAB, b.now);  // one stray byte: the pointer is now misaligned
        CHECK(b.pop16() == 0xAB12, "a misaligned pointer takes the wrong bytes");
        b.c.reset();
        b.push16(0x5678);
        CHECK(b.pop16() == 0x5678, "RESET re-inits the pointer");
    }

    SECTION("Am9511A -- 16-bit fixed point");
    {
        Apu a;
        a.push16(0x7FFF); a.push16(1);
        uint8_t st = a.run(0x6C);  // SADD
        CHECK(a.pop16() == 0x8000, "SADD 7FFF + 1 = 8000 (low 16 bits)");
        CHECK(st == (Am9511a::kOverflow | Am9511a::kSign), "with overflow and sign");

        a.push16(0xFFFF); a.push16(1);
        st = a.run(0x6C);
        CHECK(a.pop16() == 0 && st == (Am9511a::kCarry | Am9511a::kZero), "FFFF + 1 carries to zero");

        a.push16(5); a.push16(7);
        st = a.run(0x6D);  // SSUB: B - A
        CHECK(a.pop16() == 0xFFFE && (st & Am9511a::kCarry) && (st & Am9511a::kSign), "SSUB 5 - 7 = -2, borrow");

        a.push16(0xFFFF); a.push16(0x8000);
        st = a.run(0x6D);
        CHECK(a.pop16() == 0x7FFF && (st & Am9511a::kOverflow), "SSUB with A = 8000 sets overflow even when R fits");

        a.push16(300); a.push16((uint16_t)-7);
        st = a.run(0x6E);  // SMUL
        CHECK(a.pop16() == (uint16_t)-2100 && !(st & 0x1E), "SMUL 300 x -7 = -2100");
        a.push16(200); a.push16(200);
        st = a.run(0x6E);
        CHECK(a.pop16() == (uint16_t)40000 && (st & Am9511a::kOverflow), "SMUL 200 x 200 overflows; low bits returned");
        a.push16(0x8000); a.push16(1);
        st = a.run(0x6E);
        CHECK(a.pop16() == 0x8000 && (st & Am9511a::kOverflow), "SMUL with the most negative value returns it");

        a.push16(0x4000); a.push16(0x4000);
        a.run(0x76);  // SMUU
        CHECK(a.pop16() == 0x1000, "SMUU 4000 x 4000: upper half 1000");

        a.push16(7); a.push16((uint16_t)-2);
        a.run(0x6F);  // SDIV
        CHECK(a.pop16() == (uint16_t)-3, "SDIV 7 / -2 = -3, no remainder");
        a.push16(1234); a.push16(0);
        st = a.run(0x6F);
        CHECK(a.pop16() == 1234 && (st & 0x1E) == Am9511a::kDivZero, "SDIV by 0: R = B, divide-by-zero");
    }

    SECTION("Am9511A -- 32-bit fixed point");
    {
        Apu a;
        a.push32(0x7FFFFFFF); a.push32(1);
        uint8_t st = a.run(0x2C);  // DADD
        CHECK(a.pop32() == 0x80000000u && (st & Am9511a::kOverflow), "DADD 7FFFFFFF + 1 overflows");
        a.push32(100000); a.push32(300000);
        st = a.run(0x2D);  // DSUB
        CHECK(a.pop32() == (uint32_t)-200000 && (st & Am9511a::kCarry), "DSUB 100000 - 300000");
        a.push32(100000); a.push32((uint32_t)-30000);
        st = a.run(0x2E);  // DMUL
        // 0x4D2FA200 = the low 32 bits of -3000000000
        CHECK(a.pop32() == 0x4D2FA200u && (st & Am9511a::kOverflow),
              "DMUL 100000 x -30000 overflows; low 32 bits returned");
        a.push32(0x40000000); a.push32(0x10);
        a.run(0x36);  // DMUU
        CHECK(a.pop32() == 4, "DMUU 40000000 x 10: upper half 4");
        a.push32(1000000); a.push32(7);
        a.run(0x2F);  // DDIV
        CHECK(a.pop32() == 142857, "DDIV 1000000 / 7");
        a.push32(0x80000000u); a.push32(2);
        st = a.run(0x2F);
        CHECK(st & Am9511a::kOverflow, "DDIV with the most negative value sets overflow");
    }

    SECTION("Am9511A -- float add, subtract, multiply, divide");
    {
        Apu a;
        a.pushF(5); a.pushF(6);
        a.run(0x10);  // FADD
        CHECK(a.pop32() == 0x04B00000, "FADD 5 + 6 = 11 = 04B00000");
        a.pushF(5); a.pushF(6);
        uint8_t st = a.run(0x11);  // FSUB: B - A
        CHECK(a.pop32() == 0x81800000 && (st & Am9511a::kSign), "FSUB 5 - 6 = -1");
        a.pushF(5); a.pushF(5);
        st = a.run(0x11);
        CHECK(a.pop32() == 0 && st == Am9511a::kZero, "FSUB 5 - 5 = 0, all zeros");
        a.pushF(123); a.pushF(456);
        a.run(0x12);  // FMUL
        CHECK(a.popF() == 56088, "FMUL 123 x 456");
        a.pushF(1); a.pushF(3);
        a.run(0x13);  // FDIV
        CHECK(a.pop32() == 0x7FAAAAAA, "FDIV 1 / 3 = 7FAAAAAA (truncated)");
        a.pushF(7); a.pushF(0);
        st = a.run(0x13);
        CHECK(a.popF() == 7 && (st & 0x1E) == Am9511a::kDivZero, "FDIV by 0: R = B, divide-by-zero");
    }

    SECTION("Am9511A -- float results are truncated, with no guard bits");
    {
        Apu a;
        a.pushF(1 + std::ldexp(1.0, -23)); a.pushF(std::ldexp(1.0, -24));
        a.run(0x10);
        CHECK(a.pop32() == 0x01800001, "FADD: half an LSB is dropped, even from an odd base");
        a.pushF(1); a.pushF(std::ldexp(3.0, -25));  // 0.75 LSB
        a.run(0x10);
        CHECK(a.pop32() == 0x01800000, "FADD: three quarters of an LSB is dropped");
        a.push32(0x01800000); a.push32(0x00FFFFFF);  // 1 - (1 - 2^-24)
        a.run(0x11);
        CHECK(a.pop32() == 0x6A800000, "FSUB: A's low bit is lost in the alignment, so 1 - A = 2^-23");
        a.push32(0x00FFFFFF); a.push32(0x00FFFFFF);
        a.run(0x12);
        CHECK(a.pop32() == 0x00FFFFFE, "FMUL: the shift-and-add drops the low half");
        a.push32(0x80829087); a.push32(0x06BF132D);  // B's mantissa < A's
        a.run(0x13);
        CHECK(a.pop32() == 0xFAAEEDCC, "FDIV: 23 quotient bits and a 0 when B's mantissa < A's");
    }

    SECTION("Am9511A -- the real chip: status and result of every measured case (#908)");
    {
        for (const RealCase& rc : kReal) {
            Apu a;
            std::string ops = rc.ops;
            for (size_t i = 0; i < ops.size();) {
                size_t j = ops.find(' ', i);
                if (j == std::string::npos) j = ops.size();
                std::string h = ops.substr(i, j - i);
                uint32_t v = (uint32_t)std::stoul(h, nullptr, 16);
                if (h.size() == 4) a.push16((uint16_t)v);
                else a.push32(v);
                i = j + 1;
            }
            uint8_t st = a.run(rc.cmd);
            std::string got;
            char buf[4];
            for (size_t k = 0; k < std::string(rc.result).size() / 2; ++k) {
                std::snprintf(buf, sizeof buf, "%02X", a.c.readData(a.now));
                got += buf;
            }
            bool close = got == rc.result;
            if (!close && rc.match != '=') {
                uint32_t g = (uint32_t)std::stoul(got, nullptr, 16);
                uint32_t r = (uint32_t)std::stoul(rc.result, nullptr, 16);
                if (rc.match == '2') {  // the same sign and exponent, mantissa within 2
                    long d = (long)(g & 0xFFFFFF) - (long)(r & 0xFFFFFF);
                    close = (g >> 24) == (r >> 24) && d >= -2 && d <= 2;
                } else {
                    close = std::fabs(Am9511a::fromFloat(g) - Am9511a::fromFloat(r)) <= 1e-6;
                }
            }
            char what[160];
            std::snprintf(what, sizeof what, "%s: status %02X result %s (real %02X %s, match %c)",
                          rc.name, st, got.c_str(), rc.status, rc.result, rc.match);
            CHECK(st == rc.status && close, what);
        }
    }

    SECTION("Am9511A -- overflow and underflow keep the mantissa, exponent off by 128");
    {
        Apu a;
        a.push32(0x3F800000); a.push32(0x3F800000);  // 0.5 x 2^63, twice
        uint8_t st = a.run(0x12);
        CHECK(a.pop32() == 0x7D800000, "2^62 x 2^62 = 0.5 x 2^125: exponent 125 - 128 = -3");
        CHECK((st & 0x1E) == Am9511a::kOverflow, "with overflow");
        a.push32(0x40800000); a.push32(0x40800000);  // 0.5 x 2^-64, twice
        st = a.run(0x12);
        CHECK(a.pop32() == 0x7F800000, "0.5 x 2^-129 -> exponent -129 + 128 = -1");
        CHECK((st & 0x1E) == Am9511a::kUnderflow, "with underflow");
    }

    SECTION("Am9511A -- conversions");
    {
        Apu a;
        a.pushF(-3.75);
        uint8_t st = a.run(0x1F);  // FIXS
        CHECK(a.pop16() == (uint16_t)-3 && (st & Am9511a::kSign), "FIXS -3.75 = -3 (the integer portion)");
        a.pushF(40000);
        st = a.run(0x1F);
        CHECK((st & 0x1E) == Am9511a::kOverflow && a.popF() == 40000, "FIXS 40000 overflows; A not changed");
        a.pushF(123456789.0);
        a.run(0x1E);  // FIXD
        CHECK(a.pop32() == 123456784, "FIXD of 123456789 as a float (24 bits, truncated)");
        a.push16((uint16_t)-1);
        a.run(0x1D);  // FLTS
        CHECK(a.pop32() == 0x81800000, "FLTS -1 = 81800000");
        a.push32(0x01000003);  // 2^24 + 3: 25 bits
        a.run(0x1C);  // FLTD
        CHECK(a.pop32() == 0x19800001, "FLTD 2^24 + 3 drops the low bit");
    }

    SECTION("Am9511A -- change sign");
    {
        Apu a;
        a.push16(5);
        a.run(0x74);
        CHECK(a.pop16() == (uint16_t)-5, "CHSS 5 = -5");
        a.push16(0x8000);
        uint8_t st = a.run(0x74);
        CHECK(a.pop16() == 0x8000 && (st & Am9511a::kOverflow), "CHSS 8000 overflows, unchanged");
        a.push32(0x80000000u);
        st = a.run(0x34);
        CHECK(a.pop32() == 0x80000000u && (st & Am9511a::kOverflow), "CHSD 80000000 overflows, unchanged");
        a.pushF(5);
        a.run(0x15);
        CHECK(a.pop32() == 0x83A00000, "CHSF 5 = -5");
        a.push32(0);
        a.run(0x15);
        CHECK(a.pop32() == 0, "CHSF of zero makes no change");
    }

    SECTION("Am9511A -- stack commands");
    {
        Apu a;
        a.push16(0x1111); a.push16(0x2222);
        a.run(0x79);  // XCHS
        CHECK(a.pop16() == 0x1111 && a.pop16() == 0x2222, "XCHS swaps TOS and NOS");
        a.push32(0xAABBCCDD);
        a.run(0x37);  // PTOD
        CHECK(a.pop32() == 0xAABBCCDD && a.pop32() == 0xAABBCCDD, "PTOD pushes a copy");
        a.push16(1); a.push16(2);
        a.run(0x78);  // POPS
        CHECK(a.pop16() == 1, "POPS drops TOS");
        a.run(0x1A);  // PUPI
        CHECK(a.pop32() == 0x02C90FDA, "PUPI pushes pi truncated to 24 bits = 02C90FDA");
        a.pushF(-5); a.pushF(0);
        uint8_t pst = a.run(0x18);  // POPF
        CHECK(pst == Am9511a::kSign && a.popF() == -5, "POPF drops TOS; status is the new TOS");
        a.push32(0x80000000u); a.push16(0);
        uint8_t st = a.run(0x00);  // NOP
        CHECK(st == 0, "NOP clears the status byte");
    }

    SECTION("Am9511A -- BUSY lasts the top of each data sheet range");
    {
        struct { uint8_t cmd; unsigned cyc; const char* what; } t[] = {
            {0x6C, 18, "SADD 16-18"},   {0x6D, 32, "SSUB 30-32"},  {0x6E, 94, "SMUL 84-94"},
            {0x2C, 22, "DADD 20-22"},   {0x2E, 210, "DMUL 194-210"},
            {0x12, 168, "FMUL 146-168"}, {0x01, 870, "SQRT 782-870"}, {0x03, 4878, "COS 3840-4878"},
            {0x0B, 12032, "PWR 8290-12032"}, {0x00, 4, "NOP 4"}, {0x1A, 16, "PUPI 16"},
            {0x77, 16, "PTOS 16"}, {0x19, 26, "XCHF 26"},
        };
        for (auto& e : t) {
            Apu a;
            a.pushF(2); a.pushF(3);
            CHECK(a.cycles(e.cmd) == e.cyc, e.what);
        }
        Apu a;
        a.pushF(2); a.pushF(3);
        CHECK(a.cycles(0x10) == 368, "FADD 54-368");
        a.pushF(2); a.pushF(0);
        CHECK(a.cycles(0x10) == 24, "FADD with A = 0: 24");
        a.pushF(2); a.pushF(0);
        CHECK(a.cycles(0x13) == 22, "FDIV with A = 0: 22");
        a.push16(2); a.push16(0);
        CHECK(a.cycles(0x6F) == 14, "SDIV with A = 0: 14");
        a.pushF(1e-5);
        CHECK(a.cycles(0x02) == 30, "SIN of |A| <= 2^-12: 30");
        a.pushF(-1);
        CHECK(a.cycles(0x09) == 20, "LN of A <= 0: 20");
        a.pushF(40);
        CHECK(a.cycles(0x0A) == 34, "EXP of |A| > 32: 34");

        Apu b;
        b.pushF(2); b.pushF(3);
        b.c.writeCommand(0x12, b.now);
        CHECK(b.c.busy(b.now + 167) && !b.c.busy(b.now + 168), "busy for 168 CLKs, then idle");
        CHECK(b.c.readStatus(b.now + 100) == Am9511a::kBusy, "status shows BUSY while it runs");
        CHECK(b.c.readStatus(b.now + 168) == 0, "and the result's status after");
    }

    SECTION("Am9511A -- END: set at the end, cleared by any access or EACK");
    {
        Apu a;
        CHECK(!a.c.endOut(a.now), "END is off after power-on");
        a.pushF(2); a.pushF(3);
        a.c.writeCommand(0x10, a.now);
        uint64_t end = a.c.busyUntil();
        CHECK(!a.c.endOut(end - 1), "not while BUSY");
        a.c.readStatus(end - 1);
        CHECK(a.c.endOut(end), "a status read during BUSY does not stop it setting");
        a.c.readStatus(end);
        CHECK(!a.c.endOut(end), "a status read after the end clears it");

        a.c.writeCommand(0x00, end);
        CHECK(a.c.endOut(end + 4), "the next command sets it again");
        a.c.readData(end + 4);
        CHECK(!a.c.endOut(end + 4), "a data read clears it");
        a.c.writeCommand(0x00, end + 4);
        a.c.eack(end + 8);
        CHECK(!a.c.endOut(end + 8), "EACK clears it");
        a.c.writeCommand(0x00, end + 8);
        a.c.reset();
        CHECK(!a.c.endOut(end + 20) && !a.c.busy(end + 8), "RESET clears it and stops the command");
    }

    SECTION("Am9511A -- SVREQ follows the SR bit");
    {
        Apu a;
        a.c.writeCommand(0x80, a.now);  // NOP with SR = 1
        CHECK(!a.c.svreqOut(a.now), "not while the command runs");
        a.now = a.c.busyUntil();
        CHECK(a.c.svreqOut(a.now), "set at its end");
        a.c.readStatus(a.now);
        a.c.readData(a.now);
        CHECK(a.c.svreqOut(a.now), "an access does not clear it");
        a.c.writeCommand(0x00, a.now);  // SR = 0
        CHECK(a.c.svreqOut(a.now), "it stays while the next command runs");
        a.now = a.c.busyUntil();
        CHECK(!a.c.svreqOut(a.now), "and clears at the end of an SR = 0 command");
        a.run(0x80);
        a.c.svack();
        CHECK(!a.c.svreqOut(a.now), "SVACK clears it");
        a.run(0x80);
        a.c.reset();
        CHECK(!a.c.svreqOut(a.now), "RESET clears it");
    }

    // The data sheet gives one error figure per function, and AMD's graphs (Algorithm
    // Details pp.6-7) show where it holds. The real chip holds these. It does not hold the
    // others: every one of them is broken by a measured result in the table above (#908),
    // and the simulator gives the same result there. Measured worst: SIN 5.5e-7 absolute at
    // -3.2; COS 6.1e-7 relative at -0.963 and 6.9e-7 absolute at -2.5; ASIN 5.2e-7 relative
    // at 0.9965; ACOS 2.9e-7 relative at -0.9965, 1.4e-4 absolute at 0.99999994; LN 2.3e-7
    // at 0.458; LOG 3.8e-7 at 7.41; EXP 1.2e-6 relative at -16 and 1.9e-6 at -31.9; PWR
    // 2 ^ 10 = 0AFFFFF4 (7.2e-7).
    SECTION("Am9511A -- derived functions inside the data sheet's error figures");
    {
        const double kHalfPi = 1.5707963267948966;
        double w;
        w = worst(0x02, -kHalfPi, kHalfPi, 2000, [](double x) { return std::sin(x); }, true);
        CHECK(w < 5.0e-7, "SIN: 5.0e-7 relative over -pi/2 ... +pi/2");
        w = worst(0x04, -0.78, 0.78, 2000, [](double x) { return std::tan(x); }, true);
        CHECK(w < 5.0e-7, "TAN: 5.0e-7 relative inside -pi/4 ... +pi/4");
        w = worst(0x07, -1e4, 1e4, 8000, [](double x) { return std::atan(x); }, true);
        CHECK(w < 3.0e-7, "ATAN: 3.0e-7 relative");
        w = worst(0x01, 1e-6, 1e6, 8000, [](double x) { return std::sqrt(x); }, true);
        CHECK(w < 2.0e-7, "SQRT: 2.0e-7 relative (the graph peaks near 1.3e-7)");

        Apu a;
        a.pushF(1e-5);
        a.run(0x02);
        CHECK(a.popF() == Am9511a::fromFloat(Am9511a::toFloat(1e-5)), "SIN of a tiny angle returns A");
    }

    SECTION("Am9511A -- derived-function argument errors");
    {
        Apu a;
        a.pushF(-4);
        uint8_t st = a.run(0x01);
        CHECK((st & 0x1E) == Am9511a::kNegArg && a.pop32() == 0x80800000,
              "SQRT of a negative: error 0100; the mantissa stays, the exponent is 0");
        a.pushF(-4);
        CHECK(a.cycles(0x01) == 20, "SQRT of a negative stops at once");
        a.pop32();
        a.pushF(2);
        CHECK(a.cycles(0x05) == 140, "ASIN of 2 stops after 140 CLKs");
        a.pop32();
        a.pushF(40000);
        CHECK(a.cycles(0x1F) == 20, "FIXS overflow stops at once");
        a.pop32();
        a.pushF(2); a.pushF(100);
        CHECK(a.cycles(0x0B) == 6956 + 168, "PWR 2 ^ 100 stops after the LN and the FMUL");
        CHECK(a.pop32() == 0x078AA121, "and leaves A x LN B");
        a.pushF(0);
        st = a.run(0x09);
        CHECK((st & 0x1E) == Am9511a::kNegArg, "LN of 0: error 0100");
        a.pushF(1.5);
        st = a.run(0x05);
        CHECK((st & 0x1E) == Am9511a::kArgRange, "ASIN of 1.5: error 1100");
        a.pushF(33);
        st = a.run(0x0A);
        CHECK((st & 0x1E) == Am9511a::kArgRange, "EXP of 33: error 1100");
        a.pushF(-2); a.pushF(2);
        st = a.run(0x0B);
        CHECK((st & 0x1E) == Am9511a::kNegArg, "PWR with B < 0: error 0100");
        a.pushF(0); a.pushF(2);
        st = a.run(0x0B);
        CHECK((st & 0x1E) == Am9511a::kNegArg, "PWR with B = 0: error 0100 (B must be positive)");
        a.pushF(10); a.pushF(20);
        st = a.run(0x0B);
        CHECK((st & 0x1E) == Am9511a::kArgRange, "PWR 10 ^ 20: A x LN B > 32, error 1100");
    }

    SECTION("Am9511A -- a snapshot keeps the stack, the status and the time left");
    {
        Apu a;
        a.pushF(2); a.pushF(3);
        a.c.writeCommand(0x92, a.now);  // FMUL, SR = 1
        StateWriter w;
        a.c.serialize(w, a.now + 100);
        Am9511a b("apu");
        StateReader r(w.data());
        b.deserialize(r, 5000);
        CHECK(b.busy(5067) && !b.busy(5068), "68 CLKs were left, and 68 are left");
        CHECK(b.svreqOut(5068) && b.endOut(5068), "SVREQ and END come at the new end");
        CHECK(b.readStatus(5068) == 0, "the status travels");
        CHECK(Am9511a::fromFloat(b.readData(5068) << 24 | b.readData(5068) << 16 |
                                 b.readData(5068) << 8 | b.readData(5068)) == 6,
              "and so does the stack");
    }
}
