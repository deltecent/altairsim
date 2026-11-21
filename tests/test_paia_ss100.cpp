#include "test.h"

#include "boards/paia-ss100.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/audio.h"
#include "host/speaker.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace altair;

namespace {

// An Audio that keeps what it is given and reports the queue depth the test sets.
struct StubAudio : public Audio {
    std::map<Owner, std::vector<int16_t>> voices;
    size_t depth  = 1000;   // neither dry nor deep
    bool   device = true;

    int    rate() const override { return 44100; }
    bool   available() const override { return device; }
    void   push(Owner o, std::span<const int16_t> s) override {
        voices[o].insert(voices[o].end(), s.begin(), s.end());
    }
    size_t queued(Owner) const override { return depth; }

    std::vector<int16_t> only() const {
        return voices.size() == 1 ? voices.begin()->second : std::vector<int16_t>{};
    }
};

struct Rig {
    Machine    m;
    StubAudio  aud;
    PaiaSs100* ss = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        ss = dynamic_cast<PaiaSs100*>(m.add("ss100", "ss0", err));
        Speaker::setAudio(&aud);
        m.power();
    }
    ~Rig() { Speaker::setAudio(nullptr); }

    void out(uint8_t port, uint8_t v) { m.bus.ioWrite(port, v); }
    bool vi0() const { return (m.bus.viLines() & 1) != 0; }

    // Speak `codes` the way PAiA's driver does: the next phoneme goes out when the
    // interrupt comes. The board gets its host turn every `slice` T-states. Ends when
    // the last phoneme is done.
    void speak(const std::vector<uint8_t>& codes, uint64_t slice) {
        size_t   next = 0;
        uint64_t since = 0;
        out(0x30, codes[next++]);
        for (;;) {
            m.clock.advance(1);
            if (++since >= slice) {
                ss->pump();
                since = 0;
            }
            if (m.bus.viLines() & 1) {
                if (next == codes.size()) break;
                out(0x30, codes[next++]);
            }
        }
        ss->pump();
    }
};

bool decodesIo(PaiaSs100* b, Cycle type, uint8_t port) {
    BusCycle c;
    c.type = type;
    c.addr = port;
    return b->decodes(c);
}

// T-states from a strobe to the rise of A/R, at 2 MHz with the master clock at `hz`.
uint64_t phonemeT(uint8_t code, uint32_t hz) {
    return (VotraxSc01::phonemeClocks(code) * 2000000 + hz - 1) / hz;
}

double sumAbs(const std::vector<int16_t>& v) {
    double s = 0;
    for (int16_t x : v) s += std::abs((double)x);
    return s;
}

} // namespace

void test_paia_ss100() {
    SECTION("SS-100 -- four output strobes from BASE, nothing on input");
    {
        Rig g;
        for (uint8_t p = 0x30; p <= 0x33; ++p)
            CHECK(decodesIo(g.ss, Cycle::IoWrite, p), "decodes an OUT in its 4-port block");
        CHECK(!decodesIo(g.ss, Cycle::IoWrite, 0x2F) && !decodesIo(g.ss, Cycle::IoWrite, 0x34),
              "and nothing either side of it");
        CHECK(!decodesIo(g.ss, Cycle::IoRead, 0x30), "an IN is not driven: the input strobes are unused");
        std::string err;
        CHECK(!setProperty(*g.ss, "port", "34", err), "a base that is not a multiple of 10 is refused");
        CHECK(setProperty(*g.ss, "port", "50", err), "S1-S4 move the block");
        CHECK(decodesIo(g.ss, Cycle::IoWrite, 0x50) && !decodesIo(g.ss, Cycle::IoWrite, 0x30),
              "to 50, and away from 30");
    }

    SECTION("SS-100 -- the master clock from the current source (Fig. 4)");
    {
        CHECK(PaiaSs100::masterHz(0x08) == 720000, "XF8 in speech mode is the calibration point, 720 kHz");
        bool falls = true, lower = true;
        for (uint8_t xf = 1; xf < 16; ++xf) {
            falls = falls && PaiaSs100::masterHz(xf) < PaiaSs100::masterHz(xf - 1);
            lower = lower && PaiaSs100::masterHz(0x10 | xf) < PaiaSs100::masterHz(xf);
        }
        CHECK(falls, "0000 is the highest frequency and each step is lower, to 1111");
        CHECK(lower, "effects mode takes R20/R21 out, so its clock is lower at every step");
        CHECK(PaiaSs100::masterHz(0x00) < 810000 && PaiaSs100::masterHz(0x0F) > 640000,
              "speech mode stays within about 10% of 720 kHz");
    }

    SECTION("SS-100 -- the R21 trim pot moves the speech-mode clock");
    {
        CHECK(PaiaSs100::masterHz(0x08, PaiaSs100::kR21Mid) == 720000,
              "mid-travel is the default, and the booklet's 720 kHz at XF8");
        bool slower = true, same = true;
        for (uint8_t xf = 0; xf < 16; ++xf) {
            slower = slower && PaiaSs100::masterHz(xf, 1000) < PaiaSs100::masterHz(xf, 500) &&
                     PaiaSs100::masterHz(xf, 500) < PaiaSs100::masterHz(xf, 0);
            same = same && PaiaSs100::masterHz(0x10 | xf, 0) == PaiaSs100::masterHz(0x10 | xf, 1000);
        }
        CHECK(slower, "more resistance, less current, a lower clock at every XF");
        CHECK(same, "effects mode takes R20/R21 out, so the pot does not reach it");
        const uint32_t walt = PaiaSs100::masterHz(0x20, 690);
        CHECK(walt > 690000 && walt < 710000, "control 20 with R21 near 690 ohms is about 700 kHz");

        Rig g;
        std::string err;
        CHECK(storedText(g.ss->properties(), "r21", "?") == "500", "r21 reads 500 by default");
        CHECK(!setProperty(*g.ss, "r21", "1001", err), "past the pot's 1 K is refused");
        CHECK(setProperty(*g.ss, "r21", "1000", err), "1000 is the end of travel");
        g.out(0x31, 0x08);
        CHECK(g.ss->chip().masterHz() == PaiaSs100::masterHz(0x08, 1000),
              "the chip runs at the trimmed clock");

        // A turn of the pot while a phoneme plays: A/R rises at the new rate.
        Rig h;
        h.out(0x31, 0x88);
        h.out(0x30, 0x24);
        CHECK(setProperty(*h.ss, "r21", "0", err), "r21 0, at once");
        const uint64_t fast = phonemeT(0x24, PaiaSs100::masterHz(0x08, 0));
        h.m.clock.advance(fast - 1);
        CHECK(!h.vi0(), "still speaking just before the faster deadline");
        h.m.clock.advance(1);
        CHECK(h.vi0(), "and done at it, sooner than at 720 kHz");
    }

    SECTION("SS-100 -- the volume divider (R26, R30, R28, R29) and the amplifier shutdown");
    {
        const double full = 10000.0 / 57000.0;
        const double med  = 5000.0 / 52000.0 / full;                     // R29 || R30
        const double low  = (1500.0 * 10000 / 11500) / (47000 + 1500.0 * 10000 / 11500) / full;
        CHECK(PaiaSs100::volumeGain(0x00) == 1.0, "00 is full power");
        CHECK(std::abs(PaiaSs100::volumeGain(0x20) - med) < 1e-9, "01 shunts R30 with R29: medium");
        CHECK(std::abs(PaiaSs100::volumeGain(0x40) - low) < 1e-9, "10 shunts R30 with R28: low");
        CHECK(PaiaSs100::volumeGain(0x60) == 0.0, "11 shuts the amplifier down (Q4)");
    }

    SECTION("SS-100 -- POC leaves the board disabled; ENABLE gates A/R onto the VI line");
    {
        Rig g;
        CHECK(g.ss->control() == 0, "POC clears the control word");
        CHECK(g.ss->chip().ar(g.m.clock.now()), "A/R is high: the chip wants a phoneme");
        CHECK(!g.vi0(), "but the request is gated off");
        g.out(0x31, 0x88);
        CHECK(g.vi0(), "ENABLE puts A/R on VI0 at once");
        g.out(0x31, 0x08);
        CHECK(!g.vi0(), "and clearing it takes the request away");
    }

    SECTION("SS-100 -- a phoneme: A/R falls at the strobe and rises after its length");
    {
        Rig g;
        g.out(0x31, 0x88);                         // enabled, full, speech, XF8 = 720 kHz
        g.out(0x30, 0xC0 | 0x24);                  // AH, inflection 3
        CHECK(!g.vi0(), "the strobe drops A/R, and with it the request");
        CHECK(g.ss->chip().phoneme() == 0x24 && g.ss->chip().inflection() == 3,
              "bits 0-5 are the phoneme, bits 6-7 I1 and I2");
        const uint64_t len = phonemeT(0x24, 720000);
        g.m.clock.advance(len - 1);
        CHECK(!g.vi0(), "not one T-state early");
        g.m.clock.advance(1);
        CHECK(g.vi0(), "AH's length at 720 kHz, then the request");
        CHECK(g.m.clock.queued() == 0, "and nothing is left on the clock");
        g.out(0x30, 0x3F);
        CHECK(!g.vi0(), "the next strobe clears it");
    }

    SECTION("SS-100 -- XF sets the length of a phoneme");
    {
        Rig g;
        g.out(0x31, 0x80);                         // XF0, the highest clock
        g.out(0x30, 0x24);
        g.m.clock.advance(phonemeT(0x24, PaiaSs100::masterHz(0x00)));
        CHECK(g.vi0(), "at XF0 AH ends sooner");
        g.out(0x31, 0x8F);                         // XF15, the lowest
        g.out(0x30, 0x24);
        const uint64_t start = g.m.clock.now(), due = g.ss->chip().arDeadline();
        const uint64_t len   = phonemeT(0x24, PaiaSs100::masterHz(0x0F));
        CHECK(due - start >= len - 1 && due - start <= len, "at XF15 AH takes its length at the lower clock");
        g.m.clock.advance(due - start - 1);
        CHECK(!g.vi0(), "at XF15 it is still speaking a T-state before its own length");
        g.m.clock.advance(1);
        CHECK(g.vi0(), "and ends on it");
    }

    SECTION("SS-100 -- the interrupt jumper");
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.ss, "interrupt", "vi6", err), "vi6 (PAiA's CP/M software)");
        g.out(0x31, 0x88);
        CHECK(g.m.bus.viLines() == 0x40, "the request goes to VI6");
        CHECK(setProperty(*g.ss, "interrupt", "int", err), "int is the PINT wire");
        g.out(0x31, 0x88);
        CHECK(g.m.bus.viLines() == 0 && g.m.bus.intPending(), "PINT, and no VI line");
    }

    SECTION("SS-100 -- POC* clears the latches, RESET* does not reach them");
    {
        Rig g;
        g.out(0x31, 0xA5);
        g.ss->reset(Reset::Bus);
        CHECK(g.ss->control() == 0xA5, "the front-panel RESET leaves the control word");
        g.ss->reset(Reset::PowerOn);
        CHECK(g.ss->control() == 0 && !g.vi0(), "POC clears it, and the interrupt with it");
    }

    SECTION("SS-100 -- SNAPSHOT/RESTORE carries a phoneme in progress");
    {
        Rig g;
        g.out(0x31, 0x88);
        g.out(0x30, 0x40 | 0x24);
        const uint64_t due = g.m.clock.now() + phonemeT(0x24, 720000);
        g.m.clock.advance(1000);
        StateWriter w;
        g.ss->serialize(w);
        g.out(0x31, 0x2F);                         // change everything after the snapshot
        g.out(0x30, 0x3F);
        StateReader r(w.data());
        g.ss->deserialize(r);
        CHECK(g.ss->control() == 0x88 && g.ss->chip().phoneme() == 0x24 &&
                  g.ss->chip().inflection() == 1,
              "the control word, the phoneme and the inflection come back");
        g.m.clock.advance(due - g.m.clock.now() - 1);
        CHECK(!g.vi0(), "still speaking");
        g.m.clock.advance(1);
        CHECK(g.vi0(), "and A/R rises when it was due");
    }

    SECTION("SS-100 -- speech reaches the sound service with a crystal, and not without");
    {
        const std::vector<uint8_t> hello = {0x1B, 0x02, 0x18, 0x35, 0x37, 0x3E, 0x3F};
        Rig a;
        a.m.clock.setHz(2000000);
        a.out(0x31, 0x88);
        a.speak(hello, 20000);
        const std::vector<int16_t> s = a.aud.only();
        int16_t peak = 0;
        for (int16_t v : s) peak = std::max<int16_t>(peak, (int16_t)std::abs(v));
        CHECK(s.size() > 44100 / 4, "HELLO is more than a quarter second of sound");
        CHECK(peak > 8000, "and it is loud at full volume");

        Rig b;
        b.m.clock.setHz(2000000);
        b.out(0x31, 0x88);
        b.speak(hello, 7777);
        CHECK(b.aud.only() == s, "the same samples however the time is sliced");

        Rig c;
        c.m.clock.setHz(2000000);
        c.out(0x31, 0xA8);                         // medium
        c.speak(hello, 20000);
        const double ratio = sumAbs(c.aud.only()) / sumAbs(s);
        CHECK(std::abs(ratio - PaiaSs100::volumeGain(0x20)) < 0.01, "medium volume scales it");

        Rig d;
        d.m.clock.setHz(2000000);
        d.out(0x31, 0xE8);                         // the amplifier off
        d.speak(hello, 20000);
        CHECK(d.aud.voices.empty(), "with the amplifier off nothing is pushed");

        Rig e;                                     // flat out: no crystal
        e.out(0x31, 0x88);
        e.speak(hello, 20000);
        CHECK(e.aud.voices.empty(), "flat out nothing is played");
        CHECK(e.ss->statusLines()[2].find("no crystal") != std::string::npos, "and SHOW says why");
    }

    SECTION("SS-100 -- a volume change is heard when it was made, not at the next slice");
    {
        const std::vector<uint8_t> word = {0x24, 0x24, 0x3F};
        auto run = [&word](uint64_t slice) {
            Rig g;
            g.m.clock.setHz(2000000);
            g.out(0x31, 0x88);
            g.out(0x30, word[0]);
            uint64_t since = 0;
            size_t   next  = 1;
            bool     turned = false;
            for (;;) {
                g.m.clock.advance(1);
                if (++since >= slice) {
                    g.ss->pump();
                    since = 0;
                }
                if (!turned && g.m.clock.now() == 150000) {
                    g.out(0x31, 0xC8);             // low, mid-slice
                    turned = true;
                }
                if (g.vi0()) {
                    if (next == word.size()) break;
                    g.out(0x30, word[next++]);
                }
            }
            g.ss->pump();
            return g.aud.only();
        };
        const std::vector<int16_t> a = run(20000), b = run(9999);
        CHECK(!a.empty() && a == b, "the change lands on the same sample either way");
    }
}
