#include "test.h"

#include "boards/mits-884pio.h"
#include "boards/mits-88cpu.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/endpoint.h"
#include "host/file.h"
#include "host/stream.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

// Write `bytes` to `path` (binary, whole thing), returning the path so a spec can
// be built inline. Used to lay down a reader tape before connecting it.
std::string layTape(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), (std::streamsize)bytes.size());
    return path;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// An 88-4PIO with section A on some endpoint, driven through the REAL connect path
// (resolveEndpoint under the hood), so a reader tape reaches a guest exactly as an
// operator's `CONNECT pio0:ja in:tape` would. Port J at base 20: 20 = A control,
// 21 = A data.
struct Rig {
    Machine    m;
    Pio4Board* pio = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        m.add("memory", "mem0", err);
        pio = dynamic_cast<Pio4Board*>(m.add("4pio", "pio0", err));
        m.add("8080", "cpu0", err);
        m.power();
    }

    uint8_t aCtrl() { return m.bus.ioRead(0x20); }
    uint8_t aData() { return m.bus.ioRead(0x21); }
};

constexpr uint8_t kDdrSelect = 0x04;  // control bit 2: 1 = data register, 0 = DDR
constexpr uint8_t kIrq1Flag  = 0x80;  // control bit 7: data available

} // namespace

void test_papertape() {
    SECTION("in: reader -- a tape feeds a guest, one byte at a time, then falls quiet");
    {
        const std::string path = "papertape_reader.tmp";
        layTape(path, "ABC");

        Rig         g;
        std::string err;
        CHECK(g.pio->connect("ja", "in:" + path, err), "in: connects to a 4PIO section");
        g.m.bus.ioWrite(0x20, kDdrSelect);  // section A: reach the data register, not the DDR

        // Each pump pulls at most one byte from the reader into the input latch; the
        // guest reads it from the data register (clearing the flag) before the next
        // arrives. So the tape arrives A, B, C in order.
        std::string got;
        for (int i = 0; i < 3; ++i) {
            g.m.pump();
            CHECK((g.aCtrl() & kIrq1Flag) != 0, "a taped byte raises data-available");
            got += (char)g.aData();
            CHECK((g.aCtrl() & kIrq1Flag) == 0, "reading the data register clears the flag");
        }
        CHECK(got == "ABC", "the guest reads the tape's bytes, in order");

        // Past the end the line is QUIET, not an error and not EOF-as-a-byte: further
        // pumps latch nothing, forever.
        g.m.pump();
        g.m.pump();
        CHECK((g.aCtrl() & kIrq1Flag) == 0, "a spent reader latches nothing more");

        std::remove(path.c_str());
    }

    SECTION("in: reader -- describe() round-trips and readable() tracks the head");
    {
        const std::string path = "papertape_desc.tmp";
        layTape(path, "hi");
        const std::string spec = "in:" + path;

        std::string err;
        auto        s = resolveEndpoint(spec, err);
        CHECK(s != nullptr, "in: resolves to a stream");
        if (s) {
            CHECK(s->describe() == spec, "describe() echoes the spec (CONFIG SAVE round-trip)");
            CHECK(s->readable(), "readable while bytes remain");
            uint8_t b = 0;
            CHECK(s->read(&b, 1) == 1 && b == 'h', "first byte");
            CHECK(s->read(&b, 1) == 1 && b == 'i', "second byte");
            CHECK(!s->readable(), "not readable at EOF -- a quiet line");
            CHECK(s->read(&b, 1) == 0, "a read past the end yields nothing, not an error");
        }
        std::remove(path.c_str());
    }

    SECTION("in: an empty tape is a legal, immediately-quiet reader");
    {
        const std::string path = "papertape_empty.tmp";
        layTape(path, "");
        std::string err;
        auto        s = resolveEndpoint("in:" + path, err);
        CHECK(s != nullptr, "a 0-byte tape resolves");
        if (s) CHECK(!s->readable(), "and is quiet from the start");
        std::remove(path.c_str());
    }

    SECTION("in: a missing tape is a clean refusal with the path named");
    {
        std::string err;
        auto        s = resolveEndpoint("in:no_such_papertape_here.tmp", err);
        CHECK(s == nullptr, "a missing reader file fails");
        CHECK(err.find("no_such_papertape_here.tmp") != std::string::npos,
              "and the message names the path");
    }

    SECTION("out: punch -- captures the guest's bytes, 8-bit clean, and NEVER truncates");
    {
        const std::string path = "papertape_punch.tmp";
        // Pre-seed a LONGER file: a punch overwrites forward from 0 without blanking,
        // so a shorter run must leave the old tail behind (the documented semantics).
        layTape(path, "OLD-AND-LONGER-TAIL");

        std::string err;
        auto        s = resolveEndpoint("out:" + path, err);
        CHECK(s != nullptr, "out: resolves");
        if (s) {
            CHECK(s->writable(), "a punch is always writable");
            CHECK(!s->readable(), "and not readable -- no in: was given");

            const char* msg = "\xFF\x00Hi\r\n";  // high bit + NUL + CR/LF must survive
            s->write(reinterpret_cast<const uint8_t*>(msg), 6);
            s->flush();
            s.reset();  // close before reading back

            std::string got = slurp(path);
            CHECK(got.substr(0, 6) == std::string(msg, 6), "the punched bytes land verbatim");
            CHECK(got == std::string(msg, 6) + "D-LONGER-TAIL",
                  "and the stale tail past the new run is NOT truncated");
        }
        std::remove(path.c_str());
    }

    SECTION("out: an absent punch file is created");
    {
        const std::string path = "papertape_created.tmp";
        std::remove(path.c_str());
        std::string err;
        auto        s = resolveEndpoint("out:" + path, err);
        CHECK(s != nullptr, "out: on a nonexistent path creates it");
        if (s) {
            const char* msg = "new";
            s->write(reinterpret_cast<const uint8_t*>(msg), 3);
            s.reset();
            CHECK(slurp(path) == "new", "and the bytes are there");
        }
        std::remove(path.c_str());
    }

    SECTION("in:PATH,out:PATH -- one bidirectional line, two files, two heads");
    {
        const std::string in  = "papertape_bi_in.tmp";
        const std::string out = "papertape_bi_out.tmp";
        layTape(in, "RD");
        std::remove(out.c_str());
        const std::string spec = "in:" + in + ",out:" + out;

        std::string err;
        auto        s = resolveEndpoint(spec, err);
        CHECK(s != nullptr, "the combined form resolves");
        if (s) {
            CHECK(s->describe() == spec, "describe() round-trips the whole combined spec");
            CHECK(s->readable(), "the reader half has bytes");
            CHECK(s->writable(), "and the punch half takes bytes");

            // The two halves are independent files: writing the punch cannot disturb
            // the reader's head, and vice versa.
            const char* wr = "WR";
            s->write(reinterpret_cast<const uint8_t*>(wr), 2);
            uint8_t b = 0;
            CHECK(s->read(&b, 1) == 1 && b == 'R', "reader still delivers its own bytes");
            CHECK(s->read(&b, 1) == 1 && b == 'D', "unaffected by the write");
            s.reset();
            CHECK(slurp(out) == "WR", "the punch captured only what was written");
        }
        std::remove(in.c_str());
        std::remove(out.c_str());
    }

    SECTION("in:?cps= -- a paced reader gates each byte on the wall clock (88-HSR)");
    {
        // Drive the reader's wall clock by hand. cps=100 => one byte per 10 ms; the
        // FIRST byte never waits, and each later one is unreadable until its turn.
        uint64_t now  = 0;
        auto     wall = [&now]() { return now; };
        const uint64_t tenMs = 10'000'000;  // ns per byte at 100 cps

        FileStream fs("in:x?cps=100", std::vector<uint8_t>{'A', 'B', 'C'}, std::fstream{},
                      1'000'000'000ull / 100, wall);

        CHECK(fs.pacesItself(), "a rate makes the reader pace itself");
        uint8_t b = 0;
        CHECK(fs.readable(), "the first byte does not wait");
        CHECK(fs.read(&b, 1) == 1 && b == 'A', "read A at t=0");

        CHECK(!fs.readable(), "the second byte is not yet due");
        CHECK(fs.read(&b, 1) == 0, "and a premature read yields nothing");

        now += tenMs;  // one byte time later
        CHECK(fs.readable(), "now the second byte is due");
        CHECK(fs.read(&b, 1) == 1 && b == 'B', "read B at t=10ms");

        now += tenMs;
        CHECK(fs.read(&b, 1) == 1 && b == 'C', "read C at t=20ms");
        CHECK(!fs.readable(), "and then the tape is spent");
    }

    SECTION("in: cps and baud are two spellings of one rate; a bad rate is refused");
    {
        std::string err;
        CHECK(resolveEndpoint("in:x?cps=300&baud=110", err) == nullptr,
              "giving both cps and baud is an error");
        err.clear();
        CHECK(resolveEndpoint("in:x?cps=0", err) == nullptr, "a non-positive rate is refused");
        err.clear();
        CHECK(resolveEndpoint("in:x?spool=fast", err) == nullptr, "an unknown option is refused");
        err.clear();
        CHECK(resolveEndpoint("out:x?cps=300", err) == nullptr,
              "a punch takes no options -- it runs at the line's speed");
    }
}
