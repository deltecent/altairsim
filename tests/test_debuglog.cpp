#include "test.h"

#include "boards/mits-88mds.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/debuglog.h"
#include "core/machine.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>

using namespace altair;

namespace {

namespace fs = std::filesystem;

// The two flags every emit-site channel in this test carries. Bit index is the flag's
// position in the constructor list -- the same contract a real board's emit site uses.
enum { SECTOR = 0, SEEK = 1 };

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::in | std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Route the global sink to a scratch file, run `f` (which emits), then close the file
// by switching the sink back to stderr -- which flushes it -- and return its contents.
// This exercises the real File sink, not a test-only stream.
std::string capture(const std::function<void()>& f) {
    std::string err;
    const fs::path p = fs::temp_directory_path() / "altair_debuglog_test.log";
    std::error_code ec;
    fs::remove(p, ec);
    dbg::setSink(dbg::Sink::File, p.string(), err);
    f();
    dbg::setSink(dbg::Sink::Stderr, "", err);  // closes + flushes the file
    const std::string s = readFile(p);
    fs::remove(p, ec);
    return s;
}

} // namespace

void test_debuglog() {
    SECTION("a channel's flags map to bits, and enable/disable are additive/subtractive");

    std::string err;
    dbg::Channel ch("mds0", {"sector", "seek"});

    CHECK(ch.mask() == 0, "a fresh channel has nothing on");
    CHECK(!ch.on(SECTOR) && !ch.on(SEEK), "and on() reads false for every flag");

    CHECK(ch.enable("sector", err) && err.empty(), "DEBUG=sector succeeds");
    CHECK(ch.on(SECTOR) && !ch.on(SEEK), "only sector is on");

    CHECK(ch.enable("seek", err), "DEBUG=seek is additive");
    CHECK(ch.on(SECTOR) && ch.on(SEEK), "now both are on");

    CHECK(ch.disable("seek", err), "NODEBUG=seek removes just seek");
    CHECK(ch.on(SECTOR) && !ch.on(SEEK), "sector stays, seek gone");

    CHECK(ch.enable("all", err), "DEBUG=all turns everything on");
    CHECK(ch.on(SECTOR) && ch.on(SEEK), "all flags on");
    CHECK(ch.mask() == 0x3u, "and no bit beyond the two defined flags");

    CHECK(ch.enable("none", err), "DEBUG=none clears");
    CHECK(ch.mask() == 0, "nothing on after none");

    CHECK(ch.disable("all", err), "NODEBUG=all also clears");
    CHECK(ch.mask() == 0, "still nothing on");

    SECTION("case and whitespace are forgiven; an unknown flag errors atomically");

    CHECK(ch.enable("  SECTOR , Seek ", err), "spacing and case are folded");
    CHECK(ch.on(SECTOR) && ch.on(SEEK), "both parsed despite the noise");

    ch.enable("none", err);
    ch.enable("sector", err);
    err.clear();
    CHECK(!ch.enable("sector,bogus", err), "an unknown flag fails the whole call");
    CHECK(!err.empty(), "and reports which flag");
    CHECK(ch.mask() == (1u << SECTOR), "the mask is unchanged -- the call is atomic");

    SECTION("the registry finds a channel case-insensitively");

    CHECK(dbg::find("MDS0") == &ch, "find folds case");
    CHECK(dbg::find("mds0") == &ch, "and matches exactly too");
    CHECK(dbg::find("nosuch") == nullptr, "an absent channel is null, not a crash");
    bool listed = false;
    for (dbg::Channel* c : dbg::channels())
        if (c == &ch) listed = true;
    CHECK(listed, "channels() includes the registered channel");

    SECTION("a line is emitted only through the sink, gated by on()");

    ch.enable("none", err);
    ch.enable("sector", err);
    const std::string body = capture([&] {
        if (ch.on(SECTOR)) dbg::line(ch) << "read c=0 h=0 s=1\n";
        if (ch.on(SEEK))   dbg::line(ch) << "seek track=5\n";  // gated off -- must not appear
    });
    CHECK(body.find("read c=0 h=0 s=1") != std::string::npos, "the enabled flag's line landed");
    CHECK(body.find("seek track=5") == std::string::npos, "the disabled flag emitted nothing");
    CHECK(body.find("mds0: ") != std::string::npos, "the channel name prefixes the line");

    SECTION("line() prefixes the PC column from the provider, lazily");

    dbg::setPcProvider([] { return std::optional<uint16_t>(0x0A3F); });
    const std::string withPc = capture([&] { dbg::line(ch) << "x\n"; });
    CHECK(withPc.rfind("0A3F  mds0: x", 0) == 0, "a provided PC is four uppercase hex digits");

    dbg::setPcProvider([] { return std::optional<uint16_t>{}; });
    const std::string noPc = capture([&] { dbg::line(ch) << "x\n"; });
    CHECK(noPc.rfind("----  mds0: x", 0) == 0, "an empty provider yields the dash column");

    dbg::setPcProvider(nullptr);
    const std::string cleared = capture([&] { dbg::line(ch) << "x\n"; });
    CHECK(cleared.rfind("----  mds0: x", 0) == 0, "no provider also yields dashes");

    SECTION("the File sink reports an unopenable path and keeps the prior sink");

    err.clear();
    const std::string bad = (fs::temp_directory_path() / "no_such_dir_altairsim" / "d.log").string();
    CHECK(!dbg::setSink(dbg::Sink::File, bad, err), "SET CONSOLE DEBUG=<bad path> fails");
    CHECK(!err.empty(), "and returns a message");
    CHECK(dbg::sinkName() == "stderr", "the sink stayed at its default");

    SECTION("a board with debugFlags() gets a channel named by its id in the backplane");

    {
        // Clock BEFORE the board: a board holds a bare Clock* and the Windows sanitizer
        // catches the reverse order as a lifetime bug (see the memory note).
        Clock clk;
        Bus   bus;
        {
            // A distinct id -- the local channel `ch` above is also "mds0" and is still
            // in scope, so the framework's channel must be found under its own name.
            MdsBoard mds;
            mds.id = "mds1";
            mds.attachClock(&clk);

            CHECK(dbg::find("mds1") == nullptr, "no channel before the card is in a slot");
            bus.attach(&mds);

            dbg::Channel* c = dbg::find("mds1");
            CHECK(c != nullptr, "attaching the card created its channel, named by id");
            if (c) {
                CHECK(c->flags().size() == 2, "carrying both declared flags");
                CHECK(c->flags().size() == 2 && c->flags()[0] == "sector" &&
                          c->flags()[1] == "seek",
                      "in the bit order the card's emit sites assume");
            }

            bus.detach(&mds);
            CHECK(dbg::find("mds1") != nullptr,
                  "detach does NOT drop it -- a CONFIG LOAD re-attaches the same card");
        }
        CHECK(dbg::find("mds1") == nullptr, "but destroying the card unregisters it");
    }

    SECTION("the machine's PC provider reads instrPc and honours running");

    {
        // The SAME provider main() installs: while the machine runs, the current
        // instruction's PC (Bus::instrPc, published once per instruction by the run
        // loop); at the monitor prompt -- not running -- nothing, so the column dashes.
        Machine m;
        dbg::setPcProvider([&m]() -> std::optional<uint16_t> {
            if (!m.running) return std::nullopt;
            return m.bus.instrPc();
        });

        // At the prompt: running is false, so the provider yields nothing.
        const std::string atPrompt = capture([&] { dbg::line(ch) << "y\n"; });
        CHECK(atPrompt.rfind("----  mds0: y", 0) == 0,
              "stopped at the prompt, the PC column dashes");

        // Running, with a PC published exactly as the run loop publishes it.
        m.running = true;
        m.bus.setInstrPc(0x1234);
        const std::string running = capture([&] { dbg::line(ch) << "y\n"; });
        CHECK(running.rfind("1234  mds0: y", 0) == 0,
              "while running, the column is the published instruction PC");

        m.running = false;  // and it dashes again the moment the machine stops
        const std::string stopped = capture([&] { dbg::line(ch) << "y\n"; });
        CHECK(stopped.rfind("----  mds0: y", 0) == 0, "stopping returns the column to dashes");
    }

    // Leave the global facility as we found it, so other suites see a clean sink and
    // no leftover provider. The local channel unregisters itself as it goes out of scope.
    dbg::setSink(dbg::Sink::Stderr, "", err);
    dbg::setPcProvider(nullptr);
}
