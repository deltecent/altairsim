#include "test.h"

#include "boards/cromemco-d7a.h"
#include "boards/newtech-music.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/audio.h"
#include "host/level_pcm.h"
#include "host/speaker.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace altair;

namespace {

// An Audio that keeps what it is given, one list of samples for each voice, and reports
// whatever queue depth the test sets. The same injection main() does (Speaker::setAudio),
// so the board renders and pushes as it would to a sound device.
struct StubAudio : public Audio {
    std::map<Owner, std::vector<int16_t>> voices;
    int    pushes = 0;      // how many times push() was called
    size_t depth  = 1000;   // what queued() reports: neither dry nor deep

    int    rate() const override { return 44100; }
    bool   available() const override { return true; }
    void   push(Owner o, std::span<const int16_t> s) override {
        ++pushes;
        voices[o].insert(voices[o].end(), s.begin(), s.end());
    }
    size_t queued(Owner) const override { return depth; }

    // Every sample pushed, when the test has one voice.
    const std::vector<int16_t>& only() const {
        static const std::vector<int16_t> none;
        return voices.size() == 1 ? voices.begin()->second : none;
    }
};

// Sign changes in a run of samples: two for each cycle of a tone.
int crossings(const std::vector<int16_t>& pcm) {
    int n = 0, last = 0;
    for (int16_t v : pcm) {
        int sign = v > 0 ? 1 : (v < 0 ? -1 : 0);
        if (sign == 0) continue;
        if (last != 0 && sign != last) ++n;
        last = sign;
    }
    return n;
}

struct Rig {
    Machine            m;
    StubAudio          aud;
    NewtechMusicBoard* mus = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        mus = dynamic_cast<NewtechMusicBoard*>(m.add("music6", "music0", err));
        m.add("8080", "cpu0", err);
        Speaker::setAudio(&aud);
        m.power();
    }
    ~Rig() { Speaker::setAudio(nullptr); }

    // A square wave on `port` for `tStates`, between `hi` and `lo`: the value flips every
    // `half` T-states, and the board gets its host turn every 20000.
    void tone(uint8_t port, uint64_t tStates, uint64_t half, uint8_t hi = 0xFC,
              uint8_t lo = 0x00) {
        bool     up = true;
        uint64_t sinceSlice = 0;
        for (uint64_t t = 0; t < tStates; t += half, up = !up) {
            out(port, up ? hi : lo);
            m.clock.advance(half);
            sinceSlice += half;
            if (sinceSlice >= 20000) {
                mus->pump();
                sinceSlice = 0;
            }
        }
        mus->pump();
    }

    void out(uint8_t port, uint8_t v) { m.bus.ioWrite(port, v); }

    bool decodes(Cycle type, uint8_t port) {
        BusCycle c;
        c.type = type;
        c.addr = port;
        return mus->decodes(c);
    }
};

} // namespace

void test_music6() {
    SECTION("music6 -- one output port at four addresses, and no input port");
    {
        Rig g;
        for (uint8_t p = 0x24; p <= 0x27; ++p) {
            CHECK(g.decodes(Cycle::IoWrite, p), "OUT 24..27 is decoded: A1 and A0 are not wired");
            CHECK(!g.decodes(Cycle::IoRead, p), "IN 24..27 is not: the board has no input port");
        }
        CHECK(!g.decodes(Cycle::IoWrite, 0x20), "20 is not the board (A2 must be 1)");
        CHECK(!g.decodes(Cycle::IoWrite, 0x23), "nor 23");
        CHECK(!g.decodes(Cycle::IoWrite, 0x28), "nor 28");
        CHECK(!g.decodes(Cycle::IoWrite, 0x2C), "nor 2C (A3 must be 0)");

        uint8_t v = 0x10;
        for (uint8_t p = 0x24; p <= 0x27; ++p, v = (uint8_t)(v + 0x10)) {
            g.out(p, v);
            CHECK(g.mus->latch() == v, "each of the four addresses writes the one latch");
        }
        auto map = g.mus->ioMap();
        CHECK(map.size() == 1 && map[0].lo == 0x24 && map[0].hi == 0x27, "the map shows all four");
    }

    SECTION("music6 -- the port strap: A7..A4 by jumper, A3 = 0 and A2 = 1 fixed");
    {
        Rig g;
        std::string err;
        CHECK(!setProperty(*g.mus, "port", "25", err), "25 is refused");
        CHECK(err.find("must end in 4") != std::string::npos, "and the error says why");
        CHECK(!setProperty(*g.mus, "port", "28", err), "28 is refused");
        CHECK(!setProperty(*g.mus, "port", "2C", err), "2C is refused");
        CHECK(!setProperty(*g.mus, "port", "20", err), "20 is refused");
        CHECK(setProperty(*g.mus, "port", "44", err), "44 is one of the sixteen");
        for (uint8_t p = 0x44; p <= 0x47; ++p)
            CHECK(g.decodes(Cycle::IoWrite, p), "the four addresses moved with it");
        CHECK(!g.decodes(Cycle::IoWrite, 0x24), "and 24 is no longer the board");
        CHECK(setProperty(*g.mus, "port", "04", err) && setProperty(*g.mus, "port", "F4", err),
              "04 and F4 are the ends of the range");
    }

    SECTION("music6 -- six bits are latched; DO1 and DO0 go nowhere");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.out(0x24, 0xFF);
        CHECK(g.mus->latch() == 0xFC, "FF latches as FC");
        g.out(0x24, 0x03);
        CHECK(g.mus->latch() == 0x00, "and 03 as 00");

        // A program that toggles only the low two bits moves nothing, so it is silent.
        g.out(0x24, 0x40);
        g.m.clock.advance(1500000);            // the one real change, and it has gone quiet
        g.mus->pump();
        g.aud.pushes = 0;
        g.tone(0x24, 200000, 1000, 0x43, 0x40);
        CHECK(g.aud.pushes == 0, "the low bits alone push nothing");
    }

    SECTION("music6 -- nothing clears the latch but the power switch");
    {
        Rig g;
        g.out(0x24, 0xA8);
        g.m.reset(Reset::Bus);
        CHECK(g.mus->latch() == 0xA8, "a bus reset leaves the latch (its clear is tied high)");
        g.m.power();
        CHECK(g.mus->latch() == 0x00, "power-on starts it at 0");
    }

    SECTION("music6 -- the manual's square-wave test routine plays at the pitch its timing gives");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        // reference/Newtech Music Board.md 6.1: the maximum-amplitude square wave.
        static const uint8_t prog[] = {
            0x97,              // START  SUB  A
            0x06, 0x40,        // LOOP1  MVI  B,64
            0x05,              //        DCR  B
            0xC2, 0x03, 0x00,  //        JNZ  $-4
            0x2F,              //        CMA
            0xD3, 0x24,        //        OUT  24H
            0xC3, 0x01, 0x00,  //        JMP  LOOP1
        };
        uint16_t a = 0;
        for (uint8_t b : prog) g.m.bus.memWrite(a++, b);
        g.m.cpu()->setPc(0);
        while (g.m.clock.now() < 2000000) {    // one second at 2 MHz
            g.m.debug.run(1000);
            g.mus->pump();
        }
        // A half cycle is MVI 7 + 64 x (DCR 5 + JNZ 10) + CMA 4 + OUT 10 + JMP 10 = 991
        // T-states: 1009.08 Hz. (The manual prints 1005 Hz, which is 995 T-states.) The
        // run stops on an instruction boundary a little past the second, so both counts
        // are taken from the clock.
        const uint64_t now = g.m.clock.now();
        const auto&    pcm = g.aud.only();
        const long     want = (long)(now * 44100 / 2000000);
        CHECK((long)pcm.size() >= want - 1 && (long)pcm.size() <= want,
              "the time the machine ran is the length of the sound");
        const int n     = crossings(pcm);
        const int edges = (int)(now / 991);
        CHECK(n >= edges - 2 && n <= edges, "one zero crossing for each 991 T-states");
        CHECK(n > (int)(now / 995) + 2, "which is not the 1005 Hz the manual prints");
        CHECK(g.mus->statusLines().size() == 2 &&
              g.mus->statusLines()[1].find("-> playing") != std::string::npos,
              "SHOW says it is playing");
    }

    SECTION("music6 -- flat out, nothing is played and SHOW says why");
    {
        Rig g;
        CHECK(g.m.clock.free(), "precondition: flat out is the default");
        g.tone(0x24, 200000, 1000);
        CHECK(g.aud.pushes == 0, "no crystal, no sound");
        auto s = g.mus->statusLines();
        CHECK(s.size() == 2 && s[0].find("latch    00") != std::string::npos, "line one is the latch");
        CHECK(s.size() == 2 && s[1].find("no crystal (clock_hz = 0)") != std::string::npos,
              "line two gives the reason");
    }

    SECTION("music6 -- sound is pushed in pump(), not inside a bus cycle");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        for (int i = 0; i < 50; ++i) {
            g.out(0x24, (i & 1) ? 0xFC : 0x00);
            g.m.clock.advance(1000);
        }
        CHECK(g.aud.pushes == 0, "fifty writes to the port push nothing");
        g.mus->pump();
        CHECK(g.aud.pushes == 1, "the host turn pushes them");
    }

    SECTION("music6 -- the coupling capacitor: a level that sits is 0 V at the amplifier");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.aud.depth = 0;                       // the device has nothing: a cushion goes first
        g.out(0x24, 0xFC);                     // the D/A goes to +5 V and stays there
        g.m.clock.advance(1500000);
        g.mus->pump();
        CHECK(g.aud.pushes == 0, "a level that only sits is not sound");

        g.tone(0x24, 20000, 1000, 0x00, 0xFC); // then a wave between 0 V and +5 V
        const auto& pcm = g.aud.only();
        CHECK(!pcm.empty() && pcm[0] == 0, "the sound starts from 0 V, not from the held level");
        int16_t lo = 0, hi = 0;
        for (int16_t v : pcm) {
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        // From rest at +5 V the wave goes DOWN by the whole swing: 63 steps of 4, times
        // the gain -- 252 * 256 = 64512, twice full scale. The volume's limiter rounds it
        // off; a hard clip would give -32768. With no capacitor it would sit between
        // -32768 and +31744.
        CHECK(lo >= -32767 && lo < -29490,
              "the first half wave is rounded off past the knee, not clipped");
        CHECK(hi < 8 * LevelPcm::kGain, "and the top of it is the rest level");
    }

    SECTION("music6 -- once the capacitor settles, a full-swing wave fills the range");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.tone(0x24, 4000000, 1000, 0x00, 0xFC);   // two seconds of 0 V / +5 V
        const auto& pcm = g.aud.only();
        CHECK(pcm.size() > 44100, "two seconds of sound");
        int16_t lo = 0, hi = 0;
        for (size_t i = pcm.size() - 4410; i < pcm.size(); ++i) {   // the last 0.1 s
            lo = pcm[i] < lo ? pcm[i] : lo;
            hi = pcm[i] > hi ? pcm[i] : hi;
        }
        // About +-126 steps around 0 V, times the gain: +-32256, near full scale.
        CHECK(hi > 29490 && hi <= 32767, "the top is near full scale");
        CHECK(lo < -29490 && lo >= -32767, "and so is the bottom");
    }

    SECTION("music6 -- a Model 6 and a D+7A speaker are two voices");
    {
        Rig g;
        std::string err;
        auto* d7a = dynamic_cast<D7aBoard*>(g.m.add("d7a", "d7a0", err));
        g.m.power();
        g.m.clock.setHz(2000000);
        for (int i = 0; i < 20; ++i) {
            g.out(0x24, (i & 1) ? 0xFC : 0x00);
            g.out(0x19, (i & 1) ? 0x40 : 0xC0);
            g.m.clock.advance(1000);
        }
        g.mus->pump();
        d7a->pump();
        CHECK(g.aud.voices.size() == 2, "each speaker pushes under its own owner");
    }

    SECTION("music6 -- the latch travels in a snapshot, and the jumped time is not played");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.out(0x24, 0x54);
        StateWriter w;
        g.mus->serialize(w);
        g.out(0x24, 0xFC);
        g.m.clock.advance(3600ull * 2000000);   // an hour on, as a RESTORE can move it
        StateReader r(w.data());
        g.mus->deserialize(r);
        CHECK(g.mus->latch() == 0x54, "the latch came back");
        g.mus->pump();
        CHECK(g.aud.pushes == 0, "the hour is not rendered");
        // The speaker rests at the level that came back (54), not at the one the latch
        // held before the restore (FC): the first write of FC is a step UP from rest.
        g.tone(0x24, 200000, 1000, 0xFC, 0x54);
        const auto& pcm = g.aud.only();
        CHECK(pcm.size() == 4410, "and the sound after it is the right length");
        int16_t hi = 0;
        for (int16_t v : pcm) hi = v > hi ? v : hi;
        CHECK(hi > 10000, "and it starts from the restored level");
    }
}
