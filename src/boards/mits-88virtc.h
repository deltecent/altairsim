#pragma once
//
// The MITS 88-VI/RTC -- vectored interrupts and a real-time clock, on one card.
// docs/boards/mits-88virtc.md. Source: reference/88-VI-RTC.pdf (MITS, 1976).
//
// TWO MODULES, ONE BOARD, ONE PORT. The manual gives them separate theories of
// operation and MITS sold them as separate options, but they share a schematic, a
// slot and a control register -- so they are one card here. The RTC's interrupt
// output goes out through the VI section like any other device's would.
//
// ---------------------------------------------------------------------------
// WHAT THE VI SECTION IS FOR (manual, "88-Vector Interrupt Theory of Operation"):
//
//     "The CPU has a single interrupt input... One purpose of the Vectored
//      Interrupt Board is to put the RST instruction on the data buss with the
//      correct 3 bit address at the correct time. The other purpose is to allow
//      only the highest active priority of the eight levels to interrupt the CPU."
//
// So it is an 8214 priority encoder that WATCHES the eight VI lines, applies a
// mask, drives pin 73 itself, and then CLAIMS THE IntAck CYCLE to jam an RST onto
// the data bus. Every one of those is an ordinary Board behavior. The bus needed to
// grow nothing but the eight wires (DESIGN.md 4.4): it still picks no winner and
// invents no vector, because picking the winner is this chip's entire job.
//
//     "The buss lines are prioritized in the 88-VI; VI0 is highest priority and
//      VI7 is lowest."
//
// Level n -> RST n -> octal n0. VI7 -> RST 7 -> 0x0038, and *that* is what the MITS
// Programming System II ReadMe means by "the console serial port must be set to
// vector 7".
//
// WITH NO 88-VI IN THE MACHINE, an unclaimed IntAck floats to 0xFF, which the 8080
// executes as RST 7 -- so a card strapped straight to pin 73 lands on 0x0038 too,
// by a completely different route. That is not a coincidence to lean on; the manual
// forbids mixing the two:
//
//     "A system designed to use the 88-VI may not have any I/O board strapped for
//      single level interrupt. Interrupts on I/O boards must be hardwire connected
//      to one of the eight 88-VI interrupt levels."
//
// ---------------------------------------------------------------------------
// THE CONTROL REGISTER -- PORT 376 OCTAL (254 DECIMAL, 0xFE). WRITE ONLY.
//
//   bit 7   1 = enable the 88-VI structure.   POC leaves it DISABLED.
//   bit 6   1 = enable the RTC interrupt.
//   bit 5   1 = clear the RTC's divider chain (restart it at time zero).
//   bit 4   1 = clear the RTC's interrupt flip-flop.
//   bit 3   1 = the current-level register is live (see below).
//   bits 0-2  the current interrupt level -- ONES-COMPLEMENT. See below.
//
// There is no IN. The card drives the data bus during IntAck and at no other time.
//
// ---------------------------------------------------------------------------
// BITS 0-2 ARE INVERTED, AND THE MANUAL CONTRADICTS ITSELF ABOUT IT.
//
// Page 3 says the level-4 routine "outputs a 100 for bits 2, 1 and 0" -- that is 4,
// read straight. Page 5's table says level 4 -> `MVI A,13Q` -> bits 2-0 = 011 = 3,
// and its worked example for level 2 -> `MVI A,15Q` -> 101 = 5. Those are 7-level,
// not level. Two of the three disagree with the prose, and the 8214's B0-B2 inputs
// are active-low, which is exactly why.
//
// SO WE ASKED THE ARTIFACT, not the document. The PS2 monitor's own service routine
// (it is the only real client of this card we have) opens with:
//
//      0038  F5        PUSH PSW          <- the RST 7 vector
//      0039  C3 DB 08  JMP 08DB
//      08DB  3E 08     MVI A,08          <- level 7: bit 3 set, bits 0-2 = 000
//      08E0  F6 C0     ORI C0            <- + bit 7 (VI enable) + bit 6 (RTC enable)
//      08E2  D3 FE     OUT FE
//      08E4  DB 11     IN 11             <- read the 6850, dropping its IRQ
//
// 0x08 is exactly the manual's table entry for level 7 (`MVI A,10Q`). The console is
// on level 7 and it writes 000. The table is right, the prose is wrong, and the code
// that shipped agrees with the table: bits 0-2 = 7 - level.
//
// BIT 3 the same way. The manual's sentence about it parses two ways; the monitor
// settles it. It sets bit 3 on ENTRY (with level 7) so that another level 7 cannot
// interrupt the handler, and CLEARS it on exit --
//
//      0921  AF        XRA A             <- the Ctrl-C break path
//      0925  F6 C0     ORI C0            <- 0xC0: bit 3 CLEAR
//      0927  D3 FE     OUT FE
//
// -- which re-opens the machine to level 7. So bit 3 = 1 makes the current level
// live ("uninterruptable by another level of the same or lesser priority"), and
// bit 3 = 0 disables the comparison so anything can interrupt. That also makes sense
// of the manual's warning that "if data bit 3 is not set by the initialization,
// level 7 can not interrupt": a card that has never been initialized sits at current
// level 0 with the comparison live, and nothing is higher priority than 0.
//
// ---------------------------------------------------------------------------
// THE RTC, AND HOW YOU TURN IT OFF.
//
// A divider chain off either the 60 Hz line or a 10 kHz derivative of the 2 MHz
// system clock, divided again by 1, 10, 100 or 1000 -- a jumper each. When it fires
// it sets a flip-flop, and the flip-flop's output ("RI" on the schematic) is
// JUMPERED TO ONE OF THE EIGHT VI LINES. The service routine clears it by writing
// bit 4.
//
// That jumper is why `rtc_interrupt = none` is not a cop-out. The PS2 ReadMe says:
//
//     "The programming package assumes the 88-VI/RTC board is present and enables
//      real-time clock interrupts from the board, yet doesn't provide an interrupt
//      handler for the RTC. In order to run with interrupts enabled, interrupts from
//      the RTC on the 88-VI/RTC must be disabled..."
//
// And the monitor really does enable them -- `ORI C0` sets bit 6, unconditionally,
// every time through the ISR. You cannot talk it out of that. What you CAN do is
// leave RI unsoldered, which is what the operator did, and then the flip-flop sets
// and sets and nothing is listening. So: the strap defaults to `none`, and no
// special case appears anywhere in this file.

#include "core/board.h"
#include "core/clock.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class VirtcBoard : public Board {
public:
    std::string type() const override { return "virtc"; }

    // The control port on writes -- and the IntAck cycle, but ONLY when we are the
    // card actually asking for it. A disabled 88-VI drives nothing, the cycle goes
    // unclaimed, the bus floats 0xFF and the 8080 takes RST 7. Same rule as every
    // other unclaimed cycle in this machine; no special case.
    bool decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void write(const BusCycle& c) override;

    // Pin 73. We pull it when the VI structure is enabled and some line is asking at
    // a priority the current-level register will admit.
    bool assertsInt() const override;

    // ...and we PULL a VI line too, when the RTC fires and its RI strap is installed.
    // The card's own interrupt goes out onto the same backplane wire as everybody
    // else's and comes back in through the same priority encoder. That is what the
    // schematic does, and it means the RTC gets prioritized against the other seven
    // levels for free, instead of through a private back door.
    uint8_t assertsVi() const override;

    // ...and we WATCH all eight, which is the entire point of the card.
    bool watchesVi() const override { return true; }

    // ...and we are the one who decides which of them wins. SHOW BUS IRQ asks.
    int intWinner() const override;

    void reset(Reset) override;
    void power() override;
    void configChanged() override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // ---- what the tests and SHOW look at ----

    // The highest-priority line currently asking that the mask will admit, or -1.
    // VI0 wins over VI7: LOWER NUMBER IS HIGHER PRIORITY.
    int winner() const;

    // The RST opcode for a level. The encoding is the 8080's, not ours -- we only jam
    // it onto the bus -- so it lives in board.h and we forward to it.
    static uint8_t rstFor(int level) { return rstOpcode(level); }

private:
    // Every VI line being pulled right now, INCLUDING OUR OWN RTC's. Ours is OR'd in
    // directly rather than read back from the bus, because Board::intChanged() settles
    // pin 73 before it publishes our VI output -- so for one instant the bus's copy of
    // the wire would be missing our contribution, and we would fail to raise our own
    // clock interrupt. OR-ing is idempotent, so this is just the settled wire.
    uint8_t pendingLines() const;

    void armRtc();     // cancel and re-arm the divider's one-shot
    void onRtcTick();  // the flip-flop sets; RI goes out onto its VI line
    uint64_t rtcPeriod() const;

    uint8_t port_ = 0xFE;  // 376 octal. The manual decodes all eight address lines.

    // ---- the VI section (the 8214 and its latches) ----
    bool viEnabled_ = false;  // bit 7. POC leaves the structure disabled.
    bool levelLive_ = false;  // bit 3. Is the current-level comparison in circuit?
    int  curLevel_  = 0;      // bits 0-2, DECODED (we store the level, not the code)

    // ---- the RTC section ----
    enum class RtcSource { Line, Clock };
    RtcSource rtcSource_  = RtcSource::Line;      // 60 Hz mains, or 10 kHz off the CPU
    int       rtcDivide_  = 1;                    // 1, 10, 100 or 1000
    bool      rtcEnabled_ = false;                // bit 6
    bool      rtcInt_     = false;                // the flip-flop (IC Fb)
    IrqJumper rtcJumper_  = IrqJumper::None;      // "RI" -- and `none` is the PS2 machine

    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
