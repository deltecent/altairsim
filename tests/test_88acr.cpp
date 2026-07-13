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
#include "core/machine.h"
#include "host/media.h"

#include <cstdio>
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

        // The verb is declared, it is REACHABLE, and it is the one the .md promises.
        auto cs = r.acr->commands();
        CHECK(cs.size() == 1 && std::string(cs[0].name) == "REWIND",
              "the card brings exactly one verb, and it is REWIND");
        CHECK(cs[0].built && cs[0].waiting == nullptr,
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
        CHECK(r.acr->tape()->pos() == 0, "the head is back at the beginning");

        // 🔴 AND THE CARD IS NOT STILL HOLDING A BYTE FROM THE TAPE IT WOUND PAST.
        //
        // The UART receives eagerly, so at the moment you rewind there is normally a
        // character sitting in its receive register. Leave it there and the guest's
        // next read gets that stale byte, and then gets it AGAIN when the tape
        // replays it -- a byte DUPLICATED, by us, in the middle of a program image.
        CHECK(r.getByte(b) && b == 'H', "and the tape replays from 'H'...");
        CHECK(r.getByte(b) && b == 'I', "...then 'I' -- no stale byte duplicated at the seam");
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
}
