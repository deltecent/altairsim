#pragma once
//
// The MITS hard-sector floppy controllers -- the register model the 88-DCDD and the
// 88-MDS share, and the reason they share it.
//
// THIS BASE CLASS IS A HARDWARE FACT, NOT A CODE-SHARING CONVENIENCE. MITS built the
// minidisk controller to present the 8" controller's programming model, so that driver
// code would port: the same three ports, the same status bits in the same places, the
// same inverted sense, the same 137-byte hard-sector slot, the same "arm the write and
// shift bytes in real time" discipline. Mike Douglas's period BOOT.ASM says it out loud
// by construction -- it declares DRVSLCT/DRVSTAT/DRVCMD/DRVSEC/DRVDATA and every s*/c*
// bit OUTSIDE its `if MINIDSK` blocks, and only the geometry inside them.
//
// So what lives here is exactly what the two cards genuinely have in common, and no more:
//
//   - the three-port decode at BASE+0..2;
//   - the INVERTED status byte, assembled once from true-sense flags;
//   - the sector-position register, and rotation as a reading taken off the Clock;
//   - the 137-byte read and write paths, and the partial-write flush;
//   - the per-drive Spindle + DiskImage, and the size probe;
//   - port/drives/interrupt properties, units, MOUNT/UNMOUNT, [[board.drive]].
//
// AND WHAT IS *NOT* HERE IS THE POINT. The command byte is pure virtual, because that is
// where the two cards stop agreeing: the 88-DCDD's bit 2 loads a head solenoid, and the
// 88-MDS's bit 2 restarts a motor timer for a drive whose head is ALWAYS loaded. So is
// the rotation speed (360 vs 300 RPM), the byte clock (32 vs 64 us), the drive count
// (16 vs 4), and what happens when both step bits arrive at once.
//
// THE BUG THIS FILE EXISTS TO KILL. Before it, the minidisk was a row in the 88-DCDD's
// format table plus one line:
//
//     if (v & 0x08) {                        // cHDUNLD
//         if (d->fmt.sectors != 16) { ... }  // <-- the 8" card asking "am I a minidisk?"
//     }
//
// -- a controller inferring which controller it was from the shape of the disk in the
// drive. It never failed, because nothing in the tree ever mounted a minidisk. That is
// the expensive kind of wrong: the card also turned the platter at 360 RPM instead of
// 300 and clocked bytes at twice the real rate, and would have booted from a PROM that
// cannot read the medium. See docs/boards/mits-88mds.md.
//
// AN UNUSUALLY RAW CONTROLLER, on both cards. Neither reads or writes a sector for you.
// Software steps the head, watches the sector counter go by, and shifts bytes one at a
// time through a data port in real time -- so nearly all the disk logic lives in the
// BIOS, and what these cards owe the guest is mostly TIMING that is honest. There is no
// controller chip to model (contrast the Tarbell, which wires up an FD1771): both are
// TTL, and their "registers" are three ports over a handful of flip-flops.
//
// THE SLOT IS 137 BYTES AND THE IMAGE HOLDS ALL OF IT. These are HARD-SECTOR cards: sync
// byte, header, 128-byte payload, checksum, stop byte, trailer -- the lot is on the
// medium and the lot is in the file. (A soft-sector image holds the payload only, because
// those headers lived in the inter-sector gap and never reached the file.) What is INSIDE
// the slot is the BIOS's business, not ours: these cards move 137 bytes and never look at
// one of them.

#include "core/board.h"
#include "core/spindle.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// The slot, on every format, on both cards. Not a payload size -- see above.
inline constexpr int kHsSectorBytes = 137;

// SECTOR TRUE IS 30 MICROSECONDS ON BOTH CARDS, and that is not an assumption -- it is
// the one timing constant the two manuals agree on to the digit.
//
//   88-DCDD manual: "D0 - SR0 - Sector True - True when = 0, and is 30 us long."
//   88-MDS  manual, p33/p34: "It is True during the first 30us of a Sector."
//   88-MDS  handwritten one-shot sheet: "TP-7  30 uS (20/40)  SECTOR COUNT"
//   MDBL.ASM: "SECVAL equ 01h ;Sector Valid (1st 30 uS of sector pulse)"
//
// An earlier version of the 88-DCDD DERIVED this instead: it made the signal last for the
// whole inter-sector gap, because that was self-consistent, tidy, and "what the signal must
// mean". It is 30 us -- a 0.58% duty cycle on the 8" card -- and the difference was a factor
// of twenty-seven. CP/M booted either way, which is the entire problem with inventing a
// number: a too-generous window is forgiving, so the guest never complains and the bug never
// surfaces. Both manuals were in the tree the whole time. (DESIGN.md 0.1, once more.)
inline constexpr uint64_t kSectorTrueUs = 30;

// ---------------------------------------------------------------------------
// THE MEDIA A CARD KNOWS.
//
// Geometry probing belongs to the BOARD (DESIGN.md 7.3): 337,568 bytes means a 77-track
// 8" floppy *because it is a DCDD*, and the same byte count on another controller means
// something else. Only the card knows which formats are even candidates, so only the card
// can probe -- which is exactly why `minidisk` had no business being a row in the 8"
// card's table.
//
// `dataTrack` is where the sector layout changes from the system format to the data
// format, and it is NOT the same on every medium -- the minidisk's is 4 where the 8" is 6.
// Neither card uses it (what is inside the slot is the BIOS's affair) but the docs record
// it, and a formatter written against either card would need it.
// ---------------------------------------------------------------------------
struct HsFormat {
    const char* name;
    int         tracks;
    int         sectors;
    int         dataTrack;
    uint64_t    bytes;  // tracks * sectors * 137
};

class HardSectorFdc : public Board {
public:
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    void reset(Reset) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

    // One drive and whatever is in it. The Spindle is PER DRIVE, and the reason is not the
    // one this comment used to give ("a minidisk turns 16 sectors past the head where an 8"
    // turns 32") -- that was never a reason, because a minidisk belongs in a different CARD.
    //
    // The real reason is the FDC+ (reference/FDC+ Manual.pdf, 3.7.4): period software expects
    // an 8 MB image on drives A/B and ORDINARY 77-TRACK FLOPPIES ON C/D, on one controller,
    // in one daisy chain. A board-wide geometry cannot answer "which sector is under the
    // head" for a 2048-track disk and a 77-track one at the same time.
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string path;
        std::string forced;  // the `media` property: "" means "probe it"
        HsFormat    fmt{};
        bool        loaded = false;  // head loaded. The 88-MDS has no say in this; see headLoaded()
        bool        roSaid = false;  // "write-protected, discarding" -- said once, not per sector
        int         track  = 0;
        Spindle     spindle;
    };

    // ---- WHAT A CARD MUST SAY ABOUT ITSELF ----------------------------------

    // Which media this card can even take. The probe picks among these and nothing else.
    virtual const std::vector<HsFormat>& formats() const = 0;

    virtual int      maxDrives()  const = 0;  // 16 on the daisy chain (DCDD) / 4 (MDS)
    virtual uint8_t  selectMask() const = 0;  // 0x0F (DCDD) / 0x03 (MDS -- only two bits)
    virtual int      rpm()        const = 0;  // 360 (8") / 300 (minidisk)
    virtual uint64_t byteUs()     const = 0;  // 32 (250 kbit/s) / 64 (125 kbit/s)

    // How long after the sector hole the read path is un-gated, and the write circuit asks
    // for its first byte. Both are RC one-shots and both differ between the cards.
    virtual uint64_t readStartUs()  const = 0;  // 140 (DCDD) / 500 (MDS "READ CLEAR")
    virtual uint64_t writeStartUs() const = 0;  // 280 (DCDD) / 1000 (MDS "WRITE CLEAR")

    // THE COMMAND BYTE, which is where the two cards stop being the same card. Independent
    // bits, not an opcode -- more than one arrives at a time and the BIOS does exactly that.
    virtual void command(uint8_t v) = 0;

    // ---- STATUS, as predicates a card answers for itself ---------------------

    // Is the head loaded? The 88-DCDD has a solenoid and a bit to drive it. The 88-MDS does
    // not: "the head is always loaded when the Drive is enabled" -- so for that card this is
    // really "is the motor up to speed", one second after the drive is enabled.
    virtual bool headLoaded(const Drive&) const = 0;

    // MOVE OK. The 88-DCDD models no step-settling at all; the 88-MDS's manual documents a
    // 50 ms window in which MH reads false and it would be a lie not to.
    virtual bool moveOk(const Drive&) const { return true; }

    // Is the card answering at all? The 88-MDS turns ITSELF off -- when the 6.4 s Disk
    // Disable Timer expires, and when the selected drive has no diskette in it ("all status
    // bits are logic 1 when there is not a Minidiskette in the Drive").
    virtual bool online(const Drive&) const { return true; }

    // Can the guest read a sector number right now? The 88-MDS gates this channel shut for
    // 1 second after the drive is enabled and 50 ms after every step; the 88-DCDD never does.
    virtual bool sectorChannelLive() const { return true; }

    // Called from selectDrive(), BEFORE sel_ moves, so a card can see the transition it is
    // about to make. The 88-MDS arms its motor delay here.
    virtual void onSelect(int oldSel, int newSel) { (void)oldSel; (void)newSel; }

    // The `drives` property resized us.
    virtual void onDrivesChanged() {}

    // ---- WHAT THE BASE GIVES A CARD BACK -------------------------------------

    Drive*       selected();
    const Drive* selected() const;
    int          sel() const { return sel_; }

    // Rotation, in the only place it is computed. Everything the guest can observe about
    // time -- the sector number, sector-true, NRDA, ENWD -- comes out of here.
    struct Position {
        bool spinning   = false;
        int  sector     = 0;
        bool sectorTrue = false;  // the 30 us one-shot: positioned, write NOW
        int  byteIndex  = -1;     // which of the 137 bytes the read head has passed
        int  writeIndex = -1;     // which byte the write circuit is asking for
    };
    Position where() const;

    // The card's one-shots are RC networks, so they are measured in MICROSECONDS and know
    // nothing about the CPU. A 4 MHz machine gets twice as many instructions inside the same
    // 30 us sector-true window; the window does not grow.
    uint64_t tFromUs(uint64_t us) const;
    uint64_t tPerByte() const { return tFromUs(byteUs()); }

    void armWrite();          // WRITE ENABLE: 137 bytes, starting now
    void flushWrite();        // and the PARTIAL ones are the whole point -- see the .cpp
    void invalidatePosition();
    void say(std::string s) { log_.push_back(std::move(s)); }

    // ---- The card's flags, TRUE-SENSE. The inversion happens once, on the way out. ----
    struct Status {
        bool enwd    = false;  // 0x01 write circuit wants another byte
        bool moveok  = false;  // 0x02 head movement allowed
        bool hdstat  = false;  // 0x04 head loaded (and, on the MDS, motor at speed)
        bool dsken   = false;  // 0x08 a drive is selected and enabled
        bool inten   = false;  // 0x20 interrupts enabled (decoded, not wired)
        bool track0  = false;  // 0x40 head on track 0
        bool nrda    = false;  // 0x80 new read data available
    };
    Status st_;

    uint16_t  port_   = 0x08;  // three ports: base, base+1, base+2
    int       drives_ = 4;
    IrqJumper irq_    = IrqJumper::None;

    std::vector<Drive> drive_;

    bool writing_ = false;

private:
    void selectDrive(int n);
    void syncSector(const Position&);
    bool probe(Drive&, std::string& err);

    int sel_ = -1;  // -1 = none selected. Status then reads 0xFF.

    // The read path. `bufSector_` is which sector is in the buffer, or -1; `readPos_` is how
    // many of its bytes the guest has taken. The guest cannot read a byte the disk has not
    // turned past yet -- that is what NRDA is for.
    uint8_t buf_[kHsSectorBytes]{};
    int     bufSector_ = -1;
    int     readPos_   = 0;

    // The write path. WRITE ENABLE arms it; the card then consumes EXACTLY 137 bytes and
    // commits. A partial write that is interrupted by a step or a select must still be
    // flushed -- the system sectors are only 133 bytes long and never reach 137, so a card
    // that waited for the 137th byte would lose every one of them.
    uint8_t wbuf_[kHsSectorBytes]{};
    int     writePos_ = 0;
    int     wSector_  = -1;
    int     wTrack_   = -1;

    std::vector<std::string> log_;
};

} // namespace altair
