// Jade Double D (docs/boards/jade-doubled.md): the host side, the board Z80 and how its
// time follows the host's, and the FD1791, the local ports and the drives behind it.
//
// Every test drives the board the way the host does: through its port and its 1 K window,
// with real bus cycles. The programs the board Z80 runs are hand-assembled here and loaded
// through the window, which is how DDBOOT loads its injection module.

#include "boards/jade-dd.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/machine.h"
#include "host/cardimg.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr uint8_t  PORT = 0x43;
constexpr uint16_t WIN  = 0xE000;

// The control values DDBOOT and DDBIOS write (reference §2.3).
constexpr uint8_t MRQ = 0x01;  // take the RAM, bank 0
constexpr uint8_t MB1 = 0x03;  // take the RAM, bank 1
constexpr uint8_t MRT = 0x00;  // give it back
constexpr uint8_t EXC = 0x02;  // give it back and interrupt the Z80
constexpr uint8_t BGN = 0x80;  // give it back and reset the Z80

struct Rig {
    Machine      m;
    JadeDdBoard* dd = nullptr;

    Rig() {
        std::string err;
        dd = dynamic_cast<JadeDdBoard*>(m.add("jadedd", "dd", err));
        m.bus.setVerify(true);
        m.power();
    }
    uint8_t status() { return m.bus.ioRead(PORT); }
    void    control(uint8_t v) { m.bus.ioWrite(PORT, v); }
    bool    halted() { return (status() & 0x01) == 0; }

    // Load bank 0 at `at` through the window, then start the Z80 at 0000h.
    void boot(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        control(MRQ);
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite((uint16_t)(WIN + a++), b);
    }
    void load(std::initializer_list<uint8_t> code, uint16_t at) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite((uint16_t)(WIN + a++), b);
    }
    void run(uint64_t hostT) { m.clock.advance(hostT); }
    // The host's time passes, then it reads the status port, which catches the board up.
    void go(uint64_t hostT) {
        run(hostT);
        status();
    }
    // Load a whole program into bank 0 and start it with OUT 80h.
    void start(const std::vector<uint8_t>& code) {
        control(MRQ);
        for (size_t i = 0; i < code.size(); ++i) m.bus.memWrite((uint16_t)(WIN + i), code[i]);
        control(BGN);
    }
    uint8_t  peekRam(uint16_t a) { return dd->ram(a); }
    uint16_t hl() {  // the board Z80's HL, through its debugger registers
        for (RegDef& d : dd->z80().registers())
            if (d.name == "HL") return (uint16_t)d.get();
        return 0;
    }
    uint16_t word(uint16_t a) { return (uint16_t)(dd->ram(a) | (dd->ram(a + 1) << 8)); }
    static constexpr uint8_t kGap = 0x76;  // HALT: the filler between a program's pieces
};

// A disk whose byte i holds (i + i/128) & 0xFF, so each 128-byte sector reads differently.
// The last medium the resolver opened stays reachable, to check what a write left on it.
MemoryMedia* gMedia = nullptr;
uint8_t      rampByte(uint64_t i) { return (uint8_t)((i + i / 128) & 0xFF); }
void withRampDisk(uint64_t bytes) {
    setMediaResolver([bytes](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> d((size_t)bytes);
        for (size_t i = 0; i < d.size(); ++i) d[i] = rampByte(i);
        auto mm = std::make_unique<MemoryMedia>(path, std::move(d), ro);
        gMedia  = mm.get();
        return mm;
    });
}

constexpr uint64_t SD_TRACK = 26 * 128;
constexpr uint64_t DD_TRACK = 50 * 128;
constexpr uint64_t DD_DISK  = SD_TRACK + 76 * DD_TRACK;            // 489,728
constexpr uint64_t SD_DISK  = SD_TRACK + DD_TRACK + 75 * SD_TRACK;  // 259,328

// The board command latch (OUT 00h).
constexpr uint8_t DSE  = 0x04;
constexpr uint8_t DDEN = 0x10;
constexpr uint8_t SID1 = 0x20;

// Append bytes to a program one at a time. GCC 15 at -O2 reports a false -Warray-bounds on
// vector::insert of a brace list into a vector that has just been built, and CI builds with
// -Werror.
void emit(std::vector<uint8_t>& p, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) p.push_back(b);
}

// The NMI handler, as DCM2's WD.INT at 0066h: read the FD1791 status, un-invert it, keep it at
// 0100h, and return to the address in IY (EX (SP),IY ; RETN).
void nmiHandler(std::vector<uint8_t>& p) {
    if (p.size() < 0x70) p.resize(0x70, Rig::kGap);
    const uint8_t h[] = {0xDB, 0x04, 0xA9, 0x32, 0x00, 0x01, 0xFD, 0xE3, 0xED, 0x45};
    for (size_t i = 0; i < sizeof h; ++i) p[0x66 + i] = h[i];
}

// A sector transfer as DCM2's RD.SEC and WR.SEC do it: select, load the track and sector
// registers, point IY at the end, issue the command, then move one byte per IN 80h until the
// FD1791's INTRQ (NMI*) ends the loop. Buffer at 0200h. At the end, HL goes to 0102h and the
// Z80 halts at 0033h. Every FD1791 byte goes through XOR C (C = FFh), as DCM2's do.
std::vector<uint8_t> transfer(bool write, uint8_t ctl, uint8_t track, uint8_t sector) {
    std::vector<uint8_t> p = {
        0x31, 0x00, 0x04,                     // 0000 LD SP,0400h
        0x0E, 0xFF,                           // 0003 LD C,FFh
        0x3E, ctl,  0xD3, 0x00,               // 0005 LD A,ctl ; OUT (00h),A
        0x3E, (uint8_t)~track,  0xD3, 0x05,   // 0009 the track register
        0x3E, (uint8_t)~sector, 0xD3, 0x06,   // 000D the sector register
        0xFD, 0x21, 0x30, 0x00,               // 0011 LD IY,0030h
        0x21, 0x00, 0x02,                     // 0015 LD HL,0200h
        0x3E, (uint8_t)~(write ? 0xA0 : 0x80), 0xD3, 0x04,  // 0018 Write/Read Sector
        0xDB, 0x80,                           // 001C IN A,(80h): wait for DRQ or INTRQ
    };
    if (write) emit(p, {0x7E, 0xA9, 0xD3, 0x07});  // LD A,(HL) ; XOR C ; OUT (07h),A
    else       emit(p, {0xDB, 0x07, 0xA9, 0x77});  // IN A,(07h) ; XOR C ; LD (HL),A
    emit(p, {0x23, 0x18, 0xF7});    // 0022 INC HL ; JR 001Ch
    p.resize(0x30, Rig::kGap);
    emit(p, {0x22, 0x02, 0x01, 0x76});  // 0030 LD (0102h),HL ; HALT
    nmiHandler(p);
    return p;
}

// Step the selected drive `n` times in one direction (IN 08h), then halt.
std::vector<uint8_t> steps(uint8_t ctl, int n) {
    std::vector<uint8_t> p = {0x3E, ctl, 0xD3, 0x00};
    for (int i = 0; i < n; ++i) emit(p, {0xDB, 0x08});
    p.push_back(0x76);
    return p;
}

constexpr uint64_t SECOND = 2000000;  // host T-states in one second at the default 2 MHz

}  // namespace

void test_jadedd() {
    SECTION("jadedd: reset gives the RAM to the host, with ZINT set (bank 1)");
    {
        Rig r;
        CHECK(r.dd != nullptr, "the registry builds a jadedd");
        CHECK(r.dd->hostHasRam(), "after BR* the host has the RAM");
        // ZHLT* = 1 (the Z80 is not halted, it is stopped on BUSRQ*); SA10-12 = 0 (E000);
        // D4-D7 are not driven.
        CHECK(r.status() == 0xF1, "status after reset is F1");
        CHECK(r.dd->statusLines().at(0).find("stopped  RAM held by the host") != std::string::npos,
              "SHOW says the board Z80 is stopped while the host has the RAM");
        r.m.bus.memWrite(WIN, 0xA5);
        CHECK(r.dd->ram(0x400) == 0xA5, "BR* leaves ZINT set: the window shows bank 1");
        r.control(MRQ);
        r.m.bus.memWrite(WIN + 0x3FF, 0x5A);
        CHECK(r.dd->ram(0x3FF) == 0x5A, "OUT 01h: the window shows bank 0");
        r.control(MB1);
        CHECK(r.m.bus.memRead(WIN) == 0xA5, "OUT 03h: bank 1 again");
        r.control(MRT);
        CHECK(!r.dd->hostHasRam(), "OUT 00h gives the RAM back");
        CHECK(r.m.bus.memRead(WIN) == 0xFF, "a released window does not decode");
        uint8_t v = 0;
        CHECK(!r.dd->peek(WIN, v), "...and a monitor peek does not see it");
        r.m.reset(Reset::Bus);
        CHECK(r.dd->hostHasRam(), "a bus reset gives the RAM to the host again");
        CHECK(r.m.bus.memRead(WIN) == 0xA5, "...with bank 1 in the window");
    }

    SECTION("jadedd: the window follows the M10-M12 switches, and the status port shows them");
    {
        Rig r;
        std::string err;
        CHECK(!setProperty(*r.dd, "window", "E200", err), "window refuses a non-1K boundary");
        CHECK(setProperty(*r.dd, "window", "EC00", err), "window = EC00");
        r.control(MRQ);
        r.m.bus.memWrite(0xEC10, 0x42);
        CHECK(r.dd->ram(0x10) == 0x42, "the window moved to EC00");
        CHECK(r.m.bus.memRead(WIN) == 0xFF, "...and left E000");
        // DDBOOT: IN 43h / ANI 0Eh / RLC / ORI 0E0h gives the window's high byte.
        uint8_t hi = (uint8_t)((((r.status() & 0x0E) << 1) | 0xE0));
        CHECK(hi == 0xEC, "DDBOOT's arithmetic finds the window at EC00");
        CHECK(!setProperty(*r.dd, "port", "44", err), "port refuses 44");
        CHECK(setProperty(*r.dd, "port", "40", err), "port = 40");
        CHECK(r.m.bus.ioRead(0x43) == 0xFF, "43 no longer answers");
        CHECK((r.m.bus.ioRead(0x40) & 0xF0) == 0xF0, "40 does");
        CHECK(!setProperty(*r.dd, "interrupt", "int", err), "no pINT strap on this board");
        CHECK(setProperty(*r.dd, "interrupt", "vi3", err), "interrupt = vi3");
    }

    SECTION("jadedd: OUT 80h releases the RAM and resets the Z80, which runs to HLT");
    {
        Rig r;
        // LD A,55h ; LD (0100h),A ; HALT
        r.boot({0x3E, 0x55, 0x32, 0x00, 0x01, 0x76});
        r.control(BGN);
        CHECK(!r.dd->hostHasRam(), "the board has its RAM");
        CHECK(!r.dd->z80().halted(), "the board Z80 has not run yet: nothing asked");
        r.run(100);
        CHECK(r.halted(), "the host's status read caught the board up; ZHLT* = 0");
        r.control(MRQ);
        CHECK(r.m.bus.memRead(WIN + 0x100) == 0x55, "the board Z80's store is in the window");
        CHECK(r.dd->ram(0x900) == 0x55 && r.dd->ram(0x100) == 0x55,
              "2 K repeats in the Z80's 64 K (0900h is 0100h)");
    }

    SECTION("jadedd: board time is host time x 4 MHz / host hz(), and follows SET clock_hz");
    {
        Rig r;
        r.boot({0x18, 0xFE});  // JR $
        r.control(BGN);
        uint64_t b0 = r.dd->boardClock().now();
        r.run(1000);
        r.status();
        uint64_t b1 = r.dd->boardClock().now();
        CHECK(b1 - b0 >= 2000 && b1 - b0 < 2012, "1000 host T at 2 MHz = 2000 board T at 4 MHz");
        r.m.clock.setHz(4000000);
        r.run(1000);
        r.status();
        uint64_t b2 = r.dd->boardClock().now();
        CHECK(b2 - b1 >= 988 && b2 - b1 < 1012, "at 4 MHz the host and the board keep step");
        r.m.clock.setHz(0);  // flat out: hz() is 2 MHz again, as a divisor
        r.run(1000);
        r.status();
        uint64_t b3 = r.dd->boardClock().now();
        CHECK(b3 - b2 >= 1988 && b3 - b2 < 2012, "flat out still counts at the 2 MHz divisor");
        // The split: 1000 host T at 2 MHz, then a change, then 1000 at 1 MHz, read once.
        r.m.clock.setHz(2000000);
        r.status();
        uint64_t c0 = r.dd->boardClock().now();
        r.run(1000);
        r.m.clock.setHz(1000000);
        r.run(1000);
        r.status();
        uint64_t c1 = r.dd->boardClock().now();
        CHECK(c1 - c0 >= 5988 && c1 - c0 < 6012, "a rate change between reads splits at it");
        CHECK(r.dd->z80().pc() <= 1, "the Z80 is still in its loop");
    }

    SECTION("jadedd: OUT 02h interrupts the Z80 (IM 1, 0038h), and IN 20h clears ZINT");
    {
        Rig r;
        r.boot({0xED, 0x56, 0xFB, 0x76});  // IM 1 ; EI ; HALT
        // 0038h: IN A,(00h) ; LD (0102h),A ; IN A,(20h) ; IN A,(00h) ; LD (0103h),A ; HALT
        r.load({0xDB, 0x00, 0x32, 0x02, 0x01, 0xDB, 0x20, 0xDB, 0x00, 0x32, 0x03, 0x01, 0x76},
               0x38);
        r.control(BGN);
        r.run(100);
        CHECK(r.halted(), "the board waits in HLT");
        CHECK(r.dd->z80().pc() == 0x0004, "...after the HLT at 0003h");
        r.control(EXC);
        r.run(200);
        CHECK(r.halted(), "the handler ran and halted");
        CHECK(r.dd->z80().pc() == 0x0045, "...at the HLT at 0044h");
        r.control(MRQ);
        CHECK((r.m.bus.memRead(WIN + 0x102) & 0x08) != 0, "board status showed ZINT");
        CHECK((r.m.bus.memRead(WIN + 0x103) & 0x08) == 0, "IN 20h cleared it");
        CHECK((r.m.bus.memRead(WIN + 0x102) & 0x01) == 0, "U0 reads closed (an FD1791)");
    }

    SECTION("jadedd: a halted board queues nothing; the VI strap catches up on its own");
    {
        Rig r;
        r.boot({0x76});  // HALT
        r.control(BGN);
        CHECK(r.m.clock.queued() == 0, "no strap: nothing on the host clock while it runs");
        r.run(100);
        CHECK(r.halted(), "halted");
        CHECK(r.m.clock.queued() == 0, "halted: nothing on the host clock");

        Rig v;
        std::string err;
        setProperty(*v.dd, "interrupt", "vi3", err);
        // LD B,0 ; DJNZ $ (256 x 13 T = 3,328 board T) ; HALT
        v.boot({0x06, 0x00, 0x10, 0xFE, 0x76});
        v.control(BGN);
        CHECK(v.m.clock.queued() == 1, "vi3 and running: one host deadline");
        CHECK(v.m.bus.viLines() == 0, "VI3 is not pulled while the Z80 runs");
        for (int i = 0; i < 4; ++i) v.run(2000);  // 4 ms of host time, nobody asks
        CHECK(v.dd->z80().halted(), "the deadline caught the board up");
        CHECK(v.m.bus.viLines() == 0x08, "VI3 is pulled: halted, RAM released, no ZINT");
        CHECK(v.m.clock.queued() == 0, "...and the deadline is gone");
        v.control(MRQ);
        CHECK(v.m.bus.viLines() == 0, "SLVRQ ends the request");
    }

    SECTION("jadedd: the FD1791 bus is inverted, and the board passes the complement");
    {
        Rig r;
        // LD A,EDh ; OUT (05h),A ; IN A,(05h) ; LD (0100h),A ;
        // LD A,FEh ; OUT (06h),A ; IN A,(06h) ; LD (0101h),A ; HALT
        r.start({0x3E, 0xED, 0xD3, 0x05, 0xDB, 0x05, 0x32, 0x00, 0x01,
                 0x3E, 0xFE, 0xD3, 0x06, 0xDB, 0x06, 0x32, 0x01, 0x01, 0x76});
        r.go(100);
        CHECK(r.halted(), "halted");
        CHECK(r.dd->fdc().readTrackReg() == 0x12, "OUT EDh to the track register loads 12h");
        CHECK(r.dd->fdc().readSectorReg() == 0x01, "OUT FEh to the sector register loads 01h");
        CHECK(r.peekRam(0x100) == 0xED && r.peekRam(0x101) == 0xFE,
              "...and the Z80 reads back what it wrote: the complement both ways");
    }

    SECTION("jadedd: a sector read is an IN 80h loop that NMI ends (DCM2's RD.SEC)");
    {
        withRampDisk(DD_DISK);
        Rig r;
        std::string err;
        CHECK(r.dd->mount("drive0", "dd.dsk", false, err), "a 489,728-byte disk mounts");
        r.start(transfer(false, DSE, 0, 1));  // track 0 is single density
        r.go(SECOND);
        CHECK(r.halted(), "the board Z80 finished and halted");
        CHECK(r.dd->z80().pc() == 0x0034, "...through the NMI handler and RETN to IY (0030h)");
        CHECK(r.peekRam(0x100) == 0x00, "FD1791 status: no error");
        CHECK(r.word(0x102) == 0x0280, "NMI ended the loop after 128 bytes, not one more");
        bool same = true;
        for (int i = 0; i < 128; ++i) same = same && r.peekRam((uint16_t)(0x200 + i)) == rampByte(i);
        CHECK(same, "the buffer holds track 0, sector 1");
    }

    SECTION("jadedd: a sector write (DCM2's WR.SEC) lands on the disk");
    {
        withRampDisk(DD_DISK);
        Rig r;
        std::string err;
        r.dd->mount("drive0", "dd.dsk", false, err);
        r.control(MRQ);
        for (int i = 0; i < 128; ++i) r.m.bus.memWrite((uint16_t)(WIN + 0x200 + i), (uint8_t)(0xC0 ^ i));
        std::vector<uint8_t> p = transfer(true, DSE, 0, 2);
        for (size_t i = 0; i < p.size(); ++i) r.m.bus.memWrite((uint16_t)(WIN + i), p[i]);
        r.control(BGN);
        r.go(SECOND);
        CHECK(r.halted(), "the write finished");
        CHECK(r.peekRam(0x100) == 0x00, "FD1791 status: no error");
        CHECK(r.word(0x102) == 0x0280, "128 bytes went out");
        bool same = true;
        for (int i = 0; i < 128; ++i) same = same && gMedia->bytes()[128 + (size_t)i] == (uint8_t)(0xC0 ^ i);
        CHECK(same, "the image holds them at track 0, sector 2 (byte 128)");
        CHECK(gMedia->bytes()[256] == rampByte(256), "...and sector 3 is untouched");
    }

    SECTION("jadedd: NMI* is the FD1791 INTRQ gated by DSE (CR18)");
    {
        // LD A,ctl ; OUT (00h),A ; LD A,27h ; OUT (04h),A (D8h, the immediate interrupt) ;
        // HALT ; then at 0066h: LD A,1 ; LD (0101h),A ; HALT.
        auto prog = [](uint8_t ctl, bool enableAfter) {
            std::vector<uint8_t> p = {0x3E, ctl, 0xD3, 0x00, 0x3E, 0x27, 0xD3, 0x04};
            if (enableAfter) emit(p, {0x3E, DSE, 0xD3, 0x00});
            p.push_back(0x76);
            p.resize(0x66, Rig::kGap);
            emit(p, {0x3E, 0x01, 0x32, 0x01, 0x01, 0x76});
            return p;
        };
        Rig a;
        a.start(prog(0x00, false));
        a.go(1000);
        CHECK(a.dd->fdc().intrq(), "the FD1791 raised INTRQ");
        CHECK(a.halted() && a.dd->z80().pc() == 0x0009, "DSE = 0: no NMI, the Z80 stays in HLT");
        CHECK(a.peekRam(0x101) == 0x00, "...and the handler never ran");

        Rig b;
        b.start(prog(DSE, false));
        b.go(1000);
        CHECK(b.peekRam(0x101) == 0x01 && b.dd->z80().pc() == 0x006C, "DSE = 1: NMI to 0066h");

        Rig c;
        c.start(prog(0x00, true));
        c.go(1000);
        CHECK(c.peekRam(0x101) == 0x01, "setting DSE with INTRQ up releases the clamp: NMI");
    }

    SECTION("jadedd: IN 08h steps the selected drive, and SID1 is the direction");
    {
        withRampDisk(DD_DISK);
        Rig r;
        std::string err;
        r.dd->mount("drive0", "dd.dsk", false, err);
        r.dd->mount("drive1", "dd.dsk", false, err);
        r.start(steps(DSE | SID1, 3));
        r.go(1000);
        CHECK(r.dd->headTrack(0) == 3, "three steps in (SID1 = 1)");
        r.start(steps(DSE, 1));
        r.go(1000);
        CHECK(r.dd->headTrack(0) == 2, "one step out (SID1 = 0)");
        r.start(steps(SID1, 2));
        r.go(1000);
        CHECK(r.dd->headTrack(0) == 2, "DSE = 0: no drive is selected, and nothing moves");
        r.start(steps(DSE | SID1 | 0x01, 1));
        r.go(1000);
        CHECK(r.dd->headTrack(1) == 1 && r.dd->headTrack(0) == 2, "DSA = 1 steps drive 1 only");
    }

    SECTION("jadedd: DDEN picks the density, and each layout's tracks read at theirs");
    {
        withRampDisk(DD_DISK);
        Rig r;
        std::string err;
        r.dd->mount("drive0", "dd.dsk", false, err);
        r.start(steps(DSE | SID1, 1));
        r.go(1000);
        r.start(transfer(false, DSE | DDEN, 1, 50));
        r.go(SECOND);
        CHECK(r.halted() && r.peekRam(0x100) == 0x00, "DD disk: track 1, sector 50 at DDEN = 1");
        uint64_t at = SD_TRACK + 49 * 128;
        CHECK(r.peekRam(0x200) == rampByte(at) && r.peekRam(0x27F) == rampByte(at + 127),
              "...holds byte 3,328 + 49 x 128 of the image");
        r.start(transfer(false, DSE, 1, 1));
        r.go(2 * SECOND);
        CHECK(r.halted() && (r.peekRam(0x100) & 0x10) != 0,
              "the same track at DDEN = 0: Record Not Found (the density check)");

        withRampDisk(SD_DISK);
        Rig s;
        CHECK(s.dd->mount("drive0", "sd.dsk", false, err), "a 259,328-byte disk mounts");
        s.start(steps(DSE | SID1, 1));
        s.go(1000);
        s.start(transfer(false, DSE | DDEN, 1, 50));
        s.go(SECOND);
        CHECK(s.peekRam(0x100) == 0x00 && s.peekRam(0x200) == rampByte(SD_TRACK + 49 * 128),
              "SD disk: track 1 is the double-density system track");
        s.start(steps(DSE | SID1, 1));
        s.go(1000);
        s.start(transfer(false, DSE, 2, 26));
        s.go(SECOND);
        at = SD_TRACK + DD_TRACK + 25 * 128;
        CHECK(s.peekRam(0x100) == 0x00 && s.peekRam(0x200) == rampByte(at),
              "...and track 2 is single density again (sector 26)");
    }

    SECTION("jadedd: mounting refuses a size that is no Jade disk");
    {
        std::string err;
        withRampDisk(300000);
        Rig r;
        CHECK(!r.dd->mount("drive0", "x.dsk", false, err), "300,000 bytes does not mount");
        CHECK(err.find("489,728") != std::string::npos, "...and the error names the sizes");
        withRampDisk(77 * SD_TRACK);
        CHECK(r.dd->mount("drive0", "ibm.dsk", false, err), "a 256,256-byte IBM 3740 disk mounts");
        withRampDisk(0);
        CHECK(r.dd->mount("drive1", "blank.dsk", false, err), "an empty file mounts, unformatted");
        CHECK(!r.dd->mount("drive4", "blank.dsk", false, err), "there is no drive4");
    }

    SECTION("jadedd: board status: the timer (TOFF), TWO SIDED* and CHNG*");
    {
        withRampDisk(DD_DISK);
        Rig r;
        std::string err;
        r.dd->mount("drive0", "dd.dsk", false, err);
        // LD A,04h ; OUT (00h),A ; IN A,(00h) ; LD (0100h),A ; IN A,(40h) ;
        // IN A,(00h) ; LD (0101h),A ; HALT
        r.start({0x3E, DSE, 0xD3, 0x00, 0xDB, 0x00, 0x32, 0x00, 0x01, 0xDB, 0x40,
                 0xDB, 0x00, 0x32, 0x01, 0x01, 0x76});
        r.go(1000);
        CHECK((r.peekRam(0x100) & 0x20) != 0, "after power-on the timer is off (TOFF = 1)");
        CHECK((r.peekRam(0x101) & 0x20) == 0, "IN 40h starts it (TOFF = 0)");
        CHECK((r.peekRam(0x100) & 0x40) != 0, "TWO SIDED* is high: a one-sided image");
        CHECK((r.peekRam(0x100) & 0x80) != 0, "CHNG* reads high: no disk change");
        CHECK(r.dd->timerPeriod() == 16694400, "0.37 x 240 K x 47 uF = 4.1736 s at 4 MHz");

        // IN A,(00h) ; LD (0100h),A ; HALT -- 4 s later, and again after 4.2 s.
        r.start({0xDB, 0x00, 0x32, 0x00, 0x01, 0x76});
        r.go(4 * SECOND);
        r.start({0xDB, 0x00, 0x32, 0x00, 0x01, 0x76});
        r.go(1000);
        CHECK((r.peekRam(0x100) & 0x20) == 0, "4 s on, the timer still runs");
        r.go(SECOND / 2);
        r.start({0xDB, 0x00, 0x32, 0x00, 0x01, 0x76});
        r.go(1000);
        CHECK((r.peekRam(0x100) & 0x20) != 0, "4.5 s on, it has timed out");

        // IN 40h ; IN 10h ; IN A,(00h) ; LD (0100h),A ; HALT
        r.start({0xDB, 0x40, 0xDB, 0x10, 0xDB, 0x00, 0x32, 0x00, 0x01, 0x76});
        r.go(1000);
        CHECK((r.peekRam(0x100) & 0x20) != 0, "IN 10h clears it at once");

        // A snapshot carries a running timer. IN A,(40h) ; HALT, then snapshot, clear the
        // timer, and restore: the restored timer is still running.
        r.start({0xDB, 0x40, 0x76});
        r.go(1000);
        const std::string snap = "jadedd_timer.tmp";
        std::remove(snap.c_str());
        CHECK(r.m.snapshot(snap, err), "snapshot with the timer running");
        // IN A,(10h) ; IN A,(00h) ; LD (0100h),A ; HALT
        r.start({0xDB, 0x10, 0xDB, 0x00, 0x32, 0x00, 0x01, 0x76});
        r.go(1000);
        CHECK((r.peekRam(0x100) & 0x20) != 0, "the original clears it");
        std::vector<std::string> notes;
        CHECK(r.m.restore(snap, err, notes) && notes.empty(), "restore it");
        std::remove(snap.c_str());
        r.start({0xDB, 0x00, 0x32, 0x00, 0x01, 0x76});
        r.go(1000);
        CHECK((r.peekRam(0x100) & 0x20) == 0, "the restored timer still runs");
        std::string e2;
        setProperty(*r.dd, "size", "5", e2);
        CHECK(r.dd->timerPeriod() == 8347200, "the same 4.1736 s at the 5-inch 2 MHz");
    }

    SECTION("jadedd: a snapshot taken mid-sector restores the whole private machine");
    {
        withRampDisk(DD_DISK);
        Rig r;
        std::string err;
        r.dd->mount("drive0", "dd.dsk", false, err);
        r.start(transfer(false, DSE, 0, 1));
        // Creep forward until the loop has some bytes and not all of them.
        int guard = 0;
        while (r.hl() < 0x0210 && guard++ < 100000) {
            r.go(200);
            r.status();
        }
        uint16_t hl = r.hl();
        CHECK(hl >= 0x0210 && hl < 0x0280 && !r.halted(), "mid-sector: the loop is running");
        const std::string snap = "jadedd_snapshot.tmp";
        std::remove(snap.c_str());
        CHECK(r.m.snapshot(snap, err), "snapshot the machine mid-command");
        r.go(SECOND);
        CHECK(r.halted() && r.word(0x102) == 0x0280, "the original finishes");
        std::vector<std::string> notes;
        CHECK(r.m.restore(snap, err, notes) && notes.empty(), "restore it, whole");
        std::remove(snap.c_str());
        CHECK(!r.dd->hostHasRam() && r.hl() == hl, "back mid-sector, at the same byte");
        r.control(MRQ);
        r.control(MRT);  // touch nothing: take and give back the RAM is not a command
        r.go(SECOND);
        CHECK(r.halted() && r.peekRam(0x100) == 0x00 && r.word(0x102) == 0x0280,
              "the restored command finishes with no error, at 128 bytes");
        bool same = true;
        for (int i = 0; i < 128; ++i) same = same && r.peekRam((uint16_t)(0x200 + i)) == rampByte(i);
        CHECK(same, "...and the buffer is the sector");
    }

    setMediaResolver(openHostMedia);
}
