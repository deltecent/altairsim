#include "test.h"

#include "boards/floppy-drive.h"
#include "chips/upd765.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "host/disk.h"
#include "host/media.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace altair;

namespace {

// ---------------------------------------------------------------------------
// A DRIVE MADE OF NOTHING (the test_wd17xx.cpp idea): sectors in a map, no image and
// no board. Tracks are keyed by (physical track, side); the side is what the chip's HD
// pin last selected.
// ---------------------------------------------------------------------------
struct FakeDrive : FloppyDrive {
    struct Sec {
        SectorId             id{};
        std::vector<uint8_t> data;
    };

    int  head     = 0;
    int  side     = 0;
    int  tracks   = 77;
    bool isReady  = true;
    bool isWp     = false;
    bool isTwo    = false;
    bool fault    = false;
    int  steps    = 0;
    bool canFmt   = true;

    std::vector<SectorId> lastFormat;
    uint8_t               lastFill = 0;

    std::map<std::pair<int, int>, std::vector<Sec>> trk;

    // `n` sectors of `size` bytes on (t, s), numbered from 1, data = 0x10*s + sector.
    void lay(int t, int s, int n, int size, bool dd) {
        auto& v = trk[{t, s}];
        v.clear();
        int code = 0;
        while ((128 << code) < size) ++code;
        for (int r = 1; r <= n; ++r) {
            Sec sec;
            sec.id.track         = t;
            sec.id.sector        = r;
            sec.id.size          = size;
            sec.id.lengthCode    = code;
            sec.id.doubleDensity = dd;
            sec.data.assign((size_t)size, (uint8_t)(0x10 * s + r));
            v.push_back(sec);
        }
    }
    Sec* find(int t, int s, int r) {
        for (Sec& x : trk[{t, s}])
            if (x.id.sector == r) return &x;
        return nullptr;
    }

    bool ready() const override { return isReady; }
    bool writeProtected() const override { return isWp; }
    bool trackZero() const override { return head == 0; }
    bool index() const override { return false; }
    bool writeFault() const override { return fault; }
    bool twoSided() const override { return isTwo; }
    void step(bool in) override {
        ++steps;
        if (in) {
            if (head < tracks + 10) ++head;
        } else if (head > 0) {
            --head;
        }
    }
    int headTrack() const override { return head; }

    int sectorCount() const override {
        auto it = trk.find({head, side});
        return it == trk.end() ? 0 : (int)it->second.size();
    }
    bool sectorIdAt(int i, SectorId& out) const override {
        auto it = trk.find({head, side});
        if (it == trk.end() || i < 0 || i >= (int)it->second.size()) return false;
        out = it->second[(size_t)i].id;
        return true;
    }
    bool readData(const SectorId& id, uint8_t* buf, size_t* n) override {
        Sec* s = find(head, side, id.sector);
        if (!s || *n < s->data.size()) return false;
        std::copy(s->data.begin(), s->data.end(), buf);
        *n = s->data.size();
        return true;
    }
    bool writeData(const SectorId& id, const uint8_t* buf, size_t n) override {
        Sec* s = find(head, side, id.sector);
        if (!s || n != s->data.size()) return false;
        s->data.assign(buf, buf + n);
        return true;
    }
    bool formatTrack(const std::vector<SectorId>& ids, uint8_t fill) override {
        if (!canFmt) return false;
        lastFormat = ids;
        lastFill   = fill;
        return true;
    }
};

// The chip, four drive slots, and a clock at 2 MHz.
struct Rig {
    Clock     clk;
    Upd765    f{"fdc"};
    FakeDrive d[2];
    bool      present[4] = {true, true, false, false};

    Rig() {
        clk.setHz(2000000);
        f.attachClock(&clk);
        f.wiring.drive = [this](int us) -> FloppyDrive* {
            return (us < 2 && present[us]) ? &d[us] : nullptr;
        };
        f.wiring.head = [this](int us, int hd) {
            if (us < 2) d[us].side = hd;
        };
        f.powerOn();
    }

    // Send a command, byte by byte, the way the data sheet says: RQM up, DIO = 0 first.
    bool cmd(std::vector<uint8_t> b) {
        for (uint8_t v : b) {
            const uint8_t m = f.readMsr();
            if ((m & (Upd765::kMsrRqm | Upd765::kMsrDio)) != Upd765::kMsrRqm) return false;
            f.writeData(v);
        }
        return true;
    }
    // Read the result phase until DIO drops.
    std::vector<uint8_t> results() {
        std::vector<uint8_t> out;
        for (int k = 0; k < 16; ++k) {
            const uint8_t m = f.readMsr();
            if ((m & (Upd765::kMsrRqm | Upd765::kMsrDio)) != (Upd765::kMsrRqm | Upd765::kMsrDio))
                break;
            out.push_back(f.readData());
        }
        return out;
    }
    // Let emulated time pass, as the board's wake-ups would.
    void run(uint64_t t) {
        const uint64_t end = clk.now() + t;
        while (clk.now() < end) {
            uint64_t e    = f.nextEdge();
            uint64_t step = (e && e > clk.now() && e < end) ? e - clk.now() : end - clk.now();
            clk.advance(step);
            f.poll();
        }
    }
    // Clear the reset interrupts, then Specify: SRT 3 ms (D), HUT 16 ms, HLT 2 ms, DMA.
    void ready(uint8_t nd = 0) {
        while (f.intPin()) {
            cmd({0x08});
            results();
        }
        cmd({0x03, 0xD1, (uint8_t)(0x02 | nd)});
    }
    // A DMA read of everything the chip offers.
    std::vector<uint8_t> dmaRead() {
        std::vector<uint8_t> out;
        for (int k = 0; k < 100000 && f.drq(); ++k) {
            uint8_t v = 0;
            if (f.dackRead(v)) out.push_back(v);
        }
        return out;
    }
    std::vector<uint8_t> seek(int us, int ncn) {
        cmd({0x0F, (uint8_t)us, (uint8_t)ncn});
        run(2000000);
        cmd({0x08});
        return results();
    }
};

} // namespace

void test_upd765() {
    SECTION("uPD765 -- the registers, reset, Specify and invalid commands");
    {
        Rig r;
        CHECK(r.f.intPin(), "reset with drive 0 ready raises the ready-change interrupt");
        r.cmd({0x08});
        auto s = r.results();
        CHECK(s.size() == 2 && s[0] == 0xC0, "Sense Interrupt Status: IC = 11, drive 0");
        CHECK(!r.f.intPin(), "...and that clears INT");
        CHECK(r.f.readMsr() == Upd765::kMsrRqm, "idle: RQM only");

        r.cmd({0x08});
        s = r.results();
        CHECK(s.size() == 1 && s[0] == 0x80, "Sense Interrupt Status with nothing pending is invalid");
        CHECK(!r.f.intPin(), "an invalid command raises no interrupt");

        r.cmd({0x03, 0xDF, 0x28});
        CHECK(r.f.readMsr() == Upd765::kMsrRqm, "Specify has no result phase");
        CHECK(!r.f.nonDma(), "Specify DF 28: DMA mode");

        r.cmd({0x00});
        s = r.results();
        CHECK(s.size() == 1 && s[0] == 0x80, "opcode 00 is invalid: ST0 = 80H");
        r.cmd({0x87});
        s = r.results();
        CHECK(s.size() == 1 && s[0] == 0x80, "a Recalibrate with a flag bit set is not in the table");

        r.f.writeData(0x04);
        CHECK((r.f.readMsr() & (Upd765::kMsrCb | Upd765::kMsrRqm)) ==
                  (Upd765::kMsrCb | Upd765::kMsrRqm),
              "the command phase is busy (CB) and still wants bytes");
        r.f.writeData(0x00);
        s = r.results();
        CHECK(s.size() == 1, "Sense Drive Status: one result byte");
        CHECK(s[0] == (Upd765::kSt3Ry | Upd765::kSt3T0), "ST3: ready, track 0, unit 0, single sided");
        r.d[1].isWp  = true;
        r.d[1].isTwo = true;
        r.d[1].head  = 3;
        r.cmd({0x04, 0x05});
        s = r.results();
        CHECK(s.size() == 1 && s[0] == (Upd765::kSt3Wp | Upd765::kSt3Ry | Upd765::kSt3Ts | 0x05),
              "ST3: write protect, two side, HD 1, unit 1");
        r.present[1] = false;
        r.cmd({0x04, 0x01});
        s = r.results();
        CHECK(s.size() == 1 && s[0] == 0x01, "ST3 of an empty connector: nothing but the unit");
    }

    SECTION("uPD765 -- Recalibrate and Seek, step by step");
    {
        Rig r;
        r.ready();
        r.d[0].head = 5;
        r.cmd({0x07, 0x00});
        CHECK((r.f.readMsr() & 0x01) != 0, "Recalibrate sets D0B");
        CHECK((r.f.readMsr() & Upd765::kMsrCb) == 0, "...and the FDC is not busy during it");
        r.run(5 * 6000 - 1);  // SRT D = 3 ms = 6000 T-states at 2 MHz; the first pulse at once
        CHECK(r.d[0].head == 0 && !r.f.intPin(), "five pulses, one per step time, not yet ended");
        r.run(1);
        CHECK(r.f.intPin(), "the next step time sees Track 0: INT");
        r.cmd({0x08});
        auto s = r.results();
        CHECK(s.size() == 2 && s[0] == 0x20 && s[1] == 0, "SIS: ST0 = 20H (SE), PCN 0");
        CHECK((r.f.readMsr() & 0x0F) == 0, "SIS clears D0B");

        s = r.seek(0, 10);
        CHECK(s.size() == 2 && s[0] == 0x20 && s[1] == 10, "Seek 10: SE, PCN 10");
        CHECK(r.d[0].head == 10 && r.f.pcn(0) == 10, "the head and the PCN agree");
        const int before = r.d[0].steps;
        s = r.seek(0, 10);
        CHECK(s[0] == 0x20 && r.d[0].steps == before, "a Seek to where it is: SE, no step");

        r.d[0].head = 80;  // an 80-track drive, far out
        r.cmd({0x07, 0x00});
        r.run(2000000);
        r.cmd({0x08});
        s = r.results();
        CHECK(s.size() == 2 && s[0] == 0x70, "77 pulses without Track 0: SE + EC, abnormal");
        CHECK(r.d[0].head == 3, "...after exactly 77 pulses");

        r.d[1].isReady = false;
        r.cmd({0x0F, 0x01, 0x05});
        r.run(10);
        r.cmd({0x08});
        s = r.results();
        CHECK(s.size() == 2 && s[0] == 0x69, "Seek on a not-ready drive: AT, SE, NR, unit 1");
    }

    SECTION("uPD765 -- overlapped seeks, and the SIS that must follow");
    {
        Rig r;
        r.ready();
        r.cmd({0x0F, 0x00, 0x14});
        r.cmd({0x0F, 0x01, 0x03});
        CHECK((r.f.readMsr() & 0x03) == 0x03, "two drives seeking at once: D0B and D1B");
        r.run(5 * 6000);
        CHECK(r.f.intPin() && r.d[1].head == 3 && r.d[0].head < 20, "drive 1 ends first");
        r.cmd({0x03, 0xD1, 0x02});
        auto s = r.results();
        CHECK(s.size() == 1 && s[0] == 0x80, "a Specify before the SIS is invalid");
        r.cmd({0x08});
        s = r.results();
        CHECK(s.size() == 2 && s[0] == 0x21 && s[1] == 3, "SIS: drive 1, SE, PCN 3");
        CHECK((r.f.readMsr() & 0x03) == 0x01, "D1B clear, D0B still set");

        r.cmd({0x06, 0x00, 0, 0, 1, 0, 26, 7, 128});
        s = r.results();
        CHECK(s.size() == 1 && s[0] == 0x80, "a read while a drive is seeking is refused");

        r.run(20 * 6000);
        r.cmd({0x08});
        s = r.results();
        CHECK(s.size() == 2 && s[0] == 0x20 && s[1] == 20, "then drive 0: SE, PCN 20");
    }

    SECTION("uPD765 -- Specify timing: SRT and the 4 MHz clock");
    {
        Rig r;
        r.ready();
        r.cmd({0x03, 0xF1, 0x02});  // SRT F = 1 ms
        r.cmd({0x0F, 0x00, 0x04});
        r.run(4 * 2000 - 1);
        CHECK(r.d[0].head == 4 && !r.f.intPin(), "SRT F: a pulse each ms");
        r.run(1);
        CHECK(r.f.intPin(), "...and the end one ms after the last pulse");
        r.cmd({0x08});
        r.results();

        r.f.fdcClockHz = 4000000;
        r.cmd({0x0F, 0x00, 0x00});
        r.run(2 * 4000 - 1);
        CHECK(r.d[0].head == 2, "at 4 MHz every interval doubles: 2 ms a step");
    }

    SECTION("uPD765 -- Read Data by DMA, to EOT and with TC");
    {
        Rig r;
        r.d[0].lay(0, 0, 26, 128, false);
        r.ready();
        // The boot ROM's read: FM, N = 0, R = 1, EOT = 26, DTL = 128, and no TC.
        r.cmd({0x06, 0x00, 0, 0, 1, 0, 26, 7, 128});
        CHECK(!r.f.drq(), "the head-load time comes first");
        r.run(4000);  // HLT 1 = 2 ms
        CHECK(r.f.drq() && r.f.toMemory(), "then DRQ, toward memory");
        CHECK((r.f.readMsr() & (Upd765::kMsrCb | Upd765::kMsrRqm)) == Upd765::kMsrCb,
              "DMA execution: CB, no RQM");
        auto data = r.dmaRead();
        CHECK(data.size() == 3328, "the whole track: 26 x 128 bytes");
        CHECK(data[0] == 0x01 && data[128] == 0x02 && data[3327] == 26, "in sector order");
        CHECK(r.f.intPin(), "the result phase raises INT");
        auto s = r.results();
        CHECK(s.size() == 7, "seven result bytes");
        CHECK(s[0] == 0x40 && s[1] == 0x80 && s[2] == 0, "past EOT without TC: AT, ST1 EN");
        CHECK(s[3] == 1 && s[4] == 0 && s[5] == 1 && s[6] == 0, "C + 1, R = 1 (Table 2)");
        CHECK(!r.f.intPin(), "the first result byte cleared INT");

        // One sector and TC: a normal end, R + 1.
        r.cmd({0x06, 0x00, 0, 0, 5, 0, 26, 7, 128});
        r.run(10);
        CHECK(r.f.drq(), "inside the head-unload time there is no head-load wait");
        for (int k = 0; k < 128; ++k) {
            uint8_t v = 0;
            r.f.dackRead(v);
        }
        r.f.terminalCount();
        s = r.results();
        CHECK(s.size() == 7 && s[0] == 0x00 && s[1] == 0 && s[5] == 6, "TC after sector 5: NT, R = 6");

        // TC in the middle of a sector: no more bytes, a normal end.
        r.cmd({0x06, 0x00, 0, 0, 26, 0, 26, 7, 128});
        r.run(10);
        for (int k = 0; k < 10; ++k) {
            uint8_t v = 0;
            r.f.dackRead(v);
        }
        r.f.terminalCount();
        CHECK(!r.f.drq(), "TC mid-sector: no more DRQ");
        s = r.results();
        CHECK(s[0] == 0x00 && s[3] == 1 && s[5] == 1, "TC in the EOT sector: C + 1, R = 1, NT");
    }

    SECTION("uPD765 -- multi-track, and side 1 of a single-sided drive");
    {
        Rig r;
        r.d[0].isTwo = true;
        r.d[0].lay(0, 0, 2, 256, true);
        r.d[0].lay(0, 1, 2, 256, true);
        r.ready();
        r.cmd({0xC6, 0x00, 0, 0, 1, 1, 2, 0x0E, 0xFF});  // MT MF, N = 1, EOT 2
        r.run(4000);
        auto data = r.dmaRead();
        CHECK(data.size() == 1024, "MT: two sectors on side 0, two on side 1");
        CHECK(data[0] == 0x01 && data[256] == 0x02 && data[512] == 0x11 && data[768] == 0x12,
              "side 0 sectors 1-2, then side 1 sectors 1-2");
        auto s = r.results();
        CHECK(s[0] == 0x44 && s[1] == 0x80, "ends past EOT on head 1: AT, HD 1, EN");
        CHECK(s[3] == 1 && s[4] == 0 && s[5] == 1, "MT, HD 1 at EOT: C + 1, H flipped, R = 1");

        r.d[0].isTwo = false;
        r.cmd({0x46, 0x04, 0, 1, 1, 1, 2, 0x0E, 0xFF});
        r.run(4000);
        s = r.results();
        CHECK(s.size() == 7 && s[0] == 0x4C, "HD 1 on a single-sided drive: NR (§5)");

        r.cmd({0xC6, 0x00, 0, 0, 1, 1, 2, 0x0E, 0xFF});
        r.run(4000);
        data = r.dmaRead();
        s    = r.results();
        CHECK(data.size() == 512 && s[0] == 0x4C, "MT onto side 1 of a single-sided drive: NR");
    }

    SECTION("uPD765 -- the errors: not ready, density, no data, cylinder, CRC, protect");
    {
        Rig r;
        r.d[0].lay(0, 0, 26, 128, false);
        r.ready();
        r.d[0].isReady = false;
        r.cmd({0x06, 0x00, 0, 0, 1, 0, 26, 7, 128});
        auto s = r.results();
        CHECK(s.size() == 7 && s[0] == 0x48, "not ready: AT + NR, at once");
        r.d[0].isReady = true;
        r.cmd({0x08});
        r.results();  // the ready change the poll saw

        r.cmd({0x46, 0x00, 0, 0, 1, 1, 26, 7, 0xFF});  // MFM on an FM track
        r.run(4000);
        s = r.results();
        CHECK(s[0] == 0x40 && s[1] == Upd765::kSt1Ma, "the wrong density finds no address mark: MA");

        r.cmd({0x06, 0x00, 0, 0, 30, 0, 30, 7, 128});
        r.run(4000);
        s = r.results();
        CHECK(s[0] == 0x40 && s[1] == Upd765::kSt1Nd && s[2] == 0, "no sector 30: ND");

        r.cmd({0x06, 0x00, 5, 0, 1, 0, 26, 7, 128});
        r.run(4000);
        s = r.results();
        CHECK(s[1] == Upd765::kSt1Nd && s[2] == Upd765::kSt2Wc, "cylinder 5 asked, 0 found: ND + WC");

        r.d[0].find(0, 0, 3)->id.dataCrcOk = false;
        r.cmd({0x06, 0x00, 0, 0, 3, 0, 26, 7, 128});
        r.run(4000);
        auto data = r.dmaRead();
        s         = r.results();
        CHECK(data.size() == 128 && s[0] == 0x40 && s[1] == Upd765::kSt1De &&
                  s[2] == Upd765::kSt2Dd,
              "a data CRC error: the sector, then AT with DE + DD");
        r.d[0].find(0, 0, 3)->id.dataCrcOk = true;

        r.d[0].isWp = true;
        r.cmd({0x05, 0x00, 0, 0, 1, 0, 1, 7, 128});
        s = r.results();
        CHECK(s[0] == 0x40 && s[1] == Upd765::kSt1Nw, "Write Data, write protected: NW");
        r.cmd({0x0D, 0x00, 0, 26, 0x1B, 0xE5});
        s = r.results();
        CHECK(s[0] == 0x40 && s[1] == Upd765::kSt1Nw, "Format, write protected: NW");
    }

    SECTION("uPD765 -- deleted data marks and SK");
    {
        Rig r;
        r.d[0].lay(0, 0, 4, 128, false);
        r.d[0].find(0, 0, 2)->id.deleted = true;
        r.ready();
        r.cmd({0x06, 0x00, 0, 0, 1, 0, 4, 7, 128});
        r.run(4000);
        auto data = r.dmaRead();
        auto s    = r.results();
        CHECK(data.size() == 256 && s[0] == 0x00 && s[2] == Upd765::kSt2Cm && s[5] == 3,
              "SK = 0: the deleted sector is read, CM, and the command ends");
        r.cmd({0x26, 0x00, 0, 0, 1, 0, 4, 7, 128});
        r.run(4000);
        data = r.dmaRead();
        s    = r.results();
        CHECK(data.size() == 384 && data[128] == 0x03 && s[1] == 0x80 && s[2] == 0,
              "SK = 1: sector 2 is skipped, and the read runs to EOT");
        r.cmd({0x26, 0x00, 0, 0, 1, 0, 2, 7, 128});
        r.run(10);
        data = r.dmaRead();
        s    = r.results();
        CHECK(data.size() == 128 && s[0] == 0x40 && s[1] == 0x80 && s[3] == 1 && s[5] == 1,
              "SK = 1 and the skipped sector is EOT: past EOT, AT + EN, C + 1, R = 1");
        r.cmd({0x0C, 0x00, 0, 0, 2, 0, 4, 7, 128});
        r.run(4000);
        data = r.dmaRead();
        s    = r.results();
        CHECK(data.size() == 256 && data[0] == 0x02 && data[128] == 0x03 && s[1] == 0x00 &&
                  s[2] == Upd765::kSt2Cm,
              "Read Deleted Data: sector 2, then sector 3 with its normal mark, CM, and the end");
    }

    SECTION("uPD765 -- Write Data, and TC mid-sector fills with 00");
    {
        Rig r;
        r.d[0].lay(0, 0, 26, 128, false);
        r.ready();
        r.cmd({0x05, 0x00, 0, 0, 7, 0, 26, 7, 128});
        r.run(4000);
        CHECK(r.f.drq() && !r.f.toMemory(), "a write's DRQ asks memory for a byte");
        for (int k = 0; k < 128 + 10; ++k) r.f.dackWrite((uint8_t)(0x80 | (k & 0x3F)));
        r.f.terminalCount();
        auto s = r.results();
        CHECK(s[0] == 0x00 && s[5] == 9, "1 + a bit sectors and TC: NT, R = 9");
        CHECK(r.d[0].find(0, 0, 7)->data[127] == (0x80 | (127 & 0x3F)), "sector 7 written");
        auto& s8 = r.d[0].find(0, 0, 8)->data;
        CHECK(s8[9] == (0x80 | (137 & 0x3F)) && s8[10] == 0 && s8[127] == 0,
              "sector 8: ten bytes, then 00 to the end");
    }

    SECTION("uPD765 -- non-DMA mode: an interrupt per byte");
    {
        Rig r;
        r.d[0].lay(0, 0, 26, 128, false);
        r.ready(1);
        CHECK(r.f.nonDma(), "Specify ND = 1");
        r.cmd({0x06, 0x00, 0, 0, 1, 0, 1, 7, 128});
        r.run(4000);
        const uint8_t m = r.f.readMsr();
        CHECK(m == (Upd765::kMsrRqm | Upd765::kMsrDio | Upd765::kMsrExm | Upd765::kMsrCb),
              "MSR: RQM DIO EXM CB");
        CHECK(r.f.intPin() && !r.f.drq(), "INT per byte, and no DRQ");
        std::vector<uint8_t> data;
        while (r.f.readMsr() & Upd765::kMsrExm) data.push_back(r.f.readData());
        CHECK(data.size() == 128 && data[0] == 0x01, "128 bytes through the data register");
        auto s = r.results();
        CHECK(s.size() == 7 && s[0] == 0x40 && s[1] == 0x80, "then the result phase: EN");
    }

    SECTION("uPD765 -- Read ID walks the track; Read a Track");
    {
        Rig r;
        r.d[0].lay(0, 0, 3, 128, false);
        r.ready();
        r.cmd({0x0A, 0x00});
        r.run(4000);
        auto s = r.results();
        CHECK(s.size() == 7 && s[0] == 0 && s[3] == 0 && s[5] == 1 && s[6] == 0, "Read ID: C 0 R 1 N 0");
        r.cmd({0x0A, 0x00});
        r.run(10);
        s = r.results();
        CHECK(s[5] == 2, "the next Read ID meets the next ID field");
        r.cmd({0x4A, 0x00});
        r.run(10);
        s = r.results();
        CHECK(s[0] == 0x40 && (s[1] & Upd765::kSt1Ma), "Read ID in MFM on an FM track: MA");

        r.cmd({0x02, 0x00, 0, 0, 1, 0, 3, 7, 128});
        r.run(10);
        auto data = r.dmaRead();
        s         = r.results();
        CHECK(data.size() == 384 && s[0] == 0x00 && s[1] == 0, "Read a Track: three sectors, NT");
        r.cmd({0x02, 0x00, 0, 0, 9, 0, 2, 7, 128});
        r.run(10);
        data = r.dmaRead();
        s    = r.results();
        CHECK(data.size() == 256 && (s[1] & Upd765::kSt1Nd), "IDs not the IDR's: ND, and it reads on");
    }

    SECTION("uPD765 -- Format a Track takes C/H/R/N through the data path");
    {
        Rig r;
        r.ready();
        r.d[0].head = 4;
        r.f.wiring.head(0, 0);
        r.cmd({0x4D, 0x00, 1, 3, 0x36, 0xE5});  // MFM, N = 1, 3 sectors, fill E5
        r.run(4000);
        CHECK(r.f.drq() && !r.f.toMemory(), "Format asks for the ID bytes");
        const uint8_t ids[] = {4, 0, 1, 1, 4, 0, 3, 1, 4, 0, 2, 1};
        for (uint8_t v : ids) r.f.dackWrite(v);
        auto s = r.results();
        CHECK(s.size() == 7 && s[0] == 0x00 && s[5] == 3, "NT; R = the last sector + 1");
        auto& f = r.d[0].lastFormat;
        CHECK(f.size() == 3 && f[1].sector == 3 && f[1].track == 4 && f[1].size == 256 &&
                  f[1].doubleDensity,
              "the drive got the IDs in track order, at MFM, 256 bytes");
        CHECK(r.d[0].lastFill == 0xE5, "...and the fill byte D");
        r.d[0].canFmt = false;
        r.cmd({0x4D, 0x00, 1, 1, 0x36, 0xE5});
        r.run(10);
        for (int k = 0; k < 4; ++k) r.f.dackWrite(1);
        s = r.results();
        CHECK(s[0] == 0x50, "a drive that cannot record it: AT + EC");
    }

    SECTION("uPD765 -- Scan Equal, Low or Equal, High or Equal");
    {
        Rig r;
        r.d[0].lay(0, 0, 4, 128, false);
        r.ready();
        auto scan = [&](uint8_t op, uint8_t r0, uint8_t eot, uint8_t stp, uint8_t v, bool mask) {
            r.cmd({op, 0x00, 0, 0, r0, 0, eot, 7, stp});
            r.run(4000);
            for (int k = 0; k < 2000 && r.f.drq(); ++k)
                r.f.dackWrite(mask && k % 128 != 0 ? 0xFF : v);
            return r.results();
        };
        auto s = scan(0x11, 1, 4, 1, 0x03, false);
        CHECK(s[0] == 0 && s[2] == Upd765::kSt2Sh && s[5] == 3, "Scan Equal finds sector 3: SH");
        s = scan(0x11, 1, 4, 1, 0x07, false);
        CHECK(s[0] == 0 && s[2] == Upd765::kSt2Sn, "no sector equals 07: SN");
        s = scan(0x19, 1, 4, 1, 0x02, false);
        CHECK(s[0] == 0 && s[2] == 0 && s[5] == 1, "Scan Low or Equal: sector 1 is below 02: met, not equal");
        s = scan(0x1D, 1, 4, 1, 0x02, true);
        CHECK(s[2] == Upd765::kSt2Sh, "FF masks: only the first byte of each sector counts");
        s = scan(0x1D, 3, 4, 1, 0x01, false);
        CHECK(s[2] == 0 && s[5] == 3, "Scan High or Equal: sector 3 is above 01: met, not equal");
        s = scan(0x11, 2, 4, 2, 0x09, false);
        CHECK(s[0] == 0 && s[2] == Upd765::kSt2Sn, "STP 2 from sector 2: 2, 4 = EOT, SN, normal");
        s = scan(0x11, 1, 4, 2, 0x09, false);
        CHECK(s[0] == 0x40 && s[1] == Upd765::kSt1Nd, "STP 2 from sector 1: 1, 3, then 5 is not there");
    }

    SECTION("uPD765 -- Format a Track onto a disk image (DiskImageDrive)");
    {
        // An 8" SSSD image, all 77 tracks declared 26 x 128.
        DiskImage img(std::make_unique<MemoryMedia>("t.dsk", std::vector<uint8_t>(256256, 0x00)));
        img.init(77, 1, false);
        img.initFormat(0, 76, 0, 0, Density::SD, 26, 128, 1);
        DiskImageDrive dd;
        dd.mount(&img, false);
        dd.setHeadTrack(2);
        CHECK(!dd.twoSided(), "a one-head image is single sided (TS low)");

        std::vector<FloppyDrive::SectorId> ids;
        for (int k = 0; k < 26; ++k) {  // interleave 2: 1, 14, 2, 15, ...
            FloppyDrive::SectorId id;
            id.track = 2, id.sector = (k % 2) ? 14 + k / 2 : 1 + k / 2, id.size = 128;
            ids.push_back(id);
        }
        CHECK(!dd.formatTrack(ids, 0xE5), "formatting is off until the board turns it on");
        dd.setFormatting(true);

        Clock  clk;
        Upd765 f("fdc");
        clk.setHz(2000000);
        f.attachClock(&clk);
        f.wiring.drive = [&dd](int us) -> FloppyDrive* { return us == 0 ? &dd : nullptr; };
        f.wiring.head  = [&dd](int, int hd) { dd.setSide(hd); };
        f.powerOn();
        while (f.intPin()) {
            f.writeData(0x08);
            f.readData(), f.readData();
        }
        for (uint8_t v : {0x03, 0xD1, 0x02, 0x0D, 0x00, 0x00, 26, 0x1B, 0xE5}) f.writeData(v);
        clk.advance(4000);
        f.poll();
        for (const auto& id : ids)
            for (uint8_t v : {(uint8_t)id.track, (uint8_t)0, (uint8_t)id.sector, (uint8_t)0})
                f.dackWrite(v);
        uint8_t st0 = f.readData();
        CHECK(st0 == 0x00, "Format through the chip onto the image: NT");
        uint8_t buf[128];
        size_t  n  = sizeof buf;
        bool    ok = img.readSector(2, 0, 26, buf, &n);
        CHECK(ok && n == 128 && buf[0] == 0xE5 && buf[127] == 0xE5, "sector 26 of track 2 is E5 fill");

        ids[3].sector = 99;
        CHECK(!dd.formatTrack(ids, 0xE5), "a gap in the sector numbers has no image form");
        ids[3].sector = ids[4].sector;
        CHECK(!dd.formatTrack(ids, 0xE5), "...nor does a sector number twice");
    }

    SECTION("DiskImageDrive -- every write reaches the host file (#900)");
    {
        // The boards that hold this drive sync only on unmount, which a killed simulator never
        // reaches. So the drive syncs: once per sector written, once per track formatted, and
        // never for a write that did not land.
        auto         owned = std::make_unique<MemoryMedia>("t.dsk", std::vector<uint8_t>(256256, 0x00));
        MemoryMedia* m     = owned.get();
        DiskImage    img(std::move(owned));
        img.init(77, 1, false);
        img.initFormat(0, 76, 0, 0, Density::SD, 26, 128, 1);
        DiskImageDrive dd;
        dd.mount(&img, false);
        dd.setHeadTrack(2);

        FloppyDrive::SectorId id;
        id.track = 2, id.sector = 5, id.size = 128;
        const std::vector<uint8_t> sec(128, 0x5A);
        int before = m->syncs();
        CHECK(dd.writeData(id, sec.data(), sec.size()), "a sector write lands");
        CHECK(m->syncs() == before + 1, "...and is synced to the host file before it returns");
        CHECK(m->bytes()[(size_t)(2 * 26 + 4) * 128] == 0x5A, "...at track 2 sector 5");

        id.sector = 27;
        before    = m->syncs();
        CHECK(!dd.writeData(id, sec.data(), sec.size()), "a sector the track does not have fails");
        CHECK(m->syncs() == before, "...and a write that did not land is not synced");

        // Write Track: a raw IBM 3740 stream of 26 x 128, the same geometry the track has.
        // Sized up front and filled by index: GCC 15 -Warray-bounds misfires on a grown vector.
        auto stream = [](int sectors, size_t dataLen) {
            const size_t         per = 6 + 11 + 1 + dataLen + 1 + 27;  // ID, gap, FB, data, F7, gap
            std::vector<uint8_t> t(41 + (size_t)sectors * per, 0xFF);  // 0xFF gap everywhere
            t[40] = 0xFC;                                              // index mark
            for (int s = 0; s < sectors; ++s) {
                const size_t b   = 41 + (size_t)s * per;
                const uint8_t hdr[6] = {0xFE, 2, 0, (uint8_t)(s + 1), 0, 0xF7};
                for (size_t k = 0; k < 6; ++k) t[b + k] = hdr[k];
                t[b + 17] = 0xFB;
                for (size_t k = 0; k < dataLen; ++k) t[b + 18 + k] = 0xE5;
                t[b + 18 + dataLen] = 0xF7;
            }
            return t;
        };
        dd.setFormatting(true);
        before = m->syncs();
        CHECK(dd.writeTrackImage(stream(26, 128), false), "Write Track lays down a whole track");
        CHECK(m->syncs() == before + 1, "...and syncs once for the track, not once per sector");

        // formatTrack (the uPD765's Format a Track), in order 1..26.
        std::vector<FloppyDrive::SectorId> ids;
        for (int k = 1; k <= 26; ++k) {
            FloppyDrive::SectorId f;
            f.track = 2, f.sector = k, f.size = 128;
            ids.push_back(f);
        }
        before = m->syncs();
        CHECK(dd.formatTrack(ids, 0xE5), "Format a Track lays down a whole track");
        CHECK(m->syncs() == before + 1, "...and syncs once for the track");
        ids[3].sector = 99;
        before        = m->syncs();
        CHECK(!dd.formatTrack(ids, 0xE5), "an ID list with no image form is refused");
        CHECK(m->syncs() == before, "...before it touches the image, so nothing is synced");

        // A data field shorter than the sector: the track's new geometry is already in the image
        // when the first sector write fails, so the file is synced even though the command faults.
        before = m->syncs();
        CHECK(!dd.writeTrackImage(stream(26, 100), false), "a short data field is a WRITE FAULT");
        CHECK(m->syncs() == before + 1, "...and what changed in the image still reaches the file");

        // A write-protected medium: nothing lands, nothing is synced.
        auto         roOwned = std::make_unique<MemoryMedia>("ro.dsk", std::vector<uint8_t>(256256, 0x00), true);
        MemoryMedia* ro      = roOwned.get();
        DiskImage    roImg(std::move(roOwned));
        roImg.init(77, 1, false);
        roImg.initFormat(0, 76, 0, 0, Density::SD, 26, 128, 1);
        DiskImageDrive rd;
        rd.mount(&roImg, false);
        id.sector = 1;
        before    = ro->syncs();
        CHECK(!rd.writeData(id, sec.data(), sec.size()), "a read-only medium refuses the write");
        CHECK(ro->syncs() == before, "...and syncs nothing");
    }

    SECTION("uPD765 -- snapshot and restore in the middle of a read");
    {
        Rig a;
        a.d[0].lay(0, 0, 26, 128, false);
        a.ready();
        a.cmd({0x06, 0x00, 0, 0, 1, 0, 2, 7, 128});
        a.run(4000);
        for (int k = 0; k < 100; ++k) {
            uint8_t v = 0;
            a.f.dackRead(v);
        }
        StateWriter w;
        a.f.serialize(w);

        Rig b;
        b.d[0].lay(0, 0, 26, 128, false);
        StateReader rd(w.data());
        b.f.deserialize(rd);
        CHECK(rd.ok() && rd.remaining() == 0, "the chip's state reads back whole");
        auto data = b.dmaRead();
        CHECK(data.size() == 156 && data[27] == 0x01 && data[28] == 0x02,
              "the restored chip carries on mid-sector");
        auto s = b.results();
        CHECK(s.size() == 7 && s[1] == 0x80, "...to the same end");
    }
}
