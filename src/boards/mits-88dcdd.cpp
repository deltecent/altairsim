#include "boards/mits-88dcdd.h"

namespace altair {

// ---------------------------------------------------------------------------
// The media THIS card knows (DESIGN.md 7.3).
//
// Both rows come from BOOT.ASM's own equates -- NUMTRK / NUMSEC / DATATRK, under
// `if NOT MINIDSK` -- which is a period artifact written against the real drive, not a
// reconstruction.
//
// THE 8 MB ROW IS NOT A DIFFERENT CARD. It is the same 137-byte hard-sector format, 32
// sectors to a track, with 2048 tracks -- more than would fit on a physical platter, but the
// controller only steps the head and shifts bytes and cannot tell. It comes from the FDC+, a
// modern card that EMULATES this one and whose serial disk server serves images that big;
// its manual (reference/FDC+ Manual.pdf, 3.7.4) says it in one line: "The 8Mb drive looks
// like an Altair 8 inch drive with 2048 tracks instead of 77." So it is one more row here,
// not a mode.
//
// AND THE MINIDISK IS NOT A ROW HERE AT ALL, which is what this card got wrong for months.
// A 5.25" minidisk turns at 300 RPM, not 360, and clocks a byte every 64 us, not 32 -- so it
// is not a MEDIUM of this controller, it is a DIFFERENT CONTROLLER, and it now has one:
// src/boards/mits-88mds.cpp.
// ---------------------------------------------------------------------------
const std::vector<HsFormat>& dcddFormats() {
    static const std::vector<HsFormat> f = {
        {"8in",      77, 32, 6,   77ull * 32 * kHsSectorBytes},  //   337,568
        {"fdc8mb", 2048, 32, 6, 2048ull * 32 * kHsSectorBytes},  // 8,978,432
    };
    return f;
}

DcddBoard::DcddBoard() { drive_.resize((size_t)drives_); }

// ---------------------------------------------------------------------------
// base+1 OUT: the command bits. They are INDEPENDENT bits, not an opcode -- more than one
// can arrive in the same byte, and the BIOS does exactly that.
//
// This is the half of the card the 88-MDS does NOT share, and the head is why. Compare
// MdsBoard::command(): there, bit 2 restarts a motor timer and bits 3 and 6 do not exist.
// ---------------------------------------------------------------------------
void DcddBoard::command(uint8_t v) {
    Drive* d = selected();
    if (!d) return;

    // Any STEP invalidates the sector and byte position -- and the flush has to happen
    // first: the pending write belongs to the track we are about to leave.
    if (v & 0x03) {
        flushWrite();
        if (v & 0x01) {  // cSTEPI -- in, toward the spindle
            if (d->track < d->fmt.tracks - 1) d->track++;
        }
        if (v & 0x02) {  // cSTEPO -- out, toward track 0. Stops there.
            if (d->track > 0) d->track--;
        }
        invalidatePosition();
    }

    if (v & 0x04) {  // cHDLOAD -- energize the solenoid.
        d->loaded = true;
    }
    if (v & 0x08) {  // cHDUNLD -- drop it.
        flushWrite();
        d->loaded = false;
        invalidatePosition();
    }

    if (v & 0x10) st_.inten = true;   // decoded, never wired to pin 73 by default
    if (v & 0x20) st_.inten = false;
    // 0x40 cHCSON -- reduce head current. Decoded and ignored, exactly as software cannot
    // tell the difference on real hardware either.

    if (v & 0x80) armWrite();  // cWRTEN
}

} // namespace altair
