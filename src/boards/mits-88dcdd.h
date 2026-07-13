#pragma once
//
// MITS 88-DCDD -- the Altair 8" floppy disk controller (docs/boards/mits-dcdd.md).
//
// The register model lives in HardSectorFdc (boards/mits-hardsector.h), which this card
// shares with the 88-MDS minidisk controller because MITS built that compatibility on
// purpose. What is left here is what is actually THIS card:
//
//   - the media it can take: an 8" floppy, and the FDC+'s 8 MB image;
//   - 360 RPM, and a byte every 32 us (250,000 bits/sec);
//   - a HEAD SOLENOID, which is the thing the minidisk does not have -- bit 2 loads it and
//     bit 3 unloads it, and bit 6 reduces the current through it;
//   - sixteen drives on the daisy chain.
//
// THE MINIDISK IS NOT HERE ANY MORE, and that is the point of the split. It used to be a
// row in this card's format table plus `if (d->fmt.sectors != 16)` in the head-unload
// handler -- an 8" controller asking the disk in the drive which controller it was. See
// docs/boards/mits-88mds.md, and src/boards/mits-88mds.h.

#include "boards/mits-hardsector.h"

namespace altair {

// Kept as the name the rest of the tree already uses.
inline constexpr int kDcddSectorBytes = kHsSectorBytes;

const std::vector<HsFormat>& dcddFormats();

class DcddBoard : public HardSectorFdc {
public:
    DcddBoard();

    std::string type() const override { return "dcdd"; }

protected:
    const std::vector<HsFormat>& formats() const override { return dcddFormats(); }

    int      maxDrives()  const override { return 16; }
    uint8_t  selectMask() const override { return 0x0F; }
    int      rpm()        const override { return 360; }

    // "ENWD goes true every 32 us" -- one byte per 32 us is 250,000 bits/sec, the 8"
    // single-density rate, and it is what BOOT.ASM's cycle-counted transfer loop is timed
    // against ("data at 64 and 128" T-states).
    uint64_t byteUs() const override { return 32; }

    // The READ CLEAR one-shot (IC F1, R5 10K / C3 .047 uF, nominal 140 us) holds the read
    // path cleared after the sector hole. "Read data will be available 140 us after SR0 goes
    // true."
    //
    // THE MANUAL CONTRADICTS ITSELF HERE and does not reconcile it: the prose says the first
    // byte lands at 140 us, while the READ/WRITE timing diagram dimensions the first NRDA at
    // 280 us. We take the prose, because it agrees with the one-shot the schematic actually
    // shows. Nothing observable rides on the choice -- the BIOS polls NRDA rather than
    // counting -- and either value leaves the 137 bytes room to finish inside the sector.
    uint64_t readStartUs() const override { return 140; }

    // "Write data will be requested 280 us after D0 goes true."
    uint64_t writeStartUs() const override { return 280; }

    void command(uint8_t v) override;

    // This card really does have a solenoid, and software really does drive it.
    bool headLoaded(const Drive& d) const override { return d.loaded; }
};

} // namespace altair
