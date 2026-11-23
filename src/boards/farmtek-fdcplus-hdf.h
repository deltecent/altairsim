#pragma once
//
// FDC+ drive type 5 -- the 1.5 MB floppy (docs/boards/farmtek-fdcplus.md).
//
// With its Drive Type switches at 5, the FDC+ runs a 96 tpi high-density drive (a Teac 55-GFR,
// or an 8" DSDD drive) in a format of Mike Douglas's own: ONE 10,240-byte sector per track,
// both sides, MFM. 149 tracks of it is 1,525,760 bytes, five times an Altair floppy. The 8080
// sees none of the 137-byte Altair sectors: it sends READ ENABLE after the index hole and
// takes a whole track from the data port with no handshake, one byte every 16 us.
//
// THIS IS NOT A BOARD. It is the firmware module hdfloppy.s, and FdcPlusBoard owns it and
// hands it the bus when the card was powered up at type 5. It knows the Clock and its four
// drives and nothing else.
//
// EVERYTHING HAPPENS ON EMULATED TIME, worked out when the 8080 looks -- the Spindle idea
// (core/spindle.h). The disk turns whether or not anyone is reading it, so an index hole, a
// sector true, the moment the read starts and the byte that has arrived by now are all
// readings off the clock. advance() runs the card forward to NOW before every port access.
// That is what makes a guest that reads faster than 16 us a byte get OLD bytes, which is
// exactly what the real card gives it, and why the BIOS needs a 2 MHz 8080.
//
// THE ORACLE is the FDC+ firmware v1.8 source, hdfloppy.s (M. Douglas, v1.1 07/01/20),
// distilled in reference/FDC+ HD Floppy Firmware.md.

#include "core/clock.h"
#include "host/diskfaults.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class MediaFile;
class StateReader;
class StateWriter;

class FdcPlusHdf {
public:
    static constexpr int      kDrives      = 4;        // NUM_DRIVES
    static constexpr int      kTrackBytes  = 10240;    // XFER_LENGTH
    static constexpr int      kTracks      = 149;      // 0-143 both sides, 144-148 bottom
    static constexpr uint64_t kImageBytes  = (uint64_t)kTracks * kTrackBytes;  // 1,525,760
    static constexpr int      kCylinders   = 80;       // the Teac 55-GFR's 96 tpi
    static constexpr int      kBootLen     = 137;      // bootSector: an Altair sector

    FdcPlusHdf();
    ~FdcPlusHdf();

    void setClock(const Clock* c) { clock_ = c; }

    // The four ports, as offsets from the card's base.
    uint8_t read(int off);
    void    write(int off, uint8_t v);

    // Power-on: every drive deselected, the motor off, the registers FF. The disks stay in.
    void power();

    // A disk in drive `i`, already opened. False, and `err`, if it is not a 1.5 MB image.
    bool mount(int i, std::unique_ptr<MediaFile> m, const std::string& path, std::string& err);
    bool unmount(int i, std::string& err);
    bool        mounted(int i) const { return (bool)drive_[(size_t)i].img; }
    std::string path(int i) const { return drive_[(size_t)i].path; }
    bool        readOnly(int i) const;
    bool        readOnlyForced(int i) const;

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

    // THE BAD SPOTS OF THE DISK IN DRIVE `i` (#969), or null with no disk. A fault's address is
    // where the head is: <cylinder>/<side>/0, the bottom side 0 and the top 1, and sector 0, the
    // one sector of a track. The firmware reports each fault with the bits it already has:
    //   data     byte 64 of the track comes in wrong, and done brings CHECKSUM ERROR (bit 0)
    //   id       the sync byte names another track: TRACK ERROR (bit 1); the data still comes
    //   missing  no sync byte: END OF SECTOR (bit 2) and done at the next index hole
    //   write    the drive does not write the track; done, as on a write-protected disk
    // A read or a write uses the fault the moment the firmware meets it: id and missing as the
    // read starts, data at the checksum, write when the write ends.
    DiskFaults*       faults(int i);
    const DiskFaults* faults(int i) const {
        return const_cast<FdcPlusHdf*>(this)->faults(i);
    }

    // Called each time a fault is used: drive, cylinder, side, and the fault after the use. The
    // board writes the `fault` debug line from it.
    using FaultHook = std::function<void(int, int, int, const DiskFault&)>;
    void setFaultHook(FaultHook h) { faultHook_ = std::move(h); }

    static constexpr int kFaultDataByte = 64;  // the byte of the track a `data` fault spoils

    // The image track at a cylinder and side, or -1: the layout the BIOS uses.
    static int imageTrack(int cyl, bool top);

    // The Altair sector in the firmware's flash that a stock boot PROM reads (HDFBL.ASM).
    static const uint8_t* bootSector();

    // ---- for tests ----
    int  cylinder(int i) const { return drive_[(size_t)i].cyl; }
    bool motorOn() const { return motor_; }

private:
    enum class Phase { Off, Loop, Window, Reading, Writing };

    struct Drive {
        std::unique_ptr<MediaFile> img;
        std::string                path;
        int                        cyl = 0;
        DiskFaults                 faults;
    };

    uint64_t now() const { return clock_ ? clock_->now() : 0; }
    uint64_t us(uint64_t n) const;  // microseconds of the card's crystal, in T-states
    bool     spinning() const { return sel_ >= 0 && motor_ && drive_[(size_t)sel_].img; }
    bool     top() const { return curTrack_ < 144 && (curTrack_ & 1); }
    int      headTrack() const;     // the image track under the selected head, or -1
    bool     formatted(int t) const;

    void advance(uint64_t t);       // run the card forward to emulated time t
    void hole(uint64_t h);          // one index hole: sectorLoop
    void startRead(uint64_t at);
    void startWrite(uint64_t at);
    void fillTo(uint64_t t);        // the bytes the disk has delivered by t
    void drainTo(uint64_t t);       // the bytes the disk has taken by t
    void finishRead();
    void finishWrite();
    void startMotor();              // the motor on, and waitMotor: skip the first hole
    DiskFault* findFault(FaultKind k);  // the armed fault of kind k under the head, or null
    void       useFault(DiskFault& f);
    void idle();                    // enterIdle
    void select(uint8_t v);
    void command(uint8_t v);

    const Clock* clock_ = nullptr;
    std::array<Drive, kDrives> drive_;

    // ---- the registers the 8080 reads, raw (status asserted LOW) ----
    uint8_t status_    = 0xFF;
    uint8_t sectorPos_ = 0xFF;
    uint8_t readData_  = 0x00;
    uint8_t ioStatus_  = 0x00;
    uint8_t curTrack_  = 0;

    // ---- the firmware's state ----
    int     sel_         = -1;     // the drive whose select line is on
    bool    motor_       = false;  // B11_MOTOR_ON
    bool    readFlag_    = false;  // fREAD_ENABLE
    bool    writeFlag_   = false;  // fWRITE_ENABLE
    bool    headLoaded_  = false;  // fHEAD_LOADED
    int     motorTimer_  = 0;      // holes seen with the head not loaded
    uint8_t sector_      = 0xC0;   // toggles between sector 0 and sector 15

    // bootImage[256] and then trackBuf, one after the other in the PIC's RAM: the read
    // pointer runs from the boot sector straight into the track, as it does on the card.
    std::vector<uint8_t> mem_;
    size_t               rd_ = 0;  // wRemovePtr: the next byte the 8080 is given
    size_t               wr_ = 0;  // wInsertPtr: where the next byte OUT goes
    std::vector<uint8_t> pending_; // the track as the disk took it, during a write

    // ---- emulated time ----
    Phase    phase_     = Phase::Off;
    uint64_t spinFrom_  = 0;      // the moment the motor came on: holes are spinFrom_ + k*rev
    uint64_t nextHole_  = 0;
    bool     skipHole_  = false;  // waitMotor's first hole, or the one a read abort took
    uint64_t holeAt_    = 0;      // the hole this revolution started at
    uint64_t trueUntil_ = 0;      // sector true ends
    bool     trueOn_    = false;
    int      done_      = 0;      // bytes moved so far in this transfer
    bool     noSync_    = false;  // the track under the head was never written
    int      xferTrack_ = -1;     // the image track under the head when it started
    int      xferFault_ = -1;     // the FaultKind this transfer meets, or -1 (#969)
    int      xferCyl_   = 0;      // where the head was when it started: the fault's address
    int      xferSide_  = 0;
    uint64_t moveAt_    = 0;      // OC1: the step timer
    bool     moving_    = false;
    uint64_t settleAt_  = 0;      // OC2: the head settle timer
    bool     settling_  = false;

    FaultHook faultHook_;
};

} // namespace altair
