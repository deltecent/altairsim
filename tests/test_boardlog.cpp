#include "test.h"

#include "boards/mits-88c700.h"
#include "core/board.h"
#include "core/machine.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <memory>
#include <string>
#include <vector>

using namespace altair;

// THE FAR END OF A LINE IS THE MACHINE'S TO DRAIN (issue #893). What a unit's stream has to
// say -- a print job that could not be spooled, the name of a `pty:` link -- is drained by
// Machine::drainBoardLog() for every board, and printed as `id:unit: message`. A board's own
// drainLog() never asks the stream. Before #893 the Board default and three overrides drained
// it first, each with its own prefix, so the same machine printed `io40:pa pty: ...` and
// `io40:b: pty: ...`.

namespace {

// A line whose far end has something to say.
class FarEndStream : public ScriptedStream {
public:
    std::vector<std::string> farEnd;
    std::vector<std::string> drainLog() override {
        auto out = std::move(farEnd);
        farEnd.clear();
        return out;
    }
};

// Connect a FarEndStream to `unit` on `b`, through the board's real connectStream(). Null
// if the board refused it: the stream is gone then, and nothing may write to it.
FarEndStream* plug(Board& b, const std::string& unit) {
    auto        s   = std::make_unique<FarEndStream>();
    auto*       raw = s.get();
    std::string err;
    bool        ok  = b.connectStream(unit, std::move(s), err);
    CHECK(ok, ("a line on " + b.id + ":" + unit).c_str());
    return ok ? raw : nullptr;
}

// The c700 takes no pre-built stream (it uses the Board default for that, and for
// drainLog()). Connect it through its endpoint resolver, which for this one call hands
// back a FarEndStream, and then put the real resolver back.
FarEndStream* plugC700(Board& b) {
    FarEndStream* raw = nullptr;
    C700Board::setResolver([&raw](const std::string&, std::string&) -> std::unique_ptr<ByteStream> {
        auto s = std::make_unique<FarEndStream>();
        raw    = s.get();
        return s;
    });
    std::string err;
    bool        ok = b.connect("prn", "farend", err);
    C700Board::setResolver(resolveEndpoint);
    CHECK(ok && raw, "a line on lpt0:prn");
    return ok ? raw : nullptr;
}

// How many lines of the log are exactly `want`.
int count(const std::vector<std::string>& log, const std::string& want) {
    int n = 0;
    for (const auto& l : log)
        if (l == want) ++n;
    return n;
}

} // namespace

void test_boardlog() {
    SECTION("board log -- the machine drains every unit's stream, once, as id:unit: message");
    {
        Machine     m;
        std::string err;
        // One board on the Board default (c700), and the three overrides that drained their
        // streams themselves: io4 (serial and parallel), gsio and interfacer1.
        Board* c700 = m.add("c700", "lpt0", err);
        Board* io4  = m.add("io4", "io40", err);
        Board* gsio = m.add("gsio", "gsio0", err);
        Board* if1  = m.add("interfacer1", "if10", err);
        CHECK(c700 && io4 && gsio && if1, "the four boards add");
        if (!(c700 && io4 && gsio && if1)) return;

        FarEndStream* prn = plugC700(*c700);
        FarEndStream* io4b = plug(*io4, "b");
        FarEndStream* io4pa = plug(*io4, "pa");
        FarEndStream* gsioa = plug(*gsio, "a");
        FarEndStream* if1b = plug(*if1, "b");
        if (!(prn && io4b && io4pa && gsioa && if1b)) return;
        m.drainBoardLog();  // whatever connecting had to say

        for (FarEndStream* s : {prn, io4b, io4pa, gsioa, if1b}) s->farEnd.push_back("could not spool");
        auto log = m.drainBoardLog();
        CHECK(count(log, "lpt0:prn: could not spool") == 1, "c700 (the Board default): once, lpt0:prn:");
        CHECK(count(log, "io40:b: could not spool") == 1, "io4 serial unit b: once, io40:b:");
        CHECK(count(log, "io40:pa: could not spool") == 1, "io4 parallel unit pa: once, io40:pa:");
        CHECK(count(log, "gsio0:a: could not spool") == 1, "gsio unit a: once, gsio0:a:");
        CHECK(count(log, "if10:b: could not spool") == 1, "interfacer1 unit b: once, if10:b:");
        int far = 0;
        for (const auto& l : log)
            if (l.find("could not spool") != std::string::npos) ++far;
        CHECK(far == 5, "and in no other form: five lines, one per line");

        // No board takes the stream's message for itself.
        for (FarEndStream* s : {prn, io4b, io4pa, gsioa, if1b}) s->farEnd.push_back("again");
        for (Board* b : {c700, io4, gsio, if1})
            for (const auto& l : b->drainLog())
                CHECK(l.find("again") == std::string::npos,
                      (b->id + ": the board's own drainLog() leaves the stream alone").c_str());
        CHECK(count(m.drainBoardLog(), "io40:pa: again") == 1, "so the machine still finds it");
    }
}
