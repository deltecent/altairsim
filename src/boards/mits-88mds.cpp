#include "boards/mits-88mds.h"

#include "core/clock.h"

namespace altair {

// ---------------------------------------------------------------------------
// The one medium this card takes. From BOOT.ASM's `if MINIDSK` equates -- NUMTRK 35,
// NUMSEC 16, DATATRK 4 -- and confirmed by the manual, p4: 35 tracks, 16 sectors, 128 data
// bytes per sector, 71,680 formatted bytes (35 * 16 * 128).
//
// The FILE holds the whole 137-byte slot, not the 128-byte payload, because this is a
// hard-sector card: 35 * 16 * 137 = 76,720.
//
// AND THE REAL IMAGES ARE 76,800. XMODEM padded them up to a 128-byte block boundary, exactly
// as it did the 8" disks. 76,720 is not a multiple of 128 (it is 599.375 blocks), so this
// format needs sizeMatches()'s tolerance quite as much as the 8" one does -- the claim that
// "the other formats are already clean multiples of 128" was simply wrong, and there was no
// minidisk image in the tree to disprove it.
// ---------------------------------------------------------------------------
const std::vector<HsFormat>& mdsFormats() {
    static const std::vector<HsFormat> f = {
        {"minidisk", 35, 16, 4, 35ull * 16 * kHsSectorBytes},  // 76,720
    };
    return f;
}

// The DRIVE MOTOR ON DELAY one-shot (Board #2, IC B1). Manual p58: "The Drive Motor Delay
// Flip-Flop, B1 pin 4 triggers LOW for 1 second when the Disk Enable Flip-Flop is toggled
// LOW... allows the Disk Drive Motor time to stabilize." The handwritten one-shot sheet:
// "TP-6  1 SEC (0.9/1.5)  DRIVE MOTOR ON DELAY."
static constexpr uint64_t kMotorOnDelayUs = 1000000;  // 1 s

// THE DISK DISABLE TIMER, and it is not an RC network at all -- which is why we can state it
// to the digit. Manual p68: it "turns the system off after 6.4 seconds if there is no head
// movement or the Timer is not reset by software", and "The Timer is clocked every 12.5ms by
// the START OF SECTOR CLR pulse." The counter is a 4020 (14-stage CMOS ripple counter,
// IC B2 on Board #2 -- p73 has you bend its pin 10 up to disable it for a test).
//
//     12.5 ms x 512 = 6.4 s, exactly.
//
// THREE SOURCES, THREE NUMBERS, AND ONLY ONE OF THEM IS DERIVED. The MITS technical sheet
// says five seconds; this manual's own introduction (p3) and its test procedure (p73) say
// six. But 6.4 falls out of a divider tap, and MDBL.ASM ("Reset the 6.4 second timer") and
// FORMAT8M.ASM ("..minidisk for 6.4 Sec") -- two independent pieces of period software --
// both agree with the arithmetic. The round numbers are marketing and prose.
static constexpr uint64_t kDisableTimerUs = 6400000;  // 6.4 s

// The HEAD SETTLE one-shot (Board #2, IC B1). Manual p31/p32/p34/p75 and Figure 4-13 all say
// 50 ms; p68 says 40 ms and is outvoted. It gates BOTH the MH status bit and the sector
// channel (p34).
static constexpr uint64_t kStepSettleUs = 50000;  // 50 ms

MdsBoard::MdsBoard() { drive_.resize((size_t)drives_); }

uint64_t MdsBoard::now() const { return clock_ ? clock_->now() : 0; }

// The Disk Enable flip-flop AND the disable timer, in one question -- but the timer only gets
// a vote when the motor is REAL. With motor = "free" the 4020 never reaches its 512th sector
// pulse, because we never let it count: the drive is on because a drive is selected, and it
// stays on.
bool MdsBoard::systemOn() const {
    if (!diskEnable_ || sel() < 0) return false;
    return !motorReal_ || now() < disableAt_;
}

// Likewise the DRIVE MOTOR ON DELAY. A free motor is already at speed -- which is exactly
// what a drive that never stopped turning would report.
bool MdsBoard::atSpeed() const {
    if (!systemOn()) return false;
    return !motorReal_ || now() >= atSpeedAt_;
}

// NOT under the motor policy. The 50 ms HEAD SETTLE is a step-logic one-shot, not the motor:
// MH goes false and the sector channel goes dark, and MDBL polls for exactly that before it
// steps again. It costs 50 ms of EMULATED time per step and nothing at all of real time.
bool MdsBoard::stepping() const { return now() < settleAt_; }

void MdsBoard::restartTimer() { disableAt_ = now() + tFromUs(kDisableTimerUs); }

// ---------------------------------------------------------------------------
// OUT 08 -- drive enable. The base has already masked the drive number down to two bits and
// spotted a bit-7 "turn the system off"; what is left for us is the MOTOR.
//
// THE ONE-SHOT FIRES ON THE TRANSITION, NOT ON THE WRITE. Manual p58: the delay is triggered
// "when the Disk Enable Flip-Flop is toggled LOW" -- so re-selecting a drive that is already
// spinning does NOT re-arm it. That distinction is the difference between a card that works
// and a card that stops dead for a second on every OUT 8, and the period BIOS writes the
// select port on every single access (`lda curDrv / out DRVSLCT`).
// ---------------------------------------------------------------------------
void MdsBoard::onSelect(int oldSel, int newSel) {
    (void)oldSel;
    const bool wasOn = systemOn();  // asked BEFORE sel_ moves -- that is why we get the hook

    if (newSel < 0) {  // bit 7: "Minidisk system is turned off"
        diskEnable_ = false;
        atSpeedAt_  = 0;
        settleAt_   = 0;
        return;
    }

    diskEnable_ = true;
    if (!wasOn) atSpeedAt_ = now() + tFromUs(kMotorOnDelayUs);  // spin it up
    restartTimer();  // "the Disk Disable Flip-Flop is reset" on enable, p58
}

// ---------------------------------------------------------------------------
// The one jumper this card has that the 88-DCDD does not, and it is not a jumper at all --
// it is a POLICY, and it is off by default.
//
// FLAT OUT IS THE DEFAULT, and that is a rule this project already made once, for the CPU:
// `clock_hz = 0` free-runs, and 2 MHz is what you ASK for. Same call here, same reason. The
// 88-MDS's motor really does take a second to reach speed and really does stop after 6.4
// seconds of neglect, and a simulator that could not reproduce that would be hiding the very
// thing that shapes the minidisk driver. But a simulator that INSISTS on it makes you wait a
// real second, over and over, for a fidelity you did not ask for.
//
// So: `motor = "free"` (default) -- the motor is always at speed and never stops.
//     `motor = "real"` -- 1 s DRIVE MOTOR ON DELAY, and the 6.4 s Disk Disable Timer.
//
// Note what is NOT behind this switch: the 50 ms head settle, the 30 us sector-true window,
// the 64 us byte clock and the 300 RPM platter. Those are not waits, they are the card, and
// turning them off would not make anything faster -- it would just make it wrong.
// ---------------------------------------------------------------------------
std::vector<Property> MdsBoard::properties() {
    auto p = HardSectorFdc::properties();

    Property x;
    x.name    = "motor";
    x.help    = "free: always at speed (default). real: 1 s spin-up, and it stops after 6.4 s";
    x.kind    = Kind::Enum;
    x.choices = {"free", "real"};
    x.get     = [this] { return Value::ofStr(motorReal_ ? "real" : "free"); };
    x.set     = [this](const Value& v, std::string& err) {
        if (v.s() == "free")      motorReal_ = false;
        else if (v.s() == "real") motorReal_ = true;
        else { err = "motor is `free` or `real`"; return false; }
        return true;
    };
    p.push_back(std::move(x));
    return p;
}

// ---------------------------------------------------------------------------
// OUT 09 -- the control bits (manual pp. 31-32). Independent bits, not an opcode.
//
// COMPARE DcddBoard::command(). Same port, same card family, and yet:
//
//   bit 2 -- there, the head solenoid. HERE, the timer reset, and nothing else.
//   bit 3 -- there, head unload. HERE, "D3 = Not used" (p32).
//   bit 6 -- there, head current. HERE, "D6 = Not used" (p32).
//
// AND THE MANUAL'S OWN SAMPLE CODE GETS THIS WRONG. pp. 27 and 28 print a driver containing
//
//     MVI  A,8      ; UNLOAD HEAD
//     OUT  9        ; SEND COMMAND
//
// -- on the bit p32 calls "Not used", for a head p31 says is "always loaded when the Drive is
// enabled". Someone at MITS wrote the minidisk driver by editing the 8" driver and left the
// head-unload in; p26 does it again with "ENABLE WRITE WITHOUT SPECIAL CURRENT" on bit 6.
//
// So bit 3 has to be a NO-OP that is quietly, deliberately, correctly ignored -- because real
// software sends it. The card is the authority. The driver that happens to run on it is not,
// and this simulator believed the driver for months.
// ---------------------------------------------------------------------------
void MdsBoard::command(uint8_t v) {
    Drive* d = selected();
    if (!d) return;

    // Any STEP: flush first (the pending write belongs to the track we are leaving), then
    // move, then hold MH and the sector channel down for 50 ms.
    if (v & 0x03) {
        flushWrite();

        // BOTH BITS AT ONCE IS STEP *OUT*, and it is not a tie-break we invented. p67: "if
        // DØØ and DØ1 are both HIGH during an output to Channel Ø11, the STEP OUT direction
        // will always be selected due to the clearing action on the Step Direction
        // Flip-Flop." The 88-DCDD applies both and nets to zero. Different card, different
        // answer, and this is exactly the kind of difference a shared `if` would have buried.
        if (v & 0x02) {  // STEP OUT -- toward track 0. Stops there.
            if (d->track > 0) d->track--;
        } else if (v & 0x01) {  // STEP IN -- toward the spindle
            if (d->track < d->fmt.tracks - 1) d->track++;
        }

        settleAt_ = now() + tFromUs(kStepSettleUs);
        restartTimer();  // "STEP IN also resets the 6.4 second Disk Disable Timer" (p31, p32)
        invalidatePosition();
    }

    if (v & 0x04) restartTimer();  // TIMER RESET -- the whole of bit 2 on this card

    // 0x08 -- NOT USED. See the block comment: the manual's own driver sends it anyway.

    if (v & 0x10) st_.inten = true;   // interrupt at the start of every sector (12.5 ms).
    if (v & 0x20) st_.inten = false;  // Decoded, never wired to pin 73 by default -- and
                                      // "not used for Minidisk BASIC" (p4) either.

    // 0x40 -- NOT USED.

    if (v & 0x80) armWrite();  // WRITE ENABLE. Self-clears at the end of the sector (p32).
}

} // namespace altair
