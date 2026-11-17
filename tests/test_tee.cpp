#include "test.h"

#include "host/endpoint.h"
#include "host/stream.h"
#include "host/tee_stream.h"
#include "platform/socket.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

// A fake wall clock, exactly the printer test's: host time only moves when the test
// moves it, so every timestamp in the log is deterministic and asserted.
struct FakeClock {
    uint64_t                  ns = 0;
    std::function<uint64_t()> fn() {
        return [this] { return ns; };
    }
    void advanceMs(double ms) { ns += (uint64_t)(ms * 1'000'000.0); }
};

// A temp path that cleans itself up -- the capture file for one section.
struct TmpFile {
    std::filesystem::path path;
    explicit TmpFile(const char* tag)
        : path(std::filesystem::temp_directory_path() / (std::string("altair_tee_") + tag)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    ~TmpFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

void put(ByteStream& s, const std::string& bytes) {
    s.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

// Read up to n bytes out of a tee (line -> guest), returning what arrived.
std::string get(ByteStream& s, size_t n) {
    std::vector<uint8_t> buf(n);
    size_t               got = s.read(buf.data(), n);
    return std::string(buf.begin(), buf.begin() + got);
}

// Build a tee over a fresh ScriptedStream, returning both so a section can drive the
// inner (feed RX bytes) and the tee (write TX bytes). Wall base is pinned so the header
// date is stable regardless of the host zone (we assert only body lines anyway).
TeeStream makeTee(ScriptedStream*& innerOut, const std::string& path, TeeStream::Params p,
                  FakeClock& clk) {
    auto            inner = std::make_unique<ScriptedStream>();
    innerOut              = inner.get();
    std::ofstream   log(path, std::ios::out | std::ios::trunc);
    return TeeStream(std::move(inner), "cap?x", p, std::move(log), clk.fn(), (std::time_t)1);
}

} // namespace

void test_tee() {
    SECTION("tee: dump format logs TX then RX with hex + ascii, in order");
    {
        TmpFile           tf("dump.hex");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;  // defaults: dump, width 16, elapsed
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            put(tee, "ATZ\r");            // guest -> line
            sc->feed("OK");               // stage the far end's reply
            CHECK(tee.rxPending() == 2, "the tap reports the backlog of the line it wraps (#934)");
            CHECK(get(tee, 16) == "OK", "the tap forwards the reply byte-for-byte");
        }  // destructor flushes the pending RX row and closes the file
        std::string log = slurp(tf.str());
        CHECK(has(log, "# altairsim capture"), "a header banner names the capture");
        CHECK(has(log, "TX 0000  41 54 5A 0D"), "the TX row is the guest's bytes in hex");
        CHECK(has(log, "ATZ."), "with a printable-ascii gutter (CR shows as '.')");
        CHECK(has(log, "RX 0000  4F 4B"), "the RX row is the reply's bytes");
        CHECK(has(log, "+0.000000"), "and an elapsed timestamp column");
        CHECK(log.find("TX 0000") < log.find("RX 0000"), "TX is logged before RX");
    }

    SECTION("tee: a full row flushes at `width` and the byte offset advances");
    {
        TmpFile           tf("width.hex");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;
        p.width = 4;  // small, so 6 bytes span two rows
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            put(tee, "ABCDEF");
        }
        std::string log = slurp(tf.str());
        CHECK(has(log, "TX 0000  41 42 43 44"), "the first four bytes are row 0000");
        CHECK(has(log, "TX 0004  45 46"), "and the remainder is row 0004 (offset advanced)");
    }

    SECTION("tee: a direction change ends the current row even mid-width");
    {
        TmpFile           tf("dir.hex");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            put(tee, "AB");     // 2 bytes, row not full
            sc->feed("Z");
            (void)get(tee, 4);  // an RX byte forces the TX row out first
        }
        std::string log = slurp(tf.str());
        CHECK(has(log, "TX 0000  41 42 "), "the short TX row was flushed by the direction change");
        CHECK(has(log, "RX 0000  5A"), "and the RX row follows it");
    }

    SECTION("tee: an idle gap flushes a burst without a direction change (pump)");
    {
        TmpFile           tf("gap.hex");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;
        p.gapNs = 200'000'000ull;  // 200 ms
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            put(tee, "HI");
            tee.pump();
            tee.flush();
            CHECK(!has(slurp(tf.str()), "TX 0000  48 49"), "nothing flushes before the gap elapses");
            clk.advanceMs(250);
            tee.pump();
            tee.flush();
            CHECK(has(slurp(tf.str()), "TX 0000  48 49"), "the idle gap flushed the partial row");
        }
    }

    SECTION("tee: elapsed timestamps are relative to the first event");
    {
        TmpFile           tf("ts.hex");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;
        p.width = 2;
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            clk.advanceMs(1000);  // first event is at host t=1s -> trace starts at +0
            put(tee, "AB");
            clk.advanceMs(500);   // 0.5 s later
            sc->feed("CD");
            (void)get(tee, 2);
        }
        std::string log = slurp(tf.str());
        CHECK(has(log, "+0.000000"), "the first row is at +0, not the host's absolute time");
        CHECK(has(log, "+0.500000"), "and the reply is stamped +0.5 s after it");
        CHECK(log.find("+0.000000") < log.find("+0.500000"), "in that order");
        CHECK(log.find("+0.500000") < log.find("43 44"), "the +0.5 s stamp is on the RX row");
    }

    SECTION("tee: pin edges are logged and are 8-bit-clean forwards");
    {
        TmpFile         tf("pins.hex");
        FakeClock       clk;
        // A loopback inner reflects the card's control lines back as status, so a DTR
        // rise becomes a DCD rise -- the one endpoint that tests modem pins with no hw.
        auto            inner = std::make_unique<LoopbackStream>();
        LoopbackStream* lb    = inner.get();
        (void)lb;
        std::ofstream     log(tf.str(), std::ios::out | std::ios::trunc);
        TeeStream::Params p;
        {
            TeeStream tee(std::move(inner), "cap", p, std::move(log), clk.fn(), (std::time_t)1);
            (void)tee.status();                         // baseline: carrier down
            tee.setControl(LineControl{true, true, false});  // rts + dtr rise
            (void)tee.status();                         // loopback now reflects carrier up
        }
        std::string logtxt = slurp(tf.str());
        CHECK(has(logtxt, "[RTS^]"), "an RTS rise is logged");
        CHECK(has(logtxt, "[DTR^]"), "a DTR rise is logged");
        CHECK(has(logtxt, "[DCD^]"), "and the reflected carrier rise is logged");
    }

    SECTION("tee: jsonl emits one record per transfer plus a meta header");
    {
        TmpFile           tf("j.jsonl");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;
        p.fmt = TeeStream::Fmt::Jsonl;
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            put(tee, "AB");
            sc->feed("CD");
            (void)get(tee, 2);
        }
        std::string log = slurp(tf.str());
        CHECK(has(log, "\"fmt\":\"jsonl\""), "a JSON meta record heads the file");
        CHECK(has(log, "{\"ns\":0,\"dir\":\"tx\",\"hex\":\"4142\",\"txt\":\"AB\"}"),
              "the TX transfer is one record");
        CHECK(has(log, "\"dir\":\"rx\",\"hex\":\"4344\",\"txt\":\"CD\""),
              "and the RX transfer is another");
    }

    SECTION("tee: cols format lays TX left and RX right under a legend");
    {
        TmpFile           tf("c.hex");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;
        p.fmt = TeeStream::Fmt::Cols;
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            put(tee, "AB");
            sc->feed("CD");
            (void)get(tee, 2);
        }
        std::string log = slurp(tf.str());
        CHECK(has(log, "TX (guest -> line)"), "a two-column legend heads the capture");
        CHECK(has(log, "RX (line -> guest)"), "naming both directions");
        CHECK(has(log, "41 42"), "the TX bytes appear");
        CHECK(has(log, "43 44"), "and the RX bytes appear");
    }

    SECTION("tee: passthrough is byte-identical, 8-bit clean over 0x00..0xFF");
    {
        TmpFile           tf("clean.hex");
        FakeClock         clk;
        ScriptedStream*   sc = nullptr;
        TeeStream::Params p;
        std::string       all;
        for (int i = 0; i < 256; ++i) all += (char)i;
        {
            TeeStream tee = makeTee(sc, tf.str(), p, clk);
            put(tee, all);
            CHECK(sc->out() == all, "every byte written reaches the inner unchanged");
            sc->feed(all);
            std::string back;
            while (back.size() < all.size()) {
                std::string chunk = get(tee, 64);
                if (chunk.empty()) break;
                back += chunk;
            }
            CHECK(back == all, "and every byte read back is identical, including 0x00 and high bits");
        }
    }

    // ---- grammar, through the REAL resolver an operator's CONNECT uses ----

    SECTION("tee: describe() round-trips the whole tap for SHOW / CONFIG SAVE");
    {
        TmpFile     tf("rt.hex");
        std::string spec = "loopback|" + tf.str() + "?fmt=cols&gap=300";
        std::string err;
        auto        s = resolveEndpoint(spec, err);
        CHECK(s != nullptr, "loopback|FILE?opts resolves");
        if (s) CHECK(s->describe() == spec, "and describe() echoes it verbatim");
    }

    SECTION("tee: an empty endpoint or an empty file is refused");
    {
        std::string err;
        CHECK(resolveEndpoint("|cap.hex", err) == nullptr, "|FILE with no endpoint is an error");
        CHECK(has(err, "endpoint"), "and the error says so");
        err.clear();
        CHECK(resolveEndpoint("loopback|", err) == nullptr, "ENDPOINT| with no file is an error");
        CHECK(has(err, "log file"), "and the error says so");
    }

    SECTION("tee: an unknown format or option is refused, naming the real ones");
    {
        TmpFile     tf("bad.hex");
        std::string err;
        CHECK(resolveEndpoint("loopback|" + tf.str() + "?fmt=bogus", err) == nullptr,
              "a bad fmt does not silently resolve");
        CHECK(has(err, "dump") && has(err, "cols") && has(err, "jsonl"), "the error names the formats");
        err.clear();
        CHECK(resolveEndpoint("loopback|" + tf.str() + "?nope=1", err) == nullptr,
              "an unknown option is refused");
        CHECK(has(err, "fmt") && has(err, "width") && has(err, "pins"), "and the error lists the options");
    }

    // ---- fmt=text: a transcript, not a trace ----

    SECTION("tee: fmt=text holds exactly what the guest wrote -- no header, no hex, no RX");
    {
        TmpFile     tf("text.log");
        std::string err;
        auto        s = resolveEndpoint("scripted|" + tf.str() + "?fmt=text", err);
        CHECK(s != nullptr, "scripted|FILE?fmt=text resolves");
        if (s) {
            CHECK(slurp(tf.str()).empty(), "a transcript starts empty: no header line");
            put(*s, "A>DIR\r\n");
            CHECK(slurp(tf.str()) == "A>DIR\r\n",
                  "the bytes are on disk while the line is still open (tail -f is live)");

            // What the guest READS is not in a transcript: the guest echoes it, or it does not.
            auto* tee = dynamic_cast<TeeStream*>(s.get());
            CHECK(tee != nullptr, "the tap is a TeeStream");
            if (tee) {
                auto* inner = dynamic_cast<ScriptedStream*>(tee->inner());
                CHECK(inner != nullptr, "inner() reaches the wrapped scripted line");
                if (inner) {
                    inner->feed("secret\r");
                    CHECK(get(*s, 16) == "secret\r", "the guest still reads its input");
                    CHECK(inner->out() == "A>DIR\r\n", "and the wrapped line got the output");
                }
            }
            (void)s->status();  // a pin poll writes nothing into a transcript
            s->pump();
            put(*s, "ok");
            CHECK(slurp(tf.str()) == "A>DIR\r\nok", "only guest output, in order, nothing else");
        }
        s.reset();  // close before the file is removed (Windows)
    }

    SECTION("tee: fmt=text is 8-bit clean -- every byte value lands unchanged");
    {
        TmpFile     tf("bin.log");
        std::string err, all;
        for (int i = 0; i < 256; ++i) all += (char)i;
        auto s = resolveEndpoint("null|" + tf.str() + "?fmt=text", err);
        CHECK(s != nullptr, "null|FILE?fmt=text resolves");
        if (s) {
            put(*s, all);
            CHECK(slurp(tf.str()) == all,
                  "0x00..0xFF byte for byte: LF is not made CR LF, 0x1A does not end the file");
        }
        s.reset();
    }

    SECTION("tee: append adds to the log; without it a capture starts over");
    {
        TmpFile     tf("app.log");
        std::string err;
        {
            auto s = resolveEndpoint("null|" + tf.str() + "?fmt=text", err);
            if (s) put(*s, "one ");
        }
        {
            std::string spec = "null|" + tf.str() + "?fmt=text&append";
            auto        s    = resolveEndpoint(spec, err);
            CHECK(s != nullptr, "the append option parses");
            if (s) {
                put(*s, "two");
                CHECK(s->describe() == spec, "and describe() round-trips it");
            }
        }
        CHECK(slurp(tf.str()) == "one two", "append kept what the file held");
        {
            auto s = resolveEndpoint("null|" + tf.str() + "?fmt=text", err);
            if (s) put(*s, "three");
        }
        CHECK(slurp(tf.str()) == "three", "the default truncates");

        err.clear();
        CHECK(resolveEndpoint("null|" + tf.str() + "?append=maybe", err) == nullptr,
              "a bad append value is refused");
        CHECK(has(err, "append"), "and the error names the option");
    }

    SECTION("tee: fmt=text refuses the hex-layout options instead of ignoring them");
    {
        TmpFile tf("opts.log");
        for (const char* opt : {"width=8", "gap=100", "ts=wall", "pins=off"}) {
            std::string err;
            CHECK(resolveEndpoint("null|" + tf.str() + "?fmt=text&" + opt, err) == nullptr,
                  "a layout option with fmt=text is refused");
            CHECK(has(err, "fmt=text"), "and the error says why");
            err.clear();
            // Order must not matter: the option before fmt is refused as well.
            CHECK(resolveEndpoint("null|" + tf.str() + "?" + opt + "&fmt=text", err) == nullptr,
                  "whichever side of fmt it is on");
        }
        std::string err;
        CHECK(resolveEndpoint("null|" + tf.str() + "?fmt=bogus", err) == nullptr, "a bad fmt");
        CHECK(has(err, "text"), "the fmt error names text among the formats");
    }

    SECTION("tee: a tap goes on a mirror -- the spec splits on the LAST bar");
    {
        TmpFile     tf("chain.log");
        std::string err;
        // A free port, found the way the mirror suite finds one: bind 0, read it back.
        uint16_t port = 0;
        {
            std::string e;
            if (auto probe = platform::listenTcp(0, e)) port = probe->port();
        }
        CHECK(port != 0, "a free port for the mirror");
        std::string spec =
            "scripted|socket:" + std::to_string(port) + "|" + tf.str() + "?fmt=text";
        auto s = resolveEndpoint(spec, err);
        CHECK(s != nullptr, "scripted|socket:PORT|FILE?fmt=text resolves");
        if (s) {
            CHECK(s->describe() == spec, "describe() reads back in the same order");
            auto* tee = dynamic_cast<TeeStream*>(s.get());
            CHECK(tee != nullptr, "the LAST sink, the file, is the outermost decorator");
            put(*s, "hello");
            CHECK(slurp(tf.str()) == "hello", "the tap logs what passes to the mirror");
            if (tee) {
                CHECK(tee->inner()->describe() == "scripted|socket:" + std::to_string(port),
                      "and what it wraps is the mirror on the scripted line");
            }
        }
        s.reset();

        auto rebase = [](const std::string& p) { return "/cfg/" + p; };
        CHECK(rebaseEndpointPaths("in:t.tap|socket:2323|s.log?fmt=text", rebase) ==
                  "in:/cfg/t.tap|socket:2323|/cfg/s.log?fmt=text",
              "rebase: the inner path and the log path move, the socket sink does not");
    }

    SECTION("tee: an unopenable log path is a failed CONNECT, not a half-built tap");
    {
        std::string err;
        auto s = resolveEndpoint("loopback|/no/such/dir/altair_tee_nope.hex", err);
        CHECK(s == nullptr, "a log file that cannot be opened refuses cleanly");
        CHECK(has(err, "capture"), "and the error mentions the capture");
    }

    SECTION("tee: rebaseEndpointPaths rewrites both the inner path and the log path");
    {
        auto rebase = [](const std::string& p) { return "/cfg/" + p; };
        CHECK(rebaseEndpointPaths("in:tape.tap|../logs/cap.hex", rebase) ==
                  "in:/cfg/tape.tap|/cfg/../logs/cap.hex",
              "a machine-file relative tap rebases inner and file against the config dir");
        CHECK(rebaseEndpointPaths("socket:2323|cap.hex?fmt=cols", rebase) ==
                  "socket:2323|/cfg/cap.hex?fmt=cols",
              "a non-file inner is untouched, the log path is rebased, options preserved");
    }
}
