#include "test.h"

#include "chips/votrax-sc01-rom.h"
#include "chips/votrax-sc01.h"
#include "core/crc32.h"
#include "core/statefile.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

using namespace altair;

namespace {

constexpr long long kCpu  = 2000000;   // T-states per second in these tests
constexpr int       kRate = 40000;     // the chip's own sample rate at 720 kHz

// Table 1 of the datasheet: each phoneme's length in ms at 720 kHz, by code. (AW, 3D,
// reads 250 in the SS-100 booklet and 253 in the Votrax sheet; 250 is used.)
constexpr int kTable1Ms[64] = {
    59,  71,  121, 47,  47,  71,  103, 90,  71,  55,  80,  121, 103, 80,  71,  71,
    71,  121, 71,  146, 121, 146, 103, 185, 103, 80,  47,  71,  71,  103, 55,  90,
    185, 65,  80,  47,  250, 103, 185, 185, 185, 103, 71,  90,  185, 80,  185, 103,
    90,  71,  103, 185, 80,  121, 59,  90,  80,  71,  146, 185, 121, 250, 185, 47,
};

double rms(const std::vector<int16_t>& v, size_t from, size_t to) {
    double sum = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) sum += double(v[i]) * v[i];
    return to > from ? std::sqrt(sum / double(to - from)) : 0;
}

// The lag in [lo, hi] at which the samples best match themselves: the pitch period.
int period(const std::vector<int16_t>& v, size_t from, size_t to, int lo, int hi) {
    int best = 0;
    double bestScore = -1e300;
    for (int lag = lo; lag <= hi; ++lag) {
        double s = 0;
        for (size_t i = from; i + size_t(lag) < to; ++i) s += double(v[i]) * v[i + size_t(lag)];
        s /= double(to - from - size_t(lag));
        if (s > bestScore) {
            bestScore = s;
            best      = lag;
        }
    }
    return best;
}

// Speak `codes` back to back, each strobed when A/R rises, and render all of it.
std::vector<int16_t> speak(VotraxSc01& c, const std::vector<uint8_t>& codes, uint8_t infl,
                           uint64_t& t) {
    std::vector<int16_t> pcm;
    for (uint8_t code : codes) {
        c.setInflection(t, infl);
        c.strobe(t, code);
        t = c.arDeadline();
        c.render(t, kRate, pcm);
    }
    return pcm;
}

} // namespace

void test_votrax_sc01() {
    SECTION("SC-01: the ROM is the decap image, and holds each phoneme once");
    {
        const auto& rom = sc01rom::image();
        CHECK(sizeof(rom) == 512, "512 bytes");
        CHECK(crc32(std::span<const uint8_t>(rom, sizeof(rom))) == 0xFC416227u,
              "CRC32 fc416227, as docs/sources.md records");
        bool seen[64] = {};
        bool once     = true;
        for (int i = 0; i < sc01rom::kWords; ++i) {
            const uint8_t code = sc01rom::decode(i).code;
            once      = once && !seen[code];
            seen[code] = true;
        }
        CHECK(once, "no code is in two words, so all 64 are there");
    }

    SECTION("SC-01: the fields read as the datasheet describes the sounds");
    {
        // Table 1: PA0, PA1 and STOP make no sound.
        bool pauses = true;
        for (uint8_t code : {0x03, 0x3E, 0x3F}) {
            const auto p = sc01rom::lookup(code);
            pauses = pauses && p.va == 0 && p.fa == 0;
        }
        CHECK(pauses, "the pauses have no voice and no noise");

        // The voiceless fricatives (S, SH, F, TH) are noise and no voice.
        bool frics = true;
        for (uint8_t code : {0x1F, 0x11, 0x1D, 0x39}) {
            const auto p = sc01rom::lookup(code);
            frics = frics && p.va == 0 && p.fa > 0;
        }
        CHECK(frics, "S, SH, F and TH have noise and no voice");

        // The vowels are voice and no noise.
        bool vowels = true;
        for (uint8_t code : {0x00, 0x02, 0x06, 0x0B, 0x15, 0x17, 0x20, 0x24, 0x26, 0x27,
                             0x28, 0x2C, 0x2E, 0x33, 0x3A, 0x3B}) {
            const auto p = sc01rom::lookup(code);
            vowels = vowels && p.va > 0 && p.fa == 0;
        }
        CHECK(vowels, "the vowels have voice and no noise");
    }

    SECTION("SC-01: every phoneme lasts as Table 1 says, within one tick");
    {
        // The phoneme counter counts 16 ticks (patent Fig. 7), so the length is known to
        // one tick: a sixteenth of it.
        int bad = -1;
        for (int code = 0; code < 64; ++code) {
            VotraxSc01 c;
            c.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
            c.strobe(1000, uint8_t(code));
            const double ms   = double(c.arDeadline() - 1000) * 1000.0 / kCpu;
            const double tick = ms / 16;
            if (std::fabs(ms - kTable1Ms[code]) > tick && bad < 0) bad = code;
        }
        CHECK(bad < 0, "all 64 lengths agree with Table 1 at 720 kHz");
        if (bad >= 0) std::printf("    first wrong: %02X %s\n", bad, VotraxSc01::name(uint8_t(bad)));
    }

    SECTION("SC-01: A/R falls on the strobe and rises when the phoneme ends");
    {
        VotraxSc01 c;
        CHECK(c.ar(0) && c.arDeadline() == VotraxSc01::kNever, "A/R is high at power-up");
        c.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
        c.strobe(5000, 0x24);   // AH
        const uint64_t end = c.arDeadline();
        CHECK(!c.ar(5000), "A/R is low at the strobe (Table 3: within 1 master clock)");
        CHECK(end != VotraxSc01::kNever && !c.ar(end - 1) && c.ar(end),
              "A/R rises at the deadline, not before");
        CHECK(c.ar(end + 100000), "and stays high until the next strobe");

        // A strobe that comes before the previous code was latched: the pins are read at
        // the latch, so the newer code sets the length.
        VotraxSc01 d;
        d.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
        d.strobe(1000, 0x24);   // AH, long
        d.strobe(1010, 0x03);   // PA0, short, 10 T-states later
        VotraxSc01 e;
        e.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
        e.strobe(1000, 0x03);
        CHECK(d.arDeadline() == e.arDeadline(), "a second strobe before the latch replaces the code");
        CHECK(d.phoneme() == 0x03, "and the chip reports the newer code");
    }

    SECTION("SC-01: the length follows the master clock");
    {
        VotraxSc01 a, b;
        a.setClocks(0, kCpu, 720000);
        b.setClocks(0, kCpu, 360000);
        a.strobe(0, 0x24);
        b.strobe(0, 0x24);
        const long long la = (long long)a.arDeadline(), lb = (long long)b.arDeadline();
        CHECK(std::llabs(lb - 2 * la) <= 2, "half the clock is twice the length");

        // A clock change in the middle of a phoneme: the rest of it runs at the new rate.
        VotraxSc01 m;
        m.setClocks(0, kCpu, 720000);
        m.strobe(0, 0x24);
        const uint64_t half = (uint64_t)la / 2;
        m.setClocks(half, kCpu, 360000);
        const long long expect = (long long)half + 2 * (la - (long long)half);
        CHECK(std::llabs((long long)m.arDeadline() - expect) <= 2,
              "a clock change stretches what is left of the phoneme");
    }

    SECTION("SC-01: the pauses are silent");
    {
        VotraxSc01 c;
        c.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
        uint64_t t = 0;
        auto pcm = speak(c, {0x03, 0x3F, 0x3E}, 0, t);
        bool silent = !pcm.empty();
        for (int16_t s : pcm) silent = silent && s == 0;
        CHECK(silent, "PA0, STOP and PA1 from power-up are exact silence");

        VotraxSc01 v;
        v.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
        t = 0;
        auto vowel = speak(v, {0x24}, 0, t);   // AH
        auto pause = speak(v, {0x03, 0x03, 0x03}, 0, t);
        const double loud  = rms(vowel, vowel.size() / 2, vowel.size());
        const double after = rms(pause, pause.size() - 400, pause.size());
        CHECK(loud > 1000, "a vowel makes sound");
        CHECK(after < loud / 100, "and the pauses after it fall silent");
    }

    SECTION("SC-01: voice and noise reach the output");
    {
        VotraxSc01 c;
        c.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
        uint64_t t = 0;
        auto s = speak(c, {0x1F, 0x1F}, 0, t);   // S, S
        CHECK(rms(s, s.size() / 2, s.size()) > 100, "S, all noise, makes sound");
    }

    SECTION("SC-01: the pitch follows I1, I2 and F1 as the patent's counter does");
    {
        // Patent Fig. 6: a count from the preset to the carry at master / 36, with I1 and
        // I2 above the inverse of F1 in the preset. Period = 226 + 2 F1 - 32 I1 - 64 I2
        // counts; at 720 kHz one count is two samples at 40 kHz. AH has F1 = 15.
        const int f1 = sc01rom::lookup(0x24).f1;
        int got[4], want[4];
        for (int i = 0; i < 4; i += 3) {
            VotraxSc01 c;
            c.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
            uint64_t t = 0;
            auto pcm = speak(c, {0x24, 0x24}, uint8_t(i), t);
            const size_t n = pcm.size();
            want[i] = 2 * (226 + 2 * f1 - 32 * (i & 1) - 64 * (i >> 1));
            got[i]  = period(pcm, n - 4000, n, 280, 600);
        }
        CHECK(std::abs(got[0] - want[0]) <= 2, "I1 = I2 = 0: the longest period");
        CHECK(std::abs(got[3] - want[3]) <= 2, "I1 = I2 = 1: the shortest");
        std::printf("    periods: %d (want %d), %d (want %d) samples\n", got[0], want[0],
                    got[3], want[3]);
    }

    SECTION("SC-01: the sound is the same however the time is sliced");
    {
        auto run = [](bool sliced) {
            VotraxSc01 c;
            c.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
            std::vector<int16_t> pcm;
            uint64_t t = 0, done = 0;
            uint32_t lcg = 12345;
            const uint8_t words[] = {0x1B, 0x00, 0x18, 0x35, 0x37, 0x03};   // H EH3 L O1 U1 PA0
            for (int i = 0; i < 6; ++i) {
                c.setInflection(t, uint8_t(i & 3));
                c.strobe(t, words[i]);
                const uint64_t end = c.arDeadline();
                if (i == 3) c.setClocks(t + 20000, kCpu, 600000);
                while (sliced && done < end) {
                    lcg  = lcg * 1103515245u + 12345u;
                    done = std::min<uint64_t>(end, done + 1 + (lcg >> 16) % 3000);
                    c.render(done, 44100, pcm);
                }
                t = end;
            }
            if (!sliced) c.render(t, 44100, pcm);
            return pcm;
        };
        const auto whole = run(false), parts = run(true);
        CHECK(!whole.empty() && whole == parts, "one call or a thousand give the same samples");
    }

    SECTION("SC-01: skipped time is not rendered, and the voice carries on");
    {
        VotraxSc01 c;
        c.setClocks(0, kCpu, VotraxSc01::kStandardClockHz);
        c.strobe(0, 0x24);
        const uint64_t from = c.arDeadline();
        c.skipTo(from);
        CHECK(c.ar(from) && c.arDeadline() == VotraxSc01::kNever, "A/R is high after the skip");
        std::vector<int16_t> pcm;
        c.render(from, kRate, pcm);
        CHECK(pcm.empty(), "nothing is made for the skipped time");
        c.strobe(from, 0x24);
        const uint64_t end = c.arDeadline();
        c.render(end, kRate, pcm);
        CHECK(rms(pcm, pcm.size() / 2, pcm.size()) > 1000, "the next phoneme is heard");
        CHECK(pcm.size() + 1 >= (end - from) * kRate / kCpu && pcm.size() <= (end - from) * kRate / kCpu,
              "and its samples start where the skip ended");
    }

    SECTION("SC-01: SNAPSHOT/RESTORE keeps the phoneme, the pitch and A/R");
    {
        VotraxSc01 a;
        a.setClocks(0, kCpu, 500000);
        a.setInflection(10, 2);
        a.strobe(100, 0x2C);
        StateWriter w;
        a.serialize(w);
        VotraxSc01 b;
        StateReader r(w.data());
        b.deserialize(r);
        CHECK(r.ok() && r.remaining() == 0, "the state reads back whole");
        CHECK(b.phoneme() == 0x2C && b.inflection() == 2 && b.masterHz() == 500000,
              "the code, the inflection and the clock come back");
        CHECK(b.arDeadline() == a.arDeadline() && !b.ar(200), "A/R comes back mid-phoneme");
    }
}
