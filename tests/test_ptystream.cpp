#include "test.h"

#include "chips/mc6850.h"
#include "cli/monitor.h"
#include "config/toml.h"
#include "core/clock.h"
#include "core/machine.h"
#include "host/endpoint.h"
#include "host/pty_stream.h"
#include "host/stream.h"
#include "platform/pty.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

// `pty` and `pty:LINK` -- a serial line on a pseudo-terminal (issue #685).
//
// The far end here is platform::openPtyPeer -- a program that opens the link, as
// `screen` does. No OS header and no platform test in this file: the sections that need
// a pseudo-terminal sit under platform::havePty(), and the other branch proves the
// refusal where there is none.

using namespace altair;

namespace {

// Poll `ready` for up to ~2 s of REAL time. Attach, delivery and hang-up are the
// kernel's to schedule, so a fixed spin can CHECK a state it never had time to reach.
template <typename Fn>
bool waitFor(Fn ready, int ms = 2000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

void put(ByteStream& s, const std::string& bytes) {
    s.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

// Everything the guest can read now.
std::string take(ByteStream& s) {
    std::string out;
    uint8_t     buf[512];
    for (;;) {
        size_t r = s.read(buf, sizeof buf);
        if (r == 0) break;
        out.append((const char*)buf, r);
    }
    return out;
}

// The program on the far end.
struct Program {
    std::unique_ptr<platform::PtyPeer> peer;
    explicit Program(const std::string& link) {
        std::string err;
        peer = platform::openPtyPeer(link, err);
    }
    bool ok() const { return peer != nullptr; }
    void type(const std::string& keys) { peer->write((const uint8_t*)keys.data(), keys.size()); }
    void drain(std::string& seen) {
        uint8_t buf[4096];
        for (;;) {
            size_t r = peer->read(buf, sizeof buf);
            if (r == 0) break;
            seen.append((const char*)buf, r);
        }
    }
};

// A link path of our own in the temp folder -- removed first, so a run that died does
// not decide this one.
std::string ptyLink(const char* leaf) {
    namespace fs = std::filesystem;
    fs::path        p = fs::temp_directory_path() / leaf;
    std::error_code ec;
    fs::remove(p, ec);
    return p.string();
}

bool isLink(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_symlink(p, ec);
}

bool pathExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::symlink_status(p, ec).type() != std::filesystem::file_type::not_found;
}

bool isCharDevice(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_character_file(p, ec);
}

std::unique_ptr<PtyStream> open(const std::string& spec, std::string& err) {
    auto s = resolveEndpoint(spec, err);
    auto* p = dynamic_cast<PtyStream*>(s.get());
    if (!p) return nullptr;
    s.release();
    return std::unique_ptr<PtyStream>(p);
}

// Pump until the line has noticed the program (or its leaving).
bool pumpUntil(PtyStream& p, bool attached) {
    return waitFor([&] {
        p.pump();
        return p.attached() == attached;
    });
}

constexpr uint8_t kTdre = 0x02;
constexpr uint8_t kDcd  = 0x04;
constexpr uint8_t k8n1  = 0x15;

} // namespace

void test_ptystream() {
    if (platform::havePty()) {
    SECTION("pty: `pty:LINK` makes a pseudo-terminal and a link to it");
    {
        const std::string link = ptyLink("altairsim-test-ep-make");
        std::string       err;
        auto              s = resolveEndpoint("pty:" + link, err);
        CHECK(s != nullptr, ("pty:LINK resolves " + err).c_str());
        if (s) {
            CHECK(dynamic_cast<PtyStream*>(s.get()) != nullptr, "and it is the line itself");
            CHECK(isLink(link), "the link is there, and it is a symbolic link");
            CHECK(isCharDevice(link), "and it points at a character device");
            CHECK(s->describe() == "pty:" + link,
                  "describe() round-trips the operator's text for SHOW / CONFIG SAVE");
            auto log = s->drainLog();
            CHECK(log.size() == 1 && has(log[0], "pty: " + link) && has(log[0], "/dev/"),
                  "the operator is told the link and the device behind it");
            CHECK(s->drainLog().empty(), "...and only once");
        }
        s.reset();
        CHECK(!pathExists(link), "the link is removed with the line");
    }

    SECTION("pty: a name that is given stays the same at every open");
    {
        const std::string link = ptyLink("altairsim-test-ep-same");
        std::string       err;
        for (int pass = 0; pass < 2; ++pass) {
            auto s = open("pty:" + link, err);
            CHECK(s != nullptr, "pty:LINK resolves");
            if (!s) continue;
            CHECK(has(s->note(), link + " ("), "the line is at the name that was given");
            Program p(link);
            CHECK(p.ok() && pumpUntil(*s, true), "and a program reaches the line by that name");
        }
        CHECK(!pathExists(link), "the link is removed each time");
    }

    SECTION("pty: bare `pty` takes the first free /tmp/altairsim{n}");
    {
        std::string err;
        auto        a = open("pty", err);
        auto        b = open("pty", err);
        CHECK(a && b, "two numbered lines resolve");
        if (a && b) {
            auto linkOf = [](const std::string& note) { return note.substr(0, note.find(' ')); };
            const std::string la = linkOf(a->note()), lb = linkOf(b->note());
            CHECK(la.rfind("/tmp/altairsim", 0) == 0 && lb.rfind("/tmp/altairsim", 0) == 0,
                  "each is /tmp/altairsim{n}");
            CHECK(la != lb, "and they are two different names");
            CHECK(isLink(la) && isLink(lb), "both links are there");
            CHECK(a->describe() == "pty" && b->describe() == "pty",
                  "describe() keeps the operator's text, not the number");
            a.reset();
            b.reset();
            CHECK(!pathExists(la) && !pathExists(lb), "both are removed with their lines");
        }
    }

    SECTION("pty: with nobody there the pins say so, the guest never waits, and nothing is kept");
    {
        const std::string link = ptyLink("altairsim-test-ep-drop");
        std::string       err;
        auto              s = open("pty:" + link, err);
        CHECK(s != nullptr, "the line resolves");
        if (s) {
            s->pump();
            LineStatus st = s->status();
            CHECK(!st.carrier && !st.dsr, "nobody has opened the link: NO CARRIER, no DSR");
            CHECK(st.cts, "CTS is up all the same: a line that nobody reads must not stop the guest");

            const std::string chunk(1000, 'x');
            size_t            took     = 0;
            bool              stalled  = false;
            for (int i = 0; i < 40; ++i) {  // 40 KB: far past the queue cap
                took += s->write((const uint8_t*)chunk.data(), chunk.size());
                s->pump();
                if (!s->writable() || !s->status().cts) stalled = true;
            }
            CHECK(took == 40u * 1000u, "every write is taken in full");
            CHECK(!stalled, "and the line never once said wait");

            Program p(link);
            CHECK(p.ok() && pumpUntil(*s, true), "a program opens the link");
            put(*s, "HELLO");
            std::string seen;
            CHECK(waitFor([&] { s->pump(); p.drain(seen); return seen.size() >= 5; }),
                  "it sees what the guest prints now");
            CHECK(seen == "HELLO", "and none of what was printed before it was there");
        }
    }

    SECTION("pty: a program opening the link is carrier; closing it is carrier lost");
    {
        const std::string link = ptyLink("altairsim-test-ep-dcd");
        std::string       err;
        auto              s = open("pty:" + link, err);
        CHECK(s != nullptr, "the line resolves");
        if (s) {
            {
                Program p(link);
                CHECK(p.ok() && pumpUntil(*s, true), "a program opens the link");
                LineStatus st = s->status();
                CHECK(st.carrier && st.dsr && st.cts, "DCD and DSR ROSE, and CTS is up");
                CHECK(!st.ring, "there is no ring on a cable");
                p.type("LAST");
                CHECK(waitFor([&] { s->pump(); return s->readable(); }), "it types");
            }
            CHECK(pumpUntil(*s, false), "the program closes the link");
            LineStatus st = s->status();
            CHECK(!st.carrier && !st.dsr, "DCD and DSR DROPPED");
            CHECK(take(*s) == "LAST", "what it typed before it left is still the guest's to read");

            Program again(link);
            CHECK(again.ok() && pumpUntil(*s, true) && s->status().carrier,
                  "the next program is carrier again: the line outlives a caller");
        }
    }

    SECTION("pty: every byte value crosses unchanged, both ways, with no echo");
    {
        const std::string link = ptyLink("altairsim-test-ep-raw");
        std::string       err;
        auto              s = open("pty:" + link, err);
        CHECK(s != nullptr, "the line resolves");
        if (s) {
            Program p(link);
            CHECK(p.ok() && pumpUntil(*s, true), "the program is on the line");
            CHECK(!p.peer->echoes(), "the line is raw before a byte is sent: no echo");

            std::string all;
            for (int i = 0; i < 256; ++i) all.push_back((char)i);

            put(*s, all);
            std::string seen;
            CHECK(waitFor([&] { s->pump(); p.drain(seen); return seen.size() >= 256; }),
                  "all 256 reach the program");
            CHECK(seen == all, "byte for byte: no CR added to LF, nothing eaten as a control key");
            s->pump();
            CHECK(!s->readable(), "and none of it came back to the guest as typed keys");

            p.type(all);
            std::string got;
            CHECK(waitFor([&] { s->pump(); got += take(*s); return got.size() >= 256; }),
                  "all 256 reach the guest");
            CHECK(got == all, "byte for byte: 00, 03, 7F and FF are data, as XMODEM needs");
        }
    }

    SECTION("pty: a program that is there and does not read makes the guest WAIT, not lose");
    {
        const std::string link = ptyLink("altairsim-test-ep-slow");
        std::string       err;
        auto              s = open("pty:" + link, err);
        CHECK(s != nullptr, "the line resolves");
        if (s) {
            Program p(link);
            CHECK(p.ok() && pumpUntil(*s, true), "the program is on the line");

            // A guest sends only while the line says it may -- that is what TDRE is.
            std::string sent;
            for (int i = 0; i < 200000 && s->writable(); ++i) {
                char c = (char)('A' + i % 26);
                s->write((const uint8_t*)&c, 1);
                sent.push_back(c);
            }
            CHECK(!s->writable(), "the kernel buffer and the queue fill: the line says WAIT");
            CHECK(!s->status().cts, "and that is CTS dropping");
            CHECK(s->status().carrier, "the carrier is still up: slow is not gone");

            std::string seen;
            CHECK(waitFor([&] { s->pump(); p.drain(seen); return seen.size() >= sent.size(); }),
                  "the program reads at last");
            CHECK(seen == sent, "and every byte is there, in order: nothing was dropped");
            CHECK(s->writable() && s->status().cts, "the line says go again");
        }
    }

    SECTION("pty: a leftover link is replaced; anything else at LINK is refused");
    {
        const std::string link = ptyLink("altairsim-test-ep-left");
        std::error_code   ec;
        std::filesystem::create_symlink("/dev/altairsim-no-such-device", link, ec);
        CHECK(!ec && isLink(link), "a link from a run that did not clean up");
        std::string err;
        auto        s = resolveEndpoint("pty:" + link, err);
        CHECK(s != nullptr, "the leftover link is replaced");
        CHECK(isCharDevice(link), "and now points at ours");
        s.reset();

        { std::ofstream f(link); f << "mine"; }
        err.clear();
        CHECK(resolveEndpoint("pty:" + link, err) == nullptr, "an ordinary file at LINK is refused");
        CHECK(has(err, "not a link"), "and the error says why");
        std::ifstream f(link);
        std::string   body;
        f >> body;
        CHECK(body == "mine", "the file is untouched");
        f.close();
        std::filesystem::remove(link, ec);
    }

    SECTION("pty on a 6850: wired CTS does not stop a guest that nobody reads");
    {
        const std::string link = ptyLink("altairsim-test-ep-6850");
        std::string       err;
        auto              s    = open("pty:" + link, err);
        PtyStream*        line = s.get();
        CHECK(line != nullptr, "the line resolves");
        if (line) {
            Clock  clk;
            Mc6850 u{"a"};
            u.dcdStrap = PinStrap::Wired;
            u.ctsStrap = PinStrap::Wired;
            u.connect(std::move(s));
            u.powerOn(clk);
            u.writeControl(0x03, clk);  // master reset first, as a real program must (#938)
            u.writeControl(k8n1, clk);

            // The guest's transmit loop: wait for TDRE, send, 20000 times -- more than
            // the queue cap, so a line that kept the bytes would run out of room.
            int sent = 0;
            for (int i = 0; i < 200000 && sent < 20000; ++i) {
                clk.advance(100000);
                line->pump();
                u.poll(clk);
                if (u.readStatus(clk) & kTdre) {
                    u.writeData((uint8_t)('0' + sent % 10), clk);
                    ++sent;
                }
            }
            CHECK(sent == 20000, "the transmit loop finishes with nobody on the line");
            CHECK(!u.carrier(), "with DCD wired, the chip sees no carrier");

            {
                Program p(link);
                CHECK(p.ok() && waitFor([&] { line->pump(); u.poll(clk); return u.carrier(); }),
                      "a program opens the link: the chip sees CARRIER");
            }
            CHECK(waitFor([&] { line->pump(); u.poll(clk); return !u.carrier(); }),
                  "it closes the link: the chip sees the carrier go");
            CHECK((u.readStatus(clk) & kDcd) != 0, "and the status register reports the loss");
        }
    }

    SECTION("pty on a 2SIO: CONNECT says the name; CONFIG SAVE keeps it and it loads back");
    {
        const std::string link = ptyLink("altairsim-test-ep-save");
        Machine           m;
        Monitor           mon(m);
        auto ex = [&](const std::string& line) {
            std::ostringstream o;
            mon.exec(line.c_str(), o);
            return o.str();
        };
        ex("BOARDS ADD 2sio sio0");
        const std::string said = ex("CONNECT sio0:a pty:" + link);
        CHECK(has(said, "sio0:a: connected to pty:" + link), "CONNECT takes pty:LINK");
        CHECK(has(said, "\npty: " + link + " (/dev/"), "and prints where the line is, at once");
        CHECK(isLink(link), "the link is there");

        const std::string text = saveTomlText(m);
        CHECK(has(text, "pty:" + link), "CONFIG SAVE writes the name that was given");

        Machine     back;
        std::string lerr;
        CHECK(loadTomlText(text, "pty (saved)", back, lerr),
              ("the saved machine loads back: " + lerr).c_str());
        // A machine file has no CONNECT to print the name, so the machine says it.
        bool told = false;
        for (const auto& line : back.drainBoardLog())
            if (has(line, "sio0:a: pty: " + link + " (/dev/")) told = true;
        CHECK(told, "a machine that is LOADED says where the line is, with the unit's name");
        CHECK(back.drainBoardLog().empty(), "...once");
        Monitor            mon2(back);
        std::ostringstream o;
        mon2.exec("SHOW sio0", o);
        CHECK(has(o.str(), "pty:" + link), "and its line is on the same name");

        const std::string gone = ex("DISCONNECT sio0:a");
        (void)gone;
    }
    } else {
    SECTION("pty: refused where there is no pseudo-terminal, with what to use");
    {
        std::string err;
        CHECK(resolveEndpoint("pty", err) == nullptr, "pty is refused");
        CHECK(has(err, "not available on Windows") && has(err, "socket:PORT"),
              "and the error names the endpoint that works");
        err.clear();
        CHECK(resolveEndpoint("pty:con", err) == nullptr, "pty:LINK is refused the same way");
        CHECK(has(err, "not available on Windows"), "by name, not as an unknown endpoint");
        CHECK(!has(endpointHelp(), "pty"), "and the help does not offer it");
    }
    }

    SECTION("pty: the grammar -- what is refused, what rebases, what the help lists");
    {
        std::string err;
        CHECK(resolveEndpoint("pty:", err) == nullptr, "`pty:` with no link is refused");
        CHECK(has(err, "link"), "and the error asks for the link");
        err.clear();
        CHECK(resolveEndpoint("pty?ro", err) == nullptr, "an option is refused: there are none");
        CHECK(has(err, "no options"), "and the error says so");
        err.clear();
        CHECK(resolveEndpoint("ptyx", err) == nullptr && has(err, "no endpoint 'ptyx'"),
              "a word that only starts with pty is not this endpoint");

        auto rebase = [](const std::string& p) { return "/cfg/" + p; };
        CHECK(rebaseEndpointPaths("pty", rebase) == "pty", "a bare pty is not a path");
        CHECK(rebaseEndpointPaths("pty:con", rebase) == "pty:/cfg/con",
              "the link after pty: is a path, relative to the machine file");
        CHECK(rebaseEndpointPaths("pty:con|cap.hex", rebase) == "pty:/cfg/con|/cfg/cap.hex",
              "and it still rebases under a tap");
        CHECK(has(endpointHelp(true), "serial:DEVICE | pty[:LINK]"),
              "the full grammar lists it with the other lines");
    }
}
