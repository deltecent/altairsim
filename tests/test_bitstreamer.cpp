// The Vector Graphic Bit Streamer (docs/boards/vector-bitstreamer.md). Real bus cycles through
// the decode, with the serial setup and the routines from the manual.

#include "boards/vector-bitstreamer.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/stream.h"
#include "test.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

using namespace altair;

namespace {

constexpr uint8_t kTxRDY = 0x01;
constexpr uint8_t kRxRDY = 0x02;

struct Rig {
    Machine           m;
    BitstreamerBoard* b = nullptr;

    Rig() {
        std::string err;
        b = dynamic_cast<BitstreamerBoard*>(m.add("vgbs", "bs0", err));
        m.power();
    }

    uint8_t in(uint8_t p) { return m.bus.ioRead(p); }
    void    out(uint8_t p, uint8_t v) { m.bus.ioWrite(p, v); }

    ScriptedStream* line() {
        auto        s = std::make_unique<ScriptedStream>();
        auto*       p = s.get();
        std::string err;
        b->connectStream("serial", std::move(s), err);
        return p;
    }

    void init() {
        out(3, 0xCE);
        out(3, 0x27);
    }
};

} // namespace

void test_bitstreamer() {
    SECTION("vgbs: the parallel ports");
    {
        Rig r;
        r.out(0, 0x12);
        r.out(1, 0x34);
        CHECK(r.b->latchA() == 0x12, "OUT 0 latches port A");
        CHECK(r.b->latchB() == 0x34, "OUT 1 latches port B");
        CHECK(r.in(0) == 0xFF, "open port A inputs read FF");
        CHECK(r.in(1) == 0xFF, "open port B inputs read FF");
        r.m.reset(Reset::Bus);
        CHECK(r.b->latchA() == 0x12, "a reset leaves latch A");
        CHECK(r.b->latchB() == 0x34, "a reset leaves latch B");
    }

    SECTION("vgbs: the base address");
    {
        Rig         r;
        std::string err;
        CHECK(!setProperty(*r.b, "port", "22", err), "the pads cannot set A0 or A1");
        CHECK(setProperty(*r.b, "port", "20", err), "a multiple of 4 is good");
        r.m.bus.invalidateDecode();
        r.out(0x20, 0x5A);
        CHECK(r.b->latchA() == 0x5A, "port A moves with the base");
        r.out(0x24, 0xA5);
        CHECK(r.b->latchA() == 0x5A, "the board decodes four ports");
    }

    SECTION("vgbs: the serial port, as the manual programs it");
    {
        Rig  r;
        auto* line = r.line();
        r.init();
        CHECK(r.in(3) & kTxRDY, "D0 of the status port is TxRDY");
        r.out(2, 'A');
        for (int i = 0; i < 100000 && line->out().empty(); ++i) {
            r.m.clock.advance(100);
            r.b->pump();
        }
        CHECK(line->out() == "A", "OUT 2 sends a byte");

        line->feed("Z");
        for (int i = 0; i < 100000 && !(r.in(3) & kRxRDY); ++i) {
            r.m.clock.advance(100);
            r.b->pump();
        }
        CHECK(r.in(3) & kRxRDY, "D1 of the status port is RxRDY");
        CHECK(r.in(2) == 'Z', "IN 2 reads the byte");
    }

    SECTION("vgbs: the baud switch");
    {
        Rig         r;
        std::string err;
        const struct {
            const char* label;
            long long   hz;
        } rows[] = {{"9600", 153846}, {"4800", 76923}, {"2400", 38461}, {"1200", 19230},
                    {"600", 9615},    {"300", 4807},   {"150", 2403},   {"110", 1748}};
        auto props = r.b->unitProperties("serial");
        for (const auto& row : rows) {
            for (auto& p : props)
                if (p.name == "baud") p.set(Value::ofStr(row.label), err);
            CHECK(r.b->usart().txcHz == row.hz, row.label);
        }
    }

    SECTION("vgbs: a snapshot keeps the latches");
    {
        Rig r;
        r.out(0, 0x11);
        r.out(1, 0x22);
        StateWriter w;
        r.b->serialize(w);
        Rig         other;
        StateReader rd(w.data());
        other.b->deserialize(rd);
        CHECK(other.b->latchA() == 0x11, "latch A comes back");
        CHECK(other.b->latchB() == 0x22, "latch B comes back");
    }
}
