// The 88-ACR (docs/boards/mits-88acr.md).
//
// THE CARD IS AN 88-SIO B WITH A MODEM MATED TO IT, so most of what could be wrong
// here is already pinned by tests/test_88sio.cpp -- and that is the point of this
// suite as much as anything: it asserts that the INHERITANCE IS REAL. If somebody
// ever "tidies up" AcrBoard into a standalone copy of the SIO, the status word will
// drift, and section 2 goes red.
//
// What is genuinely this card's, and could silently be wrong:
//
//   * THE STRAPS ARE SOURCED, not chosen. 006, 300 baud, 8N1 -- the assembly manual
//     says so in one sentence. The 88-SIO's own defaults are a guess; these are not,
//     and a test is how a sourced number stays sourced.
//
//   * THERE IS NO CONNECTOR AND NO MOTOR. CONNECT must be refused; PLAY and RECORD
//     are the operator's fingers.
//
//   * ONE HEAD MEANS ONE POSITION -- and the UART reads EAGERLY. A tape that could be
//     read and written at once has its first byte eaten before the guest runs, and
//     every recording begins at byte ONE. That bug is REAL: it was in this board
//     until the mode was added, and NO BOOT TEST WOULD EVER HAVE FOUND IT, because
//     loading a tape works perfectly while it is broken.
//
//   * REWIND is the only verb any board has, so it is also the only executable proof
//     that Board::commands() works at all.
//
// No filesystem: MemoryMedia through setMediaResolver.

#include "test.h"

#include "boards/mits-88acr.h"
#include "chips/uart1602.h"
#include "core/clock.h"
#include "core/machine.h"
#include "host/media.h"
#include "host/tape.h"
#include "host/tapecodec.h"
#include "host/tapemodem.h"
#include "host/wav.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

// ---- The bus, reduced to what a card actually sees ----
uint8_t in(AcrBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(AcrBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// The bytes the guest's recording actually put on the host file -- which is the only
// question that matters about a write, and the only one the guest cannot answer.
MemoryMedia* g_media = nullptr;

void withTape(const std::string& contents, bool ro = false) {
    setMediaResolver([contents, ro](const std::string& path, bool wantRo, std::string&) {
        auto m = std::make_unique<MemoryMedia>(
            path, std::vector<uint8_t>(contents.begin(), contents.end()), ro || wantRo);
        g_media = m.get();
        return m;
    });
}

struct Rig {
    Machine   m;
    AcrBoard* acr = nullptr;

    Rig() {
        std::string err;
        // PARANOID MODE, PERMANENTLY ON. Same reason as the 88-SIO's suite: this card
        // drives pin 73 and has to remember to say so.
        m.bus.setVerify(true);
        acr = dynamic_cast<AcrBoard*>(m.add("acr", "acr0", err));
        m.power();
    }
    bool mount(const std::string& p, bool ro = false) {
        std::string err;
        return acr->mount("tape", p, ro, err);
    }
    // Press a button on the recorder.
    bool press(const char* mode) {
        std::string err;
        return setUnitProperty(*acr, "tape", "mode", mode, err);
    }
    // Wind the tape back, through the verb, exactly as a user would.
    bool rewind(std::string& said) {
        std::ostringstream o;
        std::string        err;
        bool ok = acr->runCommand("REWIND", {"REW", "acr0:tape"}, o, err);
        said    = ok ? o.str() : err;
        return ok;
    }
    // Wind to a position (a time, START or END), through the verb.
    bool wind(const std::string& where, std::string& said) {
        std::ostringstream o;
        std::string        err;
        bool ok = acr->runCommand("WIND", {"WI", "acr0:tape", where}, o, err);
        said    = ok ? o.str() : err;
        return ok;
    }
    // Read a tape-unit property (position, counter, ...) as text.
    std::string prop(const std::string& name) {
        for (Property& p : acr->unitProperties("tape"))
            if (p.name == name) return p.get().s();
        return "<none>";
    }
    // Take the next byte off the tape the way a loader does: poll until DAV, read.
    // Returns false if the tape ran out. `budget` is in character times.
    bool getByte(uint8_t& b, int budget = 4) {
        for (int i = 0; i < budget; ++i) {
            if ((in(*acr, 0x06) & 0x01) == 0) {  // bit 0 LOW = data available
                b = in(*acr, 0x07);
                return true;
            }
            m.clock.advance(m.clock.tStatesPer(30));  // let a character time go by
        }
        return false;
    }
};

std::string tapeBytes() {
    if (!g_media) return "";
    const auto& v = g_media->bytes();
    return std::string(v.begin(), v.end());
}

} // namespace

void test_88acr() {
    SECTION("88-ACR -- cassette (an 88-SIO B, plus a modem, plus a tape)");

    // -----------------------------------------------------------------------
    // 1. THE STRAPS THE KIT TELLS YOU TO SOLDER.
    //
    // "For the 88-ACR, wire address select for 006. Wire BAUD Rate for 300 (max.),
    //  and wire UART options for 8 data bits, one stop bit, no parity bit."
    //
    // The 88-SIO's defaults are a CHOICE and its .md says so. THESE ARE A SOURCE, and
    // that is the whole difference. A test is how a sourced number stays sourced.
    // -----------------------------------------------------------------------
    {
        withTape("");
        Rig r;
        CHECK(r.acr, "the machine takes an 88-ACR");
        CHECK(r.acr->type() == "acr", "and it calls itself an acr");

        auto val = [&](const char* k) {
            for (Property& p : r.acr->properties())
                if (p.name == k) return p.get();
            return Value::ofStr("(no such property)");
        };
        CHECK(val("port").i() == 0x06, "port 006 -- the manual, not our taste");
        CHECK(val("baud").i() == 300, "300 baud");
        CHECK(val("data_bits").i() == 8, "8 data bits");
        CHECK(val("stop_bits").i() == 1, "1 stop bit");
        CHECK(val("parity").s() == "none", "no parity");

        // Rev 1 WITHOUT HAVING TO ASSUME IT: the ACR manual's own Bit Definition table
        // puts TBMT at bit 7 and DAV at bit 0 and marks bits 5 and 1 NOT USED, which
        // IS the post-errata status word. The card documents itself.
        CHECK(val("rev").s() == "1", "a Rev 1 SIO B, per the ACR manual's own bit table");

        // "If the 88-ACR is used with MITS software, interrupts are not used. Do not
        // make any connections to interrupt lines if using MITS software."
        CHECK(val("in_int").s() == "none" && val("out_int").s() == "none",
              "and the interrupt pads are bare, because the kit says leave them bare");

        // Two ports, and NOT a third.
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x06;
        CHECK(r.acr->decodes(c), "decodes 0x06 -- status");
        c.addr = 0x07;
        CHECK(r.acr->decodes(c), "decodes 0x07 -- data");
        c.addr = 0x08;
        CHECK(!r.acr->decodes(c), "and NOT 0x08 -- that is the disk's");
    }

    // -----------------------------------------------------------------------
    // 2. THE STATUS WORD IS THE 88-SIO'S, BIT FOR BIT -- INVERTED READY BITS AND ALL.
    //
    // THIS IS THE TEST THAT GUARDS THE INHERITANCE. The 88-ACR *is* an 88-SIO B: the
    // manual says so in its first sentence and then reprints the SIO's documentation
    // as the ACR's own assembly chapter. Both cards therefore have ONE status word,
    // written ONCE, in SioBoard.
    //
    // Fork that -- give the ACR its own copy "for clarity" -- and the two will drift
    // the first time somebody fixes a bug in one of them, and the machine will have
    // two different 88-SIO Bs in it. If this goes red, that is what happened.
    // -----------------------------------------------------------------------
    {
        withTape("");
        Rig r;

        // Idle, with no cassette in it: 0x63. THE SAME BYTE AS A REV 1 88-SIO --
        // asserted as a whole byte and not a mask, so the convention cannot drift a
        // bit at a time. Bit 0 SET = no data (inverted!); bit 7 CLEAR = ready to send.
        CHECK(in(*r.acr, 0x06) == 0x63, "idle status is 0x63 -- the Rev 1 88-SIO's byte");
    }
    {
        // And the trap, said out loud: CLEAR means READY.
        withTape("A");
        Rig r;
        r.mount("t.tap");
        CHECK((in(*r.acr, 0x06) & 0x01) == 0,
              "INVERTED: with a byte waiting, bit 0 reads ZERO");
        CHECK((in(*r.acr, 0x06) & 0x80) == 0, "and bit 7 CLEAR means ready to transmit");
    }

    // -----------------------------------------------------------------------
    // 3. PLAY. A tape reads back the bytes that were recorded on it, in order, and
    //    then it ends -- which is not an error, it is the end of the tape.
    // -----------------------------------------------------------------------
    {
        withTape("AB");
        Rig r;
        CHECK(r.mount("t.tap"), "a cassette goes in");

        uint8_t b = 0;
        CHECK(r.getByte(b) && b == 'A', "the first byte off the tape is the first byte on it");
        CHECK(r.getByte(b) && b == 'B', "then the second");
        CHECK(!r.getByte(b), "and then the tape runs out -- a quiet line, not an error");

        // The UART reads EAGERLY -- one byte, held in the receive register. So the
        // head sits one byte ahead of what the guest has taken, and that is not an
        // artifact: the byte HAS come off the tape and is in the chip.
        CHECK(r.acr->tape()->atEnd(), "the head is at the end of the tape");
    }

    // -----------------------------------------------------------------------
    // 4. 🔴 ONE HEAD, ONE POSITION -- AND THE READ-AHEAD MUST NOT EAT IT.
    //
    // THIS IS THE BUG THIS SUITE EXISTS FOR, and it was really in the board.
    //
    // A cassette has ONE head, so read and write share ONE position -- they must; it
    // is the same piece of tape. But the UART receives EAGERLY: it pulls a byte off
    // its line the moment it has room, because that is how DAV works. So a tape that
    // was readable and writable AT ONCE had its first byte pulled away by the card
    // before the guest ever ran, the position sat at 1, and the guest's recording
    // began at byte ONE. Every tape. Silently.
    //
    // A LOAD TEST WOULD NEVER HAVE FOUND IT: playback works perfectly while this is
    // broken. Only a recording is wrong, and only in its first byte.
    //
    // The fix is the hardware's own: a recorder is in PLAY or in RECORD, never both,
    // because the 88-ACR has NO MOTOR CONTROL and a human worked the buttons.
    // -----------------------------------------------------------------------
    {
        withTape("OLDTAPE!");
        Rig r;
        r.mount("t.tap");

        // PLAY is pressed: the card has already taken a byte, and the head shows it.
        CHECK(r.acr->tape()->pos() == 1, "playing: the head is one byte in, and says so");

        // Now the operator presses RECORD and winds back to the start -- which is what
        // you do, and what a real operator did.
        CHECK(r.press("record"), "RECORD goes down");
        std::string said;
        CHECK(r.rewind(said), "and the tape is wound back");
        CHECK(r.acr->tape()->pos() == 0, "the head is at the beginning");

        // In RECORD, NOTHING can advance the head by reading. That is the guarantee.
        CHECK((in(*r.acr, 0x06) & 0x01) == 1, "a recording deck plays nothing back: no DAV");
        for (int i = 0; i < 8; ++i) r.m.clock.advance(r.m.clock.tStatesPer(30));
        CHECK(r.acr->tape()->pos() == 0,
              "and the head has NOT MOVED -- the read-ahead cannot steal the write position");

        // Record two bytes. They must land at 0 and 1.
        out(*r.acr, 0x07, 'N');
        r.m.clock.advance(r.m.clock.tStatesPer(30));
        out(*r.acr, 0x07, 'E');
        r.m.clock.advance(r.m.clock.tStatesPer(30));

        CHECK(tapeBytes() == "NEDTAPE!",
              "the recording begins at byte ZERO -- off by one here corrupts every tape");
    }

    // -----------------------------------------------------------------------
    // 5. PLAY AND RECORD ARE EXCLUSIVE, and the write-protect tab is a SECOND and
    //    INDEPENDENT reason a tape can refuse.
    // -----------------------------------------------------------------------
    {
        withTape("KEEP");
        Rig r;
        r.mount("t.tap");

        // Playing: a write goes nowhere. The recorder is not recording.
        out(*r.acr, 0x07, 'X');
        r.m.clock.advance(r.m.clock.tStatesPer(30));
        CHECK(tapeBytes() == "KEEP", "a deck in PLAY records nothing, whatever the guest sends");
    }
    {
        withTape("KEEP", /*ro=*/true);  // the write-protect tab is out
        Rig r;
        r.mount("t.tap", /*ro=*/true);
        CHECK(r.press("record"), "RECORD goes down even on a protected tape -- the button moves");

        out(*r.acr, 0x07, 'X');
        r.m.clock.advance(r.m.clock.tStatesPer(30));
        CHECK(tapeBytes() == "KEEP", "...but the tab is out, so nothing is cut into it");
    }

    // -----------------------------------------------------------------------
    // 6. REWIND -- the verb, and the only executable proof Board::commands() works.
    //
    // A tape is the one medium with a POSITION you cannot seek, and REWIND is the one
    // thing an operator can do that the guest cannot. Pull the card and the verb goes
    // with it; that is tested in test_cli.cpp, from the monitor's side.
    // -----------------------------------------------------------------------
    {
        withTape("HI");
        Rig r;

        // The verbs are declared, REACHABLE, and the ones the .md promises: WIND and its
        // REWIND alias.
        auto cs = r.acr->commands();
        CHECK(cs.size() == 3, "the card brings three verbs");
        CHECK(std::string(cs[0].name) == "WIND" && std::string(cs[1].name) == "REWIND" &&
                  std::string(cs[2].name) == "EXTRACT",
              "WIND, REWIND (its wind-to-start alias), and EXTRACT");
        CHECK(cs[0].built && cs[0].waiting == nullptr && cs[1].built && cs[1].waiting == nullptr &&
                  cs[2].built && cs[2].waiting == nullptr,
              "a card that is IN THE MACHINE has no unbuilt verbs");

        // With no cassette in it, REWIND fails with a sentence rather than a crash.
        std::string said;
        CHECK(!r.rewind(said), "REWIND with no tape in the recorder is refused");
        CHECK(said.find("no cassette") != std::string::npos, ("...and it says so: " + said).c_str());

        r.mount("t.tap");
        uint8_t b = 0;
        CHECK(r.getByte(b) && b == 'H', "read the first byte");
        CHECK(r.getByte(b) && b == 'I', "and the second -- the tape is now at the end");
        CHECK(r.acr->tape()->atEnd(), "confirmed: at the end");

        CHECK(r.rewind(said), "REW");

        // THE HEAD IS BACK AT THE START. At full speed (the default -- host/tape.h) the
        // relined receiver latches byte 0 the instant the tape is back, so the head may
        // already read 1 rather than 0 -- an eager UART, not a failed rewind. What the
        // rewind guarantees is that it is at the START, not stuck at the END (which was 2).
        CHECK(r.acr->tape()->pos() <= 1, "the head is back at the beginning, not at the end");

        // 🔴 AND THE CARD IS NOT STILL HOLDING THE BYTE FROM THE TAPE IT WOUND PAST.
        //
        // The UART receives eagerly, so at the moment you rewind there is normally a
        // character sitting in its receive register. Leave the OLD one there and the
        // guest's next read gets that stale byte -- a byte DUPLICATED, by us, in the
        // middle of a program image. So the replay must begin at 'H', the wound-to byte,
        // and never the 'I' that was in flight when the operator hit rewind.
        CHECK(r.getByte(b) && b == 'H', "the tape replays from 'H'...");
        CHECK(r.getByte(b) && b == 'I', "...then 'I' -- no stale byte duplicated at the seam");
    }

    // -----------------------------------------------------------------------
    // 6b. WIND -- the counter, and moving the head to a time.
    //
    // A byte tape has no audio, so its time is the honest estimate from the 300-baud
    // strap: bytes x frame-bits / baud. This card is 8N1, so a frame is 10 bits and a
    // byte is a thirtieth of a second. 3000 bytes is therefore exactly 100 s = 01:40,
    // which is a tape long enough to read a real mm:ss off. (The WAV exact-timeline path
    // -- where the time is the recording's own -- is covered end to end in test_cli.)
    // -----------------------------------------------------------------------
    {
        // 3000 bytes, byte i = i & 0xFF, so a head position can be verified by content.
        std::string big;
        for (int i = 0; i < 3000; ++i) big += char(i & 0xFF);
        withTape(big);
        Rig r;

        std::string said;
        CHECK(!r.wind("0:30", said), "WIND with no tape in the recorder is refused");

        r.mount("t.tap");
        CHECK(r.acr->tape()->size() == 3000, "a 3000-byte tape -- 100 s at 300 baud");

        // The counter reads a real time and percent, and it is READ-ONLY.
        CHECK(r.prop("position") == "00:00 / 01:40 (0%)",
              ("at the top the counter is 00:00 of 01:40: " + r.prop("position")).c_str());
        bool posWritable = false;
        for (Property& p : r.acr->unitProperties("tape"))
            if (p.name == "position") posWritable = (bool)p.set;
        CHECK(!posWritable, "position is a measurement, not a switch -- no setter");

        // WIND to a time lands the head there (+/- the one byte an eager UART pulls).
        CHECK(r.wind("0:50", said), "WIND acr0:tape 0:50");
        CHECK(r.acr->tape()->pos() >= 1500 && r.acr->tape()->pos() <= 1501,
              ("0:50 is 1500 bytes in at 300 baud: " + std::to_string(r.acr->tape()->pos())).c_str());
        CHECK(r.prop("position") == "00:50 / 01:40 (50%)",
              ("halfway, by time: " + r.prop("position")).c_str());

        // END winds to the end; START back to the top.
        CHECK(r.wind("END", said), "WIND acr0:tape END");
        CHECK(r.acr->tape()->atEnd(), "END is the end of the tape");
        CHECK(r.wind("START", said), "WIND acr0:tape START");
        CHECK(r.acr->tape()->pos() <= 1, "START is the top (eager UART may sit at 1)");

        // A time past the end lands AT the end, visibly -- not an error.
        CHECK(r.wind("99:00", said), "WIND past the end is allowed");
        CHECK(r.acr->tape()->atEnd(), "...and clamps to the end");

        // Garbage is refused with a sentence, not silently taken as zero.
        CHECK(!r.wind("banana", said), "WIND to nonsense is refused");
        CHECK(said.find("not a position") != std::string::npos,
              ("...and says why: " + said).c_str());
    }

    // -----------------------------------------------------------------------
    // 6c. The live counter's switch, and the activityLabel() seam the run loop paints.
    //
    // activityLabel() is non-empty only when a tape is actually playing through the
    // middle of itself with the counter on -- which is the whole gate the run loop
    // leans on without knowing what a tape is.
    // -----------------------------------------------------------------------
    {
        std::string big(2000, 'x');
        withTape(big);
        Rig r;
        r.mount("t.tap");

        // Mounted and relined, the eager UART has pulled byte 0, so the head is one in --
        // playing, mid-tape, counter on by default: the run loop has a line to paint.
        CHECK(r.prop("counter") == "on", "the counter is on by default");
        CHECK(!r.acr->activityLabel().empty(),
              "a tape playing mid-way reports a live label");
        CHECK(r.acr->activityLabel().find("tape:") == 0,
              ("...and it is the tape counter: " + r.acr->activityLabel()).c_str());

        // Turn it off and the line goes quiet, though SHOW's position still answers.
        std::string err;
        CHECK(setUnitProperty(*r.acr, "tape", "counter", "off", err), "counter=off");
        CHECK(r.acr->activityLabel().empty(), "counter off: nothing to paint");
        CHECK(r.prop("position") != "<none>", "...but SHOW still reports the position");

        // Back on, and wound to the very end, there is again nothing to watch.
        CHECK(setUnitProperty(*r.acr, "tape", "counter", "on", err), "counter=on");
        std::string said;
        r.wind("END", said);
        CHECK(r.acr->activityLabel().empty(), "at the end there is nothing loading");
    }

    // -----------------------------------------------------------------------
    // 6d. The auto-stop mark -- playback halts at a time, and moving it resumes.
    //
    // The operator's STOP button at a counter mark: the tape stops handing bytes back at
    // the mark (a quiet line, like the physical end), so a multi-program tape can be cued
    // to halt at a boundary. It gates PLAYBACK only -- a recording writes through it. At
    // 300 baud with 10-bit frames a byte is a thirtieth of a second, so 0:05 is 150 bytes.
    // -----------------------------------------------------------------------
    {
        std::string big;
        for (int i = 0; i < 300; ++i) big += char(i & 0xFF);  // 300 bytes = 10 s
        withTape(big);
        Rig         r;
        std::string err;
        r.mount("t.tap");

        CHECK(setUnitProperty(*r.acr, "tape", "stop", "0:05", err), "SET stop=0:05");
        CHECK(r.prop("stop") == "00:05", ("the stop reads back as a time: " + r.prop("stop")).c_str());

        // Read until the line goes quiet -- it must halt AT the mark, not at the end.
        uint8_t b;
        int     got = 0;
        while (r.getByte(b)) ++got;
        CHECK(r.acr->tape()->pos() == 150,
              ("the head halted at the 150-byte mark: " + std::to_string(r.acr->tape()->pos())).c_str());
        CHECK(r.acr->tape()->atStop() && !r.acr->tape()->atEnd(),
              "parked at the stop, which is NOT the end of the tape");
        CHECK(got == 150, ("exactly the bytes before the mark came off: " + std::to_string(got)).c_str());

        // Move the stop on: more bytes come, up to the new mark.
        CHECK(setUnitProperty(*r.acr, "tape", "stop", "0:08", err), "SET stop=0:08 -- 240 bytes");
        while (r.getByte(b)) ++got;
        CHECK(r.acr->tape()->pos() == 240,
              ("now halted at 240: " + std::to_string(r.acr->tape()->pos())).c_str());

        // Clear it and the rest of the tape runs out at the physical end.
        CHECK(setUnitProperty(*r.acr, "tape", "stop", "off", err), "SET stop=off");
        CHECK(r.prop("stop") == "off", "the stop reads back off");
        while (r.getByte(b)) ++got;
        CHECK(r.acr->tape()->atEnd(), "cleared: the tape now runs to its physical end");
    }

    // -----------------------------------------------------------------------
    // 6e. A RECORDING WRITES STRAIGHT THROUGH THE STOP -- it gates playback only.
    // -----------------------------------------------------------------------
    {
        withTape("OLDTAPE!");
        Rig         r;
        std::string err;
        r.mount("t.tap");

        CHECK(r.press("record"), "RECORD goes down");
        std::string said;
        r.rewind(said);
        // A stop at the very start would freeze ALL playback -- but this is a recording.
        CHECK(setUnitProperty(*r.acr, "tape", "stop", "0:00", err), "arm a stop at byte 0");
        out(*r.acr, 0x07, 'N');
        r.m.clock.advance(r.m.clock.tStatesPer(30));
        out(*r.acr, 0x07, 'E');
        r.m.clock.advance(r.m.clock.tStatesPer(30));
        CHECK(tapeBytes() == "NEDTAPE!",
              "the write landed at byte zero -- the stop mark does not gate recording");
    }

    // -----------------------------------------------------------------------
    // 6f. EXTRACT -- split a mounted WAV into one .TAP per program, at the gaps.
    // -----------------------------------------------------------------------
    {
        // A two-program WAV: program A (40 bytes) then a ~4 s gap then program B (50 bytes).
        const TapeFormat f = tapeformats::fsk300_1850();
        const AudioBuffer a = modulate(std::vector<uint8_t>(40, 0x41), f, 22050, 2.0, 2.0);
        const AudioBuffer b = modulate(std::vector<uint8_t>(50, 0x42), f, 22050, 2.0, 2.0);
        AudioBuffer       both;
        both.rate = 22050;
        both.s    = a.s;
        both.s.insert(both.s.end(), b.s.begin(), b.s.end());
        const std::vector<uint8_t> wav = buildWav(both);

        withTape(std::string(wav.begin(), wav.end()));
        Rig r;
        r.mount("games.wav");

        namespace fs           = std::filesystem;
        const std::string base = (fs::temp_directory_path() / "altairsim-extract-test").string();
        const std::string f1 = base + "-1.tap", f2 = base + "-2.tap";
        fs::remove(f1);
        fs::remove(f2);

        std::ostringstream o;
        std::string        err;
        CHECK(r.acr->runCommand("EXTRACT", {"EXTRACT", "acr0:tape", base}, o, err),
              ("EXTRACT runs: " + err).c_str());
        CHECK(fs::exists(f1) && fs::file_size(f1) == 40, "program 1 was written, 40 bytes");
        CHECK(fs::exists(f2) && fs::file_size(f2) == 50, "program 2 was written, 50 bytes");
        CHECK(o.str().find("40 bytes") != std::string::npos &&
                  o.str().find("2 programs") != std::string::npos,
              ("the console names the files and their sizes: " + o.str()).c_str());
        fs::remove(f1);
        fs::remove(f2);
    }

    // -----------------------------------------------------------------------
    // 6g. EXTRACT refuses a byte tape -- there is nothing to demodulate.
    // -----------------------------------------------------------------------
    {
        withTape("RAWBYTES");
        Rig r;
        r.mount("raw.tap");
        std::ostringstream o;
        std::string        err;
        CHECK(!r.acr->runCommand("EXTRACT", {"EXTRACT", "acr0:tape"}, o, err),
              "EXTRACT on a byte tape is refused");
        CHECK(err.find("byte tape") != std::string::npos, ("...and says why: " + err).c_str());
    }

    // -----------------------------------------------------------------------
    // 7. THERE IS NO CONNECTOR ON THIS CARD, AND NO MOTOR EITHER.
    //
    // The UART's serial pins are soldered to the modem board ("XS" on the Modem to
    // "STSO" on the S I/O board), and the modem's audio to the recorder. A CONNECT
    // would advertise a socket where the hardware has a cassette -- so it is refused
    // WITH THE REASON, rather than silently inherited from the 88-SIO.
    // -----------------------------------------------------------------------
    {
        withTape("");
        Rig r;
        std::string err;
        CHECK(!r.acr->connect("tape", "socket:2400", err), "CONNECT is refused");
        CHECK(err.find("soldered") != std::string::npos, ("...and says why: " + err).c_str());

        // And the card offers no endpoint knob to reach around it with.
        bool hasConnect = false;
        for (Property& p : r.acr->properties()) hasConnect = hasConnect || p.name == "connect";
        CHECK(!hasConnect, "there is no `connect` property either");

        // THE TRANSFORM CHAIN IS GONE TOO, and that is not tidiness. A cassette carries
        // a BINARY image; a CRLF transform on that line does not annoy you, it
        // silently corrupts the program -- and corrupts it on the way ONTO the tape as
        // well as off. The modem passes bits and has never heard of a newline.
        bool hasFilter = false;
        for (Property& p : r.acr->properties())
            hasFilter = hasFilter || p.name == "crlf" || p.name == "upper";
        CHECK(!hasFilter, "and no character transforms -- a tape is binary, not text");
    }

    // -----------------------------------------------------------------------
    // 8. A RESET DOES NOT EJECT THE CASSETTE, AND IT DOES NOT REWIND IT.
    //
    // RESET* is a wire on the BACKPLANE. It does not reach into the recorder and press
    // a button, because nothing on this card can: there is no motor control. So the
    // tape stays in, and it stays where it is.
    //
    // WHAT THE RESET *DOES* DO is lose the byte in flight, and that is not a bug -- it
    // is the COM2502's MR pin, which this card really does drive (unlike the 2SIO's
    // 6850, which has no reset pin at all). MR clears the receive register. The
    // recorder, which has not heard about any of this, keeps rolling, and the next
    // byte off the tape lands in the register a character time later.
    //
    // So the head advances by exactly one. A byte was on its way into a chip that got
    // reset out from under it, and it is gone -- exactly as it would have been.
    // -----------------------------------------------------------------------
    {
        withTape("ABCD");
        Rig r;
        r.mount("t.tap");
        uint8_t b = 0;
        r.getByte(b);
        r.getByte(b);
        uint64_t was = r.acr->tape()->pos();
        CHECK(was > 0, "the tape has run on a bit");

        r.m.reset(Reset::Bus);
        CHECK(r.acr->tape() != nullptr, "a reset does not eject the cassette");
        CHECK(r.acr->tape()->pos() != 0, "...and it does NOT rewind it");
        CHECK(r.acr->tape()->pos() == was + 1,
              "the byte in flight is lost to the UART's MR pin, and the tape rolls on");
    }

    // -----------------------------------------------------------------------
    // 7. THE TAPE CARRIES ITS OWN CLOCK -- full speed vs `real`, at the stream itself.
    //
    // The board sections run the whole card; this pins the pacing, deterministically,
    // with an INJECTED wall clock so nothing sleeps and the result does not depend on
    // how fast this host happens to be. The point of the design: a tape that paces
    // itself is the sole authority on when a byte is ready, and the 1602 steps aside
    // for it (chips/uart1602.h) -- so the SAME UART delivers a whole tape at once, or
    // one byte per baud time, on the stream's say-so and not the emulated clock's.
    // -----------------------------------------------------------------------
    {
        SECTION("88-ACR -- the tape's own clock: full speed empties it, `real` paces it");

        auto image = [](const std::string& s) {
            return std::make_unique<TapeImage>(std::make_unique<MemoryMedia>(
                "t", std::vector<uint8_t>(s.begin(), s.end()), false));
        };
        // How many bytes a UART pulls off a stream WITHOUT the emulated clock moving --
        // the whole question is whether the baud gate belongs to the stream or the chip.
        auto drain = [](Uart1602& u, Clock& clk, int tries) {
            std::string got;
            for (int i = 0; i < tries; ++i) {
                u.poll(clk);
                if (u.dataAvailable()) got.push_back((char)u.readData());
            }
            return got;
        };

        // FULL SPEED (nsPerByte = 0): the clock never moves and the whole tape comes off.
        {
            Clock    clk;
            auto     tape = image("ABCD");
            Uart1602 u("acr");
            u.connect(std::make_unique<TapeStream>(*tape, TapeStream::Mode::Play));
            CHECK(drain(u, clk, 8) == "ABCD", "full speed: the tape empties with the clock at rest");
        }

        // REAL: a wall clock I hand it. The first byte is free (the receiver had none in
        // flight); every one after waits its baud time in that injected wall time, and
        // NOTHING in the emulated clock (which never moves here) can hurry it.
        {
            Clock          clk;
            uint64_t       nowNs = 1'000'000;      // an arbitrary, controllable 'now'
            const uint64_t step  = 9'000'000;      // ~one 1200-baud 8N2 byte, in ns
            auto           tape  = image("ABCD");
            Uart1602       u("acr");
            u.connect(std::make_unique<TapeStream>(*tape, TapeStream::Mode::Play, step,
                                                   [&] { return nowNs; }));

            CHECK(drain(u, clk, 8) == "A", "real: the free first byte, then nothing until wall time moves");
            nowNs += step;
            CHECK(drain(u, clk, 8) == "B", "real: one baud time buys exactly one byte");
            nowNs += step * 5;  // room for five -- but a paced tape never bursts
            CHECK(drain(u, clk, 8) == "C", "real: a long wait still yields ONE byte, not a burst");
            nowNs += step;
            CHECK(drain(u, clk, 8) == "D", "real: ...and the next baud time the next one");
        }

        // FOUR TIMES SLOWER IS NOT A NO-OP: a 300-baud interval still holds the second
        // byte across a wait a 1200-baud tape would have released it in.
        {
            Clock          clk;
            uint64_t       nowNs = 0;
            const uint64_t fast = 9'000'000, slow = fast * 4;
            auto           tf = image("AB");
            auto           ts = image("AB");
            Uart1602       uf("fast"), us("slow");
            uf.connect(std::make_unique<TapeStream>(*tf, TapeStream::Mode::Play, fast, [&] { return nowNs; }));
            us.connect(std::make_unique<TapeStream>(*ts, TapeStream::Mode::Play, slow, [&] { return nowNs; }));
            (void)drain(uf, clk, 4);  // take the free first byte off each
            (void)drain(us, clk, 4);
            nowNs += fast + 1;        // one 1200-baud interval
            CHECK(drain(uf, clk, 4) == "B", "1200 baud: the second byte has arrived");
            CHECK(drain(us, clk, 4) == "",  "300 baud: the same wait is not enough -- four times slower");
        }

        // A STALL DOES NOT DRIFT (issue #117). The run loop that drains the tape keeps
        // pausing -- to pace the CPU, to repaint the counter -- so it is a hair late to
        // each byte. That lateness must NOT accumulate: byte k stays pinned to k*step, not
        // k*step plus the sum of every prior delay. Read each byte a tenth of an interval
        // late; the old re-anchor-to-now slipped by that tenth every time and the third
        // byte was not ready when its true mark had already passed.
        {
            Clock          clk;
            uint64_t       nowNs = 0;
            const uint64_t step  = 9'000'000;   // one byte time
            const uint64_t late  = step / 10;   // we always look a little AFTER a byte is due
            auto           tape  = image("ABCDEFGHIJ");
            Uart1602       u("acr");
            u.connect(std::make_unique<TapeStream>(*tape, TapeStream::Mode::Play, step, [&] { return nowNs; }));

            CHECK(drain(u, clk, 4) == "A", "the free first byte");
            std::string rest;
            for (int k = 1; k <= 9; ++k) {
                nowNs = (uint64_t)k * step + late;  // byte k's true mark is k*step; we are `late` past it
                rest += drain(u, clk, 4);
            }
            CHECK(rest == "BCDEFGHIJ",
                  "absolute cadence: every byte on its own mark, the lateness never piling up");
        }
    }
}
