#pragma once
//
// FD1771 -- Western Digital's Floppy Disk Formatter/Controller. The first of the
// family; the one the Tarbell soft-sector card is built around.
//
// A CHIP, NOT A CARD. The Tarbell Floppy Disk Interface has one of these on it, and
// the next card to turn up with one gets it for free. Modeled from the FD1771 data
// sheet (reference/Western Digital FD1771 - Datasheet.pdf), cross-checked against
// the copy Tarbell reprinted as section 7-2 of their own manual -- NOT from the one
// BIOS that happens to drive it, which is how you end up implementing the subset
// that BIOS uses and quietly getting the rest wrong.
//
// It knows nothing about S-100, and it knows nothing about the Tarbell's ports.
//
// ---------------------------------------------------------------------------
// WHY THE FILE IS `wd17xx` AND THE CLASS IS `Wd1771`.
//
// The family shares a register file and a command set, and a later card will want
// the FD1791/93. It will NOT want this class: the 179x is a different chip in the
// ways that bite (MFM, side select, a one-bit record type). So the FILE is the
// family and the CLASS is the part, and the 1793 lands next to this one rather than
// inside it, behind a `#ifdef` that would be a lie either way.
//
// AND IT IS NOT THE WD1770/72/73. `reference/Western Digital WD177X-00 - Datasheet.pdf`
// is in the tree and it is THE WRONG CHIP for this card -- a 28-pin part with an
// on-board data separator, MFM, and motor control. Its step-rate table reads
// 6/12/20/30 ms. THE FD1771's READS 6/6/10/20 (Table 1, at CLK=2 MHz). A chip built
// from that sheet would look right, compile, boot nothing, and step at the wrong
// speed forever. Do not "fix" this file against it.
// ---------------------------------------------------------------------------

#include "core/clock.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace altair {

// ---------------------------------------------------------------------------
// WHAT IS ON THE OTHER END OF THE DISK PINS.
//
// The same seam ByteStream is for the 6850: the chip owns no image, no file and no
// geometry. It has pins -- STEP, DIRC, TR00, WPRT, READY, IP -- and a drive on the
// far end of them, and this is that drive.
//
// NOTE WHAT IS *NOT* HERE, because it is the whole chip/board split in one absence:
//
//   - NO DRIVE SELECT. The FD1771 has no drive-select pin. It talks to ONE drive,
//     and the CARD's select latch decides which. So the board calls attach() and
//     the chip is none the wiser -- which is exactly what the silicon does.
//   - NO SIDE SELECT. Same reason: the 1771 has no such pin either.
//   - NO MOTOR CONTROL. That is a WD1770 thing, and this is not one.
//
// THE HEAD POSITION LIVES HERE, NOT IN THE TRACK REGISTER. This is the distinction
// the whole seek/verify mechanism is built on and it is worth being blunt about: the
// chip's Track Register is SOFTWARE'S BOOKKEEPING -- a number the guest wrote, or
// that the chip counted up as it pulsed STEP. Where the head physically IS is the
// drive's business and only the drive's. They are allowed to disagree, and when they
// do, a verify sets Seek Error (S4). Fold them into one variable and Seek Error can
// never happen, which means a driver's error recovery is never exercised, and the
// first real disk with a bad ID field silently reads the wrong track.
// ---------------------------------------------------------------------------
class FloppyDrive {
public:
    virtual ~FloppyDrive() = default;

    // ---- The input pins, straight ----
    virtual bool ready() const = 0;           // READY (pin 32): is there a disk, is it spinning
    virtual bool writeProtected() const = 0;  // WRPT* (pin 36)
    virtual bool trackZero() const = 0;       // TR00* (pin 34): the head is over track 0

    // IP (pin 35): the index hole is under the sensor RIGHT NOW. A pulse, not a level
    // -- it is true for a sliver of each revolution. The chip needs it for two things
    // and cannot synthesize either: Type I status bit S1 IS this pin, and a Force
    // Interrupt on I2 fires on its edge. It belongs to the DRIVE because it is the
    // disk that is turning; the chip has no idea how fast, and no pin that tells it.
    virtual bool index() const = 0;

    // WF (pin 33): the drive's own write electronics have faulted -- no write current
    // when the gate is open, that sort of thing. "When WG=1 and WF goes low, the current
    // Write command is terminated and the Write Fault status bit is set."
    //
    // NOT PURE. A drive backed by a host file has no write electronics TO fault, so the
    // honest default is "no fault" -- but the pin exists, S5 WRITE FAULT is a documented
    // status bit, and without the pin there is no way to ever produce it except by the
    // medium refusing the write. A real drive model can override it.
    virtual bool writeFault() const { return false; }

    // DINT (pin 37): "sampled whenever a Write Track command is received. If DINT=0, the
    // operation is terminated and the Write Protect status bit is set." A second, separate
    // interlock on formatting -- the board could strap it to forbid a format outright,
    // independently of the disk's own write-protect notch. Default: formatting is permitted.
    virtual bool diskInit() const { return true; }

    // ---- STEP (pin 15) and DIRC (pin 16), as one call, because they arrive together ----
    // Moves the HEAD. A step outward at track 0 goes nowhere -- the head is against
    // its stop, and the real drive just buzzes.
    virtual void step(bool inward) = 0;

    // Where the head actually is. For the drive's own use and for tests; the chip
    // reads it only to build an ID field. See the note above.
    virtual int headTrack() const = 0;

    // ---- WHAT IS WRITTEN ON THE TRACK UNDER THE HEAD ----
    //
    // The chip searches these for an ID field matching (Track Register, Sector
    // Register). It is a LIST AND NOT A LOOKUP on purpose: Read Address returns "the
    // next ID field encountered", which is a question about rotation, and a map keyed
    // by sector number could not answer it.
    // ---- ONE DELIBERATE DEVIATION, AND IT IS HERE ----
    //
    // On the real chip, how many bytes a Read or Write moves is decided by the COMMAND's
    // `b` flag together with the ID field's length code -- the Type II flow chart has a box
    // for it: "COMPUTE LENGTH FROM b FLAG". b=1 gives IBM sizes (128 << n); b=0 gives
    // non-IBM ones (16 x N, where a code of 0 means 256 groups, i.e. 4096 bytes).
    //
    // The two readings COLLIDE: length code 00 is 128 bytes under b=1 and 4096 under b=0.
    // The chip decides, and the disk cannot.
    //
    // THIS CHIP DOES NOT DO THAT ARITHMETIC. It moves `size` bytes, and the DRIVE is what
    // decoded them. Why: the drive is the only thing here that knows its own format, and a
    // raw sector image physically cannot serve a length the format did not record. Ask a
    // real controller for 4096 bytes out of a 128-byte sector and it reads on through the
    // gaps and the next eleven sectors and hands back the lot with a CRC error. There are
    // no gaps in a .DSK to read through -- so honouring `b` would mean INVENTING the
    // garbage, which is precisely what DESIGN.md 0.1 forbids.
    //
    // To be observable you need a driver whose `b` flag disagrees with the disk in its own
    // drive -- a driver that is already broken on real hardware. Written down rather than
    // left to be discovered. If a disk ever turns up that needs it, start here.
    struct SectorId {
        int  track      = 0;  // as RECORDED in the ID field -- which is the point of a verify
        int  sector     = 0;
        int  size       = 0;  // decoded length of the data field, in bytes

        // The length byte AS RECORDED, which is NOT derivable from `size` -- the same
        // 128-byte sector is code 0 on an IBM-format disk (128 << n) and code 8 on a
        // non-IBM one (16 x N). Read Address hands the raw byte to the guest, so the
        // raw byte has to survive the trip; a chip that re-encoded it from `size`
        // would quietly change what a formatter reads back.
        int  lengthCode = 0;

        bool deleted    = false;  // the data field carries an F8, not an FB
        bool idCrcOk    = true;   // a bad ID-field CRC. Real disks have them; images do not
        bool dataCrcOk  = true;   // ...and a bad data-field CRC
    };

    virtual int  sectorCount() const = 0;                        // ID fields on this track
    virtual bool sectorIdAt(int index, SectorId& out) const = 0;  // in ROTATIONAL order

    // The data field. `n` is IN/OUT, exactly as DiskImage has it: on entry the
    // capacity, on exit the bytes moved. A short read is a failure, not a truncation.
    virtual bool readData(const SectorId& id, uint8_t* buf, size_t* n) = 0;
    virtual bool writeData(const SectorId& id, const uint8_t* buf, size_t n) = 0;

    // ---- THE WHOLE TRACK, GAPS AND ADDRESS MARKS AND ALL (Read Track / Write Track) ----
    //
    // Everything above is a SECTOR interface, because that is what a Type II command
    // addresses. These two are not: they run index-to-index and they see the bytes the
    // gaps are made of. That is a real difference in kind, and it is why they are
    // separate calls rather than a `size` that happens to be big.
    //
    // NOT PURE, AND FALSE BY DEFAULT, because a drive backed by a raw .DSK genuinely
    // CANNOT produce one: the gaps and the address marks were never in the file. That
    // is not a limitation to paper over -- it is a fact about the medium, and the right
    // answer is to say so (Write Track sets WRITE FAULT, which is the bit the hardware
    // would set) rather than to hand a formatter 5,208 invented bytes that round-trip
    // into a disk nothing can read.
    virtual int  trackImageBytes() const { return 0; }  // 0: no bit-level image here
    virtual bool readTrackImage(std::vector<uint8_t>& out) { (void)out; return false; }
    virtual bool writeTrackImage(const std::vector<uint8_t>& in) { (void)in; return false; }
};

// ---------------------------------------------------------------------------
// One FD1771.
// ---------------------------------------------------------------------------
class Wd1771 {
public:
    explicit Wd1771(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    // ---- THE REGISTER FILE (A1,A0 under RE/WE) ----
    //
    //      A1 A0     READ            WRITE
    //       0  0     Status Reg      Command Reg
    //       0  1     Track Reg       Track Reg
    //       1  0     Sector Reg      Sector Reg
    //       1  1     Data Reg        Data Reg
    //
    // The DAL is an INVERTED bus on this chip (pins 7-14, "eight bit inverted
    // bidirectional bus"). The inversion IS NOT DONE HERE: the Tarbell runs the bus
    // through inverting buffers on its way to S-100, so the two inversions cancel and
    // the guest sees true data. Putting it in the chip and un-doing it on the card
    // would be two lies that happen to agree. THE CHIP'S REGISTERS ARE TRUE SENSE.
    // (Same rule as the COM2502's status bits -- see uart1602.h.)
    uint8_t readStatus(const Clock& clk);
    uint8_t readTrackReg() const { return track_; }
    uint8_t readSectorReg() const { return sector_; }
    uint8_t readData(const Clock& clk);

    void writeCommand(uint8_t v, const Clock& clk);
    void writeTrackReg(uint8_t v) { track_ = v; }
    void writeSectorReg(uint8_t v) { sector_ = v; }
    void writeData(uint8_t v, const Clock& clk);

    // ---- MASTER RESET -- pin 19, AND THIS CHIP REALLY HAS ONE ----
    //
    // Unlike the 6850 (which has no reset pin at all -- see mc6850.h), the FD1771 has
    // MR, and on the Tarbell the S-100 RESET* reaches it. What it does is NOT "clear
    // the registers", and if you assume it is you will be hunting the difference for
    // a day:
    //
    //   "A logic low on this input resets the device and loads '03' into the command
    //    register. The Not Ready (Status bit 7) is reset during MR ACTIVE. When MR is
    //    brought to a logic high, A RESTORE COMMAND IS EXECUTED, regardless of the
    //    state of the Ready signal from the drive."
    //
    // So a front-panel RESET does not merely idle the controller -- IT DRIVES THE HEAD
    // BACK TO TRACK 0, by itself, with no help from any software. 0x03 is Restore with
    // h=0, V=0 and r1r0=11: the 20 ms step rate, the slowest one, which is what you
    // want when you have no idea where the head is.
    void masterReset(const Clock& clk);

    // POWER APPLIED. A known-good state at once, so the card is usable the instant the
    // machine comes up (DESIGN.md 6.1). Not the same thing as MR: powering on does not
    // restore, because at power-on there is not yet a drive to restore.
    void powerOn(const Clock& clk);

    // ---- THE TWO OUTPUT PINS THE CARD CARES ABOUT ----
    //
    // INTRQ (pin 39): set at the completion OR termination of any operation. "The
    // INTRQ signal remains active until reset by reading the Status Register to the
    // processor or by the loading of the Command Register." Note that it is NOT reset
    // by reading the Data Register -- a driver that polls DRQ forever and never reads
    // status leaves INTRQ asserted, and on a card that jumpers it to pin 73 that is an
    // interrupt storm. It is meant to be that way.
    bool intrq() const { return intrq_; }

    // DRQ (pin 38): the Data Register has a byte in it (read) or wants one (write).
    // Also visible as status bit S1 on every command except Type I.
    bool drq() const { return drq_; }

    // ---- THE DRIVE ----
    //
    // The CARD's drive-select latch is what points this. A null drive is not an error
    // and not a crash: it is a card with nothing plugged into it, which reads NOT
    // READY, exactly as the bare connector does.
    void         attach(FloppyDrive* d) { drive_ = d; }
    FloppyDrive* drive() const { return drive_; }

    // ---- THE STRAPS ----
    //
    // The FDC's own crystal, which IS NOT THE CPU's. The Tarbell hangs a 4 MHz can off
    // the board and halves it; the chip wants 2 MHz. It selects the column of Table 1
    // -- and therefore how long a step takes -- and NOTHING else here, because every
    // other deadline in this chip is set by how fast the DISK turns, not by how fast
    // the chip's clock runs.
    long long fdcClockHz = 2000000;

    // The bit rate on the MEDIA. 250 kbit/s is the 8" single-density standard, which
    // is what the Tarbell manual quotes ("the standard speed of 250,000 bits per
    // second") and what an 8" drive turning at 360 RPM produces. It is a strap and not
    // a constant because a 5.25" drive runs at half of it, and the same chip drives
    // both.
    long long dataRateBits = 250000;

    // ADVANCE THE STATE MACHINE. Public because the CARD must be able to call it: a
    // seek takes 20 ms per track and the guest is not touching a register while it
    // happens, so a chip that only moved when a port was read would never finish one.
    void poll(const Clock& clk);

    // THE NEXT MOMENT THIS CHIP'S PINS COULD MOVE WITH NOBODY TOUCHING IT. Zero means
    // never. Always strictly in the future -- see Mc6850::nextEdge(), the reasoning is
    // identical and it is the difference between a deadline and a spin.
    uint64_t nextEdge(const Clock& clk) const;

    // Drain what the chip has to say (a drive that could not take a format). Cleared
    // by draining. Board::drainLog() is what surfaces it.
    std::vector<std::string> drainLog();

    // For SHOW and for tests: is a command running, and which.
    bool busy() const { return phase_ != Phase::Idle; }

private:
    // ---- THE STATUS REGISTER IS SIX DIFFERENT REGISTERS (Table 6) ----
    //
    // "Status varies according to the type of command executed." Not according to the
    // command in the command register -- according to the one that WAS EXECUTED, which
    // is a LATCH, and it is the single easiest thing to get wrong in this chip:
    //
    //   - S6 is WRITE PROTECT after a Type I, and RECORD TYPE after a Read.
    //   - S4 is SEEK ERROR after a Type I, ID NOT FOUND after a Read Address, and
    //     RECORD NOT FOUND after a Read.
    //   - S2 is TRACK 0 after a Type I and LOST DATA after everything else.
    //   - S1 is INDEX after a Type I and DRQ after everything else.
    //
    // So a driver that seeks, then reads a sector, then polls status is looking at
    // three different meanings for bit 2 depending on where it is -- and a model that
    // reports one fixed layout will hand a BIOS "track 0" when it asked "did I lose
    // data". Latch the type; report through it.
    //
    // The Force Interrupt rule falls straight out of this, and it is in the data sheet
    // verbatim: FI *while a command runs* resets BUSY and LEAVES THE OTHER BITS ALONE
    // (so the context stays whatever it was). FI *while idle* updates them and "Status
    // reflects the Type I commands" -- i.e. it RESETS the context to Type I.
    enum class Ctx { TypeI, ReadAddress, Read, ReadTrack, Write, WriteTrack };

    // There is deliberately NO `Verify` phase. A verify is not a state the chip sits in
    // -- it is what HeadSettle hands off to, and it completes within the one call. The
    // 10 ms wait in front of it is the only part that takes time, and that is HeadSettle.
    enum class Phase {
        Idle,
        Settle,      // stepping: one step per rate interval
        HeadSettle,  // the 10 ms HLD->HLT delay, when E (or h) asked for it
        Read,        // streaming a data field (or an ID field, or a track) out to the guest
        WriteWait,   // Write: the FIRST DRQ. If the guest misses it, the command dies.
        Write,       // ...and the rest of them, where a miss writes a zero and carries on
    };

    // What to do when the byte stream in `buf_` runs out. Read Sector, Read Address and
    // Read Track all STREAM IDENTICALLY -- a byte every byteTStates(), DRQ each time,
    // Lost Data if the guest was late -- and differ only in where the bytes came from
    // and what happens at the end. One streamer, three endings.
    enum class Ending { Plain, NextRecord, CommitSector, CommitTrack };

    // The bits. Named, because `1 << 4` in six different meanings is how the above
    // goes wrong quietly.
    static constexpr uint8_t kBusy      = 0x01;  // S0, every context
    static constexpr uint8_t kIndex     = 0x02;  // S1, Type I
    static constexpr uint8_t kDrq       = 0x02;  // S1, everything else
    static constexpr uint8_t kTrack0    = 0x04;  // S2, Type I
    static constexpr uint8_t kLostData  = 0x04;  // S2, everything else
    static constexpr uint8_t kCrcError  = 0x08;  // S3
    static constexpr uint8_t kSeekError = 0x10;  // S4, Type I
    static constexpr uint8_t kNotFound  = 0x10;  // S4, everything else
    static constexpr uint8_t kHeadLoad  = 0x20;  // S5, Type I
    static constexpr uint8_t kWriteFault= 0x20;  // S5, Write
    static constexpr uint8_t kRecType5  = 0x20;  // S5, Read -- LSB of the record type
    static constexpr uint8_t kProtected = 0x40;  // S6, Type I and Write
    static constexpr uint8_t kRecType6  = 0x40;  // S6, Read -- MSB of the record type
    static constexpr uint8_t kNotReady  = 0x80;  // S7, every context

    // Timing, all of it derived and none of it invented.
    uint64_t stepTStates(const Clock& clk) const;        // Table 1, via fdcClockHz and r1r0
    uint64_t byteTStates(const Clock& clk) const;        // dataRateBits / 8
    uint64_t headSettleTStates(const Clock& clk) const;  // 10 ms, or 20 at CLK=1 MHz
    uint64_t msTStates(const Clock& clk, int ms) const;

    void loadHead(bool on, const Clock& clk);  // drive HLD, and remember when
    bool headEngaged(const Clock& clk) const;  // HLD *and* HLT -- what S5 actually reports

    void startCommand(uint8_t cmd, const Clock& clk);
    void startTypeI(uint8_t cmd, const Clock& clk);
    void startTypeII(const Clock& clk);
    void startTypeIII(const Clock& clk);
    void forceInterrupt(uint8_t cmd, const Clock& clk);

    void headLoadThen(bool wanted, const Clock& clk);
    void afterHeadSettle(const Clock& clk);  // where HeadSettle hands off to
    void beginTypeII(const Clock& clk);      // find the ID field, then read or write
    void doVerify(const Clock& clk);
    void commitSector(const Clock& clk);
    void commitTrack(const Clock& clk);
    void finish(const Clock& clk);  // INTRQ, drop BUSY, go Idle

    bool findSector(FloppyDrive::SectorId& out);  // an ID matching TR + SR
    void stepOnce();
    bool ready() const { return drive_ && drive_->ready(); }

    std::string  name_;
    FloppyDrive* drive_ = nullptr;

    // The register file.
    uint8_t command_ = 0;
    uint8_t status_  = 0;
    uint8_t track_   = 0;
    uint8_t sector_  = 0;
    uint8_t data_    = 0;

    Ctx   ctx_   = Ctx::TypeI;
    Phase phase_ = Phase::Idle;

    bool intrq_ = false;
    bool drq_   = false;

    // Where the state machine is going next, and when.
    uint64_t due_ = 0;

    // ---- THE STEPPING DIRECTION IS A LATCH ON THE REAL CHIP (the DIRC pin) ----
    // A bare Step command (not Step In / Step Out) repeats the LAST direction, so it
    // has to be remembered across commands. Default inward: after a Restore the head
    // is at track 0 and the only way it can go is in.
    bool dirIn_ = true;

    int stepsLeft_ = 0;  // Restore counts down from 255 -- past that it is a Seek Error

    // ---- HLD AND HLT ARE TWO DIFFERENT PINS, AND S5 IS THE *AND* OF THEM ----
    //
    // "S5 HEAD LOADED: when set, it indicates the head is loaded and engaged. This bit is
    // a logical 'and' of HLD and HLT signals."  HLD (pin 28) is the chip's output -- it
    // asks for the head. HLT (pin 23) is the drive's answer, and it is "sampled after
    // 10 msec". So there is a window, right after a head load, in which the head is
    // COMMANDED DOWN BUT NOT YET ENGAGED, and S5 reports 0 for the whole of it.
    //
    // A driver that waits on S5 to know the head has settled is reading a bit that must
    // be able to be FALSE, or it waits on nothing. So we keep the output pin (headLoaded_
    // = HLD) and the MOMENT IT WENT ACTIVE (hldAt_), and derive HLT from the clock -- no
    // extra phase, no extra deadline, and it stays right even if nobody polls us.
    //
    // ...and the settle is 10 ms at 2 MHz and TWENTY at 1 MHz ("these times doubled when
    // CLK = 1 MHz"), which is why it goes through headSettleTStates() and not a literal.
    bool     headLoaded_ = false;  // HLD, the output pin
    uint64_t hldAt_      = 0;      // ...and when it last went active, for HLT

    // WE DO NOT MODEL THE TWO-REVOLUTION AUTO-UNLOAD ("if the FD1771 does not receive
    // any commands after two revolutions of the disk, the head will be automatically
    // disengaged"). It is a power-saving detail with exactly one observable: S5 goes
    // low a third of a second after the drive goes quiet, and nothing reads S5 to find
    // that out. Modeling it would mean giving the chip a rotational clock it otherwise
    // has no use for. Said out loud rather than left as a silent divergence.

    // The transfer in flight.
    std::vector<uint8_t>  buf_;
    size_t                idx_  = 0;
    bool                  lost_ = false;
    Ending                end_  = Ending::Plain;
    FloppyDrive::SectorId id_{};

    // ---- FORCE INTERRUPT'S ARMED CONDITIONS (I0, I1, I2) ----
    // I3 is immediate and needs no state. The other three arm the chip to interrupt on
    // something that has not happened yet -- a drive coming ready, going not-ready, or
    // an index pulse -- and stay armed until the next command. Which means the pins
    // have to be SAMPLED for edges, and that is what the prev* pair is for.
    uint8_t fiConds_  = 0;
    bool    prevReady_ = false;
    bool    prevIndex_ = false;

    // Read Address returns "the next encountered ID field", which is a question about
    // where the disk has got to. WE DO NOT MODEL ANGULAR POSITION -- so successive Read
    // Addresses walk the track in rotational order and wrap, which is what a real one
    // does and what every formatter that uses the command actually depends on. It does
    // not pretend to tell you WHICH sector is under the head at T=now, because nothing
    // in this program knows that.
    int raCursor_ = 0;

    std::vector<std::string> log_;
};

} // namespace altair
