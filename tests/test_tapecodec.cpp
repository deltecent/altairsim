//
// The cassette modem and the WAV container.
//
// NO FILESYSTEM AND NO MACHINE. Everything here is bytes in, bytes out -- which is
// the point of putting the modem in host/ with no board attached (host/tapemodem.h).
//
// THE ADVERSARIAL SECTION IS THE ONE THAT MATTERS. A round trip through our own
// modulator proves the two halves agree with each other and nothing more; it would
// pass just as happily if both were wrong in the same way. What proves the decoder is
// DAMAGING the audio first, in the specific ways a physical cassette damages it, and
// each case below is aimed at one stage of the demodulator so that a failure says
// which stage broke:
//
//     DC offset            -> the high-pass          (crossings(), stage 1)
//     quiet / fading tape  -> the envelope follower  (stage 2)
//     noise                -> the Schmitt hysteresis (stage 3)
//     speed error          -> two-means calibration + per-frame re-anchoring
//
// The numbers are not decorative: 3% speed error is inside the +/-5% the real 88-ACR
// tolerated, and a tape that fades to a fifth of its level is an ordinary tape.

#include "host/tapemodem.h"
#include "host/wav.h"
#include "test.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

namespace {

// A deterministic pseudorandom payload. Deterministic because a test that fails one
// run in twenty is worse than no test: it teaches you to re-run it.
std::vector<uint8_t> payload(size_t n) {
    std::vector<uint8_t> v;
    v.reserve(n);
    uint32_t x = 0x12345678u;
    for (size_t i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;   // xorshift32
        v.push_back(uint8_t(x & 0xFF));
    }
    return v;
}

bool same(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && (a.empty() || std::equal(a.begin(), a.end(), b.begin()));
}

// Modulate, damage the audio with `f`, demodulate. Returns true if every byte came
// back. A short leader keeps the tests quick -- the leader is not what is under test.
template <typename F>
bool survives(const TapeFormat& fmt, uint32_t rate, F damage, size_t bytes = 256) {
    const std::vector<uint8_t> in = payload(bytes);
    AudioBuffer a = modulate(in, fmt, rate, 0.25);
    damage(a);
    return same(in, demodulate(a, fmt).bytes);
}

void wr32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 24));
}
void wr16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
}
void tag(std::vector<uint8_t>& v, const char* t) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(t[i]));
}

// A WAV built by hand, so the reader meets the layouts real archive files use rather
// than only the one our own writer emits.
std::vector<uint8_t> handBuiltWav(bool listChunkFirst, bool oddChunk, uint16_t bits,
                                  uint16_t channels, uint16_t fmtTag) {
    std::vector<uint8_t> body;

    if (listChunkFirst) {
        // A chunk before `fmt `. Perfectly legal, and it is what a ripping tool that
        // stamps its own name into the file produces.
        tag(body, "LIST");
        wr32(body, 4);
        tag(body, "INFO");
    }
    if (oddChunk) {
        // ODD LENGTH: the pad byte is not counted in the size. A reader that forgets
        // it reads every later chunk one byte out -- and does not fail, it just
        // returns nonsense.
        tag(body, "note");
        wr32(body, 3);
        body.push_back('a'); body.push_back('b'); body.push_back('c');
        body.push_back(0);  // the pad
    }

    tag(body, "fmt ");
    const bool ext = (fmtTag == 0xFFFE);
    wr32(body, ext ? 40u : 16u);
    wr16(body, fmtTag);
    wr16(body, channels);
    wr32(body, 22050);
    wr32(body, 22050u * channels * (bits / 8));
    wr16(body, uint16_t(channels * (bits / 8)));
    wr16(body, bits);
    if (ext) {
        wr16(body, 22);          // cbSize
        wr16(body, bits);        // valid bits
        wr32(body, 0);           // channel mask
        wr16(body, 1);           // the REAL format, first 2 bytes of the GUID
        for (int i = 0; i < 14; ++i) body.push_back(0);
    }

    // One cycle of something, in every channel, just so there are samples.
    std::vector<uint8_t> data;
    for (int i = 0; i < 64; ++i)
        for (uint16_t c = 0; c < channels; ++c) {
            const double s = std::sin(i * 0.1);
            if (bits == 8) data.push_back(uint8_t(128 + int(s * 100)));
            else if (bits == 16) wr16(data, uint16_t(int16_t(s * 30000)));
            else if (bits == 24) {
                int32_t x = int32_t(s * 8000000);
                data.push_back(uint8_t(x)); data.push_back(uint8_t(x >> 8));
                data.push_back(uint8_t(x >> 16));
            } else wr32(data, uint32_t(int32_t(s * 2000000000)));
        }
    tag(body, "data");
    wr32(body, uint32_t(data.size()));
    body.insert(body.end(), data.begin(), data.end());

    std::vector<uint8_t> v;
    tag(v, "RIFF");
    wr32(v, uint32_t(body.size() + 4));
    tag(v, "WAVE");
    v.insert(v.end(), body.begin(), body.end());
    return v;
}

} // namespace

void test_tapecodec() {
    const TapeFormat kcs  = tapeformats::kcs300();
    const TapeFormat cuts = tapeformats::cuts1200();
    const TapeFormat fsk  = tapeformats::fsk300_1850();

    // -----------------------------------------------------------------------
    SECTION("tape modem: round trip, every format, every rate");
    // The floor is deliberate. 8 kHz gives a 2400 Hz tone barely three samples a
    // cycle, and cycle-counted 1200 baud does not survive it; no archive is at 8 kHz
    // and modulate() defaults to 44100. 11 kHz up is the supported range, and it is
    // stated in docs/manual/tapes.md rather than left for someone to discover.
    for (const TapeFormat& f : {kcs, cuts, fsk}) {
        for (uint32_t rate : {11025u, 22050u, 44100u, 48000u}) {
            const std::vector<uint8_t> in = payload(512);
            DemodResult r = demodulate(modulate(in, f, rate, 0.25), f);
            CHECK(same(in, r.bytes), (std::string("round trip ") + f.name + " @" +
                                      std::to_string(rate)).c_str());
            CHECK(r.framingErrors == 0,
                  (std::string("no framing errors ") + f.name + " @" +
                   std::to_string(rate)).c_str());
        }
    }

    // -----------------------------------------------------------------------
    SECTION("tape modem: the format is carried, not guessed");
    // A tape demodulated with the WRONG format must not quietly return plausible
    // bytes -- that is what the mount's confidence floor is for, and it only works if
    // a mismatch actually scores badly. CUTS is 1200 baud and KCS is 300; reading one
    // as the other is the mistake most likely to happen in the field.
    {
        const std::vector<uint8_t> in = payload(512);
        DemodResult wrong = demodulate(modulate(in, cuts, 44100, 0.25), kcs);
        CHECK(wrong.confidence() < 0.9, "cuts read as kcs scores below the mount floor");
        DemodResult right = demodulate(modulate(in, cuts, 44100, 0.25), cuts);
        CHECK(right.confidence() > 0.99, "cuts read as cuts scores above it");
    }

    // -----------------------------------------------------------------------
    SECTION("tape modem: survives what a real cassette does to a signal");
    for (const TapeFormat& f : {kcs, cuts, fsk}) {
        const std::string n = f.name;

        // DC OFFSET -- the high-pass. A recorder's coupling puts the waveform off
        // centre; a decoder that slices at zero sees one tone as a flat line.
        CHECK(survives(f, 22050, [](AudioBuffer& a) {
                  for (float& s : a.s) s += 0.30f;
              }), (n + ": +0.30 DC offset").c_str());

        // A QUIET TAPE -- the envelope follower. The hysteresis must scale to the
        // signal, not to full scale.
        CHECK(survives(f, 22050, [](AudioBuffer& a) {
                  for (float& s : a.s) s *= 0.05f;
              }), (n + ": recorded at 5% level").c_str());

        // A FADING TAPE -- the same, but moving: a dying battery or a slipping belt.
        CHECK(survives(f, 22050, [](AudioBuffer& a) {
                  const size_t m = a.s.size();
                  for (size_t i = 0; i < m; ++i)
                      a.s[i] = float(a.s[i] * (1.0 - 0.8 * double(i) / m));
              }), (n + ": level fades 1.0 -> 0.2 across the tape").c_str());

        // NOISE -- the Schmitt trigger. Deterministic noise, for the reason above.
        CHECK(survives(f, 22050, [](AudioBuffer& a) {
                  uint32_t x = 0xC0FFEEu;
                  for (float& s : a.s) {
                      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                      s += float((double(x % 2000) / 1000.0 - 1.0) * 0.03);
                  }
              }), (n + ": additive noise at -30 dB").c_str());
    }

    // -----------------------------------------------------------------------
    SECTION("tape modem: survives tape speed error");
    // The one the calibration and the per-frame re-anchoring exist for. Resampling
    // by a constant factor IS a tape running fast or slow, and +/-3% is inside what
    // the real card tolerated. Done by relabelling the sample rate, which is exactly
    // a speed change: the same samples, played at a different speed.
    for (const TapeFormat& f : {kcs, cuts, fsk}) {
        for (double k : {0.97, 1.03}) {
            const std::vector<uint8_t> in = payload(256);
            AudioBuffer a = modulate(in, f, 44100, 0.25);
            a.rate = uint32_t(44100 * k);   // the tape now plays k times too fast
            CHECK(same(in, demodulate(a, f).bytes),
                  (std::string(f.name) + ": tape speed " +
                   std::to_string(int((k - 1) * 100)) + "%").c_str());
        }
    }

    // -----------------------------------------------------------------------
    SECTION("tape modem: a blank tape is not an error");
    {
        AudioBuffer silence;
        silence.rate = 22050;
        silence.s.assign(22050, 0.0f);          // one second of nothing
        DemodResult r = demodulate(silence, kcs);
        CHECK(r.bytes.empty(), "silence decodes to no bytes");

        AudioBuffer leader = modulate({}, kcs, 22050, 1.0);  // pure carrier
        CHECK(demodulate(leader, kcs).bytes.empty(), "leader alone decodes to no bytes");
    }

    // -----------------------------------------------------------------------
    SECTION("wav: our own writer round-trips");
    {
        AudioBuffer a;
        a.rate = 22050;
        for (int i = 0; i < 500; ++i) a.s.push_back(float(std::sin(i * 0.05)));
        const std::vector<uint8_t> file = buildWav(a);
        CHECK(looksLikeWav(file.data(), file.size()), "buildWav emits RIFF/WAVE");

        AudioBuffer b;
        std::string err;
        CHECK(parseWav(file.data(), file.size(), b, err), "parseWav reads it back");
        CHECK(b.rate == a.rate, "sample rate survives");
        CHECK(b.s.size() == a.s.size(), "sample count survives");
        double worst = 0;
        for (size_t i = 0; i < b.s.size(); ++i)
            worst = std::max(worst, std::fabs(double(a.s[i]) - b.s[i]));
        CHECK(worst < 1e-4, "samples survive 16-bit quantisation");
    }

    // -----------------------------------------------------------------------
    SECTION("wav: the layouts real archive files actually use");
    {
        std::string err;
        AudioBuffer a;

        CHECK(parseWav(handBuiltWav(true, false, 16, 1, 1).data(),
                       handBuiltWav(true, false, 16, 1, 1).size(), a, err),
              "a LIST chunk before fmt is walked over");

        const std::vector<uint8_t> odd = handBuiltWav(false, true, 16, 1, 1);
        CHECK(parseWav(odd.data(), odd.size(), a, err), "an odd-length chunk is padded");

        for (uint16_t bits : {uint16_t(8), uint16_t(16), uint16_t(24), uint16_t(32)}) {
            const std::vector<uint8_t> w = handBuiltWav(false, false, bits, 1, 1);
            CHECK(parseWav(w.data(), w.size(), a, err),
                  (std::to_string(bits) + "-bit PCM is read").c_str());
        }

        const std::vector<uint8_t> st = handBuiltWav(false, false, 16, 2, 1);
        CHECK(parseWav(st.data(), st.size(), a, err), "stereo is read");
        CHECK(a.s.size() == 64, "stereo is downmixed to one channel");

        const std::vector<uint8_t> ex = handBuiltWav(false, false, 16, 1, 0xFFFE);
        CHECK(parseWav(ex.data(), ex.size(), a, err),
              "WAVE_FORMAT_EXTENSIBLE is unwrapped to its real tag");
    }

    // -----------------------------------------------------------------------
    SECTION("wav: a refusal says why");
    // media.h's contract: a medium either opens or explains itself. "Empty tape" and
    // "I cannot read this" must never look the same to the operator.
    {
        std::string err;
        AudioBuffer a;

        const uint8_t junk[16] = {'N', 'O', 'T', 'A', 'W', 'A', 'V', 0};
        err.clear();
        CHECK(!parseWav(junk, sizeof junk, a, err), "a non-RIFF file is refused");
        CHECK(!err.empty(), "...with a reason");

        // A compressed WAV: the tag is neither PCM nor float.
        const std::vector<uint8_t> mp3 = handBuiltWav(false, false, 16, 1, 0x0055);
        err.clear();
        CHECK(!parseWav(mp3.data(), mp3.size(), a, err), "a compressed WAV is refused");
        CHECK(err.find("compressed") != std::string::npos, "...naming the problem");

        // Truncated mid-data. Clamped rather than refused -- a recording cut short is
        // still most of a recording, and the tape it holds may well still load.
        std::vector<uint8_t> cut = handBuiltWav(false, false, 16, 1, 1);
        cut.resize(cut.size() - 40);
        err.clear();
        CHECK(parseWav(cut.data(), cut.size(), a, err),
              "a truncated recording still opens, with the samples that survived");
    }
}
