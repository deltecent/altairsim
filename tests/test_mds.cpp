// The 88-MDS, the Altair Minidisk (docs/boards/mits-88mds.md).
//
// THIS CARD EXISTS BECAUSE OF A BUG THAT NEVER FAILED, so this file is mostly a list of
// things that would never have failed either.
//
// The minidisk used to be one row in the 88-DCDD's format table, plus one line:
//
//     if (v & 0x08) {                        // cHDUNLD
//         if (d->fmt.sectors != 16) { ... }  // <-- an 8" card asking "am I a minidisk?"
//     }
//
// Nothing caught it, because nothing in the tree ever mounted a minidisk image. Meanwhile
// the card turned the platter at 360 RPM (a minidisk is 300), clocked a byte every 32 us (a
// minidisk is 64), and would have booted through a PROM that cannot read the medium. Every
// one of those is silent: the disk still reads, it just reads on a drive that never existed.
//
// So: the boot is the acceptance test (machines/minidisk.toml -> CP/M 2.2b), and it is the
// one that proves the card. THESE tests pin the numbers and the senses the boot cannot --
// the ones that fail on the disk you have not written yet, or on nobody's disk at all.
//
// No filesystem: MemoryMedia through setMediaResolver.

#include "boards/mits-88dcdd.h"
#include "boards/mits-88mds.h"
#include "core/clock.h"
#include "host/media.h"
#include "test.h"

#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

uint8_t in(MdsBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(MdsBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

void withDisk(uint64_t bytes, uint8_t fill = 0) {
    setMediaResolver([bytes, fill](const std::string& path, bool ro, std::string&) {
        return std::make_unique<MemoryMedia>(path, std::vector<uint8_t>((size_t)bytes, fill), ro);
    });
}

// At 2 MHz. 300 RPM = 200 ms/rev; 16 sectors -> 12.5 ms a sector -> 25,000 T.
constexpr uint64_t kPerSector = 25000;    // and the 8" card's is 10,416. THAT is the bug.
constexpr uint64_t kByte      = 128;      // 64 us -- 125 kbit/s, half the 8" rate
constexpr uint64_t kTrue      = 60;       // 30 us -- the same one-shot on both cards
constexpr uint64_t kReadStart = 1000;     // 500 us -- the READ CLEAR one-shot
constexpr uint64_t kSpinUp    = 2000000;  // 1 s   -- DRIVE MOTOR ON DELAY
constexpr uint64_t kDisable   = 12800000; // 6.4 s -- the 4020's 512th sector pulse
constexpr uint64_t kSettle    = 100000;   // 50 ms -- HEAD SETTLE

// The card as the BIOS finds it: drive 0 selected, motor up to speed. There is no head to
// load -- that is the whole point of this card -- so "ready" is a WAIT, not a command.
void ready(MdsBoard& b, Clock& c) {
    out(b, 0x08, 0x00);  // enable drive 0
    c.advance(kSpinUp);  // ...and wait a full second for the motor. p31. (Harmless when the
                         // motor is free: nothing is waiting on it.)
}

// `motor = "real"` -- OPT IN. The card ships free-running (see MdsBoard::properties), so
// every test below that cares about the motor has to ask for it, in the same words a machine
// file would use.
void realMotor(MdsBoard& b) {
    std::string err;
    if (!setProperty(b, "motor", "real", err)) CHECK(false, "motor = real should have set");
}

} // namespace

void test_mds() {
    SECTION("88-MDS -- the probe: 76,720, and the XMODEM pad that every real image has");
    {
        std::string err;
        auto probes = [&](uint64_t bytes) {
            Clock    c;
            MdsBoard b;
            b.attachClock(&c);
            withDisk(bytes);
            return b.mount("drive0", "a.dsk", false, err);
        };

        CHECK(probes(76720), "35 x 16 x 137 = 76,720 -- 35 tracks, 16 sectors, a 137-byte slot");
        CHECK(probes(76800), "AND SO IS 76,800: all four real images are this size.");
        CHECK(true, "  76,720 is NOT a multiple of 128 (it is 599.375 blocks), so XMODEM padded");
        CHECK(true, "  it up -- and this format needs sizeMatches() quite as much as the 8-inch.");
        CHECK(!probes(76720 + 128), "...but 128 over is a different disk, not a pad");

        CHECK(!probes(337568), "an 8-inch floppy does NOT go in a minidisk drive");
        CHECK(!probes(8978432), "and neither does an 8 MB FDC+ image");
    }

    SECTION("88-MDS -- 300 RPM, not 360. The 8-inch card's number was silently inherited");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        CHECK(b.mount("drive0", "a.dsk", false, err), "a minidisk mounts");
        ready(b, c);

        // The sector under the head is a READING TAKEN OFF THE CLOCK. Sector 0 at t=0.
        CHECK((in(b, 0x09) & 0x3E) >> 1 == 0, "sector 0 is under the head");
        c.advance(kPerSector);
        CHECK((in(b, 0x09) & 0x3E) >> 1 == 1, "12.5 ms later -- 25,000 T -- it is sector 1");
        CHECK(true, "  and on the 8-inch card that same 25,000 T would be nearly TWO AND A HALF");
        CHECK(true, "  sectors (10,416 T each). A minidisk in a dcdd turned 20% too fast.");

        c.advance(kPerSector * 15);
        CHECK((in(b, 0x09) & 0x3E) >> 1 == 0, "and 16 sectors round, it is back at 0 -- one rev");
    }

    SECTION("88-MDS -- SECTOR TRUE is 30 us, the one number both manuals agree on");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        CHECK((in(b, 0x09) & 0x01) == 0, "T is LOW when true, and it is true at the sector hole");
        c.advance(kTrue - 1);
        CHECK((in(b, 0x09) & 0x01) == 0, "...still true at 29 us");
        c.advance(2);
        CHECK((in(b, 0x09) & 0x01) == 1, "and FALSE at 30 us. 60 T-states out of 25,000 -- 0.24%");
    }

    SECTION("88-MDS -- a byte every 64 us. 125 kbit/s is HALF the 8-inch rate");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720, 0xA5);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        // NRDA is not a flag the guest can outrun. The read path is held cleared for 500 us
        // (the READ CLEAR one-shot -- the 8" card's is 140), and then a byte arrives every
        // 64 us.
        CHECK((in(b, 0x08) & 0x80) != 0, "NRDA is FALSE inside the 500 us read-clear window");
        c.advance(kReadStart);
        CHECK((in(b, 0x08) & 0x80) == 0, "...and TRUE once the first byte has passed the head");

        CHECK(in(b, 0x0A) == 0xA5, "take that byte");
        CHECK((in(b, 0x08) & 0x80) != 0, "and NRDA drops -- the next one is not under the head yet");
        c.advance(kByte);
        CHECK((in(b, 0x08) & 0x80) == 0, "64 us later it is. 128 T, where the 8-inch card is 64.");
    }

    SECTION("88-MDS -- THERE IS NO HEAD. OUT 9,08h is a no-op, and real software sends it");
    {
        // The 88-MDS manual's OWN SAMPLE CODE (pp. 27, 28) contains
        //
        //     MVI  A,8    ; UNLOAD HEAD
        //     OUT  9      ; SEND COMMAND
        //
        // on the bit p32 of the same manual calls "Not used", for a head p31 of the same
        // manual says is "always loaded when the Drive is enabled". Someone at MITS wrote the
        // minidisk driver by editing the 8" floppy driver and left the head-unload in.
        //
        // THAT IS THE EXACT MISTAKE THIS SIMULATOR MADE. So the bit must be inert -- and if
        // it ever unloads a head again, this fails.
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720, 0x5A);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        CHECK((in(b, 0x08) & 0x04) == 0, "HS is true: the head is loaded because the drive is on");

        out(b, 0x09, 0x08);  // "UNLOAD HEAD" -- straight out of the manual's own driver
        CHECK((in(b, 0x08) & 0x04) == 0, "and after cHDUNLD the head is STILL loaded. No solenoid.");
        c.advance(kReadStart);
        CHECK(in(b, 0x0A) == 0x5A, "and the card still reads -- the bit did nothing at all");

        out(b, 0x09, 0x40);  // "special current" -- also not a bit on this card
        CHECK((in(b, 0x08) & 0x04) == 0, "bit 6 is not a bit either");
    }

    SECTION("88-MDS -- both step bits at once is STEP OUT, and the 8-inch card disagrees");
    {
        // p67: "if D00 and D01 are both HIGH ... the STEP OUT direction will always be
        // selected due to the clearing action on the Step Direction Flip-Flop." The 88-DCDD
        // applies both and nets to zero. Two cards, two answers, and a shared `if` would have
        // buried it.
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        out(b, 0x09, 0x01);   // step in
        c.advance(kSettle);
        out(b, 0x09, 0x01);   // step in again -- track 2
        c.advance(kSettle);
        CHECK((in(b, 0x08) & 0x40) != 0, "two steps in: we are off track 0");

        out(b, 0x09, 0x03);   // BOTH bits
        c.advance(kSettle);
        out(b, 0x09, 0x03);   // BOTH bits again
        c.advance(kSettle);
        CHECK((in(b, 0x08) & 0x40) == 0, "both bits twice walked us back OUT to track 0");
    }

    SECTION("88-MDS -- the head stops at track 34, not 76");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        for (int i = 0; i < 100; ++i) {  // step in far past the end of the medium
            out(b, 0x09, 0x01);
            c.advance(kSettle);
        }
        for (int i = 0; i < 34; ++i) {  // and 34 steps back out must reach track 0
            out(b, 0x09, 0x02);
            c.advance(kSettle);
        }
        CHECK((in(b, 0x08) & 0x40) == 0, "35 tracks: the clamp is at 34, so 34 steps out is home");
    }

    SECTION("88-MDS -- FLAT OUT IS THE DEFAULT. The motor does not stop unless you ask it to");
    {
        // Patrick, 2026-07-13: the 6.4s timer and the 1s spin-up are an OPTION and not the
        // default, because the machine runs at full speed unless it is told otherwise. Same
        // call the Clock already made -- clock_hz = 0 free-runs, and 2 MHz is what you ASK
        // for.
        //
        // The card can still model both timers exactly, and does (every SECTION below this
        // one). It just does not make you live through them by default.
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);

        out(b, 0x08, 0x00);  // enable drive 0 -- and DO NOT wait
        CHECK(in(b, 0x09) != 0xFF, "the sectors are there IMMEDIATELY. No 1 second spin-up.");
        CHECK((in(b, 0x08) & 0x04) == 0, "and HS is already true -- the motor never stopped turning");

        c.advance(kDisable * 10);  // a full minute of neglect
        CHECK(in(b, 0x08) != 0xFF, "and 64 seconds later it is STILL alive. No disable timer.");
        CHECK(in(b, 0x09) != 0xFF, "the disk just keeps turning, which is what `free` means");

        // ...and the knob is real, and it is spelled the way a machine file spells it.
        realMotor(b);
        out(b, 0x08, 0x80);  // off
        out(b, 0x08, 0x00);  // and on again -- now it is a REAL motor, from cold
        CHECK(in(b, 0x09) == 0xFF, "motor = real, and suddenly the second matters");
        c.advance(kSpinUp);
        CHECK(in(b, 0x09) != 0xFF, "...and passes");
    }

    SECTION("88-MDS -- THE MOTOR STOPS. 6.4 seconds, and the card takes itself off the bus");
    {
        // The Disk Disable Timer is a 4020 clocked by the sector pulse: 12.5 ms x 512 = 6.4 s
        // EXACTLY. That is why the period BIOS issues cRESTMR before every single access --
        // and why a card without this models a machine nobody had.
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        realMotor(b);
        withDisk(76720, 0x77);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        CHECK(in(b, 0x08) != 0xFF, "the card is answering");

        c.advance(kDisable - kSpinUp - 10);
        CHECK(in(b, 0x08) != 0xFF, "...still answering at 6.4 s minus a hair");
        c.advance(20);
        CHECK(in(b, 0x08) == 0xFF, "and at 6.4 s the system TURNS ITSELF OFF. All ones.");
        CHECK(in(b, 0x09) == 0xFF, "the sector channel with it");
        CHECK(in(b, 0x0A) == 0xFF, "and the data channel");
    }

    SECTION("88-MDS -- TIMER RESET (bit 2) is what the whole minidisk driver is built around");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        realMotor(b);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        // Keep poking bit 2 and the motor never stops -- which is exactly what the BIOS does.
        for (int i = 0; i < 10; ++i) {
            c.advance(kDisable / 2);
            out(b, 0x09, 0x04);  // cRESTMR
        }
        c.advance(kDisable / 2);
        CHECK(in(b, 0x08) != 0xFF, "32 seconds of TIMER RESET and the card is still alive");

        // A step resets it too (p31: "STEP IN also resets the 6.4 second Disk Disable Timer").
        c.advance(kDisable - 10);
        out(b, 0x09, 0x01);
        c.advance(kDisable - 10);
        CHECK(in(b, 0x08) != 0xFF, "and a STEP restarts it as well -- not just bit 2");

        c.advance(20);
        CHECK(in(b, 0x08) == 0xFF, "but stop touching it and it does die");
    }

    SECTION("88-MDS -- 1 second to spin up, and the sector channel is dark until it does");
    {
        // p34: "The Sector position channel will be disabled (all "1"s) for 1 second after the
        // Drive is enabled." A card that answers immediately will happily run software that
        // could never have worked on the real thing -- the forgiving-window failure, again.
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        realMotor(b);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);

        out(b, 0x08, 0x00);  // enable drive 0
        CHECK(in(b, 0x09) == 0xFF, "the sector channel is DARK the instant the drive is enabled");
        CHECK((in(b, 0x08) & 0x04) != 0, "and HS is false -- the motor is not up to speed");

        c.advance(kSpinUp - 10);
        CHECK(in(b, 0x09) == 0xFF, "...still dark at 0.999 s");
        c.advance(20);
        CHECK(in(b, 0x09) != 0xFF, "and at 1 second the motor is stable and the sectors appear");
        CHECK((in(b, 0x08) & 0x04) == 0, "HS goes true. p31: 'Goes True one second after Disk Enable.'");
    }

    SECTION("88-MDS -- re-selecting a SPINNING drive must NOT re-arm the 1 second delay");
    {
        // The one-shot fires when the Disk Enable flip-flop TOGGLES (p58), not on every write
        // to the port. The period BIOS writes the select port on every single access
        // (`lda curDrv / out DRVSLCT`), so a card that re-armed here would stop dead for a
        // second on every read -- and CP/M would crawl or hang.
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        realMotor(b);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        for (int i = 0; i < 5; ++i) {
            out(b, 0x08, 0x00);  // re-select the drive that is already spinning
            CHECK(in(b, 0x09) != 0xFF, "the sectors keep coming -- the motor was never restarted");
        }

        // ...but after the timer HAS killed it, a select really does spin it up again.
        c.advance(kDisable + 10);
        CHECK(in(b, 0x08) == 0xFF, "the timer has run out");
        out(b, 0x08, 0x00);
        CHECK(in(b, 0x09) == 0xFF, "re-enabling a STOPPED drive DOES cost the full second");
        c.advance(kSpinUp);
        CHECK(in(b, 0x09) != 0xFF, "...and then it is back");
    }

    SECTION("88-MDS -- a step blinds the sector channel for 50 ms, and drops MOVE OK");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        CHECK((in(b, 0x08) & 0x02) == 0, "MH is true: you may step");

        out(b, 0x09, 0x01);  // STEP IN
        CHECK((in(b, 0x08) & 0x02) != 0, "and the instant you do, MH goes FALSE");
        CHECK(in(b, 0x09) == 0xFF, "and the sector channel goes dark with it (p34)");

        c.advance(kSettle - 10);
        CHECK((in(b, 0x08) & 0x02) != 0, "...for 50 ms");
        c.advance(20);
        CHECK((in(b, 0x08) & 0x02) == 0, "and then the head has settled");
        CHECK(in(b, 0x09) != 0xFF, "and the sectors come back");
    }

    SECTION("88-MDS -- an EMPTY drive reads all ones. There is no ready line to ask");
    {
        // p30: "all status bits are logic 1 when there is not a Minidiskette in the Drive."
        // The period CP/M BIOS knows this and carries a 1.4 s timeout in its sector hunt
        // "so that 5.25\" drives (which don't support 'ready' the same as 8\" drives) won't
        // hang when a disk is not inserted."
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);

        out(b, 0x08, 0x01);  // select drive 1 -- which is empty
        c.advance(kSpinUp);
        CHECK(in(b, 0x08) == 0xFF, "an empty drive is all ones, motor or no motor");
        CHECK(in(b, 0x09) == 0xFF, "and so is its sector channel");

        out(b, 0x08, 0x00);  // back to the drive that has a disk in it
        c.advance(kSpinUp);
        CHECK(in(b, 0x08) != 0xFF, "and drive 0 still works");
    }

    SECTION("88-MDS -- four drives, and only TWO bits of drive address");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        std::string err;

        CHECK(!b.mount("drive4", "a.dsk", false, err), "there is no drive 4 -- this card has four");

        // The 88-DCDD reads four bits of drive select and chains sixteen. This one reads two.
        // So bits 2..6 of the select byte are DON'T CARE, and writing them must still select
        // drive 0 rather than drive 12.
        withDisk(76720);
        b.mount("drive0", "a.dsk", false, err);
        out(b, 0x08, 0x7C);  // every don't-care bit set, drive bits clear
        c.advance(kSpinUp);
        CHECK(in(b, 0x08) != 0xFF, "0x7C still selects DRIVE 0 -- the top bits are not address");
    }

    SECTION("88-MDS -- bit 7 turns the whole system off, exactly as the manual says");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        realMotor(b);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);
        CHECK(in(b, 0x08) != 0xFF, "alive");

        out(b, 0x08, 0x80);  // "When set to 1, Minidisk system is turned off." p29.
        CHECK(in(b, 0x08) == 0xFF, "and off");

        out(b, 0x08, 0x00);
        c.advance(kSpinUp);
        CHECK(in(b, 0x08) != 0xFF, "and it spins up again from cold -- another full second");
    }

    SECTION("88-MDS -- sectors are numbered FROM ZERO, and a write commits 137 bytes");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b, c);

        // Land inside sector 0's write window and hand the card a full slot.
        c.advance(1000 * 2);  // 1 ms -- the WRITE CLEAR one-shot, where ENWD first goes true
        out(b, 0x09, 0x80);   // WRITE ENABLE
        for (int i = 0; i < 137; ++i) out(b, 0x0A, (uint8_t)(i + 1));

        // Read it straight back off track 0, sector 0.
        out(b, 0x08, 0x80);  // off
        out(b, 0x08, 0x00);  // and on again -- forces a re-read from the image
        c.advance(kSpinUp + kReadStart);
        CHECK(in(b, 0x0A) == 1, "byte 0 of sector 0 came back -- startSector is ZERO");
        CHECK(in(b, 0x0A) == 2, "and byte 1");
    }

    SECTION("88-MDS -- [[board.drive]] survives a CONFIG SAVE");
    {
        Clock    c;
        MdsBoard b;
        b.attachClock(&c);
        withDisk(76720);

        std::string err;
        CHECK(b.loadSubUnit("drive", {{"unit", "0"}, {"mount", "m.dsk"}, {"media", "minidisk"}}, err),
              "a [[board.drive]] with media = \"minidisk\" loads -- on THIS card");

        auto su = b.subUnits();
        CHECK(su.size() == 1, "an empty drive writes NO table");
        CHECK(su[0].table == "drive", "and the one it does write is a `drive`");

        MdsBoard b2;
        b2.attachClock(&c);
        KeyValues kv;
        for (const auto& f : su[0].fields) kv.push_back({f.key, f.text});
        CHECK(b2.loadSubUnit("drive", kv, err), "and it loads straight back into a fresh board");
        CHECK(b2.units()[0].state == "m.dsk", "with the same disk in the same drive");

        // And the media names do not cross the card boundary in EITHER direction.
        std::string e2;
        CHECK(!b2.loadSubUnit("drive", {{"unit", "1"}, {"media", "8in"}}, e2),
              "`media = \"8in\"` on an mds is an error -- an 8-inch floppy is a dcdd's disk");

        setMediaResolver(openHostFile);  // put the real filesystem back
    }
}
