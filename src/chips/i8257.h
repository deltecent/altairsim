#pragma once
//
// Intel 8257 -- Programmable DMA Controller. A CHIP, NOT A CARD, exactly as the
// FD1771 next door is (chips/wd17xx.h). The Tarbell double-density interface has one
// of these on it, wired to its FD1791, and any later card that turns up with an 8257
// on it gets this for free. Modeled from the Intel 8257 data sheet -- NOT from the one
// CP/M BIOS that happens to drive it, which is how you end up implementing the subset
// that BIOS uses and quietly getting the rest wrong.
//
// It knows nothing about S-100, and it knows nothing about the Tarbell's ports. The
// CARD decodes its register block onto some base (0xE0 on the Tarbell) and forwards
// IN/OUT here as an offset; the CARD is the thing that becomes a bus master and drives
// the cycles the chip's address/count registers describe. This chip holds REGISTER
// STATE AND TRANSFER BOOKKEEPING ONLY -- it never touches the bus or the FDC itself.
//
// ---------------------------------------------------------------------------
// THE REGISTER FILE (A3..A0, sixteen byte-wide ports)
//
//   offset 2n     channel n DMA address register       (n = 0..3)
//   offset 2n+1   channel n terminal-count register
//   offset 8      Mode Set register (write) / Status register (read)
//
// The address and count registers are SIXTEEN BITS loaded through ONE byte-wide port,
// low byte then high byte, sequenced by a single FIRST/LAST BYTE FLIP-FLOP shared by
// every one of them. A write to the Mode Set register resets that flip-flop -- which
// is exactly why the Tarbell BIOS does `XRA A ; OUT CMND` before it loads any of them
// (2abios48.asm, RWDMA): it is putting the flip-flop back to "next access is the low
// byte" so the OUT WCT0/WCT0/ADR0/ADR0 pairs land where it means them to.
//
// THE TERMINAL-COUNT REGISTER CARRIES THE R/W MODE in its top two bits (bits 15:14 of
// the 16-bit value), so the mode arrives with the HIGH byte:
//
//     00  verify (no memory cycle)
//     01  WRITE into memory   -- a DMA memory-write == device->memory == a disk READ
//     10  READ from memory    -- a DMA memory-read  == memory->device == a disk WRITE
//     11  illegal
//
// The Tarbell BIOS passes 0x40 as the high byte for a read (top bits 01) and 0x80 for a
// write (top bits 10); the low 14 bits are the transfer length minus one (0x7F = 128
// bytes). See writeToMemory().
// ---------------------------------------------------------------------------

#include <cstdint>

namespace altair {

class StateWriter;  // core/statefile.h -- SNAPSHOT/RESTORE
class StateReader;

class I8257 {
public:
    // ---- THE PORTS (A3..A0). The card decodes the base and hands us the offset. ----
    uint8_t readPort(uint8_t off);
    void    writePort(uint8_t off, uint8_t v);

    // ---- WHAT THE OWNING CARD'S BusMaster ASKS BETWEEN TRANSFERS ----
    //
    // On the real chip a peripheral's DRQ, ANDed with the channel's enable bit, is what
    // raises HRQ (our pHOLD). The board pulls pHOLD when channelEnabled() AND its FDC has
    // a byte pending; the chip does not see DRQ at all.

    // Is a channel enabled and not yet finished -- i.e. armed to respond to a DRQ? A
    // channel that hit terminal count under TC-STOP has cleared its own enable bit and
    // answers false. -1 from activeChannel() means "none".
    bool channelEnabled() const { return activeChannel() >= 0; }
    int  activeChannel()  const;

    uint16_t curAddr()      const;  // the active channel's current memory address
    bool     writeToMemory() const; // mode 01: device->memory (a disk read). false = disk write

    // ONE transfer happened on the bus: bump the address, count the byte down, and latch
    // terminal count (and, under TC-STOP, disable the channel) when the count underflows.
    // The board calls this once per byte it moves; it does not touch the registers itself.
    void advance();

    // Did the active channel just reach terminal count on the last advance()? (The board
    // watches channelEnabled() to end a burst; this is here for symmetry and for tests.)
    bool terminalCount() const { return lastTc_; }

    // Power-on / master reset: registers cleared, flip-flop to low, no channel enabled.
    void reset();

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    // A channel is its address register and its 16-bit terminal-count register. The count
    // register's low 14 bits count down; its top two bits are the R/W mode and do NOT
    // decrement -- so they are carried across every advance() untouched.
    struct Channel {
        uint16_t addr  = 0;
        uint16_t count = 0;  // bits 13:0 = remaining-1; bits 15:14 = mode
    };
    Channel ch_[4]{};

    // The Mode Set register: bits 3:0 enable channels 0..3; bit 4 rotating priority; bit 5
    // extended write; bit 6 TC-STOP (auto-disable a channel at terminal count); bit 7
    // AUTOLOAD. We honor the enables and TC-STOP -- the two the transfer path depends on.
    uint8_t mode_   = 0;

    // The Status register: bits 3:0 latch terminal count per channel, set at TC and
    // cleared by READING this register (real-chip behavior). Bit 4 is UPDATE FLAG.
    uint8_t status_ = 0;

    // The single first/last byte flip-flop, shared by every address and count register:
    // false = the next byte access is the LOW byte. Toggled on each such access; reset by
    // a write to the Mode Set register.
    bool flipHigh_ = false;

    bool lastTc_   = false;  // did the most recent advance() hit terminal count?
};

} // namespace altair
