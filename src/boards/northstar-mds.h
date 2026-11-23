#pragma once
//
// North Star Micro-Disk System -- the MDS-A (single density, 1977) and the MDS-A-D
// (double density, 1978). docs/boards/northstar-mds.md; the hardware is distilled in
// reference/North Star MDS Floppy Controllers.md.
//
// THIS BOARD HAS NO PORTS. It is the one floppy controller in the tree that is not an
// IN/OUT device: it occupies a 1 K block of MEMORY (standard origin E800), and every
// command is a memory READ whose low eight address bits ARE the command -- or the data
// byte. "Write FB to the disk" is `LDA E9FB` on the double-density board. So the decode
// is on Cycle::MemRead across the window, a read has side effects, and peek() answers
// only for the PROM page: DISASM, HISTORY and the debugger's display look through peek()
// and must not step the head. (The monitor's DUMP is a real bus cycle, by design --
// DESIGN.md 10 -- so a DUMP of EB00 does give the board commands, as IN 10 takes a
// character from a UART.)
//
// TWO BOARDS, ONE BASE, and the split is the same one the two manuals draw. What they
// share is here: the window, the 256-byte bootstrap PROM, the drives, a 5.25" hard-sector
// diskette turning at 300 RPM with ten sector holes, the sector flag, the 96 us window,
// the read and write streams, the motor, and the interrupt on every sector pulse. What
// they do NOT share is the whole command interface -- the meaning of the four address
// "cases", the command byte, the status bytes, how a drive is selected -- and that is the
// one virtual, access(). A single decoder cannot serve both.
//
// THE IMAGE IS PAYLOAD ONLY. A North Star sector on the medium is a preamble of zeros,
// a sync character (FB), the data, and a check character. A `.NSI` file holds the data
// and nothing else: 35 x 10 x 256 = 89,600 bytes single density, 35 x 10 x 512 = 179,200
// double density, twice that for two sides. So on a read the board supplies the body --
// the data and then the check character, computed -- and on a write it takes the stream
// the guest sends, finds the sync, and keeps the data.
//
// THE CPU IS STALLED, NOT POLLED. A read-data or write-data access holds PRDY until the
// shift register is ready. Under `timing = full` (the default) each such access moves the
// next byte at once, as if the CPU had waited, and the wait takes no emulated time. Under
// `timing = real` the board holds READY (Board::holdReady()) until the byte is under the
// head. Either way, what is timed is everything the guest polls for: the sector flag, the
// window, read enable, body.
//
// THE MOTOR IS FREE BY DEFAULT, the same call the 88-MDS makes: the motors start on the
// command that starts them, and with `motor = "real"` they stop again after the board's
// own count of revolutions. See properties().

#include "core/board.h"
#include "core/spindle.h"
#include "host/disk.h"
#include "host/diskfaults.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// A medium the board can take. The probe picks among these by the size of the file.
struct NsFormat {
    const char* name;
    bool        dd;           // double density: 512-byte sectors
    int         sides;
    int         sectorBytes;  // 256 or 512 -- the DATA, which is all the file holds
    uint64_t    bytes;        // 35 * 10 * sectorBytes * sides
};

inline constexpr int kNsTracks  = 35;
inline constexpr int kNsSectors = 10;
inline constexpr int kNsMaxData = 512;

class NorthStarFdc : public Board {
public:
    ~NorthStarFdc() override;

    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    void reset(Reset) override;
    void configChanged() override;

    bool    assertsInt() const override { return irq_ == IrqJumper::Int && intLevel(); }
    uint8_t assertsVi() const override { return intLevel() ? viBit(irq_) : 0; }

    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;

    // `SET fd0 DEBUG=seek` traces head steps; `DEBUG=sector` traces each sector read or
    // written; `DEBUG=fault` writes a line each time a bad sector is used (#970). The bit
    // order IS the enum, so they sit together.
    std::vector<std::string> debugFlags() const override { return {"sector", "seek", "fault"}; }
    enum DebugFlag { SECTOR = 0, SEEK = 1, FAULT = 2 };

    // BAD SECTORS (#970, host/diskfaults.h). The address is <track>/<side>/<sector>: the track
    // under the head, the side, and the hole count 0..9. The image never changes.
    //   data     the check character after the data is wrong; the data is intact. The board
    //            never checks it -- the guest does. Counted when the guest reads it.
    //   missing  no sync character, so body mode never comes. Counted once a pass, when the
    //            board looks for the sync (the guest reads the body bit after body time).
    //            The board cannot tell why the guest read it: on both boards the A-status that
    //            holds the body bit also holds SF, and the CP/M BIOS polls it to wait for the
    //            sector flag. Such a pass counts too, so a hit count can be one more than the
    //            guest's tries.
    //   write    the sector is not written, and the guest is not told. Counted at the end
    //            of the data, where the sector would have gone to the image.
    //   id       REFUSED: a North Star sector has no ID field (DiskFaults::refuse).
    bool        takesFaults(const std::string& unit) const override;
    DiskFaults* faults(const std::string& unit) override;

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;
    std::vector<Property> mountKeys(const std::string& unit) const override;
    bool mountWith(const std::string& unit, const std::string& path, bool ro, const KeyValues& keys,
                   std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The flip-flops, the motor deadline, each drive's
    // head track, and the read and write streams in flight. The images are host-backed
    // and do not travel. Rotation is a function of the Clock, so there is no counter.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    NorthStarFdc(const char* romName, uint16_t romOrigin, int drives);

    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;
    Property mediaKey() const;  // `media`: a [[board.drive]] key and a MOUNT option

    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string     path;
        std::string     forced;         // the `media` property: "" means "probe it"
        const NsFormat* fmt    = nullptr;  // null: a blank diskette, not yet written
        int             track  = 0;
        bool            roSaid = false;    // "write-protected, discarding" -- said once
        DiskFaults      faults;            // the bad sectors of the diskette (#970); MOUNT clears them
    };

    // ---- WHAT A BOARD MUST SAY ABOUT ITSELF ----------------------------------

    virtual const std::vector<NsFormat>& formats() const = 0;
    virtual int maxDrives() const = 0;  // 3 on the MDS-A, 4 on the MDS-A-D

    // The automatic motor-off time. Both boards COUNT REVOLUTIONS, so with a diskette
    // turning at 200 ms a revolution it is 3.2 s (16) and 9.6 s (48).
    virtual uint64_t motorOffUs() const = 0;

    // With no sector hole to see -- the motors off, or no diskette -- the board makes
    // its own sector pulses, and the sector counter runs free. It is a decade counter
    // on the MDS-A and a 4-bit binary counter on the MDS-A-D.
    virtual int freeRunModulo() const = 0;

    // Does power-on clear only, or the front-panel RESET too? See each board.
    virtual bool resetsOn(Reset r) const = 0;

    // Density of the sector about to be written. The MDS-A has one answer.
    virtual bool writeDD() const { return false; }

    // Which side of the diskette the head is on. Only the MDS-A-D has a say.
    virtual int side() const { return 0; }

    // What the automatic motor-off does besides stop the motors.
    virtual void onMotorOff() {}

    // Which of the four address cases read the PROM, and what each case is (SHOW BUS MAP).
    virtual bool        promCase(int kase) const = 0;
    virtual const char* caseNote(int kase) const = 0;

    // The reset command, and POC. The motors stop, no drive is selected, the interrupt is
    // disarmed. A board adds its own flip-flops.
    virtual void controllerReset();

    // THE COMMAND INTERFACE -- one memory read. `kase` is address bits 9..8, `low` is
    // bits 7..0. The base has already brought the rotation up to date.
    virtual uint8_t access(int kase, uint8_t low) = 0;

    // ---- WHAT THE BASE GIVES A BOARD BACK -------------------------------------

    // Rotation, in the only place it is computed. `real` means a diskette is turning
    // under a selected head; otherwise the pulses are the board's own.
    struct Rotation {
        bool     real   = false;
        uint64_t abs    = 0;  // sectors since power-on, at this period
        int      sector = 0;  // the sector counter
        uint64_t into   = 0;  // T-states since the sector pulse
    };
    Rotation where() const;

    uint64_t tFromUs(uint64_t us) const;

    bool inWindow(const Rotation& r) const;   // the 96 us after a sector pulse
    bool pastUs(const Rotation& r, uint64_t us) const { return r.into >= tFromUs(us); }
    bool body(const Rotation& r);             // the sync character has gone by
    bool writeActive(const Rotation& r) const;  // the write gate is open
    bool readEnabled(const Rotation& r) const;  // MDS-A-D: the phase-locked loop is on
    bool ddDetected(const Rotation& r) const;   // MDS-A-D: the double-density indicator
    bool mediumDD() const;                    // the diskette under the head is double density

    uint8_t promByte(uint8_t low) const { return prom_[low]; }
    uint8_t readData(const Rotation& r);      // the next byte of the body
    void    writeData(uint8_t v);             // the next byte of the write stream
    void    beginWrite(const Rotation& r);    // accepted only inside the window
    void    setBody() { bodyForced_ = true; }

    void selectDrive(int n);   // -1 = none
    void step(bool in);
    void motorSet();
    void motorStop();
    void resetSectorFlag();
    void armInterrupt(bool on);

    Drive*       selected();
    const Drive* selected() const;
    bool sectorFlag() const { return sf_; }
    bool motorOn() const { return motorOn_; }
    bool track0() const;
    bool writeProtected() const;

    void say(std::string s) { log_.push_back(std::move(s)); }

private:
    void loadProm();
    void tick();          // bring the sector flag and the motor up to date
    void endSector();     // a sector pulse ends a read and a write
    void scheduleWake();  // the interrupt needs to be present at the pulse
    void commitWrite();
    bool intLevel() const { return armed_ && sf_; }
    bool probe(Drive&, std::string& err) const;
    void applyFormat(Drive&, const NsFormat&) const;
    bool loadSector(const Rotation& r);
    void useFault(DiskFault& f, int track, int side, int sector);

    const char* romName_;
    uint16_t    romOrigin_;
    uint8_t     prom_[256]{};

    uint16_t  base_      = 0xE800;
    int       drives_;
    IrqJumper irq_       = IrqJumper::None;
    bool      motorReal_ = false;
    bool      timingReal_ = false;  // `timing = real`: a data access holds READY (#637)

    // `timing = real`. The clock does not move inside an instruction; the run loop charges
    // a READY hold at the boundary. Until then the board has lived through time the clock
    // has not reached, and ahead_ says how far: every time the board reads is now() =
    // max(clock, ahead_). Not serialized -- at a boundary the clock has caught up.
    uint64_t ahead_ = 0;
    uint64_t now() const;
    void     holdUntil(uint64_t t);         // hold READY until T, if T is still to come
    uint64_t byteT(bool dd) const;          // one byte under the head: 64 us SD, 32 us DD

    std::vector<Drive> drive_;
    Spindle            spindle_;  // 300 RPM, ten sector holes

    int  sel_     = -1;
    bool sf_      = false;  // SECTOR FLAG: set by the pulse, reset by software
    bool armed_   = false;
    bool motorOn_ = false;
    uint64_t motorOffAt_ = 0;

    // Where tick() last looked.
    bool     lastReal_ = false;
    uint64_t lastAbs_  = 0;

    // The read stream: the sector under the head, and how much of its body the guest
    // has taken. `rdAbs_` is which pass of the disk the buffer was loaded on.
    uint8_t  rdBuf_[kNsMaxData]{};
    uint64_t rdAbs_      = ~0ull;
    bool     rdValid_    = false;
    int      rdLen_      = 0;
    int      rdPos_      = 0;
    bool     bodyForced_ = false;
    int      rdFault_    = -1;  // the FaultKind on the loaded sector (#970), or -1

    // The write stream. Begin-write arms it; the bytes then arrive as zeros, the sync
    // character(s), the data, and the check character.
    enum class Wr : uint8_t { Idle, Preamble, Sync2, Data, Done };
    Wr      wr_       = Wr::Idle;
    bool    wrDD_     = false;
    int     wrTrack_  = 0;
    int     wrSide_   = 0;
    int     wrSector_ = 0;
    int     wrPos_    = 0;
    uint8_t wrBuf_[kNsMaxData]{};

    Clock::Handle wake_ = Clock::kNone;

    std::vector<std::string> log_;
};

// ---------------------------------------------------------------------------
// MDS-A -- single density. Cases 0 and 1 are the PROM, 2 is write-data, 3 is the
// command byte `MO RD BST CC CC CC M1 M0`.
// ---------------------------------------------------------------------------
class MdsABoard : public NorthStarFdc {
public:
    MdsABoard();
    std::string type() const override { return "mdsa"; }

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    const std::vector<NsFormat>& formats() const override;

    // THREE drives, not four. The select register is two flip-flops and the three
    // drive-select lines are decoded from them: 01, 10 and 11. 00 selects nothing.
    int maxDrives() const override { return 3; }

    // "Turns motors off after 16 revolutions" (schematic page 3, counter 1F).
    uint64_t motorOffUs() const override { return 3200000; }
    int      freeRunModulo() const override { return 10; }  // 1G is a 74LS160

    // RST is POC or the reset command, and nothing else: the board does not see the
    // front-panel RESET (schematic page 1).
    bool resetsOn(Reset r) const override { return r == Reset::PowerOn; }

    // The select register is cleared by MOTOR-ENB going false (schematic page 4), so
    // when the motors stop, no drive is selected.
    void onMotorOff() override { selectDrive(-1); }

    bool        promCase(int kase) const override { return kase < 2; }
    const char* caseNote(int kase) const override;
    void        controllerReset() override;
    uint8_t     access(int kase, uint8_t low) override;

private:
    bool stepFf_ = false;
    bool dirIn_  = false;
};

// ---------------------------------------------------------------------------
// MDS-A-D -- double density. Case 0 is the PROM, 1 is write-data, 2 loads the order
// register `DD SS DP ST DS DS DS DS`, 3 is the command byte `DM DM DM . CC CC CC`.
// ---------------------------------------------------------------------------
class MdsADBoard : public NorthStarFdc {
public:
    MdsADBoard();
    std::string type() const override { return "mdsad"; }

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    const std::vector<NsFormat>& formats() const override;
    int maxDrives() const override { return 4; }

    // "The disk drive motor(s) will turn off 9.6 seconds after the last disk activity"
    // (the manual, J2). The jumper table there offers nine other times.
    uint64_t motorOffUs() const override { return 9600000; }
    int      freeRunModulo() const override { return 16; }
    bool     resetsOn(Reset) const override { return true; }

    bool writeDD() const override { return (orders_ & 0x80) != 0; }
    int  side() const override { return (orders_ & 0x40) ? 1 : 0; }

    bool        promCase(int kase) const override { return kase == 0; }
    const char* caseNote(int kase) const override;
    void        controllerReset() override;
    uint8_t     access(int kase, uint8_t low) override;

private:
    void loadOrders(uint8_t v);

    uint8_t orders_ = 0;
};

} // namespace altair
