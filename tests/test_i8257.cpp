#include "test.h"

#include "chips/i8257.h"
#include "core/statefile.h"

using namespace altair;

namespace {

// The Tarbell DD register block base is 0xE0, so ADR0 = base+0, WCT0 = base+1, and the
// command/status register (Mode Set) = base+8. The offsets the card forwards to the chip
// are those minus the base -- 0, 1 and 8 -- which is what these tests speak in.
constexpr uint8_t ADR0 = 0;  // channel 0 address register
constexpr uint8_t WCT0 = 1;  // channel 0 terminal-count register
constexpr uint8_t CMND = 8;  // Mode Set (write) / Status (read)

// Program channel 0 exactly as 2abios48.asm's RWDMA does: reset the flip-flop, load the
// count low/high (high byte carries the R/W mode), load the address low/high, then enable
// channel 0. `hi` is 0x40 for a read (mode 01) or 0x80 for a write (mode 10); `lenLess1`
// is the transfer length minus one, exactly the `DCR A` the BIOS does to the low byte.
void program(I8257& d, uint8_t hi, uint8_t lenLess1, uint16_t addr) {
    d.writePort(CMND, 0x00);                 // XRA A ; OUT CMND -- reset the flip-flop
    d.writePort(WCT0, lenLess1);             // count low
    d.writePort(WCT0, hi);                   // count high + mode
    d.writePort(ADR0, (uint8_t)(addr));      // address low
    d.writePort(ADR0, (uint8_t)(addr >> 8)); // address high
    d.writePort(CMND, 0x41);                 // enable channel 0, TC-stop
}

} // namespace

void test_i8257() {
    SECTION("8257 -- the first/last flip-flop and the R/W mode bits decode the BIOS's writes");

    {
        I8257 d;
        d.reset();
        // A 128-byte READ (mode 01) to 0x2C00, programmed the BIOS way.
        program(d, 0x40, 0x7F, 0x2C00);

        CHECK(d.channelEnabled(), "channel 0 is armed after OUT CMND 41H");
        CHECK(d.activeChannel() == 0, "and it is the active channel (fixed priority)");
        CHECK(d.curAddr() == 0x2C00, "the address loaded low-then-high through one port");
        CHECK(d.writeToMemory(), "high byte 0x40 -> mode 01 -> write into memory (a disk read)");
    }

    {
        I8257 d;
        d.reset();
        // The same, but a WRITE (mode 10): high byte 0x80.
        program(d, 0x80, 0x7F, 0x1234);
        CHECK(d.curAddr() == 0x1234, "address round-tripped");
        CHECK(!d.writeToMemory(), "high byte 0x80 -> mode 10 -> read from memory (a disk write)");
    }

    SECTION("8257 -- a write to the Mode Set register resets the flip-flop");

    {
        // If the flip-flop did NOT reset on OUT CMND, the low/high pairing would be off by
        // one and the address would come out byte-swapped. Prove the reset by leaving the
        // flip-flop in the HIGH state first (one stray write) and then programming normally.
        I8257 d;
        d.reset();
        d.writePort(ADR0, 0x99);   // one stray byte -> flip-flop now expects the HIGH byte
        program(d, 0x40, 0x7F, 0xABCD);  // starts with OUT CMND, which must reset it
        CHECK(d.curAddr() == 0xABCD, "OUT CMND put the flip-flop back to low-byte-first");
    }

    SECTION("8257 -- advance() counts down and terminal count disables the channel");

    {
        I8257 d;
        d.reset();
        program(d, 0x40, 0x7F, 0x3000);  // 128 transfers, count = 0x7F

        uint16_t addr = 0x3000;
        for (int i = 0; i < 128; ++i) {
            CHECK(d.channelEnabled(), "the channel stays armed for every one of the 128 bytes");
            CHECK(d.curAddr() == addr, "the address walks up one per transfer");
            const bool last = (i == 127);
            d.advance();
            CHECK(d.terminalCount() == last, "terminal count latches on the 128th byte, not before");
            ++addr;
        }
        CHECK(!d.channelEnabled(), "TC-stop disabled the channel: the burst is over");
        CHECK(d.activeChannel() < 0, "no channel is active any more");
    }

    SECTION("8257 -- serialize round-trips the whole register file mid-transfer");

    {
        I8257 d;
        d.reset();
        program(d, 0x80, 0x7F, 0x4400);
        d.advance();  // move one byte, so addr/count/flip-flop are all off their reset values
        d.advance();

        StateWriter w;
        d.serialize(w);

        I8257 e;
        StateReader r(w.data());
        e.deserialize(r);

        CHECK(e.curAddr() == d.curAddr(), "the current address survived the round trip");
        CHECK(e.writeToMemory() == d.writeToMemory(), "and the R/W mode");
        CHECK(e.channelEnabled() == d.channelEnabled(), "and the enable state");
        // The restored chip must finish the transfer with the same count remaining: 126
        // more advances after two, so the 126th ends it.
        for (int i = 0; i < 126; ++i) {
            CHECK(e.channelEnabled(), "restored channel keeps its remaining count");
            e.advance();
        }
        CHECK(!e.channelEnabled(), "and reaches terminal count exactly where the original would");
    }
}
