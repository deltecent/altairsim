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
//     source; "it carries a Kansas-City-ish tone pair and this card reads fsk300" ends it.
//
//   * A RECORDING MUST SURVIVE A ROUND TRIP, and must not leave the OLD tape behind it
//     when it is shorter. Re-encoding rewrites the whole file, so a missing truncate
//     shows up as trailing bytes no guest ever recorded -- garbage in a program image,
//     produced by nothing the operator did.
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
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

// A tape, as a file: whatever bytes we say, handed to whoever mounts it.
void withFile(std::vector<uint8_t> contents, bool ro = false) {
    setMediaResolver([contents, ro](const std::string& path, bool wantRo, std::string&) {
        // `ro` is the FILE's own protection, so a mount that did not ask for read-only
        // and got it anyway is a forced one -- exactly what a host file does.
        return std::make_unique<MemoryMedia>(path, contents, ro || wantRo, ro && !wantRo);
    });
}

// A MemoryMedia that MIRRORS ITSELF into a buffer the test owns, every time it is
// synced. A raw pointer to the medium would not do: the medium is owned by the tape,
// which is owned by the board, so the most interesting case of all -- a machine torn
// down with the RECORD button still down -- would read freed memory. The mirror
// outlives the machine, which is exactly what a host file does.
class MirrorMedia : public MemoryMedia {
public:
    MirrorMedia(std::string name, std::vector<uint8_t> b, bool ro,
                std::shared_ptr<std::vector<uint8_t>> mirror)
        : MemoryMedia(std::move(name), std::move(b), ro), mirror_(std::move(mirror)) {
        *mirror_ = bytes();
    }
    void sync() override {
        MemoryMedia::sync();
        *mirror_ = bytes();
    }

private:
    std::shared_ptr<std::vector<uint8_t>> mirror_;
};

using Slot = std::shared_ptr<std::vector<uint8_t>>;
Slot withRecordableFile(std::vector<uint8_t> contents) {
    auto slot = std::make_shared<std::vector<uint8_t>>();
    setMediaResolver([contents, slot](const std::string& path, bool ro, std::string&) {
        return std::make_unique<MirrorMedia>(path, contents, ro, slot);
    });
    return slot;
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

// The bus, reduced to what a card sees -- so a recording test can go through the UART
// rather than around it.
template <typename B>
void out(B& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

bool mentions(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// SET, through the reflection layer rather than through a member -- because that is the
// path an operator, a TOML file and CONFIG SAVE all take, and a property whose setter
// was never wired would still pass a test that poked the field.
bool setProp(std::vector<Property> props, const std::string& name, const Value& v) {
    std::string err;
    for (Property& p : props)
        if (p.name == name) return p.set && p.set(v, err);
    return false;
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
        // A genuine 1200-baud CUTS tape carries 1200/600 Hz (its "0" a half cycle of 600).
        // The ACR's demodulator, reading under its own sawtooth assumption, doubles that to
        // ~2330/1310 Hz: a mark near Kansas City's 2400 but a space nowhere near the ACR's
        // 1850, which is exactly why it is refused. The refusal quotes what it measured.
        const std::vector<double> tones = tonesIn(err);
        CHECK(tones.size() == 2 && near(tones[0], 2400, 0.06) &&
                  tones[1] > 1150 && tones[1] < 1500,
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
    SECTION("tape mount: audio records, and a recording survives a round trip");
    // THE PHASE 4 CLAIM, END TO END: record onto a mounted WAV, stop the transport,
    // and mount the resulting file again. What comes back must be what was recorded --
    // which exercises the modulator, the RIFF writer, the parser, the demodulator, the
    // framer and the write-back, against a byte stream we chose.
    {
        Slot        slot = withRecordableFile(asWav(data, tapeformats::fsk300_1850()));
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "the WAV mounts");
        CHECK(!r.acr->tape()->readOnly(), "and it is NOT read-only any more");
        CHECK(!r.acr->tape()->readOnlyForced(), "nothing was forced on the operator");

        CHECK(setProp(r.acr->unitProperties("tape"), "mode", Value::ofStr("record")),
              "the RECORD button goes down");

        // Through the board's own TapeImage -- the same call the UART's TapeStream
        // makes, not a back door into the codec.
        TapeImage& t = *const_cast<TapeImage*>(r.acr->tape());
        t.rewind();
        const std::vector<uint8_t> cut = {0xC2, 0x00, 0x7F, 0xFF, 0x55, 0xAA, 0x01, 0x80};
        for (uint8_t b : cut) CHECK(t.write(b), "the guest records a byte");

        // Letting go of RECORD is a transport stop, and that is what writes the file.
        CHECK(setProp(r.acr->unitProperties("tape"), "mode", Value::ofStr("play")),
              "and comes back up");

        const std::vector<uint8_t> file = (*slot);
        CHECK(looksLikeWav(file.data(), file.size()), "what landed on the host is a WAV");

        // ...and now play it back on a card that has never seen this test's state.
        withFile(file);
        AcrRig again;
        CHECK(again.acr->mount("tape", "TAPE.WAV", true, err), "the recording mounts");
        std::vector<uint8_t> back = readOff(*again.acr->tape());
        CHECK(back.size() == data.size(), "the tape is still as long as it was");
        bool same = back.size() == data.size();
        for (size_t i = 0; same && i < cut.size(); ++i) same = back[i] == cut[i];
        CHECK(same, "and the recorded bytes came back exactly");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: recording through the UART, which syncs on every byte");
    // THE PATH THE GUEST ACTUALLY TAKES, and the one this phase changed. Uart1602
    // flushes its stream after every character, and for a tape that flush is a sync --
    // so an audio tape sees a sync per byte and must NOT re-encode itself there. That
    // is why sync() is a dirty mark and commit() does the work.
    //
    // Writing through TapeImage (as the sections either side do) would miss this
    // entirely: it never touches the UART, so it never provokes the per-byte sync.
    {
        Slot        slot = withRecordableFile(asWav(data, tapeformats::fsk300_1850()));
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "the WAV mounts");
        CHECK(setProp(r.acr->unitProperties("tape"), "mode", Value::ofStr("record")),
              "RECORD");

        // AND WIND IT BACK FIRST, which is not ceremony. Mounting attaches the line and
        // the UART receives EAGERLY, so by the time the RECORD button goes down the head
        // has already moved off the start -- and a cassette records from wherever the
        // head is. Without this the recording lands part-way into the tape, which is
        // exactly right and exactly not what anyone means.
        std::ostringstream sink;
        CHECK(r.acr->runCommand("REWIND", {"REWIND", "acr0:tape"}, sink, err),
              "wind it back to the start");

        const std::vector<uint8_t> cut = {0xAE, 0xAE, 0x3F, 0x00, 0xC9};
        for (uint8_t b : cut) out(*r.acr, 0x07, b);  // the ACR's data port

        // Nothing should have reached the host yet -- the transport has not stopped.
        CHECK(looksLikeWav((*slot).data(), (*slot).size()),
              "the file is still the WAV it was");

        CHECK(setProp(r.acr->unitProperties("tape"), "mode", Value::ofStr("play")),
              "and stop");

        withFile((*slot));
        AcrRig again;
        CHECK(again.acr->mount("tape", "TAPE.WAV", true, err), "the recording mounts");
        std::vector<uint8_t> back = readOff(*again.acr->tape());
        bool same = back.size() >= cut.size();
        for (size_t i = 0; same && i < cut.size(); ++i) same = back[i] == cut[i];
        CHECK(same, "and what the guest sent down the UART is on the tape");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: a recording nobody stopped is still not lost");
    // QUIT DOES NOT UNMOUNT. It sets a flag and lets the machine come apart, so an
    // operator who records and then quits without pressing stop never reaches any of
    // the commit points. A byte tape survives that because its sync() is real and runs
    // per byte; an audio tape's sync() deliberately does nothing, so without a backstop
    // in ~AudioTapeMedia the entire recording would go down with the machine.
    //
    // The rig is destroyed HERE, inside the block, which is the whole test.
    {
        Slot slot = withRecordableFile(asWav(data, tapeformats::fsk300_1850()));
        std::vector<uint8_t> file;
        const std::vector<uint8_t> cut = {0x11, 0x22, 0x33, 0x44};
        {
            AcrRig      r;
            std::string err;
            CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "the WAV mounts");
            CHECK(setProp(r.acr->unitProperties("tape"), "mode", Value::ofStr("record")),
                  "RECORD");
            TapeImage& t = *const_cast<TapeImage*>(r.acr->tape());
            t.rewind();
            for (uint8_t b : cut) t.write(b);
            // ...and now the machine goes away, with the button still down.
        }
        file = (*slot);

        withFile(file);
        AcrRig      again;
        std::string err;
        CHECK(again.acr->mount("tape", "TAPE.WAV", true, err), "the recording mounts");
        std::vector<uint8_t> back = readOff(*again.acr->tape());
        bool same = back.size() >= cut.size();
        for (size_t i = 0; same && i < cut.size(); ++i) same = back[i] == cut[i];
        CHECK(same, "and it survived a machine that was never told to stop");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: a shorter recording does not leave the old tape behind it");
    // THE REASON MediaFile::resize() EXISTS, and the one failure this phase could
    // plausibly ship. Re-encoding with no leader makes a much shorter WAV than the one
    // that was mounted; without a truncate, the tail of the OLD audio survives past the
    // end of the new file and the next mount demodulates it back as bytes no guest ever
    // recorded. Trailing garbage, in a program image, from nothing the operator did.
    {
        Slot        slot = withRecordableFile(asWav(data, tapeformats::fsk300_1850()));
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "the WAV mounts");

        const size_t before = (*slot).size();

        // Trim it to the data: no leader, no trailer. This is what makes the re-encoded
        // file shorter than the one it replaces.
        CHECK(setProp(r.acr->unitProperties("tape"), "leader", Value::ofInt(0)),
              "leader set to nothing");
        CHECK(setProp(r.acr->unitProperties("tape"), "trailer", Value::ofInt(0)),
              "trailer set to nothing");
        CHECK(setProp(r.acr->unitProperties("tape"), "mode", Value::ofStr("record")),
              "RECORD");

        TapeImage& t = *const_cast<TapeImage*>(r.acr->tape());
        t.rewind();
        for (uint8_t b : data) CHECK(t.write(b), "re-record the whole tape");

        CHECK(setProp(r.acr->unitProperties("tape"), "mode", Value::ofStr("play")),
              "and stop");

        const std::vector<uint8_t> file = (*slot);
        CHECK(file.size() < before, "the file really did get shorter");

        withFile(file);
        AcrRig again;
        CHECK(again.acr->mount("tape", "TAPE.WAV", true, err), "it mounts again");
        CHECK(readOff(*again.acr->tape()) == data,
              "and holds the recording and NOTHING after it");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: the Sol writes a deck back when the GUEST stops the motor");
    // THE STOP ONLY THIS CARD CAN SEE. The 88-ACR has no motor line -- the operator's
    // finger is its only transport control -- but the Sol's guest drops D7 at OUT 0FAh
    // when it has finished a SAVE, and that is a real end-of-recording. A guest that
    // saves and stops should have a file without anyone typing UNMOUNT.
    //
    // AND IT MUST HAPPEN IN pump(), NOT IN THE BUS CYCLE. Re-encoding is the one thing
    // the codec seam promises never to do inside a bus cycle, so the motor edge is
    // noted at OUT time and acted on at the next pump -- which is what the deliberate
    // pump() below is checking, not merely accompanying.
    {
        Slot        slot = withRecordableFile(asWav(data, tapeformats::cuts1200()));
        SolRig      r;
        std::string err;
        CHECK(r.sol->mount("tape1", "TAPE.WAV", false, err), "the WAV mounts in deck 1");

        std::string unused;
        for (Property& p : r.sol->unitProperties("tape1"))
            if (p.name == "mode") p.set(Value::ofStr("record"), unused);

        out(*r.sol, 0xFA, 0x80);  // deck 1 motor ON -- the line reaches the tape now
        const std::vector<uint8_t> cut = {0x01, 0x02, 0x03, 0x04};
        for (uint8_t b : cut) out(*r.sol, 0xFB, b);

        out(*r.sol, 0xFA, 0x00);  // ...and the guest stops the transport
        CHECK(looksLikeWav((*slot).data(), (*slot).size()),
              "nothing was re-encoded inside the bus cycle");
        r.sol->pump();

        withFile((*slot));
        SolRig again;
        CHECK(again.sol->mount("tape1", "TAPE.WAV", true, err), "the recording mounts");
        std::vector<uint8_t> back = readOff(*again.sol->tape(1));
        bool same = back.size() >= cut.size();
        for (size_t i = 0; same && i < cut.size(); ++i) same = back[i] == cut[i];
        CHECK(same, "and the motor stopping is what wrote it");
    }

    // -----------------------------------------------------------------------
    SECTION("tape mount: a write-protected audio file is still refused, and says so");
    // The demotion that is still real now that audio records -- not "audio is
    // read-only" any more, but "the host will not let us write THIS one, and the
    // operator did not ask for that". A silent demotion is how an
    // afternoon disappears into a loader that bounces every write.
    {
        withFile(asWav(data, tapeformats::fsk300_1850()), /*ro=*/true);
        AcrRig      r;
        std::string err;
        CHECK(r.acr->mount("tape", "TAPE.WAV", false, err), "it still mounts");
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
