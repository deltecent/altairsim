#pragma once
//
// CompuPro Disk 1 -- a floppy disk controller board (board 171F, 1981), the Disk 1A's
// predecessor. See reference/CompuPro Disk 1 & 1A Floppy Controllers.md; the section numbers
// below are that file's. What it shares with the Disk 1A -- the 765, the DMA engine, the boot
// ROM window, the drives -- is in boards/compupro-disk.h.
//
// THE PORTS (§2), at a base that is a multiple of four (default C0, S2-3..8):
//       C0  IN  the 765's main status register     OUT  (not used: the board does not decode it)
//       C1  the 765's data register
//       C2  IN  the board status register          OUT  the DMA address, 3 bytes, MSB first
//       C3  IN  D7 = the serial input              OUT  D7 = the serial output latch;
//                                                       D0 = 0 also switches the boot ROM off
//
// THE BOARD STATUS REGISTER (§2.1) has one bit: D7, the 765's INT. The manual calls D0-D6
// "not significant"; they read 0 here. The port 3 IN drives D7 only, and D0-D6 read 0 too.
//
// THE BOOT ROM is a 2716: 8 routines of 256 bytes, picked by S2-1..2 and J17 (`routine`),
// at 0000-00FF.
//
// PORT 3 D7 (§2.2) is a bit-banged RS-232 line, inverted: D7 = 1 is SPACING (logic 0),
// D7 = 0 is MARKING (logic 1). Reset clears the output latch to MARKING. With S1-3 set the
// other way (`port3 = motor`), the same latch is the MOTOR ON line of the 34-pin minifloppy
// cable, and D7 = 0 is on (1982 manual p.19: "ON by outputting a 0", "OFF by outputting a
// 1"). With S1-3 set for the serial line, the minifloppy motors are on all the time.
// The far end of the line is chips/bitserial.h, the `serial` unit: 8N1 at the unit's `baud`.
// With `port3 = motor` the latch no longer reaches it and its output rests at MARKING; the
// input still reads.
//
// THE BOARD RUNS 8" DRIVES (§5), with the 765 at 8 MHz. There is no software rate select and
// no Force Two Sided: the 765's TS input is the disk's.
//
// WHAT IS NOT MODELED HERE, AND WHY EACH IS SAFE TO LEAVE OUT (and see compupro-disk.h):
//   - THE 5.25" CONVERSION. A Disk 1 runs 5.25" drives only after a hardware change: other
//     parts, three cut traces and new jumpers (1982 manual p.19). It is not a switch, so the
//     board takes 8" images only.
//   - THE MOTOR. The MOTOR ON latch reaches no bit a guest can read: the board has no ready
//     or index bit, and a stopped motor here does not take READY away. It is kept and shown.

#include "boards/compupro-disk.h"
#include "chips/bitserial.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class CompuProDisk1Board : public CompuProDiskBoard {
public:
    CompuProDisk1Board();

    std::string type() const override { return "disk1"; }

    std::vector<MapEntry> ioMap() const override;

    void power() override;
    void pump() override;
    void clockAttached() override;
    std::vector<std::string> drainLog() override;

    // ---- the serial unit ('serial'), on port 3 D7, beside the drives ----
    std::vector<UnitDef>  units() const override;
    std::vector<Property> boardUnitProperties(const std::string& unit) override;
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "serial" ? &line_.stream() : nullptr;
    }
    uint64_t rxBytes() const override { return line_.rxBytes(); }

    // The monitor resolves an endpoint string to a stream; the board holds the resolver
    // (DESIGN.md 7.7). Installed in main.cpp and tests/main.cpp.
    static void setResolver(BitSerialLine::EndpointResolver r);

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For tests, without going through the bus.
    bool           latch() const { return latch_; }
    BitSerialLine& line() { return line_; }

protected:
    bool      decodesPort(uint8_t off, bool write) const override;
    uint8_t   readPort(uint8_t off) override;
    void      writePort(uint8_t off, uint8_t v) override;
    long long fdcClockHz() const override { return 8000000; }
    bool      takes525() const override { return false; }
    void      resetRegisters() override;
    void      boardProperties(std::vector<Property>& p) override;

private:
    // ---- straps ----
    bool motor_ = false;  // `port3`: S1-3 makes the D7 latch MOTOR ON, not the serial line

    // ---- runtime (snapshot) ----
    bool latch_ = false;  // the port 3 D7 output latch; false = MARKING (reset)

    BitSerialLine line_;  // the far end of port 3 D7; snapshot after the chip
};

} // namespace altair
