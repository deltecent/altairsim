#pragma once
//
// MITS 88-MDS -- the Altair Minidisk System (docs/boards/mits-88mds.md).
//
// TWO S-100 BOARDS, one card here. The manual, p4: "Number of slots required in 8800 bus -
// 2." Board #1 is the port decode, the sector counter, the read circuit and the status
// output; Board #2 is the control latch, the step logic, the motor timing and the write
// circuits. (A third card, the buffer/power supply, is not on the bus at all -- it lives in
// the drive cabinet.) Nothing a program can reach distinguishes them, so they are one Board.
//
// THE REGISTER MODEL IS THE 88-DCDD'S, AND MITS DID THAT ON PURPOSE -- same three ports at
// 08/09/0A, same status bits in the same places, same inverted sense, same 137-byte
// hard-sector slot. So it all lives in HardSectorFdc, and what is left in this file is the
// part that is genuinely a 5.25" minidisk:
//
//   - 300 RPM, not 360 (manual p4: "Rotational Speed -- 300 rpm (200 ms/rev.)")
//   - a byte every 64 us, not 32 (125,000 bits/sec -- half the 8" rate)
//   - 35 tracks, 16 sectors; four drives, not sixteen
//   - THERE IS NO HEAD-LOAD BIT, because there is no solenoid. p31: "Note that the head is
//     always loaded when the Drive is enabled."
//   - ...so bit 2 is the TIMER RESET, and the card has a MOTOR that stops.
//
// THE MOTOR IS THE PERSONALITY OF THIS CARD -- and it is OPT-IN. Two timers, both from the
// manual, both OFF by default:
//
//   1. Enable a drive that was off and the motor takes ONE SECOND to reach speed. Until it
//      does, HS reads false and the sector channel reads all ones (p34).
//   2. The Disk Disable Timer turns the whole system off after 6.4 SECONDS with no access.
//      That is why the period BIOS issues cRESTMR before every single read and write.
//
// `motor = "free"` IS THE DEFAULT, and it is the same call the Clock already made (see
// docs/boards/mits-88cpu.md: hz() is a divisor, free() is a policy, and flat out is the
// default). A minidisk that makes you wait a real second for a real motor is authentic and
// it is also, most of the time, just slow -- so you ask for it:
//
//     [[board]]
//     type  = "mds"
//     motor = "real"     # 1 s to spin up, and it stops after 6.4 s
//
// What is NOT optional is the 50 ms HEAD SETTLE after a step: MH goes false and the sector
// channel goes dark, exactly as the manual says. It is a status bit that is briefly false,
// not a wall the machine runs into, and MDBL polls for it by name.
//
// A CARD THAT TURNS ITSELF OFF is not a quirk to work around; it is the reason the minidisk
// driver looks the way it does, and a simulator that CANNOT model it would be lying. One
// that can, and doesn't by default, is just being quick.

#include "boards/mits-hardsector.h"

namespace altair {

const std::vector<HsFormat>& mdsFormats();

class MdsBoard : public HardSectorFdc {
public:
    MdsBoard();

    std::string type() const override { return "mds"; }

    std::vector<Property> properties() override;

protected:
    const std::vector<HsFormat>& formats() const override { return mdsFormats(); }

    // FOUR drives, and only two bits of drive address. The 88-DCDD reads four bits and
    // daisy-chains sixteen. Manual p29: "Enables one of four Minidisk Drives"; p30's table
    // gives the addresses as D0/D1 only.
    int     maxDrives()  const override { return 4; }
    uint8_t selectMask() const override { return 0x03; }

    // 300 rpm, 200 ms/rev, 16 sectors -> 12.5 ms per sector (manual p4). The 88-DCDD's 360
    // was hard-coded for every format it knew, and the minidisk silently inherited it.
    int rpm() const override { return 300; }

    // 125,000 bits/sec -> 15,625 bytes/sec -> one byte every 64 us. Manual p4, p30 ("ENWD
    // ... Occurs during the Write mode every 64us"), p31 ("NRDA occurs every 64us"), and the
    // schematic's divider chain: S-100 pin 49 (2 MHz) -> /2 -> /8 -> 125 kHz bit clock.
    //
    // THE MANUAL CONTRADICTS ITSELF ON EXACTLY THIS, and the contradiction is a fossil. p45:
    // "a new byte of Write Data is requested every 32 microseconds." 32 us is the 88-DCDD's
    // rate -- someone wrote this manual by editing the floppy manual and missed a number,
    // which is the same mistake, in the same direction, that this simulator made. Every other
    // page says 64, and 125000/8 = 15625 settles it arithmetically.
    uint64_t byteUs() const override { return 64; }

    // The READ CLEAR one-shot. Manual p34: "the Read Circuit is disabled during the first
    // 500us to insure valid detection of the sync byte", and the handwritten one-shot sheet
    // bound in after p79 lists "TP-5  500 uS (400/600)  READ CLEAR".
    uint64_t readStartUs() const override { return 500; }

    // The WRITE CLEAR one-shot. Manual p32: "Zeros are automatically written for the first
    // 1ms of the Sector. 1ms after beginning of Sector, ENWD goes True, requesting first byte
    // of Write Data."
    uint64_t writeStartUs() const override { return 1000; }

    void command(uint8_t v) override;

    // "The Read/Write Head is loaded when the Drive is enabled" (p29) -- so there is nothing
    // for software to load, and HS means something else entirely on this card: "indicates the
    // head is properly loaded AND MOTOR SPEED IS STABLE. Goes True one second after Disk
    // Enable." (p31)
    //
    // With motor = "free" the second is simply not spent, so HS is true the moment the drive
    // is enabled -- which is what a drive whose motor is already at speed would report, and
    // it is the honest reading of a machine that never turns its motor off.
    bool headLoaded(const Drive&) const override { return atSpeed(); }

    // MH: "Goes False for 50ms. after the step command... Also False during Write mode." (p30)
    bool moveOk(const Drive&) const override { return !writing_ && !stepping(); }

    // The card takes ITSELF off the bus in two ways the 88-DCDD never does. p30: "all status
    // bits are logic 1 when there is not a Minidiskette in the Drive", and NOTE 3: "If the
    // Drive selected is not connected or its power is off, the Controller will automatically
    // turn off." Plus the disable timer, below.
    bool online(const Drive& d) const override { return d.img != nullptr && systemOn(); }

    // p34, and this is the one that catches a simulator out: "The Sector position channel
    // will be disabled (all "1"s) for 1 second after the Drive is enabled, and 50 ms after a
    // step command is issued."
    bool sectorChannelLive() const override { return atSpeed() && !stepping(); }

    void onSelect(int oldSel, int newSel) override;

private:
    uint64_t now() const;
    bool     systemOn() const;   // the Disk Enable flip-flop, and the 6.4 s timer
    bool     atSpeed() const;    // the 1 s DRIVE MOTOR ON DELAY has expired
    bool     stepping() const;   // inside the 50 ms HEAD SETTLE window
    void     restartTimer();     // "Resets the 6.4 second Disk Disable Timer"

    // THE POLICY, and it is off by default. `motor = "real"` spins the motor up in a second
    // and stops it after 6.4; `motor = "free"` -- the default -- keeps it turning forever, so
    // the machine runs flat out. The timers below are still computed either way; they are
    // simply not CONSULTED when the motor is free, which keeps the two paths from drifting.
    bool motorReal_ = false;

    // Both are DEADLINES on the Clock, not counters something advances -- the same discipline
    // as Spindle (DESIGN.md 7.5.1). A counter that ticks when the guest reads a port makes
    // the hardware run at the speed of the software watching it.
    uint64_t disableAt_ = 0;  // when the Disk Disable Timer expires
    uint64_t atSpeedAt_ = 0;  // when the motor reaches speed
    uint64_t settleAt_  = 0;  // when the head has settled after a step (NOT optional)

    // The Disk Enable flip-flop, apart from the timer that clears it. A free-running motor
    // still has to answer "is a drive switched on", and that is this bit and not a deadline.
    bool diskEnable_ = false;
};

} // namespace altair
