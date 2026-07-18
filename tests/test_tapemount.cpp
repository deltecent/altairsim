// Mounting audio -- the codec seam, from a board's side (host/tapecodec.h).
//
// test_tapecodec.cpp proves the MODEM: tones in, bytes out, and back. This proves the
// MOUNT: that a real card, asked for a real file, gets what the hardware would have got
// and is refused what the hardware could not have read.
//
// The four things that could silently be wrong here, in the order they would bite:
//
//   * A BYTE TAPE MUST COME BACK UNTOUCHED. The whole design rests on the decorator
//     being invisible to a .TAP -- if a byte tape ever went through the demodulator,
//     every existing tape in the repo would decode to noise. So that is asserted
//     first, and every other suite's silence on the subject is the corroboration.
//
//   * A CARD MUST REFUSE A MODULATION ITS MODEM CANNOT HEAR. This is the one that is
//     not obvious and not optional: the receiver CALIBRATES ITSELF, so it will happily
//     read a Kansas City tape under the 88-ACR's parameters and hand the guest bytes
//     no 88-ACR could ever have recovered. The card's declared format list is the only
//     thing standing between us and inventing hardware, and section 2 is where that is
//     actually tested rather than merely commented.
//
//   * A REFUSAL MUST SAY WHAT THE TAPE IS. "Cannot mount" sends the operator to the
//     source; "it carries 2400/1200 Hz and this card reads fsk300" ends it.
//
//   * AUDIO MOUNTS READ-ONLY IN THIS PHASE, AND MUST SAY SO. A silent demotion is how
//     an afternoon disappears into a loader that bounces every write.
//
// No filesystem: MemoryMedia through setMediaResolver, as everywhere else.

#include "test.h"

#include "boards/mits-88acr.h"
#include "boards/proctech-sol.h"
#include "core/machine.h"
#include "host/media.h"
#include "host/tapecodec.h"
#include "host/tapemodem.h"
#include "host/wav.h"

#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

// A tape, as a file: whatever bytes we say, handed to whoever mounts it.
void withFile(std::vector<uint8_t> contents, bool ro = false) {
    setMediaResolver([contents, ro](const std::string& path, bool wantRo, std::string&) {
        return std::make_unique<MemoryMedia>(path, contents, ro || wantRo);
    });
}

// The same bytes, recorded as audio in a given modulation. This is the file a real
// archive hands us, built here so the test needs no network and no fixture.
std::vector<uint8_t> asWav(const std::vector<uint8_t>& data, const TapeFormat& f,
                           uint32_t rate = 22050) {
    return buildWav(modulate(data, f, rate));
}

std::vector<uint8_t> payload() {
    std::vector<uint8_t> v;
    for (int i = 0; i < 256; ++i) v.push_back(uint8_t(i * 7 + 13));
    return v;
}

// Read the whole tape back through the board's own TapeImage -- i.e. through exactly
// the path the UART uses, not through a back door into the codec.
std::vector<uint8_t> readOff(const TapeImage& t) {
    std::vector<uint8_t> out;
    TapeImage&           nc = const_cast<TapeImage&>(t);
    nc.rewind();
    uint8_t b;
    while (nc.read(b)) out.push_back(b);
    nc.rewind();
    return out;
}

bool mentions(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// The tone figures out of a refusal, as numbers.
//
// RANGE-CHECKED, NEVER STRING-MATCHED, because they are a MEASUREMENT: a Kansas City
// tape reports "2398 Hz / 1206 Hz", not the nominal 2400/1200, and it should -- the
// receiver measures what is on the tape rather than trusting a manual. Asserting the
// literal would be asserting the modem is wrong.
std::vector<double> tonesIn(const std::string& s) {
    std::vector<double> v;
    for (size_t i = 0; i + 3 < s.size(); ++i) {
        if (s.compare(i, 3, " Hz") != 0) continue;
        size_t b = i;
        while (b > 0 && (isdigit(uint8_t(s[b - 1])) || s[b - 1] == '.')) --b;
        if (b < i) v.push_back(atof(s.substr(b, i - b).c_str()));
    }
    return v;
}

bool near(double got, double want, double tol = 0.05) {
    return got > want * (1 - tol) && got < want * (1 + tol);
}

struct AcrRig {
    Machine   m;
    AcrBoard* acr = nullptr;
    AcrRig() {
        std::string err;
        m.add("acr", "acr0", err);
        acr = static_cast<AcrBoard*>(m.find("acr0"));
    }
};

struct SolRig {
    Machine   m;
    SolBoard* sol = nullptr;
    SolRig() {
        std::string err;
        m.add("sol", "sol0", err);
        sol = static_cast<SolBoard*>(m.find("sol0"));
    }
};

} // namespace

void test_tapemount() {
    const std::vector<uint8_t> data = payload();

    // -----------------------------------------------------------------------
    SECTION("tape mount: a byte tape is not audio, and comes back untouched");
    // The regression floor, made explicit. Every .TAP in the repo depends on this
    // being true, and on the magic -- not the extension -- being what decides.
    {
        withFile(data);
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "PROGRAM.TAP", false, err), "a byte tape mounts");
        CHECK(readOff(*r.acr->tape()) == data, "and reads back byte for byte");
        CHECK(!r.acr->tape()->readOnlyForced(), "and is NOT demoted to read-only");
    }
    {
        // ...even called .wav. A .tap somebody renamed must not be demodulated.
        withFile(data);
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "MISNAMED.WAV", false, err),
              "a byte tape called .WAV still mounts");
        CHECK(readOff(*r.acr->tape()) == data, "and is still read as bytes");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: a WAV in the card's own modulation loads");
    {
        withFile(asWav(data, tapeformats::fsk300_1850()));
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "an ACR-format WAV mounts");
        CHECK(readOff(*r.acr->tape()) == data, "and demodulates byte for byte");

        // And it SAYS what it found -- a clean mount narrates too.
        std::vector<std::string> said = r.acr->drainLog();
        bool told = false;
        for (const std::string& s : said)
            if (mentions(s, "fsk300") && mentions(s, "0 framing errors")) told = true;
        CHECK(told, "and reports the format and the error count");
    }
    {
        withFile(asWav(data, tapeformats::cuts1200()));
        SolRig      r;
        std::string err;
        CHECK(r.sol->mount("tape1", "SOLTAPE.WAV", false, err), "a CUTS WAV mounts on the Sol");
        CHECK(readOff(*r.sol->tape(1)) == data, "and demodulates byte for byte");
    }
    {
        // The Sol's OTHER speed. Both are its hardware -- the guest picks at OUT FAh D5
        // -- so `auto` must pick the one that is actually on the tape.
        withFile(asWav(data, tapeformats::kcs300()));
        SolRig      r;
        std::string err;
        CHECK(r.sol->mount("tape1", "SLOW.WAV", false, err), "a 300-baud KCS WAV mounts");
        CHECK(readOff(*r.sol->tape(1)) == data, "and demodulates byte for byte");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: a card REFUSES a modulation its modem could not hear");
    // THE ONE THAT MATTERS. The demodulator calibrates itself off the tape, so it can
    // read this file perfectly well -- and must not, because an 88-ACR's PLL sits at
    // 2125 Hz +/-100 and a 1200 Hz space tone is 925 Hz outside it. A real card fed
    // this tape reads NOTHING. Handing the guest bytes anyway is inventing hardware.
    {
        withFile(asWav(data, tapeformats::cuts1200()));
        AcrRig      r;
        std::string err;
        CHECK(!r.acr->mount("tape", "KANSAS.WAV", false, err),
              "the 88-ACR refuses a Kansas City tape");
        CHECK(r.acr->tape() == nullptr, "and nothing is in the recorder afterwards");
        const std::vector<double> tones = tonesIn(err);
        CHECK(tones.size() == 2 && near(tones[0], 2400) && near(tones[1], 1200),
              "and the refusal quotes the tones the tape actually carries");
        CHECK(mentions(err, "fsk300"), "and names what this card does read");
    }
    {
        // ...and the other way round: the Sol's CUTS demodulator has no business with
        // an 1850 Hz space tone either. The refusal is not a property of one card.
        withFile(asWav(data, tapeformats::fsk300_1850()));
        SolRig      r;
        std::string err;
        CHECK(!r.sol->mount("tape1", "ACRTAPE.WAV", false, err),
              "the Sol refuses an 88-ACR tape");
        CHECK(r.sol->tape(1) == nullptr, "and the deck stays empty");
        const std::vector<double> tones = tonesIn(err);
        CHECK(tones.size() == 2 && near(tones[1], 1850),
              "and the refusal quotes the space tone it actually found");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: noise is refused, not mounted");
    // A tape that decodes at 40% is not a tape. Mounting it would hand the guest
    // thousands of bytes of garbage and let them debug their loader instead.
    {
        AudioBuffer a;
        a.rate = 22050;
        uint32_t x = 12345;  // a fixed sequence: a flaky test is worse than no test
        for (int i = 0; i < 22050; ++i) {
            x = x * 1103515245u + 12345u;
            a.s.push_back(float((x >> 16) % 2000) / 1000.0f - 1.0f);
        }
        withFile(buildWav(a));
        SolRig      r;
        std::string err;
        CHECK(!r.sol->mount("tape1", "NOISE.WAV", false, err), "noise does not mount");
        CHECK(r.sol->tape(1) == nullptr, "and the deck stays empty");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: `format` selects a reading, and never widens the hardware");
    {
        // `raw` reads a real WAV's own file bytes -- how you look at a tape that
        // decodes badly, without renaming anything.
        const std::vector<uint8_t> wav = asWav(data, tapeformats::fsk300_1850());
        withFile(wav);
        AcrRig      r;
        std::string err;
        std::string unused;
        for (Property& p : r.acr->unitProperties("tape"))
            if (p.name == "format") p.set(Value::ofStr("raw"), unused);

        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "format=raw mounts a WAV");
        CHECK(readOff(*r.acr->tape()) == wav, "and hands over the FILE's bytes, not the tape's");
    }
    {
        // Asking for a format this card's modem does not have is refused -- the
        // property picks among the hardware's formats, it does not add one.
        withFile(asWav(data, tapeformats::cuts1200()));
        AcrRig      r;
        std::string err;
        std::string unused;
        for (Property& p : r.acr->unitProperties("tape"))
            if (p.name == "format") p.set(Value::ofStr("cuts1200"), unused);

        CHECK(!r.acr->mount("tape", "KANSAS.WAV", false, err),
              "the 88-ACR will not be told to demodulate Kansas City");
        CHECK(mentions(err, "fsk300"), "and the refusal names what it does read");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: audio is read-only for now, and says so");
    // Recording back out to a WAV is Phase 4. Until then the demotion is real, so it
    // is reported -- the same bargain a write-protected host file already makes.
    {
        withFile(asWav(data, tapeformats::fsk300_1850()));
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "the WAV mounts");
        CHECK(r.acr->tape()->readOnly(), "read-only, though nobody asked for it");
        CHECK(r.acr->tape()->readOnlyForced(), "and flagged as forced, not chosen");

        std::vector<std::string> said = r.acr->drainLog();
        bool told = false;
        for (const std::string& s : said)
            if (mentions(s, "read-only")) told = true;
        CHECK(told, "and the demotion is narrated, never silent");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: `detected` reports what is in the recorder, and only then");
    {
        withFile(asWav(data, tapeformats::fsk300_1850()));
        AcrRig      r;
        std::string err;

        auto detected = [&r] {
            for (Property& p : r.acr->unitProperties("tape"))
                if (p.name == "detected") return p.get().s();
            return std::string("<missing>");
        };

        CHECK(detected().empty(), "nothing is mounted, so nothing is detected");
        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "the WAV mounts");
        CHECK(detected() == "fsk300", "and `detected` names the modulation");
        CHECK(r.acr->unmount("tape", err), "the tape comes out");
        CHECK(detected().empty(), "and an empty recorder is in no format at all");
    }

    // A byte tape reports `raw` -- not blank, which would read as "we do not know".
    {
        withFile(data);
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "PROGRAM.TAP", false, err), "a byte tape mounts");
        std::string det;
        for (Property& p : r.acr->unitProperties("tape"))
            if (p.name == "detected") det = p.get().s();
        CHECK(det == "raw", "and calls itself raw");
    }

    setMediaResolver(openHostFile);  // put the real one back for whoever runs next
}
