// The North Star Micro-Disk System: the MDS-A and the MDS-A-D (docs/boards/northstar-mds.md).
//
// THE BOOT IS THE ACCEPTANCE TEST -- CP/M and North Star DOS off the period images, on both
// boards (tests/acceptance/northstar*.exp). These tests pin what a boot cannot: the bits no
// period program happens to read, the numbers a too-generous window would hide, and the
// write path on a disk nobody has written yet.
//
// THE ORACLES ARE THE TWO BOOT PROMS. Every address below is one the PROM listings name
// (roms/NSBOOT-SD/NSBOOTV2.PRN, roms/NSBOOT-DD/NSBOOT.PRN), and the check character is
// computed the way the PROM computes it.
//
// Every access is a real bus cycle (Bus::memRead), because on this board the decode IS the
// command. No filesystem: MemoryMedia through setMediaResolver.

#include "boards/northstar-mds.h"
#include "boards/s100-memory.h"
#include "core/debug.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/cardimg.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

// At 2 MHz.
constexpr uint64_t kSector  = 40000;     // 20 ms -- 300 RPM, ten sector holes
constexpr uint64_t kWindow  = 192;       // 96 us
constexpr uint64_t kBody    = 2368;      // 96 us + 17 bytes x 64 us (or 34 x 32 us)
constexpr uint64_t kRE      = 960;       // 480 us
constexpr uint64_t kDDI     = 1024;      // 512 us
constexpr uint64_t kFreeRun = 65536;     // 32.768 ms -- the board's own pulse
constexpr uint64_t kOffSD   = 6400000;   // 3.2 s -- 16 revolutions
constexpr uint64_t kOffDD   = 19200000;  // 9.6 s -- 48 revolutions

constexpr size_t kSD   = 35 * 10 * 256;
constexpr size_t kDD   = 35 * 10 * 512;
constexpr size_t kQuad = 2 * kDD;

// A byte that says where it is, so a sector read from the wrong place cannot match.
uint8_t pat(size_t off) { return (uint8_t)(off * 7 + (off >> 8) * 13 + (off >> 16) + 1); }

std::vector<uint8_t> image(size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = pat(i);
    return v;
}

uint8_t rol(uint8_t v) { return (uint8_t)((v << 1) | (v >> 7)); }

// The medium the board was last given. The board owns it; this only looks.
MemoryMedia* g_media = nullptr;

struct Rig {
    Machine       m;
    NorthStarFdc* b = nullptr;
    std::string   err;

    explicit Rig(const char* type) {
        b = dynamic_cast<NorthStarFdc*>(m.add(type, "fd0", err));
        m.add("8080", "cpu0", err);
        m.power();
    }
    ~Rig() { setMediaResolver(openHostMedia); }  // the one tests/main.cpp installs

    uint8_t rd(uint16_t a) { return m.bus.memRead(a); }

    bool mount(const char* unit, std::vector<uint8_t> bytes, bool ro = false) {
        setMediaResolver([bytes, ro](const std::string& path, bool r, std::string&) {
            auto mm = std::make_unique<MemoryMedia>(path, bytes, ro || r);
            g_media = mm.get();
            return mm;
        });
        return b->mount(unit, "a.nsi", ro, err);
    }

    bool set(const char* key, const char* val) { return setProperty(*b, key, val, err); }

    // Run to the next sector pulse of the kind the board is counting now.
    void toPulse(uint64_t per = kSector) { m.clock.advance(per - m.clock.now() % per); }
};

// `timing = real` (issue #637). A READY hold is charged by the run loop, at the instruction
// boundary, so these tests run code: RAM at 0000, the 8080 at its PC, `n` copies of an
// LDA from `addr` -- the PROM's own way into the board -- then HLT.
void loadLdas(Rig& g, uint16_t addr, int n) {
    auto* mem = dynamic_cast<MemoryBoard*>(g.m.add("memory", "mem0", g.err));
    Region r;
    r.kind = RegionKind::Ram;
    r.at   = 0x0000;
    r.size = 0x8000;
    mem->addRegion(r, g.err);
    mem->power();
    uint16_t pc = 0;
    for (int i = 0; i < n; ++i) {
        g.m.bus.memWrite(pc++, 0x3A);  // LDA addr
        g.m.bus.memWrite(pc++, (uint8_t)addr);
        g.m.bus.memWrite(pc++, (uint8_t)(addr >> 8));
    }
    g.m.bus.memWrite(pc, 0x76);  // HLT
    g.m.cpu()->setPc(0);
}

// The T-states one instruction took, READY holds included.
uint64_t step(Rig& g) {
    const uint64_t t0 = g.m.clock.now();
    g.m.debug.run(1);
    return g.m.clock.now() - t0;
}

// ---- the MDS-A, in the PROM's own names -------------------------------------------------
constexpr uint16_t CTLDS1 = 0xEB01, CTLWRT = 0xEB04, CTLSTC = 0xEB08, CTLSTS = 0xEB09;
constexpr uint16_t CTLDI = 0xEB0C, CTLEI = 0xEB0D, CTLNOP = 0xEB10, CTLRSF = 0xEB14;
constexpr uint16_t CTLRES = 0xEB18, CTLSTO = 0xEB1C, CTLSTI = 0xEB1D;
constexpr uint16_t CTLBST = 0xEB20, CTLRD = 0xEB40, CTLMO = 0xEB80;
constexpr uint16_t SDWD = 0xEA00;  // case 2: write data

constexpr uint8_t SATR0 = 0x01, SAWP = 0x02, SABDY = 0x04, SAWRT = 0x08, SAMO = 0x10;
constexpr uint8_t SAWN = 0x40, SASF = 0x80;

// Motors on, drive 1 selected -- the first two things the PROM does.
void sdReady(Rig& g) {
    g.rd(CTLMO | CTLNOP);
    g.rd(CTLDS1);
}

uint8_t sdA(Rig& g) { return g.rd(CTLNOP); }
uint8_t sdB(Rig& g) { return g.rd(CTLBST | CTLNOP); }

// Read one sector as the PROM does at RDLOOP, check character and all. True if the check
// character matched and the data is what the image holds at `off`.
bool sdReadsSector(Rig& g, size_t off) {
    uint8_t chk = 0;
    bool    ok  = true;
    for (int i = 0; i < 256; ++i) {
        uint8_t v = g.rd(CTLRD | CTLNOP);
        if (v != pat(off + (size_t)i)) ok = false;
        chk = rol(v ^ chk);  // xra b / rlc / mov b,a
    }
    return ok && (uint8_t)(g.rd(CTLRD | CTLNOP) ^ chk) == 0;
}

// ---- the MDS-A-D, in the PROM's own names -----------------------------------------------
constexpr uint16_t CTLWD = 0xE900, CTLORD = 0xEA00, CTLCMD = 0xEB00;
constexpr uint8_t  ORDDS1 = 0x01, ORDST = 0x10, ORDSIN = 0x20, ORDSS = 0x40, ORDDD = 0x80;
constexpr uint8_t  CCRSF = 1, CCDI = 2, CCEI = 3, CCSB = 4, CCMO = 5, CCWR = 6, CCRES = 7;
constexpr uint8_t  DMAS = 0x10, DMBS = 0x20, DMCS = 0x30, DMRD = 0x40;

constexpr uint8_t DABD = 0x01, DARE = 0x04, DAWI = 0x08, DSMO = 0x10, DSDD = 0x20;
constexpr uint8_t DSIX = 0x40, DSSF = 0x80;
constexpr uint8_t DBT0 = 0x01, DBWP = 0x02, DBWR = 0x08;

void ddReady(Rig& g, uint8_t orders = ORDDS1) {
    g.rd(CTLCMD | DMAS | CCMO);
    g.rd(CTLORD | orders);
}

uint8_t ddA(Rig& g) { return g.rd(CTLCMD | DMAS); }
uint8_t ddB(Rig& g) { return g.rd(CTLCMD | DMBS); }
uint8_t ddC(Rig& g) { return g.rd(CTLCMD | DMCS); }

bool ddReadsSector(Rig& g, size_t off, int n) {
    uint8_t chk = 0;
    bool    ok  = true;
    for (int i = 0; i < n; ++i) {
        uint8_t v = g.rd(CTLCMD | DMRD);
        if (v != pat(off + (size_t)i)) ok = false;
        chk = rol(v ^ chk);
    }
    return ok && (uint8_t)(g.rd(CTLCMD | DMRD) ^ chk) == 0;
}

// Route the debug sink to a scratch file while `f` runs, and return what landed there.
std::string debugCapture(const std::function<void()>& f) {
    namespace fs = std::filesystem;
    std::string    err;
    const fs::path p = fs::temp_directory_path() / "altair_northstar_debug.log";
    std::error_code ec;
    fs::remove(p, ec);
    dbg::setSink(dbg::Sink::File, p.string(), err);
    f();
    dbg::setSink(dbg::Sink::Stderr, "", err);  // closes + flushes the file
    std::ifstream     in(p, std::ios::binary);
    const std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    fs::remove(p, ec);
    return s;
}

// One read of sector `sec` of the track under the head, as the PROM reads it (#970). `data`
// says the bytes are the image's at `off`; the return is the check character the board sent
// XOR the one the data gives -- 0 for a good sector.
uint8_t sdReadAt(Rig& g, int sec, size_t off, bool& data) {
    while ((sdB(g) & 0x0F) != sec) g.toPulse();
    g.m.clock.advance(kBody);
    uint8_t chk = 0;
    data        = true;
    for (int i = 0; i < 256; ++i) {
        uint8_t v = g.rd(CTLRD | CTLNOP);
        if (v != pat(off + (size_t)i)) data = false;
        chk = rol(v ^ chk);
    }
    const uint8_t got = g.rd(CTLRD | CTLNOP);
    g.toPulse();
    return (uint8_t)(got ^ chk);
}

const DiskFault* first(Rig& g, const char* unit = "drive0") {
    const DiskFaults* f = g.b->faults(unit);
    return (f && !f->list().empty()) ? &f->list()[0] : nullptr;
}

// BAD SECTORS (#970). A North Star sector is zeros, the sync, the data and a check character,
// and the board never checks the check character -- the guest does. So a data fault spoils
// the check character and leaves the data, a missing fault takes away the sync, a write fault
// drops the sector, and an id fault is refused: there is no ID field.
void faultTests() {
    SECTION("North Star -- FAULT data: the data is intact, the check character is wrong");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        CHECK(g.b->takesFaults("drive0") && g.b->takesFaults("drive2"), "the MDS-A's drives take faults");
        CHECK(!g.b->takesFaults("drive3"), "...and it has three");
        CHECK(g.b->faults("drive1") == nullptr, "an empty drive has no table to give");
        std::string err;
        CHECK(g.b->faults("drive0")->add("0/0/3 data count=2", err), err.c_str());
        sdReady(g);

        bool data = false;
        CHECK(sdReadAt(g, 3, 3 * 256, data) == 0xFF && data,
              "sector 3: the 256 bytes are the image's, and the check character fails");
        CHECK(first(g)->hits == 1 && first(g)->left == 1, "one hit, one left");
        CHECK(sdReadAt(g, 4, 4 * 256, data) == 0 && data, "sector 4 is good");
        CHECK(sdReadAt(g, 3, 3 * 256, data) == 0xFF && data, "sector 3 fails again");
        CHECK(sdReadAt(g, 3, 3 * 256, data) == 0 && data, "count=2: the third read is good");
        CHECK(first(g)->hits == 2 && first(g)->left == 0, "two hits, none left");
        CHECK(g_media->bytes() == image(kSD), "the image never changed");
    }

    SECTION("North Star -- FAULT data on a data byte the guest never reaches is not counted");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        std::string err;
        g.b->faults("drive0")->add("0/0/5 data", err);
        sdReady(g);
        while ((sdB(g) & 0x0F) != 5) g.toPulse();
        g.m.clock.advance(kBody);
        for (int i = 0; i < 200; ++i) (void)g.rd(CTLRD | CTLNOP);
        g.toPulse();
        CHECK(first(g)->hits == 0, "a read that stops before the check character meets no fault");
    }

    SECTION("North Star -- FAULT missing: no sync, so body never comes on that sector");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        std::string err;
        g.b->faults("drive0")->add("0/0/2 missing", err);
        sdReady(g);
        while ((sdB(g) & 0x0F) != 2) g.toPulse();
        g.m.clock.advance(kSector - 1);
        CHECK((sdA(g) & SABDY) == 0 && (sdA(g) & SABDY) == 0, "sector 2: the whole sector goes by with no body");
        CHECK(g.rd(CTLRD | CTLNOP) == 0, "and read-data has nothing to give");
        CHECK(first(g)->hits == 1, "one hit for the pass, however often the guest looks");
        bool data = false;
        CHECK(sdReadAt(g, 3, 3 * 256, data) == 0 && data, "sector 3 reads");
        while ((sdB(g) & 0x0F) != 2) g.toPulse();
        g.m.clock.advance(kBody);
        CHECK((sdA(g) & SABDY) == 0 && first(g)->hits == 2, "the next pass of sector 2: no body, a second hit");
    }

    SECTION("North Star -- FAULT write: the sector is not written, and the next one is");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        std::string err;
        g.b->faults("drive0")->add("0/0/6 write count=1", err);
        sdReady(g);
        auto writeSec = [&](int sec, uint8_t fill) {
            while ((sdB(g) & 0x0F) != sec) g.toPulse();
            g.rd(CTLWRT);
            g.m.clock.advance(kWindow);
            for (int i = 0; i < 15; ++i) g.rd(SDWD | 0x00);
            g.rd(SDWD | 0xFB);
            for (int i = 0; i < 256; ++i) g.rd(SDWD | fill);
            g.toPulse();
        };
        writeSec(6, 0x11);
        CHECK(g_media->bytes()[6 * 256] == pat(6 * 256) && g_media->bytes()[6 * 256 + 255] == pat(6 * 256 + 255),
              "sector 6 keeps its old bytes");
        CHECK(first(g)->hits == 1 && first(g)->left == 0, "one hit, and the fault wore off");
        CHECK(g.b->drainLog().empty(), "the host is not told: the guest's verify finds it");
        writeSec(7, 0x22);
        CHECK(g_media->bytes()[7 * 256] == 0x22, "sector 7 is written");
        writeSec(6, 0x33);
        CHECK(g_media->bytes()[6 * 256] == 0x33, "count=1: the second write of sector 6 is taken");
    }

    SECTION("North Star -- the address is track/side/sector: the head, the side, the hole");
    {
        Rig g("mdsad");
        g.mount("drive0", image(kQuad));
        std::string err;
        CHECK(g.b->takesFaults("drive3"), "the MDS-A-D has four drives");
        g.b->faults("drive0")->add("1/1/* data", err);
        ddReady(g);
        g.rd(CTLORD | ORDSIN | ORDDS1);
        g.rd(CTLORD | ORDSIN | ORDST | ORDDS1);
        g.rd(CTLORD | ORDSIN | ORDDS1);  // track 1
        g.toPulse();
        int sec = (int)(g.m.clock.now() / kSector % 10);
        g.m.clock.advance(kBody);
        CHECK(ddReadsSector(g, (size_t)(10 + sec) * 512, 512), "track 1, side 0: good");

        g.rd(CTLORD | ORDSS | ORDSIN | ORDDS1);
        g.toPulse();
        sec = (int)(g.m.clock.now() / kSector % 10);
        g.m.clock.advance(kBody);
        CHECK(!ddReadsSector(g, (size_t)((35 + 33) * 10 + sec) * 512, 512) && first(g)->hits == 1,
              "track 1, side 1 -- the 69th track of the file -- fails: the address is the head's, not the file's");
    }

    SECTION("North Star -- an id fault is refused: the sector has no ID field");
    {
        Rig g("mdsad");
        g.mount("drive0", image(kDD));
        std::string err;
        CHECK(!g.b->faults("drive0")->add("0/0/1 id", err) &&
                  err == "bad fault '0/0/1 id': a North Star sector has no ID field. Give data, missing or write",
              err.c_str());
        CHECK(g.b->faults("drive0")->empty(), "nothing was added");
        CHECK(!g.b->loadSubUnit("drive", {{"unit", "1"}, {"mount", "b.nsi"}, {"faults", "0/0/1 data; 2/0/1 id"}}, err) &&
                  err.find("no ID field") != std::string::npos,
              "the `faults` key refuses it too");
        g.b->faults("drive0")->clearAll();
        CHECK(!g.b->faults("drive0")->add("0/0/1 id", err), "CLEAR ALL keeps the refusal");
        std::string uerr;
        g.b->unmount("drive0", uerr);
        g.mount("drive0", image(kDD));
        CHECK(!g.b->faults("drive0")->add("0/0/1 id", err), "...and so does a new disk");
    }

    SECTION("North Star -- DEBUG=fault: one line a use");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        g.b->ensureDebugChannel();
        std::string err;
        CHECK(g.b->debugChannel()->enable("fault", err), "the fault flag exists");
        g.b->faults("drive0")->add("0/0/2 missing count=2", err);
        sdReady(g);
        const std::string text = debugCapture([&] {
            while ((sdB(g) & 0x0F) != 2) g.toPulse();
            g.m.clock.advance(kBody);
            (void)sdA(g);
        });
        CHECK(text.find("fault drive0 0/0/2 missing hit 1, 1 left") != std::string::npos, text.c_str());
    }

    SECTION("North Star -- the `faults` key, SNAPSHOT, and MOUNT clearing the table");
    {
        Rig         g("mdsa");
        std::string err;
        CHECK(!g.b->loadSubUnit("drive", {{"unit", "1"}, {"faults", "2/0/0 data"}}, err) && !err.empty(),
              "`faults` with no `mount` is refused");
        g.mount("drive0", image(kSD));  // installs the resolver for the table's mount
        CHECK(g.b->loadSubUnit("drive", {{"unit", "1"}, {"mount", "b.nsi"}, {"faults", "0/0/3 data count=3; 4/*/* missing"}},
                               err),
              err.c_str());
        bool saved = false;
        for (const auto& su : g.b->subUnits())
            for (const auto& fld : su.fields)
                saved = saved || (fld.key == "faults" && fld.quoted && fld.text == "0/0/3 data count=3; 4/*/* missing");
        CHECK(saved, "CONFIG SAVE writes the `faults` key back, quoted");

        // A damaged read in flight survives a snapshot.
        g.rd(CTLMO | CTLNOP);
        g.rd(0xEB02);  // CTLDS2: drive 2 is unit 1
        while ((sdB(g) & 0x0F) != 3) g.toPulse();
        g.m.clock.advance(kBody);
        for (int i = 0; i < 100; ++i) (void)g.rd(CTLRD | CTLNOP);
        StateWriter w;
        g.b->serialize(w);

        g.b->faults("drive1")->clearAll();
        for (int i = 100; i < 256; ++i) (void)g.rd(CTLRD | CTLNOP);
        g.rd(CTLRD | CTLNOP);

        StateReader r(w.data());
        g.b->deserialize(r);
        CHECK(r.ok(), "restores cleanly");
        uint8_t chk = 0;
        for (int i = 0; i < 256; ++i) {
            const uint8_t v = i < 100 ? pat(3 * 256 + (size_t)i) : g.rd(CTLRD | CTLNOP);
            chk = rol(v ^ chk);
        }
        CHECK((uint8_t)(g.rd(CTLRD | CTLNOP) ^ chk) == 0xFF, "the read resumes, and its check character still fails");
        const DiskFaults* f = g.b->faults("drive1");
        CHECK(f && f->list().size() == 2 && f->list()[0].hits == 1 && f->list()[0].left == 2,
              "RESTORE brings the faults back with their counts");
        CHECK(!g.b->faults("drive1")->add("0/0/1 id", err), "...and the drive still refuses an id fault");

        CHECK(g.mount("drive1", image(kSD)) && g.b->faults("drive1")->empty(),
              "a new disk in the drive, with no UNMOUNT first, brings no faults");
        g.b->faults("drive1")->add("0/0/1 data", err);
        std::string uerr;
        CHECK(g.b->unmount("drive1", uerr) && g.b->faults("drive1") == nullptr, "UNMOUNT: no disk, no faults");
        g.mount("drive1", image(kSD));
        CHECK(g.b->faults("drive1")->empty(), "...and the next disk has none");
    }
}

} // namespace

void test_northstar() {
    // =====================================================================================
    // THE WINDOW IN MEMORY
    // =====================================================================================
    SECTION("North Star -- the board is a 1 K block of memory, and a READ is the command");
    {
        Rig g("mdsa");
        CHECK(g.b != nullptr, "the registry builds an mdsa");

        CHECK(g.rd(0xE900) == 0x31 && g.rd(0xE901) == 0x14 && g.rd(0xE902) == 0x21,
              "E900: LXI SP,2114 -- the first instruction of the PROM");
        CHECK(g.rd(0xE9FE) == 0xE9 && g.rd(0xE91E) == 0xF5,
              "E91E is DISKOP's PUSH PSW: the entry that DOS and the CP/M loader CALL");
        CHECK(g.rd(0xE800) == 0x31 && g.rd(0xE81E) == 0xF5,
              "case 0 reads the same PROM (errata of July 31 1978)");

        BusCycle w;
        w.type = Cycle::MemWrite;
        w.addr = 0xEB18;
        CHECK(!g.b->decodes(w), "a memory WRITE to the block is not decoded: the board gates on sMEMR");
        BusCycle io;
        io.type = Cycle::IoRead;
        io.addr = 0xE8;
        CHECK(!g.b->decodes(io), "and it has no ports at all");

        CHECK(g.rd(0xE7FF) == 0xFF && g.rd(0xEC00) == 0xFF, "one byte either side is not the board");

        // Moving the base moves all four cases.
        CHECK(g.set("base", "D000"), "base = D000 is a 1 K boundary");
        CHECK(g.rd(0xD100) == 0x31, "the PROM moved with it");
        CHECK(g.rd(0xE900) == 0xFF, "and E900 now floats");
        CHECK((g.rd(0xD310) & SAMO) == 0, "D310 is the status byte now (motors off)");
        g.rd(0xD390);
        CHECK((g.rd(0xD310) & SAMO) != 0, "and D390 is the motor-on command");

        CHECK(!g.set("base", "E900"), "E900 is refused: the board selects on six address bits");
        CHECK(g.err.find("0x400") != std::string::npos, "and the error says why");
        CHECK(!g.set("drives", "4"), "an MDS-A has three drive-select lines, not four");
    }

    SECTION("North Star -- DISASM must not step the head: peek answers for the PROM only");
    {
        Rig g("mdsa");
        uint8_t v = 0;
        CHECK(g.b->peek(0xE900, v) && v == 0x31, "the PROM can be looked at");
        CHECK(g.b->peek(0xE800, v) && v == 0x31, "in both of its pages");
        CHECK(!g.b->peek(0xEA00, v), "write-data cannot: looking would write");
        CHECK(!g.b->peek(0xEB10, v), "a command cannot: looking would act");

        g.m.clock.advance(kFreeRun);
        CHECK((sdA(g) & SASF) != 0, "a sector pulse has set the flag");
        (void)g.m.bus.peek(CTLRSF);
        (void)g.m.bus.peek(CTLMO | CTLNOP);
        CHECK((sdA(g) & SASF) != 0, "the debugger looked at 'reset sector flag' and nothing was reset");
        CHECK((sdA(g) & SAMO) == 0, "and at 'motors on', and they are still off");

        Rig d("mdsad");
        CHECK(d.b->peek(0xE800, v) && v == 0x0E, "MDS-A-D: the PROM is case 0");
        CHECK(!d.b->peek(0xE900, v), "and case 1 is write-data there, not a second PROM page");
    }

    // =====================================================================================
    // MDS-A
    // =====================================================================================
    SECTION("MDS-A -- the status is sampled BEFORE the command acts (the PROM relies on it)");
    {
        Rig g("mdsa");
        CHECK(g.mount("drive0", image(kSD)), "an 89,600-byte image mounts");

        // MOTON: `lda CTLMO OR CTLNOP / ani SAMO / jnz MOTON2` -- start the motors AND ask
        // whether they were already running.
        CHECK((g.rd(CTLMO | CTLNOP) & SAMO) == 0, "the access that starts the motors reports them OFF");
        CHECK((g.rd(CTLMO | CTLNOP) & SAMO) != 0, "and the next one reports them on");

        // A select with the motors off does nothing: the register is held clear.
        Rig h("mdsa");
        h.mount("drive0", image(kSD));
        h.rd(CTLDS1);
        CHECK((sdA(h) & SATR0) == 0, "motors off: select is ignored, so no drive reports track 0");
        h.rd(CTLMO | CTLNOP);
        h.rd(CTLDS1);
        CHECK((sdA(h) & SATR0) != 0, "motors on: drive 1 is selected and is at track 0");
    }

    SECTION("MDS-A -- sector flag, window and sector position are readings off the clock");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        sdReady(g);
        g.toPulse();
        const uint64_t s0 = g.m.clock.now() / kSector;

        g.rd(CTLRSF);
        CHECK((sdA(g) & SASF) == 0, "the flag is reset by software");
        g.m.clock.advance(kSector - 1);
        CHECK((sdA(g) & SASF) == 0, "one T-state short of 20 ms: no pulse yet");
        g.m.clock.advance(1);
        uint8_t a = sdA(g);
        CHECK((a & SASF) != 0, "20 ms -- 40,000 T at 2 MHz -- and the sector flag is set");
        CHECK((a & SAWN) != 0, "and this read fell inside the window");
        CHECK((sdB(g) & 0x0F) == (int)((s0 + 1) % 10), "B-status carries the sector position");

        g.m.clock.advance(kWindow - 1);
        CHECK((sdA(g) & SAWN) != 0, "95.5 us in: still the window");
        g.m.clock.advance(1);
        CHECK((sdA(g) & SAWN) == 0, "96 us: the window has shut");
        CHECK((sdA(g) & SASF) != 0, "the flag stays until software resets it");

        // Reading a status byte does not turn the disk.
        for (int i = 0; i < 500; ++i) (void)sdB(g);
        CHECK((sdB(g) & 0x0F) == (int)((s0 + 1) % 10), "500 polls later it is the same sector");

        g.m.clock.advance(kSector * 10);
        CHECK((sdB(g) & 0x0F) == (int)((s0 + 1) % 10), "ten sectors on is one revolution: 200 ms");
        CHECK((sdB(g) & 0x20) == 0 && (sdA(g) & 0x20) == 0, "bit 5 is the diagnostic pad: it reads 0");
    }

    SECTION("MDS-A -- body comes after the preamble and the sync, and a sector reads back");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        sdReady(g);
        g.toPulse();
        const int sec = (int)(g.m.clock.now() / kSector % 10);

        g.m.clock.advance(kBody - 1);
        CHECK((sdA(g) & SABDY) == 0, "16 zeros and the sync are not past the head yet");
        g.m.clock.advance(1);
        CHECK((sdA(g) & SABDY) != 0, "1184 us after the sector hole: body");

        CHECK(sdReadsSector(g, (size_t)sec * 256),
              "256 bytes of track 0, then a check character the PROM's own XRA/RLC accepts");

        // And the PROM's whole hunt: wait for sector 4, the boot sector.
        while ((sdB(g) & 0x0F) != 4) g.toPulse();
        g.m.clock.advance(kBody);
        CHECK(sdReadsSector(g, 4 * 256), "sector 4 of track 0 is at byte 1024 of the file");

        g.toPulse();
        CHECK((sdA(g) & SABDY) == 0, "the next sector pulse ends body mode");
    }

    SECTION("MDS-A -- a sector that is not in the file has no sync: body never comes");
    {
        Rig g("mdsa");
        CHECK(g.mount("drive0", image(3 * 256)), "a short file mounts: it is a part-formatted diskette");
        sdReady(g);
        while ((sdB(g) & 0x0F) != 2) g.toPulse();
        g.m.clock.advance(kBody);
        CHECK((sdA(g) & SABDY) != 0, "sector 2 is in the file");
        g.toPulse();
        g.m.clock.advance(kSector - 1);
        CHECK((sdA(g) & SABDY) == 0, "sector 3 is not: the whole sector goes by with no body");
        CHECK(g.rd(CTLRD | CTLNOP) == 0, "and read-data has nothing to give");

        Rig e("mdsa");
        CHECK(e.mount("drive0", {}), "an empty file mounts as a blank diskette");
        sdReady(e);
        e.toPulse();
        e.m.clock.advance(kBody);
        CHECK((sdA(e) & SABDY) == 0, "and a blank diskette has no body anywhere");
    }

    SECTION("MDS-A -- the head steps when the step pulse ENDS, in the latched direction");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        sdReady(g);
        CHECK((sdA(g) & SATR0) != 0, "at track 0");

        g.rd(CTLSTI);
        g.rd(CTLSTS);
        CHECK((sdA(g) & SATR0) != 0, "step flip-flop set: the pulse has begun, the head has not moved");
        g.rd(CTLSTC);
        CHECK((sdA(g) & SATR0) == 0, "cleared: the head is on track 1");

        g.toPulse();
        const int sec = (int)(g.m.clock.now() / kSector % 10);
        g.m.clock.advance(kBody);
        CHECK(sdReadsSector(g, (size_t)(10 + sec) * 256), "and what it reads is track 1");

        g.rd(CTLSTO);
        g.rd(CTLSTS);
        g.rd(CTLSTC);
        CHECK((sdA(g) & SATR0) != 0, "one step out: track 0 again");
        g.rd(CTLSTS);
        g.rd(CTLSTC);
        CHECK((sdA(g) & SATR0) != 0, "the drive stops at track 0");

        g.rd(CTLSTI);
        for (int i = 0; i < 50; ++i) { g.rd(CTLSTS); g.rd(CTLSTC); }
        g.toPulse();
        const int s2 = (int)(g.m.clock.now() / kSector % 10);
        g.m.clock.advance(kBody);
        CHECK(sdReadsSector(g, (size_t)(340 + s2) * 256), "50 steps in stops at track 34, the last");
    }

    SECTION("MDS-A -- write: taken only in the window; zeros, sync, data, check");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        sdReady(g);
        g.toPulse();
        const size_t off = (size_t)(g.m.clock.now() / kSector % 10) * 256;

        g.rd(CTLWRT);
        CHECK((sdA(g) & SAWRT) == 0, "in the window: writing has not begun");
        g.m.clock.advance(kWindow);
        CHECK((sdA(g) & SAWRT) != 0, "after the window the controller is ready for data");
        CHECK((sdA(g) & SABDY) == 0, "and it is not reading");

        for (int i = 0; i < 15; ++i) g.rd(SDWD | 0x00);  // the controller wrote the first zero
        g.rd(SDWD | 0xFB);
        uint8_t chk = 0;
        for (int i = 0; i < 256; ++i) {
            uint8_t v = (uint8_t)(0xA5 ^ i);
            if (i < 4) v = 0;  // data may begin with zeros: they are data, not preamble
            g.rd(SDWD | v);
            chk = rol(v ^ chk);
        }
        const int syncs = g_media->syncs();
        CHECK(syncs > 0, "the sector reached the host with its last data byte");
        g.rd(SDWD | chk);

        const auto& by = g_media->bytes();
        bool ok = by.size() == kSD;
        for (int i = 0; ok && i < 256; ++i)
            ok = by[off + (size_t)i] == (i < 4 ? 0 : (uint8_t)(0xA5 ^ i));
        CHECK(ok, "the file holds the 256 data bytes -- not the preamble, the sync or the check");
        CHECK(by[off + 256] == pat(off + 256) && (off == 0 || by[off - 1] == pat(off - 1)),
              "and its neighbours are untouched");

        g.rd(SDWD | 0x77);
        CHECK(g_media->bytes()[off] == 0, "a byte after the check character goes nowhere");

        // The same sector, read back through the read path.
        g.toPulse();
        while ((size_t)(sdB(g) & 0x0F) * 256 != off) g.toPulse();
        g.m.clock.advance(kBody);
        uint8_t c2 = 0;
        bool    rb = true;
        for (int i = 0; i < 256; ++i) {
            uint8_t v = g.rd(CTLRD | CTLNOP);
            if (v != (i < 4 ? 0 : (uint8_t)(0xA5 ^ i))) rb = false;
            c2 = rol(v ^ c2);
        }
        CHECK(rb && g.rd(CTLRD | CTLNOP) == c2, "it reads back, with the check character recomputed");

        // Outside the window the command is not taken.
        g.toPulse();
        g.m.clock.advance(kWindow);
        const size_t off2 = (size_t)(g.m.clock.now() / kSector % 10) * 256;
        g.rd(CTLWRT);
        g.m.clock.advance(10);
        CHECK((sdA(g) & SAWRT) == 0, "begin-write after the window is ignored");
        g.rd(SDWD | 0xFB);
        for (int i = 0; i < 256; ++i) g.rd(SDWD | 0xEE);
        CHECK(g_media->bytes()[off2] != 0xEE || off2 == off, "and its bytes are not written");

        // A write cut short by the next sector pulse writes nothing.
        g.toPulse();
        const size_t off3 = (size_t)(g.m.clock.now() / kSector % 10) * 256;
        g.rd(CTLWRT);
        g.m.clock.advance(kWindow);
        g.rd(SDWD | 0xFB);
        for (int i = 0; i < 100; ++i) g.rd(SDWD | 0xDD);
        g.toPulse();
        CHECK((sdA(g) & SAWRT) == 0, "the sector pulse shuts the write gate");
        CHECK(g_media->bytes()[off3] != 0xDD, "a sector with no end is not committed");
    }

    SECTION("MDS-A -- a write-protected diskette says so, and is not written");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD), /*ro=*/true);
        sdReady(g);
        CHECK((sdA(g) & SAWP) != 0, "WP is in A-status: the guest can know before it writes");

        g.toPulse();
        const size_t off = (size_t)(g.m.clock.now() / kSector % 10) * 256;
        g.rd(CTLWRT);
        g.m.clock.advance(kWindow);
        g.rd(SDWD | 0xFB);
        for (int i = 0; i < 256; ++i) g.rd(SDWD | 0x11);
        CHECK(g_media->bytes()[off] == pat(off), "the image is unchanged");
        auto log = g.b->drainLog();
        CHECK(log.size() == 1 && log[0].find("write-protected") != std::string::npos,
              "and the host is told, once");

        Rig u("mdsa");
        u.mount("drive0", image(kSD));
        sdReady(u);
        CHECK((sdA(u) & SAWP) == 0, "a writable diskette reads WP = 0");
    }

    SECTION("MDS-A -- three drives, selected in binary; an empty one has no diskette to turn");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        std::vector<uint8_t> other(kSD, 0x5A);
        g.mount("drive2", other);
        g.rd(CTLMO | CTLNOP);

        g.rd(0xEB03);  // M1M0 = 11: drive 3
        g.toPulse();
        g.m.clock.advance(kBody);
        CHECK((sdA(g) & SABDY) != 0 && g.rd(CTLRD | CTLNOP) == 0x5A, "M1M0 = 3 is drive 3 (unit drive2)");

        g.rd(0xEB02);  // drive 2: nothing in it
        g.toPulse(kFreeRun);
        const uint64_t f0 = g.m.clock.now() / kFreeRun;
        CHECK((sdB(g) & 0x0F) == (int)(f0 % 10), "no diskette: the board counts its own pulses");
        g.rd(CTLRSF);
        g.m.clock.advance(kSector);
        CHECK((sdA(g) & SASF) == 0, "20 ms is not a sector time now");
        g.m.clock.advance(kFreeRun - kSector);
        CHECK((sdA(g) & SASF) != 0, "32.768 ms is: HOLE NOT FOUND supplies the pulse");
        g.m.clock.advance(kBody);
        CHECK((sdA(g) & SABDY) == 0, "and there is never a body");
        for (int i = 0; i < 12; ++i) {
            g.m.clock.advance(kFreeRun);
            CHECK((sdB(g) & 0x0F) <= 9, "the counter is a decade counter: 0 to 9, diskette or not");
        }

        g.rd(0xEB00);  // M1M0 = 00: no drive
        CHECK((sdA(g) & SATR0) == 0, "M1M0 = 0 selects nothing");
    }

    SECTION("MDS-A -- the interrupt is SECTOR FLAG and INT ARM");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        sdReady(g);
        g.rd(CTLRSF);

        g.rd(CTLEI);
        g.m.clock.advance(kSector);
        CHECK(!g.b->assertsInt() && g.b->assertsVi() == 0, "armed, but no jumper: no interrupt");
        CHECK(g.m.clock.queued() == 0, "and with no jumper the board holds no deadline");

        CHECK(g.set("interrupt", "int"), "jumper to PINT");
        g.rd(CTLRSF);
        CHECK(!g.b->assertsInt(), "flag reset: the line is up");
        g.m.clock.advance(kSector);
        CHECK(g.b->assertsInt(), "at the sector pulse it comes down -- nobody had to read a status byte");
        g.m.clock.advance(kSector * 3);
        CHECK(g.b->assertsInt(), "and stays down");
        g.rd(CTLRSF);
        CHECK(!g.b->assertsInt(), "until software resets the sector flag");

        g.rd(CTLDI);
        g.m.clock.advance(kSector);
        CHECK(!g.b->assertsInt(), "disarmed: the next pulse sets the flag and raises nothing");
        CHECK((sdA(g) & SASF) != 0, "(the flag itself is set)");
        CHECK(g.m.clock.queued() == 0, "and disarmed, the deadline is gone");

        CHECK(g.set("interrupt", "vi3"), "jumper to VI3");
        g.rd(CTLEI);
        CHECK(g.b->assertsVi() == 0x08 && !g.b->assertsInt(), "the same level, on VI3 and not on PINT");
    }

    SECTION("MDS-A -- motor = real: off after 16 revolutions, and the select register clears");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        sdReady(g);
        g.m.clock.advance(kOffSD * 3);
        CHECK((sdA(g) & SAMO) != 0, "motor = free (the default): they are still running");

        CHECK(g.set("motor", "real"), "motor = real");
        g.m.clock.advance(kOffSD - 1);
        CHECK((sdA(g) & (SAMO | SATR0)) == (SAMO | SATR0), "one T-state short of 3.2 s: on, drive selected");
        g.m.clock.advance(1);
        uint8_t a = sdA(g);
        CHECK((a & SAMO) == 0, "3.2 s -- 16 revolutions -- and the motors are off");
        CHECK((a & SATR0) == 0, "and no drive is selected: MOTOR-ENB clears the register");
        CHECK(g.m.clock.queued() == 0, "the timer is a comparison, not a deadline: the machine can idle");

        // Every command with MO set starts the count again.
        sdReady(g);
        g.m.clock.advance(kOffSD - 1);
        g.rd(CTLMO | CTLNOP);
        g.m.clock.advance(kOffSD - 1);
        CHECK((sdA(g) & SAMO) != 0, "MO restarts the count");
    }

    SECTION("MDS-A -- reset: the command and POC, but not the front-panel RESET");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        g.set("interrupt", "int");
        sdReady(g);
        g.rd(CTLEI);
        g.m.clock.advance(kSector);
        CHECK(g.b->assertsInt(), "armed and interrupting");

        g.b->reset(Reset::Bus);
        CHECK((sdA(g) & SAMO) != 0 && g.b->assertsInt(), "RESET* does not reach this board");

        g.rd(CTLRES);
        uint8_t a = sdA(g);
        CHECK((a & SAMO) == 0, "the reset command stops the motors");
        CHECK((a & SATR0) == 0, "raises the heads (no drive selected)");
        CHECK(!g.b->assertsInt(), "and disarms the interrupt");
        CHECK((a & SASF) != 0, "the sector flag is not part of it: only its own command resets that");

        sdReady(g);
        g.b->reset(Reset::PowerOn);
        CHECK((sdA(g) & SAMO) == 0, "POC does the same");
    }

    SECTION("MDS-A -- snapshot and restore, in the middle of a sector");
    {
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        sdReady(g);
        g.rd(CTLSTI);
        g.rd(CTLSTS);
        g.rd(CTLSTC);  // track 1
        g.toPulse();
        const size_t off = (size_t)(10 + g.m.clock.now() / kSector % 10) * 256;
        g.m.clock.advance(kBody);
        for (int i = 0; i < 100; ++i) (void)g.rd(CTLRD | CTLNOP);

        StateWriter w;
        g.b->serialize(w);

        for (int i = 0; i < 7; ++i) (void)g.rd(CTLRD | CTLNOP);
        g.rd(CTLSTO);
        g.rd(CTLSTS);
        g.rd(CTLSTC);
        g.rd(CTLRES);

        StateReader r(w.data());
        g.b->deserialize(r);
        uint8_t a = sdA(g);
        CHECK((a & SAMO) != 0 && (a & SATR0) == 0, "the motor, the selected drive and its track came back");
        CHECK(g.rd(CTLRD | CTLNOP) == pat(off + 100), "and the read resumes at byte 100 of the sector");
    }

    SECTION("MDS-A -- any size mounts; the one medium is 35 x 10 x 256");
    {
        Rig g("mdsa");
        CHECK(g.mount("drive0", image(kSD)), "89,600 bytes");
        CHECK(g.mount("drive0", image(kSD + 64)), "an XMODEM pad is tolerated");
        CHECK(g.mount("drive0", image(kDD)), "a double-density image mounts (and will not read: wrong board)");
        CHECK(!g.b->mount("drive3", "a.nsi", false, g.err), "there is no drive3 on an MDS-A");
        CHECK(g.b->unmount("drive0", g.err), "unmount");
        CHECK(!g.b->unmount("drive0", g.err), "and an empty drive cannot be unmounted again");
    }

    // =====================================================================================
    // MDS-A-D
    // =====================================================================================
    SECTION("MDS-A-D -- the cases moved: PROM, write data, orders, commands");
    {
        Rig g("mdsad");
        CHECK(g.b != nullptr, "the registry builds an mdsad");
        CHECK(g.rd(0xE800) == 0x0E && g.rd(0xE801) == 0x0A, "E800: MVI C,10 -- the first instruction");
        CHECK(g.rd(0xE8FF) == 0xE9, "the last byte of the PROM");
        CHECK(g.rd(0xE900) == 0x00, "E900 is not the PROM here: it is write-data, and reads 0");
        CHECK(g.set("drives", "4"), "four drives");
        CHECK(!g.set("drives", "5"), "and no more");
    }

    SECTION("MDS-A-D -- three status bytes; IX marks sector 0; RE at 480 us, DD at 512, BD at 1184");
    {
        Rig g("mdsad");
        CHECK(g.mount("drive0", image(kDD)), "a 179,200-byte image mounts");
        CHECK((g.rd(CTLCMD | DMAS | CCMO) & DSMO) == 0, "the access that starts the motors reports them off");
        g.rd(CTLORD | ORDDS1);
        CHECK((ddA(g) & DSMO) != 0 && (ddB(g) & DSMO) != 0 && (ddC(g) & DSMO) != 0,
              "MO is in all three status bytes");
        CHECK((ddB(g) & DBT0) != 0, "T0 is in B-status");

        g.toPulse();
        while ((ddC(g) & 0x0F) != 0) g.toPulse();
        CHECK((ddA(g) & DSIX) != 0, "sector 0: the index hole went by in the sector before");
        CHECK((ddA(g) & DSSF) != 0 && (ddA(g) & DAWI) != 0, "SF, and WI for the first 96 us");
        g.rd(CTLCMD | DMAS | CCRSF);
        CHECK((ddA(g) & DSSF) == 0, "code 1 resets the sector flag");

        g.m.clock.advance(kWindow);
        CHECK((ddA(g) & DAWI) == 0, "96 us: WI is false");
        CHECK((ddA(g) & DARE) == 0, "and read is not enabled yet");
        g.m.clock.advance(kRE - kWindow);
        uint8_t a = ddA(g);
        CHECK((a & DARE) != 0, "480 us: RE");
        CHECK((a & DSDD) == 0, "but the density is not known yet");
        g.m.clock.advance(kDDI - kRE);
        CHECK((ddA(g) & DSDD) != 0, "512 us: DD -- this diskette is double density");
        CHECK((ddB(g) & DSDD) != 0 && (ddC(g) & DSDD) != 0, "(in all three bytes)");
        g.m.clock.advance(kBody - kDDI - 1);
        CHECK((ddA(g) & DABD) == 0, "one T-state short of 1184 us: no body");
        g.m.clock.advance(1);
        CHECK((ddA(g) & DABD) != 0, "1184 us: BD");

        g.toPulse();
        CHECK((ddC(g) & 0x0F) == 1 && (ddA(g) & DSIX) == 0, "sector 1: IX is false again");

        CHECK(g.rd(CTLCMD) == 0 && g.rd(CTLCMD | 0x50) == 0, "DM = 0 and DM = 5 put nothing on the bus");
    }

    SECTION("MDS-A-D -- reads 512-byte double density, and 256-byte single density");
    {
        Rig g("mdsad");
        g.mount("drive0", image(kDD));
        ddReady(g);
        while ((ddC(g) & 0x0F) != 4) g.toPulse();
        g.m.clock.advance(kBody);
        CHECK(ddReadsSector(g, 4 * 512, 512), "DD: sector 4 of track 0 -- the boot sector -- is 512 bytes");

        Rig s("mdsad");
        CHECK(s.mount("drive0", image(kSD)), "an 89,600-byte image is single density on this board too");
        ddReady(s);
        while ((ddC(s) & 0x0F) != 7) s.toPulse();
        s.m.clock.advance(kBody);
        CHECK((ddA(s) & DSDD) == 0, "the DD indicator stays false");
        CHECK(ddReadsSector(s, 7 * 256, 256), "SD: a 256-byte sector and its check character");
    }

    SECTION("MDS-A-D -- orders: one-hot drive select, a step is ST low-high-low, SS picks the side");
    {
        Rig g("mdsad");
        g.mount("drive0", image(kQuad));
        std::vector<uint8_t> d3(kDD, 0x33), d4(kDD, 0x44);
        g.mount("drive2", d3);
        g.mount("drive3", d4);
        g.rd(CTLCMD | DMAS | CCMO);

        g.rd(CTLORD | 0x04);
        g.toPulse();
        g.m.clock.advance(kBody);
        CHECK(g.rd(CTLCMD | DMRD) == 0x33, "DS = 4 is drive 3");
        g.rd(CTLORD | 0x08);
        g.toPulse();
        g.m.clock.advance(kBody);
        CHECK(g.rd(CTLCMD | DMRD) == 0x44, "DS = 8 is drive 4");
        g.rd(CTLORD | 0x00);
        CHECK((ddB(g) & DBT0) == 0, "DS = 0 selects no drive");

        // HOME in the PROM: step in once.
        g.rd(CTLORD | ORDSIN | ORDDS1);
        g.rd(CTLORD | ORDSIN | ORDST | ORDDS1);
        CHECK((ddB(g) & DBT0) != 0, "ST high: the pulse has begun");
        g.rd(CTLORD | ORDSIN | ORDDS1);
        CHECK((ddB(g) & DBT0) == 0, "ST low again: one step in");
        g.toPulse();
        int sec = (int)(g.m.clock.now() / kSector % 10);
        g.m.clock.advance(kBody);
        CHECK(ddReadsSector(g, (size_t)(10 + sec) * 512, 512), "track 1, side 1");

        // Side 2 is recorded from the inside out: DOS track 35 is physical track 34.
        g.rd(CTLORD | ORDSS | ORDSIN | ORDDS1);
        g.toPulse();
        sec = (int)(g.m.clock.now() / kSector % 10);
        g.m.clock.advance(kBody);
        CHECK(ddReadsSector(g, (size_t)((35 + 33) * 10 + sec) * 512, 512),
              "SS = 1 on physical track 1 is the 69th track of the file (35 + 33)");

        g.rd(CTLORD | ORDDS1);
        g.rd(CTLORD | ORDST | ORDDS1);
        g.rd(CTLORD | ORDDS1);
        CHECK((ddB(g) & DBT0) != 0, "DP = 0: one step out, to track 0");

        // A single-sided diskette has no side 2.
        Rig s("mdsad");
        s.mount("drive0", image(kDD));
        ddReady(s, ORDSS | ORDDS1);
        s.toPulse();
        s.m.clock.advance(kBody);
        CHECK((ddA(s) & DABD) == 0, "SS = 1 on a single-sided image: no body");
    }

    SECTION("MDS-A-D -- write: 31 zeros, FB FB, 512 bytes; density comes from the DD order");
    {
        Rig g("mdsad");
        g.mount("drive0", image(kDD));
        ddReady(g, ORDDD | ORDDS1);
        g.toPulse();
        const size_t off = (size_t)(g.m.clock.now() / kSector % 10) * 512;

        g.rd(CTLCMD | DMAS | CCWR);
        CHECK((ddB(g) & DBWR) == 0, "in the window: not writing yet");
        g.m.clock.advance(kWindow);
        CHECK((ddB(g) & DBWR) != 0, "WI false: WR is true");
        CHECK((ddA(g) & DARE) == 0, "and the read path is off");

        for (int i = 0; i < 31; ++i) g.rd(CTLWD | 0x00);
        g.rd(CTLWD | 0xFB);
        g.rd(CTLWD | 0xFB);
        for (int i = 0; i < 512; ++i) g.rd(CTLWD | (uint8_t)(i * 3));
        g.rd(CTLWD | 0x00);  // the check character; its value is not kept

        const auto& by = g_media->bytes();
        bool ok = true;
        for (int i = 0; ok && i < 512; ++i) ok = by[off + (size_t)i] == (uint8_t)(i * 3);
        CHECK(ok, "the 512 data bytes are in the file, after BOTH sync characters");
        CHECK(by[off + 512] == pat(off + 512), "and the next sector is untouched");
        CHECK(g_media->syncs() > 0, "it went to the host at once");

        // The wrong density for the image is refused, and said.
        g.rd(CTLORD | ORDDS1);  // DD = 0
        g.toPulse();
        const size_t off2 = (size_t)(g.m.clock.now() / kSector % 10) * 512;
        g.rd(CTLCMD | DMAS | CCWR);
        g.m.clock.advance(kWindow);
        g.rd(CTLWD | 0xFB);
        for (int i = 0; i < 256; ++i) g.rd(CTLWD | 0x99);
        CHECK(g_media->bytes()[off2] != 0x99, "a single-density write to a double-density image is discarded");
        auto log = g.b->drainLog();
        CHECK(log.size() == 1 && log[0].find("density") != std::string::npos, "and the host is told why");
    }

    SECTION("MDS-A-D -- a blank diskette takes the density of its first write");
    {
        Rig g("mdsad");
        CHECK(g.mount("drive0", {}), "an empty file mounts as a blank diskette");
        ddReady(g, ORDDD | ORDDS1);
        g.toPulse();
        g.m.clock.advance(kBody);
        CHECK((ddA(g) & (DABD | DSDD)) == 0, "blank: no body, no density");

        while ((ddC(g) & 0x0F) != 3) g.toPulse();
        g.rd(CTLCMD | DMAS | CCWR);
        g.m.clock.advance(kWindow);
        for (int i = 0; i < 31; ++i) g.rd(CTLWD);
        g.rd(CTLWD | 0xFB);
        g.rd(CTLWD | 0xFB);
        for (int i = 0; i < 512; ++i) g.rd(CTLWD | 0xC3);
        CHECK(g_media->bytes().size() == 4 * 512, "the file grew to hold sector 3 at 512 bytes a sector");
        CHECK(g_media->bytes()[3 * 512] == 0xC3 && g_media->bytes()[4 * 512 - 1] == 0xC3, "and holds it");

        CHECK((ddA(g) & DABD) == 0, "the write gate is still open: this pass of the sector is not read");
        g.toPulse();
        while ((ddC(g) & 0x0F) != 3) g.toPulse();
        g.m.clock.advance(kBody);
        CHECK((ddA(g) & (DABD | DSDD)) == (DABD | DSDD), "one revolution on, the sector has a body, and it is double density");

        // Side 2 of a blank is reachable: the format is the two-sided one.
        g.rd(CTLORD | ORDDD | ORDSS | ORDDS1);
        g.toPulse();
        const int sec = (int)(g.m.clock.now() / kSector % 10);
        g.rd(CTLCMD | DMAS | CCWR);
        g.m.clock.advance(kWindow);
        g.rd(CTLWD | 0xFB);
        g.rd(CTLWD | 0xFB);
        for (int i = 0; i < 512; ++i) g.rd(CTLWD | 0x3C);
        CHECK(g_media->bytes().size() == (size_t)((35 + 34) * 10 + sec + 1) * 512,
              "side 2, physical track 0, is the LAST track of the file");
    }

    SECTION("MDS-A-D -- the probe: three sizes, an empty file, or `media`");
    {
        Rig g("mdsad");
        CHECK(g.mount("drive0", image(kSD)), "89,600 = sd");
        CHECK(g.mount("drive0", image(kDD)), "179,200 = dd");
        CHECK(g.mount("drive0", image(kQuad)), "358,400 = quad");
        CHECK(g.mount("drive0", {}), "0 = blank");
        CHECK(!g.mount("drive0", image(1000)), "1000 bytes is refused: the board cannot tell the density");
        CHECK(g.err.find("media") != std::string::npos && g.err.find("179200") != std::string::npos,
              "and the error names the sizes and the `media` key");

        // `media` in any case forces the format (#761): `DD` once matched no name by exact
        // compare, so the probe fell back to the size and refused the same 1000 bytes.
        CHECK(g.b->loadSubUnit("drive", {{"unit", "0"}, {"media", "DD"}, {"mount", "a.nsi"}}, g.err),
              "media = \"DD\" forces dd on an image whose size says nothing");

        CHECK(g.mount("drive0", image(kDD), /*ro=*/true), "a write-protected diskette mounts");
        ddReady(g);
        CHECK((ddB(g) & DBWP) != 0, "and WP is in B-status");
    }

    SECTION("MDS-A-D -- with nothing to see, the sector counter runs free to 15; no index");
    {
        Rig g("mdsad");
        int  top = 0;
        bool ix  = false;
        for (int i = 0; i < 40; ++i) {
            g.m.clock.advance(kFreeRun);
            uint8_t c = ddC(g);
            if ((c & 0x0F) > top) top = c & 0x0F;
            if (c & DSIX) ix = true;
        }
        CHECK(top == 15, "motors off: a 4-bit counter at 32.768 ms a count");
        CHECK(!ix, "and IX is never true -- which is what hangs the PROM at HANG1 with no diskette");

        g.m.clock.advance(kRE);
        g.toPulse(kFreeRun);
        g.m.clock.advance(kRE);
        CHECK((ddA(g) & DARE) != 0, "RE still comes: WAITRD in the PROM has no timeout");
    }

    SECTION("MDS-A-D -- arm, disarm, set body, reset; 9.6 s motor");
    {
        Rig g("mdsad");
        g.mount("drive0", image(kDD));
        g.set("interrupt", "vi5");
        ddReady(g);
        g.rd(CTLCMD | DMAS | CCRSF);
        g.rd(CTLCMD | DMAS | CCEI);
        g.m.clock.advance(kSector);
        CHECK(g.b->assertsVi() == 0x20, "code 3 arms: the sector pulse pulls VI5");
        g.rd(CTLCMD | DMAS | CCDI);
        CHECK(g.b->assertsVi() == 0, "code 2 disarms");

        g.toPulse();
        g.m.clock.advance(kWindow);
        CHECK((ddA(g) & DABD) == 0, "no body yet");
        g.rd(CTLCMD | DMAS | CCSB);
        CHECK((ddA(g) & DABD) != 0, "code 4 sets body (diagnostic)");
        g.toPulse();
        CHECK((ddA(g) & DABD) == 0, "until the next sector pulse");

        g.rd(CTLORD | ORDDD | ORDSIN | ORDDS1);
        g.rd(CTLCMD | DMAS | CCRES);
        CHECK((ddA(g) & DSMO) == 0 && (ddB(g) & DBT0) == 0, "code 7: motors off, drives de-selected");

        ddReady(g);
        g.b->reset(Reset::Bus);
        CHECK((ddA(g) & DSMO) == 0, "the front-panel RESET clears this board");

        CHECK(g.set("motor", "real"), "motor = real");
        ddReady(g);
        g.m.clock.advance(kOffDD - 1);
        CHECK((ddA(g) & DSMO) != 0, "one T-state short of 9.6 s");
        g.m.clock.advance(1);
        CHECK((ddA(g) & DSMO) == 0, "9.6 s: off");
    }

    SECTION("MDS-A-D -- snapshot and restore carry the order register");
    {
        Rig g("mdsad");
        g.mount("drive0", image(kQuad));
        ddReady(g, ORDSS | ORDDS1);
        g.toPulse();
        const int sec = (int)(g.m.clock.now() / kSector % 10);
        g.m.clock.advance(kBody);
        for (int i = 0; i < 300; ++i) (void)g.rd(CTLCMD | DMRD);

        StateWriter w;
        g.b->serialize(w);
        g.rd(CTLCMD | DMAS | CCRES);
        StateReader r(w.data());
        g.b->deserialize(r);

        CHECK(g.rd(CTLCMD | DMRD) == pat((size_t)((35 + 34) * 10 + sec) * 512 + 300),
              "side 2 is still selected, and the read resumes at byte 300");
    }

    // ---- `timing = real`: a data access holds READY until the byte is under the head ----
    SECTION("North Star -- timing = real: a read holds READY for each byte's time under the head");
    {
        const uint64_t sdByte = 128;  // 64 us
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        CHECK(g.set("timing", "real"), "timing = real is accepted");
        sdReady(g);
        g.toPulse();
        g.m.clock.advance(kBody);  // the sync character has just gone by
        loadLdas(g, CTLRD | CTLNOP, 4);
        CHECK(step(g) == 13 + sdByte, "the first data byte is one byte time after the sync");
        bool each = true;
        for (int i = 0; i < 3; ++i) each = each && step(g) == sdByte;
        CHECK(each, "...and each LDA after it waits one byte time");

        Rig f("mdsa");
        f.mount("drive0", image(kSD));
        sdReady(f);
        f.toPulse();
        f.m.clock.advance(kBody);
        loadLdas(f, CTLRD | CTLNOP, 2);
        CHECK(step(f) == 13 && step(f) == 13, "under timing = full an LDA of the data is 13 T-states");
    }
    {
        const uint64_t ddByte = 64;  // 32 us
        Rig g("mdsad");
        g.mount("drive0", image(kDD));
        g.set("timing", "real");
        ddReady(g);
        g.toPulse();
        g.m.clock.advance(kBody);
        loadLdas(g, CTLCMD | DMRD, 3);
        CHECK(step(g) == 13 + ddByte, "MDS-A-D, double density: the first byte is 32 us after the sync");
        CHECK(step(g) == ddByte && step(g) == ddByte, "...and each next one 32 us after that");
    }

    SECTION("North Star -- timing = real: a write holds READY for the shift register");
    {
        const uint64_t sdByte = 128;
        Rig g("mdsa");
        g.mount("drive0", image(kSD));
        g.set("timing", "real");
        sdReady(g);
        g.toPulse();
        g.rd(CTLWRT);  // in the window: Begin Write
        loadLdas(g, SDWD | 0x00, 3);
        CHECK(step(g) == 13 + kWindow + sdByte,
              "the first byte waits out the window and the zero the board writes itself");
        CHECK(step(g) == sdByte && step(g) == sdByte, "...and each next byte one byte time");
    }

    faultTests();
}
