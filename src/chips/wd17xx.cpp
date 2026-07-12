#include "chips/wd17xx.h"

namespace altair {

namespace {

// ---------------------------------------------------------------------------
// THE COMMAND DECODE IS BITS 7-4, AND NOTHING ELSE (Table 2).
//
//      0 0 0 0 h V r1 r0    Restore
//      0 0 0 1 h V r1 r0    Seek
//      0 0 1 u h V r1 r0    Step
//      0 1 0 u h V r1 r0    Step In
//      0 1 1 u h V r1 r0    Step Out
//      1 0 0 m b E  0  0    Read Sector
//      1 0 1 m b E a1 a0    Write Sector
//      1 1 0 0 0 E  0  0    Read Address
//      1 1 0 1 I3 I2 I1 I0  Force Interrupt
//      1 1 1 0 0 1  0  s    Read Track
//      1 1 1 1 0 1  0  0    Write Track
//
// Note that Step/Step In/Step Out each swallow TWO of the sixteen top-nibble values,
// because their `u` flag is bit 4. Decoding on the nibble alone is therefore right
// for exactly the reason it looks wrong.
// ---------------------------------------------------------------------------
enum : uint8_t {
    kRestore    = 0x0,
    kSeek       = 0x1,
    kStep       = 0x2,  // and 0x3
    kStepIn     = 0x4,  // and 0x5
    kStepOut    = 0x6,  // and 0x7
    kReadSec    = 0x8,  // and 0x9
    kWriteSec   = 0xA,  // and 0xB
    kReadAddr   = 0xC,
    kForceInt   = 0xD,
    kReadTrack  = 0xE,
    kWriteTrack = 0xF,
};

// The flags, by the name the data sheet gives them (Tables 3-5).
inline bool flagU(uint8_t c) { return c & 0x10; }  // Type I: update the track register
inline bool flagH(uint8_t c) { return c & 0x08; }  // Type I: load the head at the start
inline bool flagV(uint8_t c) { return c & 0x04; }  // Type I: verify the destination track
inline bool flagM(uint8_t c) { return c & 0x10; }  // Type II: multiple records
inline bool flagE(uint8_t c) { return c & 0x04; }  // Type II/III: HLD + the 10 ms delay

// CRC-16/CCITT over the ID field, which is what the FD1771 puts on the disk and what
// Read Address hands back to the guest: poly x^16 + x^12 + x^5 + 1, preset to ones,
// computed across the address mark and the four ID bytes (IBM 3740).
//
// THIS IS COMPUTED, NOT INVENTED. Read Address transfers the two CRC bytes to the
// computer, so they have to be SOMETHING -- and the only defensible something is the
// CRC of the ID field we just handed over, which is exactly what a real chip would
// have read off a correctly-written disk. Emitting zeroes would be a made-up number
// that a formatter checking its own work would reject.
uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

} // namespace

// ---------------------------------------------------------------------------
// TIMING. Every number below comes off the data sheet or out of the geometry; not
// one of them is a guess, and none of them is a magic constant in a header.
// ---------------------------------------------------------------------------

uint64_t Wd1771::msTStates(const Clock& clk, int ms) const {
    return (uint64_t)(clk.hz() * (long long)ms / 1000);
}

// Table 1, STEPPING RATES. The FD1771's own crystal picks the column -- NOT the CPU's,
// which is why fdcClockHz is a strap and not clk.hz(). At 2 MHz (the Tarbell, which
// halves a 4 MHz can) the rates are 6/6/10/20 ms; at 1 MHz they are 12/12/20/40.
//
// THE 00 AND 01 ROWS ARE BOTH 6 ms. That is not a typo in this table and it is not a
// typo in the data sheet -- the FD1771 really does give you the same rate for two
// different codes. The WD1770 does not (6/12/20/30), and copying that chip's table
// into this one is the single most likely way to end up with a controller that seeks
// at the wrong speed while looking entirely correct. See wd17xx.h.
uint64_t Wd1771::stepTStates(const Clock& clk) const {
    static const int k2MHz[4] = {6, 6, 10, 20};
    static const int k1MHz[4] = {12, 12, 20, 40};
    const int* tbl = (fdcClockHz >= 2000000) ? k2MHz : k1MHz;
    return msTStates(clk, tbl[command_ & 0x03]);
}

// One byte time on the media. 250,000 bits/s is 31,250 bytes/s is 32 us a byte, which
// at a 2 MHz CPU is 64 T-states between DRQs. THIS is what makes Lost Data reachable:
// a guest that takes longer than 64 T-states to service a DRQ really does lose the
// byte, on the bench and here.
uint64_t Wd1771::byteTStates(const Clock& clk) const {
    uint64_t t = clk.tStatesPer(dataRateBits / 8);
    return t ? t : 1;  // never zero: a zero-length deadline is an infinite loop
}

// The HLD->HLT settle. TEN ms at 2 MHz -- and TWENTY at 1 MHz: "these times doubled when
// CLK = 1 MHz", printed under both the Type I and the Type II flow charts. The 1 MHz
// option is real (it is what a mini-floppy runs at), and a chip that switched its step
// table to the 1 MHz column while leaving the settle at 10 ms would be half converted,
// which is worse than being uniformly wrong.
uint64_t Wd1771::headSettleTStates(const Clock& clk) const {
    return msTStates(clk, fdcClockHz >= 2000000 ? 10 : 20);
}

// HLD, pin 28. Remember WHEN, because HLT is 10 ms after it -- see headEngaged().
void Wd1771::loadHead(bool on, const Clock& clk) {
    if (on && !headLoaded_) hldAt_ = clk.now();
    headLoaded_ = on;
}

// S5, and it is the AND of two pins. The head is not engaged the instant the chip asks
// for it; the drive takes 10 ms to get it there, and HLT is what says it arrived.
bool Wd1771::headEngaged(const Clock& clk) const {
    return headLoaded_ && clk.now() >= hldAt_ + headSettleTStates(clk);
}

// ---------------------------------------------------------------------------
// RESET
// ---------------------------------------------------------------------------

void Wd1771::powerOn(const Clock& clk) {
    command_ = 0;
    status_  = 0;
    track_   = 0;
    sector_  = 0;
    data_    = 0;
    ctx_     = Ctx::TypeI;
    phase_   = Phase::Idle;
    intrq_   = false;
    drq_     = false;
    due_     = 0;
    dirIn_   = true;
    stepsLeft_  = 0;
    headLoaded_ = false;
    hldAt_      = 0;
    lost_       = false;
    fiConds_    = 0;
    raCursor_   = 0;
    buf_.clear();
    idx_ = 0;
    prevReady_ = ready();
    prevIndex_ = drive_ && drive_->index();
    (void)clk;
}

// MR, pin 19 -- and it does a great deal more than clear the registers. See the header:
// the rising edge EXECUTES A RESTORE, "regardless of the state of the Ready signal from
// the drive". A front-panel RESET on a Tarbell drives the head home with no software
// involved at all, and a model that merely idled the chip would leave the head wherever
// it was and read the wrong track on the very first access after a reset.
// A MASTER RESET IS NOT A POWER CYCLE, and the difference is which registers survive.
//
// The data sheet's entire account of MR is: it resets the device, it loads 0x03 into the
// COMMAND register, it forces Not Ready while held, and on release it runs a Restore.
// That is the list. It does NOT say the Sector or Data registers are cleared -- and they
// are not. (The TRACK register ends up zero, but by way of the RESTORE, not the reset:
// the head goes home and the track register follows it there.)
//
// So this does not call powerOn(). Zeroing SR and DR here would be inventing an effect the
// pin does not have, and it would hide the bug class where a driver reasonably expects
// them to survive a front-panel RESET.
void Wd1771::masterReset(const Clock& clk) {
    status_ = 0;
    ctx_    = Ctx::TypeI;
    phase_  = Phase::Idle;
    intrq_  = false;
    drq_    = false;
    due_    = 0;
    lost_   = false;
    fiConds_    = 0;
    raCursor_   = 0;
    headLoaded_ = false;
    hldAt_      = 0;
    buf_.clear();
    idx_ = 0;
    id_  = {};

    command_ = 0x03;  // the data sheet's number: Restore, h=0, V=0, r1r0=11 (the slowest step)
    startCommand(command_, clk);
}

// ---------------------------------------------------------------------------
// THE REGISTER FILE
// ---------------------------------------------------------------------------

// Reading status resets INTRQ. So does loading the command register -- and NOTHING
// ELSE does, which is the whole reason a driver that only ever polls DRQ leaves the
// interrupt line stuck.
uint8_t Wd1771::readStatus(const Clock& clk) {
    poll(clk);
    intrq_ = false;

    uint8_t s = status_;  // the LATCHED bits: S6..S3, whatever they mean in this context

    if (!ready()) s |= kNotReady;  // S7 is live in every column of Table 6
    if (phase_ != Phase::Idle) s |= kBusy;

    if (ctx_ == Ctx::TypeI) {
        // S6, S5, S2 and S1 are inverted copies of PINS on a Type I -- they are not
        // latched and they are not remembered. They are what the drive is saying now.
        if (drive_ && drive_->writeProtected()) s |= kProtected;
        if (headEngaged(clk)) s |= kHeadLoad;  // HLD *and* HLT -- not merely "we asked"
        if (drive_ && drive_->trackZero()) s |= kTrack0;
        if (drive_ && drive_->index()) s |= kIndex;
    } else {
        // ...and on everything else those same two bit positions are Lost Data and DRQ.
        if (lost_) s |= kLostData;
        if (drq_) s |= kDrq;
    }
    return s;
}

uint8_t Wd1771::readData(const Clock& clk) {
    poll(clk);

    // DRQ IS CLEARED BY *SERVICING* IT, AND A WRITE IS NOT SERVICED BY A READ.
    //
    // Pin 38: "This signal is reset when serviced by the computer through READING OR
    // LOADING the DR in Read or Write operation, RESPECTIVELY." Reading clears it in a
    // read; LOADING clears it in a write. So a guest that reads the data register in the
    // middle of a write has not answered the request -- and if we cleared DRQ here anyway,
    // the streamer would see it clear at the deadline, skip the Lost Data branch, and
    // quietly recover a byte the real chip would have lost. Lost Data would become
    // unreachable down that path.
    if (phase_ != Phase::WriteWait && phase_ != Phase::Write) drq_ = false;
    return data_;
}

void Wd1771::writeData(uint8_t v, const Clock& clk) {
    poll(clk);
    data_ = v;
    if (phase_ == Phase::WriteWait || phase_ == Phase::Write) {
        // The guest made the deadline. Take the byte and drop the request; the streamer
        // will notice DRQ is clear when it next comes round and will not write a zero.
        // id_.size is the byte budget for BOTH a sector and a whole track -- the track
        // case sets it from trackImageBytes() -- so one bound serves both.
        if (buf_.size() < (size_t)id_.size) buf_.push_back(v);
        drq_ = false;
    }
}

void Wd1771::writeCommand(uint8_t v, const Clock& clk) {
    poll(clk);
    intrq_ = false;  // loading the command register resets INTRQ
    startCommand(v, clk);
}

// ---------------------------------------------------------------------------
// STARTING A COMMAND
// ---------------------------------------------------------------------------

void Wd1771::startCommand(uint8_t cmd, const Clock& clk) {
    const uint8_t op = (uint8_t)(cmd >> 4);

    // Force Interrupt is the one command that may be loaded while BUSY, and it is the
    // one command that does NOT clear the status bits. It gets out before any of the
    // "update or clear the rest of the status bits" machinery below.
    if (op == kForceInt) {
        forceInterrupt(cmd, clk);
        return;
    }

    // "Command words should only be loaded in the Command Register when the Busy status
    // bit is off." The data sheet says SHOULD, and it does not say what happens if you
    // do it anyway -- the real chip has no interlock, and the new command simply lands
    // on top of the old one. We do the same: no guard, no complaint. Inventing a refusal
    // here would make this chip stricter than the silicon and would break exactly the
    // sloppy driver that works fine on real hardware.
    command_ = cmd;
    fiConds_ = 0;  // any new command disarms Force Interrupt's conditions

    // "Upon receipt of any command, except the Force Interrupt command, the Busy Status
    // bit is set and the rest of the status bits are updated or cleared for the new
    // command."
    status_ = 0;
    lost_   = false;
    drq_    = false;
    buf_.clear();
    idx_ = 0;
    id_  = {};  // ...and the record the LAST command found is not this command's record

    // The +1s are the `u` and `m` flags eating a second nibble value apiece -- Step Out is
    // 0x6 AND 0x7, Write Sector is 0xA AND 0xB. Off by one here and a Step Out that
    // updates the track register gets dispatched as a Type II command. See startTypeI().
    if (op <= kStepOut + 1) {
        startTypeI(cmd, clk);
    } else if (op <= kWriteSec + 1) {
        ctx_ = (op <= 0x9) ? Ctx::Read : Ctx::Write;
        startTypeII(clk);
    } else {
        ctx_ = (op == kReadAddr)   ? Ctx::ReadAddress
             : (op == kReadTrack)  ? Ctx::ReadTrack
                                   : Ctx::WriteTrack;
        startTypeIII(clk);
    }
}

// ---- TYPE I: Restore, Seek, Step, Step In, Step Out ------------------------
//
// These run REGARDLESS OF READY -- "The Seek or Step commands are performed regardless
// of the state of the READY input." A drive with no disk in it still steps; that is how
// a Restore can home a head before anyone has inserted anything.
void Wd1771::startTypeI(uint8_t cmd, const Clock& clk) {
    ctx_ = Ctx::TypeI;
    const uint8_t op = (uint8_t)(cmd >> 4);

    // h: "If h=1, the head is loaded at the beginning of the command (HLD output is made
    // active). IF h=0, HLD IS DEACTIVATED." The Type I flow chart spells the second half
    // out as its own box -- `IS H = 1?` -> NO -> `RESET HLD`. There is an ELSE here, and
    // without it headLoaded_ latches high on the first h=1 command and S5 reports HEAD
    // ENGAGED for the rest of the program's life -- including through the Restore that a
    // master reset issues, which is an h=0 command whose entire job is to let go.
    loadHead(flagH(cmd), clk);

    // ---- STEP, STEP IN AND STEP OUT EACH OWN *TWO* TOP-NIBBLE VALUES ----
    //
    // Their `u` flag is BIT 4, which is inside the nibble the opcode lives in -- so Step
    // is 0x2 AND 0x3, Step In is 0x4 AND 0x5, Step Out is 0x6 AND 0x7. A switch that
    // lists only the even one compiles, runs, and silently does NOTHING on every Step
    // command that asked to update the track register -- which is most of them, and which
    // is precisely what a CP/M BIOS issues. (This is not hypothetical. It is the bug that
    // was sitting here until the step-rate test caught it.)
    switch (op) {
        case kRestore:
            // Step OUT until TR00, up to 255 times, then give up with a Seek Error.
            if (drive_ && drive_->trackZero()) {
                track_     = 0;
                stepsLeft_ = 0;
            } else {
                dirIn_     = false;
                stepsLeft_ = 255;
            }
            break;

        case kSeek:
            // THE TARGET IS IN THE DATA REGISTER, and the Track Register says where we
            // are. The chip steps until they agree -- it does not teleport, because the
            // head does not teleport, and a driver that has lied to the Track Register
            // gets to find that out from the verify.
            dirIn_     = data_ > track_;
            stepsLeft_ = (data_ > track_) ? (data_ - track_) : (track_ - data_);
            break;

        case kStep:
        case kStep + 1:
            stepsLeft_ = 1;  // ...in whatever direction the DIRC latch is already holding
            break;
        case kStepIn:
        case kStepIn + 1:
            dirIn_     = true;
            stepsLeft_ = 1;
            break;
        case kStepOut:
        case kStepOut + 1:
            dirIn_     = false;
            stepsLeft_ = 1;
            break;
        default:
            break;
    }

    if (stepsLeft_ == 0) {
        // Nowhere to go. Still verify IF ASKED -- a Seek to the track you are already on
        // is the cheapest way a driver has of asking "am I really where I think I am?" --
        // but if V is clear there is nothing left to do and the command is simply over.
        if (flagV(cmd)) headLoadThen(true, clk);
        else finish(clk);
        return;
    }
    phase_ = Phase::Settle;
    due_   = clk.now() + stepTStates(clk);
}

// ---- TYPE II: Read Sector, Write Sector ------------------------------------
void Wd1771::startTypeII(const Clock& clk) {
    // "Whenever a Read or Write command is received the FD1771 samples the READY input.
    // If this input is logic low the command is not executed and an interrupt is
    // generated." Not an error bit -- the command simply does not happen. S7 reports it.
    if (!ready()) {
        finish(clk);
        return;
    }

    // "Writing is inhibited when the Write Protect input is a logic low, in which case
    // any Write command is immediately terminated, an interrupt is generated and the
    // Write Protect status bit is set." Immediately -- before the head goes anywhere.
    if (ctx_ == Ctx::Write && drive_->writeProtected()) {
        status_ |= kProtected;
        finish(clk);
        return;
    }

    headLoadThen(flagE(command_), clk);
}

// ---- TYPE III: Read Address, Read Track, Write Track -----------------------
void Wd1771::startTypeIII(const Clock& clk) {
    if (!ready()) {
        finish(clk);
        return;
    }
    // WPRT *and* DINT, and they land on the SAME status bit. "[DINT] is sampled whenever a
    // Write Track command is received. If DINT=0, the operation is terminated and the
    // Write Protect status bit is set." The flow chart tests them as one condition
    // (`WPRT = 0 - DINT = 0?`) -- two separate interlocks on formatting, one shared bit.
    if (ctx_ == Ctx::WriteTrack && (drive_->writeProtected() || !drive_->diskInit())) {
        status_ |= kProtected;
        finish(clk);
        return;
    }
    // Read Track and Write Track have no E flag -- bit 2 is a hardwired 1 in both (Table
    // 2), so the head always loads. Read Address does have one.
    const bool e = (ctx_ == Ctx::ReadAddress) ? flagE(command_) : true;
    headLoadThen(e, clk);
}

// The 10 ms HLD->HLT settle, or straight through if the command said the head was
// already engaged (E=0: "Head is assumed Engaged and there is no 10 msec Delay").
//
// Where it hands off to is NOT a parameter: afterHeadSettle() asks ctx_, which the
// command already set. Passing a "next phase" as well would be a second, redundant
// source of truth for what the chip is doing -- and the two would eventually disagree.
void Wd1771::headLoadThen(bool wanted, const Clock& clk) {
    if (wanted) {
        loadHead(true, clk);
        phase_ = Phase::HeadSettle;
        due_   = clk.now() + headSettleTStates(clk);
        return;
    }
    // E=0: "Head is assumed Engaged and there is no 10 msec Delay." The head is ALREADY
    // down as far as this command is concerned -- so HLD stays wherever it was and HLT is
    // taken as true. Backdating hldAt_ is how we say "and it settled long ago" without
    // inventing a second flag that means the same thing.
    headLoaded_ = true;
    hldAt_      = 0;
    afterHeadSettle(clk);
}

// Where the 10 ms lands. The command register is still holding the command, so this can
// simply ask it again rather than carrying a "what was I doing" flag around.
void Wd1771::afterHeadSettle(const Clock& clk) {
    switch (ctx_) {
        case Ctx::TypeI:
            doVerify(clk);
            return;
        case Ctx::Read:
        case Ctx::Write:
            beginTypeII(clk);
            return;

        case Ctx::ReadAddress: {
            // "The next encountered ID field is then read in from the disk, and the six
            // data bytes of the ID field are assembled and transferred to the DR."
            //
            //     1 TRACK   2 ZEROS   3 SECTOR   4 SECTOR LENGTH   5 CRC 1   6 CRC 2
            //
            // Byte 2 is a hard zero on this chip: the 1771 is single-sided and has no
            // side-select pin, so the byte the 179x uses for a side number has nothing to
            // put in it. That is a fact about the silicon, not a placeholder.
            if (!drive_ || drive_->sectorCount() == 0) {
                status_ |= kNotFound;  // S4 is ID NOT FOUND in this column
                finish(clk);
                return;
            }
            FloppyDrive::SectorId id{};
            const int n = drive_->sectorCount();
            drive_->sectorIdAt(raCursor_ % n, id);
            raCursor_ = (raCursor_ + 1) % n;

            uint8_t f[5] = {0xFE, (uint8_t)id.track, 0x00, (uint8_t)id.sector,
                            (uint8_t)id.lengthCode};
            const uint16_t crc = crc16(f, sizeof f);

            buf_ = {f[1], f[2], f[3], f[4], (uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF)};

            // "Although the CRC characters are transferred to the computer, the FD1771
            // checks for validity and the CRC error status bit is set if there is a CRC
            // error."
            if (!id.idCrcOk) status_ |= kCrcError;

            // "The Sector Address of the ID field is written into the sector register."
            // A side effect, and one a formatter leans on -- Read Address is how you find
            // out what is actually on an unknown disk.
            sector_ = (uint8_t)id.sector;

            idx_   = 0;
            end_   = Ending::Plain;
            phase_ = Phase::Read;
            due_   = clk.now() + byteTStates(clk);
            return;
        }

        case Ctx::ReadTrack: {
            if (!drive_ || !drive_->readTrackImage(buf_) || buf_.empty()) {
                // Table 6's READ TRACK column has NO error bits at all -- S6..S3 are
                // hardwired zero. There is literally nowhere in the status register to
                // report this, so the chip says it in words and completes. Silence would
                // hand the guest a track of nothing and let it decide the disk was blank.
                log_.push_back(name_ + ": read track: this drive has no bit-level track "
                                       "image (a raw sector image has no gaps or address "
                                       "marks in it)");
                finish(clk);
                return;
            }
            idx_   = 0;
            end_   = Ending::Plain;
            phase_ = Phase::Read;
            due_   = clk.now() + byteTStates(clk);
            return;
        }

        case Ctx::WriteTrack: {
            const int n = drive_ ? drive_->trackImageBytes() : 0;
            if (n <= 0) {
                // ...but the WRITE TRACK column DOES have a bit for it, and it is exactly
                // the right one: the drive could not take the write. S5, WRITE FAULT.
                status_ |= kWriteFault;
                log_.push_back(name_ + ": write track: this drive cannot be formatted (a "
                                       "raw sector image has nowhere to put the gaps and "
                                       "address marks a format writes)");
                finish(clk);
                return;
            }
            // THE FIRST BYTE OF A FORMAT GETS A WHOLE REVOLUTION, NOT A BYTE TIME.
            //
            // "The Data Request is activated immediately upon receiving the command, but
            // writing will not start until after the first byte has been loaded into the
            // Data Register. IF THE DR HAS NOT BEEN LOADED BY THE TIME THE INDEX PULSE IS
            // ENCOUNTERED the operation is terminated ... and the Lost Data status bit is
            // set." Write Track starts AT the index hole, so the guest's budget for that
            // first byte is up to one full revolution -- which is exactly `n` byte times,
            // because `n` bytes IS what one revolution holds. Nothing invented.
            id_.size = n;  // the streamer's byte budget, and the revolution's length
            buf_.clear();
            buf_.reserve((size_t)n);
            end_    = Ending::CommitTrack;
            phase_  = Phase::WriteWait;
            drq_    = true;
            due_    = clk.now() + (uint64_t)n * byteTStates(clk);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// THE VERIFY -- and the only place Seek Error can come from.
//
// "If the track numbers compare and the ID Field Cyclic Redundancy Check (CRC) is
// correct, the verify operation is complete. If track comparison is not made but the CRC
// checks, an interrupt is generated, the Seek Error status (Bit 4) is set."
//
// The comparison is TRACK REGISTER against the track number RECORDED IN THE ID FIELD --
// not against where the head physically is. Those are different questions, and this one
// is the one that catches a drive that missed a step.
// ---------------------------------------------------------------------------
void Wd1771::doVerify(const Clock& clk) {
    if (!drive_) {
        status_ |= kSeekError;
        finish(clk);
        return;
    }
    // THE *FIRST* GOOD ID FIELD DECIDES. It is not a search for a matching one.
    //
    // "The FIRST ENCOUNTERED ID field is read off the disk. The track address of the ID
    // field is then compared to the Track Register; if there is a match and a valid ID
    // CRC, the verification is complete. IF THERE IS NOT A MATCH BUT THERE IS VALID ID
    // CRC, an interrupt is generated, the Seek Error status bit is set." The flow chart
    // agrees: a good-CRC ID whose track disagrees falls straight through to Seek Error --
    // it does NOT go round the track looking for a friendlier one. Only a ROTTEN ID field
    // (bad CRC) causes the chip to read the next one.
    //
    // Scan-for-any-match is the tempting version and it is wrong: on a track whose ID
    // fields disagree with each other -- which is the only kind of track that makes Seek
    // Error reachable in the first place -- it verifies happily where the chip would fault.
    const int n = drive_->sectorCount();
    bool crcSeen = false;
    for (int i = 0; i < n; ++i) {
        FloppyDrive::SectorId id{};
        if (!drive_->sectorIdAt(i, id)) continue;
        if (!id.idCrcOk) {
            crcSeen = true;
            continue;  // a rotten ID field is not an answer; read the next one
        }
        if (id.track == (int)track_) {
            finish(clk);  // verified
        } else {
            status_ |= kSeekError;  // ...and this one is the answer, even though it is no
            finish(clk);
        }
        return;
    }
    // Nothing legible on the whole track.
    if (crcSeen) status_ |= kCrcError;
    status_ |= kSeekError;
    finish(clk);
}

// ---------------------------------------------------------------------------
// TYPE II: find the record, then stream it
// ---------------------------------------------------------------------------

// "The FD1771 must find an ID field with a track number, sector number, and CRC within
// two revolutions of the disk; otherwise, the Record Not Found status bit is set."
bool Wd1771::findSector(FloppyDrive::SectorId& out) {
    if (!drive_) return false;
    const int n = drive_->sectorCount();
    for (int i = 0; i < n; ++i) {
        FloppyDrive::SectorId id{};
        if (!drive_->sectorIdAt(i, id)) continue;
        if (!id.idCrcOk) {
            status_ |= kCrcError;  // S3: "an error is found in one or more ID fields"
            continue;
        }
        if (id.track == (int)track_ && id.sector == (int)sector_) {
            out = id;
            return true;
        }
    }
    return false;
}

void Wd1771::beginTypeII(const Clock& clk) {
    FloppyDrive::SectorId id{};
    // A zero-length data field is not a sector -- it is a drive describing an ID field it
    // cannot back with any bytes, and there is nothing to stream either way. Same bit as
    // never finding it, because from the guest's side it is the same disk.
    if (!findSector(id) || id.size <= 0) {
        status_ |= kNotFound;
        finish(clk);
        return;
    }
    id_ = id;

    if (ctx_ == Ctx::Read) {
        buf_.assign((size_t)id.size, 0);
        size_t n = buf_.size();
        if (!drive_->readData(id, buf_.data(), &n) || n != (size_t)id.size) {
            status_ |= kNotFound;
            finish(clk);
            return;
        }
        idx_   = 0;
        end_   = flagM(command_) ? Ending::NextRecord : Ending::Plain;
        phase_ = Phase::Read;
        due_   = clk.now() + byteTStates(clk);
        return;
    }

    // ---- THE a1a0 FIELD: *WHICH* DATA ADDRESS MARK TO WRITE (Table 4) ----
    //
    //     a1a0 = 00 -> FB (Data Mark)      01 -> FA (user defined)
    //     a1a0 = 10 -> F9 (user defined)   11 -> F8 (DELETED Data Mark)
    //
    // "the Data Address Mark is then written on the disk as determined by the a1a0 field
    // OF THE COMMAND." The mark we write is the one the GUEST ASKED FOR -- not the one
    // that happens to be on the disk already, which is what findSector() handed back.
    // Miss this and `Write Sector` with a1a0=11 (how a CP/M-era utility marks a sector
    // deleted) silently writes a normal record -- and worse, a plain write over an
    // already-deleted sector would PRESERVE the deleted mark it was meant to clear.
    id_.deleted = (command_ & 0x03) == 0x03;

    // The first DRQ is special: "the FD1771 COUNTS OFF 11 BYTES from the CRC field and the
    // Write Gate output is made active IF the DRQ is serviced. If DRQ has not been
    // serviced, the command is terminated and the Lost Data status bit is set." Nothing is
    // written at all in that case -- a very deliberate piece of hardware design (the chip
    // will not open the write gate on a driver that is not keeping up), and the reason
    // WriteWait is its own phase.
    //
    // ELEVEN byte times, not one. That is the guest's whole budget to get the first byte
    // in, and it is the one DRQ whose miss is FATAL -- so a one-byte window here would
    // manufacture Lost Data terminations for polled drivers that are perfectly happy on
    // real hardware.
    buf_.clear();
    buf_.reserve((size_t)id.size);
    // Always CommitSector, multi-record or not: a write has to REACH THE DISK before it
    // can move to the next record, so the "then what" is commitSector()'s decision and
    // not the streamer's. (A read has no such ordering, which is why it uses NextRecord.)
    end_   = Ending::CommitSector;
    phase_ = Phase::WriteWait;
    drq_   = true;
    due_   = clk.now() + 11 * byteTStates(clk);
}

void Wd1771::commitSector(const Clock& clk) {
    // WF, pin 33: "When WG=1 and WF goes low, the current Write command is terminated and
    // the Write Fault status bit is set." The drive's own electronics saying the write did
    // not happen -- which is a different thing from the medium refusing it, and it is the
    // other way S5 can light up.
    if (drive_->writeFault() || !drive_->writeData(id_, buf_.data(), buf_.size())) {
        status_ |= kWriteFault;  // S5: the drive would not take it
        finish(clk);
        return;
    }
    // Multiple records: "the sector register [is] internally updated so that an address
    // verification can occur on the next record ... until the sector register exceeds the
    // number of sectors on the track."
    if (flagM(command_)) {
        ++sector_;
        FloppyDrive::SectorId next{};
        if (findSector(next)) {
            id_         = next;
            id_.deleted = (command_ & 0x03) == 0x03;
            buf_.clear();
            buf_.reserve((size_t)next.size);
            phase_ = Phase::WriteWait;
            drq_   = true;
            due_   = clk.now() + 11 * byteTStates(clk);
            return;
        }
        // ...AND THAT EXIT IS *RECORD NOT FOUND*. See the note in the read path below --
        // the incremented sector register goes back into the ID search, and an ID search
        // that runs out of disk is an ID search that failed.
        status_ |= kNotFound;
    }
    finish(clk);
}

void Wd1771::commitTrack(const Clock& clk) {
    if (!drive_->writeTrackImage(buf_)) status_ |= kWriteFault;
    finish(clk);
}

// ---------------------------------------------------------------------------
// FORCE INTERRUPT -- the whole of Table 6's footnote, in one function.
// ---------------------------------------------------------------------------
void Wd1771::forceInterrupt(uint8_t cmd, const Clock& clk) {
    const bool wasBusy = (phase_ != Phase::Idle);

    command_ = cmd;
    phase_   = Phase::Idle;
    due_     = 0;

    if (wasBusy) {
        // "If the Force Interrupt Command is received when there is a current command
        // under execution, the Busy status bit is reset, and THE REST OF THE STATUS BITS
        // ARE UNCHANGED." So status_, lost_, ctx_ -- and drq_ -- are all left exactly as
        // they are. A driver that aborts a read and then reads status is still looking at
        // the read's status, and that is the whole point of aborting.
        //
        // DRQ IS ONE OF THOSE BITS: S1 is DRQ in every non-Type-I column of Table 6. It is
        // tempting to clear it here on the grounds that the transfer is over -- but "the
        // rest of the status bits are unchanged" does not have an exception for the one we
        // find untidy. The next command clears it (startCommand does), which is where the
        // real chip clears it too.
    } else {
        // "If the Force Interrupt command is received when there is not a current command
        // under execution, the Busy Status bit is reset and the rest of the status bits
        // are updated or cleared. In this case, STATUS REFLECTS THE TYPE I COMMANDS."
        status_ = 0;
        lost_   = false;
        ctx_    = Ctx::TypeI;
    }

    // ---- I3, AND A PLACE WHERE THE DATA SHEET CONTRADICTS ITSELF ----
    //
    // The general rule (p5) is that INTRQ "remains active until reset by reading the Status
    // Register to the processor or by the loading of the Command Register". But the Type IV
    // note says of the immediate interrupt: "This is the ONLY command that will clear the
    // immediate interrupt" -- meaning a D0, and NOT a status read.
    //
    // The two cannot both be true, and we follow the GENERAL RULE: a status read clears
    // INTRQ, I3 included. Reasons: it is stated as a property of the pin rather than as a
    // footnote to one command; every driver in the world reads status in its interrupt
    // handler and expects that to acknowledge; and the failure mode of the other choice is
    // an interrupt line that never lets go, which on a card that jumpers INTRQ to pin 73 is
    // a hung machine. WRITTEN DOWN rather than chosen silently -- if a real driver ever
    // depends on the sticky reading, this is the paragraph to revisit.
    fiConds_ = (uint8_t)(cmd & 0x07);
    if (cmd & 0x08) intrq_ = true;

    // D0 -- every I bit clear -- is "terminate with NO interrupt", and it is what a
    // driver issues to get the chip's attention back without taking one. It must not
    // set INTRQ, which is why this is not an unconditional assignment.
    (void)clk;
}

// ---------------------------------------------------------------------------
void Wd1771::stepOnce() {
    if (!drive_) return;
    drive_->step(dirIn_);
}

void Wd1771::finish(const Clock& clk) {
    phase_ = Phase::Idle;
    due_   = 0;
    intrq_ = true;  // "an interrupt is generated at the completion of the command"
    (void)clk;
}

std::vector<std::string> Wd1771::drainLog() {
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

// ---------------------------------------------------------------------------
// THE STATE MACHINE
// ---------------------------------------------------------------------------

void Wd1771::poll(const Clock& clk) {
    // ---- The armed Force Interrupt conditions, which are EDGES on pins ----
    const bool nowReady = ready();
    const bool nowIndex = drive_ && drive_->index();
    if (fiConds_ & 0x01) {                             // I0: not-ready TO ready
        if (nowReady && !prevReady_) intrq_ = true;
    }
    if (fiConds_ & 0x02) {                             // I1: ready TO not-ready
        if (!nowReady && prevReady_) intrq_ = true;
    }
    if (fiConds_ & 0x04) {                             // I2: index pulse
        if (nowIndex && !prevIndex_) intrq_ = true;
    }
    prevReady_ = nowReady;
    prevIndex_ = nowIndex;

    // ---- ...and then the command in flight ----
    // A `while`, not an `if`: one call to poll() may have to cross several deadlines at
    // once (a card that was not polled for a millisecond has a whole seek to catch up
    // on), and each phase sets the next due_ before it returns.
    while (phase_ != Phase::Idle && clk.now() >= due_) {
        switch (phase_) {
            case Phase::Settle: {
                stepOnce();

                if ((command_ >> 4) == kRestore) {
                    // Step out until TR00, and count -- 255 steps without finding it is a
                    // drive that is not moving, and the data sheet calls that a Seek Error.
                    if (drive_ && drive_->trackZero()) {
                        track_     = 0;
                        stepsLeft_ = 0;
                    } else if (--stepsLeft_ <= 0) {
                        status_ |= kSeekError;
                        finish(clk);
                        break;
                    }
                } else {
                    // Seek, Step, Step In, Step Out.
                    //
                    // THE TRACK REGISTER FOLLOWS THE STEP, NOT THE OTHER WAY ROUND. Seek
                    // always updates it; Step/Step In/Step Out update it only if `u` is
                    // set -- which is what lets a driver walk the head without disturbing
                    // its own bookkeeping, and is a real thing real drivers do.
                    const uint8_t op = (uint8_t)(command_ >> 4);
                    const bool updates = (op == kSeek) || flagU(command_);
                    if (updates) {
                        if (dirIn_) ++track_;
                        else if (track_) --track_;
                    }
                    --stepsLeft_;
                }

                if (phase_ == Phase::Idle) break;  // the Restore above already gave up

                if (stepsLeft_ > 0) {
                    due_ += stepTStates(clk);
                    break;
                }
                // Stepping done. Verify if the command asked for one -- and note that the
                // 10 ms head settle happens HERE, before the ID field is read, not back
                // when the command started.
                if (flagV(command_)) {
                    // "During verification, the head is loaded and after an internal 10 ms
                    // delay, the HLT input is sampled." The head goes down for the verify
                    // whether or not the command's h flag asked for it.
                    loadHead(true, clk);
                    phase_ = Phase::HeadSettle;
                    due_ += headSettleTStates(clk);
                } else {
                    finish(clk);
                }
                break;
            }

            case Phase::HeadSettle:
                afterHeadSettle(clk);
                break;

            case Phase::Read: {
                // The byte the guest was supposed to have taken is still sitting in the DR
                // with DRQ up. It is now too late: the next one is here, and the old one is
                // gone. THAT IS LOST DATA, and it is why byteTStates() has to be real.
                if (drq_) lost_ = true;

                data_ = buf_[idx_++];
                drq_  = true;

                if (idx_ < buf_.size()) {
                    due_ += byteTStates(clk);
                    break;
                }

                // ---- THE RECORD TYPE IS LATCHED *BEFORE* THE CRC IS CHECKED ----
                //
                // The chip reads the data address mark at the START of the data field --
                // the Type II flow chart puts `PUT RECORD TYPE IN STATUS REG` immediately
                // after it finds the mark, before a single byte is transferred and long
                // before the CRC at the far end is looked at.
                //
                // So a sector that is BOTH deleted AND corrupt reports 11 (F8) *and* a CRC
                // error. Set the record type after the CRC check instead, and that sector
                // comes back as 00 (a normal record) -- so a BIOS asking "is this deleted
                // or is it just broken?" gets told "broken", on precisely the disk where
                // the distinction is worth having.
                //
                // ONLY A READ HAS A DATA FIELD, mind. Read Address reads an ID field and
                // Read Track reads raw bytes; neither has a mark or a data CRC, and id_ is
                // not theirs to look at.
                if (ctx_ == Ctx::Read) {
                    // "the type of Data Address Mark encountered in the data field is
                    // recorded in the Status Register (Bits 5 and 6)." TWO bits, because
                    // this chip has four data address marks -- exactly the a1a0 field the
                    // Write command writes, and the biggest single difference from a 179x.
                    // FB (a normal record) is 00. F8 (deleted) is 11.
                    if (id_.deleted) status_ |= (kRecType6 | kRecType5);

                    // "If there is a CRC error at the end of the data field, the CRC error
                    // status bit is set, and the command is terminated (EVEN IF IT IS A
                    // MULTIPLE RECORD COMMAND)."
                    if (!id_.dataCrcOk) {
                        status_ |= kCrcError;
                        finish(clk);
                        break;
                    }
                }

                if (end_ == Ending::NextRecord && flagM(command_)) {
                    ++sector_;
                    FloppyDrive::SectorId next{};
                    if (findSector(next)) {
                        id_ = next;
                        buf_.assign((size_t)next.size, 0);
                        size_t n = buf_.size();
                        if (drive_->readData(next, buf_.data(), &n) && n == buf_.size()) {
                            idx_ = 0;
                            due_ += byteTStates(clk);
                            break;
                        }
                    }
                    // ---- RUNNING OFF THE END OF THE TRACK IS *RECORD NOT FOUND* ----
                    //
                    // This is the normal termination of every multi-sector transfer, and it
                    // is NOT a clean one. The flow chart has no other exit: `IS M = 1?` ->
                    // YES -> `+1 TO SECTOR REG` -> back into the ID-SEARCH LOOP, whose own
                    // exit is `HAVE 3 INDEX HOLES PASSED?` -> YES -> `INTRQ, RESET BUSY,
                    // SET RECORD-NOT FOUND`. The chip spins for two revolutions looking for
                    // a sector that is not there, and then says so.
                    //
                    // Drivers written against the real part KNOW this and read RNF-after-a-
                    // multi-record as success. Finish clean here and such a driver sees a
                    // status it never sees on hardware.
                    status_ |= kNotFound;
                }
                finish(clk);
                break;
            }

            case Phase::WriteWait: {
                if (drq_) {
                    // The first DRQ went unserviced. The command dies here and NOTHING is
                    // written -- see beginTypeII(). This is the one place a missed DRQ is
                    // fatal rather than merely lossy.
                    lost_ = true;
                    finish(clk);
                    break;
                }
                phase_ = Phase::Write;
                if (buf_.size() >= (size_t)id_.size) {
                    if (end_ == Ending::CommitTrack) commitTrack(clk);
                    else commitSector(clk);
                    break;
                }
                drq_ = true;
                due_ += byteTStates(clk);
                break;
            }

            case Phase::Write: {
                if (drq_) {
                    // "If the DRQ is not serviced in time for continuous writing the Lost
                    // Data status bit is set AND A BYTE OF ZEROS IS WRITTEN ON THE DISK.
                    // The command is not terminated." So the sector still gets written --
                    // with a hole in it. A model that aborted here would be kinder than
                    // the hardware and would hide the corruption the hardware creates.
                    lost_ = true;
                    buf_.push_back(0x00);
                    drq_ = false;
                }
                if (buf_.size() >= (size_t)id_.size) {
                    if (end_ == Ending::CommitTrack) commitTrack(clk);
                    else commitSector(clk);
                    break;
                }
                drq_ = true;
                due_ += byteTStates(clk);
                break;
            }

            case Phase::Idle:
                break;
        }
    }
}

uint64_t Wd1771::nextEdge(const Clock& clk) const {
    if (phase_ == Phase::Idle) return 0;
    // STRICTLY IN THE FUTURE, always. A deadline already past is one poll() has yet to
    // run, not one to wake up for -- and arming a timer for now() would fire inside the
    // drain loop that is running us, and arm it again, and never stop. (Mc6850::nextEdge
    // says the same thing at more length, and for the same reason.)
    return (due_ > clk.now()) ? due_ : clk.now() + 1;
}

} // namespace altair
