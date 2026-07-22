#include "test.h"

#include "boards/mits-frontpanel.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "cpu/cpu.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace altair;

namespace {

// Set/read a CPU register by NAME through the reflection layer -- the same door
// SNAPSHOT itself would use, so the test does not depend on the 8080's field order.
void setReg(Machine& m, const std::string& name, uint32_t v) {
    for (auto& rd : m.cpu()->registers())
        if (rd.name == name) { rd.set(v); return; }
}
uint32_t getReg(Machine& m, const std::string& name) {
    for (auto& rd : m.cpu()->registers())
        if (rd.name == name) return rd.get();
    return 0xFFFFFFFF;
}

MemoryBoard* addRam(Machine& m, const std::string& id, uint16_t at, uint32_t size) {
    std::string err;
    auto* mb = dynamic_cast<MemoryBoard*>(m.add("memory", id, err));
    Region r;
    r.kind = RegionKind::Ram;
    r.at   = at;
    r.size = size;
    mb->addRegion(r, err);
    return mb;
}

std::string tmpSnap(const char* leaf) {
    return (std::filesystem::temp_directory_path() / leaf).string();
}

}  // namespace

// The machine-level round trip (DESIGN.md 13): snapshot the state, scribble over
// every part of it, restore, and prove the machine is bit-for-bit where it was.
void test_snapshot() {
    SECTION("snapshot: CPU + RAM + a board latch round trip");
    {
        Machine m;
        std::string err;
        m.add("8080", "cpu0", err);
        MemoryBoard* mem = addRam(m, "mem0", 0x0000, 0x10000);
        m.add("2sio", "sio0", err);
        auto* fp = dynamic_cast<FrontPanelBoard*>(m.add("fp", "panel", err));
        m.power();

        // Put a fingerprint on every layer.
        setReg(m, "A", 0x42);
        setReg(m, "B", 0x11);
        setReg(m, "SP", 0xABCD);
        m.cpu()->setPc(0x1234);
        m.bus.memWrite(0x2000, 0x37);
        m.bus.memWrite(0xFFFF, 0x9E);
        fp->setSwitches(0xC3A5);

        std::string path = tmpSnap("altairsim-snap-test.snap");
        CHECK(m.snapshot(path, err), "snapshot writes");

        // Now scribble over all of it.
        setReg(m, "A", 0x00);
        setReg(m, "B", 0x00);
        setReg(m, "SP", 0x0000);
        m.cpu()->setPc(0x0000);
        m.bus.memWrite(0x2000, 0x00);
        m.bus.memWrite(0xFFFF, 0x00);
        fp->setSwitches(0x0000);

        CHECK(m.restore(path, err), "restore reads");

        CHECK(getReg(m, "A") == 0x42, "A restored");
        CHECK(getReg(m, "B") == 0x11, "B restored");
        CHECK(getReg(m, "SP") == 0xABCD, "SP restored");
        CHECK(m.cpu()->pc() == 0x1234, "PC restored");
        CHECK(m.bus.memRead(0x2000) == 0x37, "RAM at 2000 restored");
        CHECK(m.bus.memRead(0xFFFF) == 0x9E, "RAM at FFFF restored");
        CHECK(fp->switches() == 0xC3A5, "front-panel switches restored");
        (void)mem;

        std::filesystem::remove(path);
    }

    SECTION("snapshot: enabled_ travels (a board that switched itself off)");
    {
        Machine m;
        std::string err;
        m.add("8080", "cpu0", err);
        MemoryBoard* mem = addRam(m, "mem0", 0x0000, 0x1000);
        m.power();

        mem->setEnabled(false);
        std::string path = tmpSnap("altairsim-snap-enabled.snap");
        CHECK(m.snapshot(path, err), "snapshot writes");
        mem->setEnabled(true);
        CHECK(m.restore(path, err), "restore reads");
        CHECK(!mem->enabled(), "a disabled board comes back disabled");
        std::filesystem::remove(path);
    }

    SECTION("snapshot: a snapshot is deterministic (byte-for-byte)");
    {
        Machine m;
        std::string err;
        m.add("8080", "cpu0", err);
        addRam(m, "mem0", 0x0000, 0x1000);
        m.power();
        setReg(m, "A", 0x7E);
        m.bus.memWrite(0x0100, 0x5A);

        std::string p1 = tmpSnap("altairsim-snap-det1.snap");
        std::string p2 = tmpSnap("altairsim-snap-det2.snap");
        CHECK(m.snapshot(p1, err), "first snapshot writes");
        CHECK(m.snapshot(p2, err), "second snapshot writes");

        std::ifstream f1(p1, std::ios::binary), f2(p2, std::ios::binary);
        std::vector<uint8_t> b1((std::istreambuf_iterator<char>(f1)), {});
        std::vector<uint8_t> b2((std::istreambuf_iterator<char>(f2)), {});
        CHECK(!b1.empty() && b1 == b2, "two snapshots of one state are identical bytes");
        std::filesystem::remove(p1);
        std::filesystem::remove(p2);
    }

    SECTION("snapshot: a mismatched machine is refused, whole");
    {
        Machine a;
        std::string err;
        a.add("8080", "cpu0", err);
        addRam(a, "mem0", 0x0000, 0x1000);
        a.power();
        a.bus.memWrite(0x0000, 0xEE);
        std::string path = tmpSnap("altairsim-snap-topo.snap");
        CHECK(a.snapshot(path, err), "snapshot writes");

        // A DIFFERENT machine -- one board short.
        Machine b;
        b.add("8080", "cpu0", err);
        b.power();
        b.bus.memWrite(0x0000, 0x11);  // its own state, which must NOT be touched
        CHECK(!b.restore(path, err), "restore into a different topology is refused");
        CHECK(!err.empty(), "and it says why");

        // A machine with the right count but the wrong type.
        Machine c;
        c.add("8080", "cpu0", err);
        c.add("2sio", "mem0", err);  // right id, wrong type
        c.power();
        CHECK(!c.restore(path, err), "restore is refused when a board's type differs");

        std::filesystem::remove(path);
    }

    SECTION("snapshot: a corrupt file is refused");
    {
        Machine m;
        std::string err;
        m.add("8080", "cpu0", err);
        addRam(m, "mem0", 0x0000, 0x1000);
        m.power();
        std::string path = tmpSnap("altairsim-snap-corrupt.snap");
        CHECK(m.snapshot(path, err), "snapshot writes");

        // Flip a byte in the middle of the file.
        {
            std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
            f.seekg(0, std::ios::end);
            auto len = f.tellg();
            f.seekp(len / 2);
            char c;
            f.seekg(len / 2);
            f.get(c);
            c ^= 0x01;
            f.seekp(len / 2);
            f.put(c);
        }
        CHECK(!m.restore(path, err), "a tampered snapshot is refused");
        std::filesystem::remove(path);
    }
}
