#include "test.h"

#include "cli/monitor.h"
#include "core/board.h"
#include "core/machine.h"
#include "core/machines.h"
#include "host/console.h"
#include "host/endpoint.h"
#include "host/filter.h"
#include "host/media.h"
#include "host/mirror_stream.h"
#include "host/stream.h"
#include "host/tee_stream.h"
#include "mcp/server.h"
#include "platform/pty.h"
#include "platform/socket.h"
#include "util/json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <istream>
#include <map>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

using namespace altair;

// The MCP server is driven by feeding JSON-RPC lines to runMcp and reading the
// replies back -- the same door an assistant uses, over a pair of stringstreams
// instead of a pipe. No mocks: a real built-in machine, a real 8080, a real 6850.
namespace {

// A tiny thread-safe "pipe": feed() appends bytes from any thread (real time, real
// gaps), close() marks EOF once everything fed has been drained. A static
// std::istringstream makes every line available to runMcp's reader thread at once,
// which is fine for an ordinary script but wrong for testing #488's
// notifications/cancelled -- the reader would race straight through a cancellation
// line before the targeted `run` even starts executing, since nothing paces it
// against real time the way a live client's stdin does. FeedBuf lets a test control
// exactly when each line becomes visible, the same way a real client's writes do.
class FeedBuf : public std::streambuf {
public:
    void feed(const std::string& s) {
        std::lock_guard<std::mutex> lk(mu_);
        for (char c : s) q_.push_back(c);
        cv_.notify_all();
    }
    void close() {
        std::lock_guard<std::mutex> lk(mu_);
        closed_ = true;
        cv_.notify_all();
    }

protected:
    int_type underflow() override {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return traits_type::eof();
        ch_ = q_.front();
        q_.pop_front();
        setg(&ch_, &ch_, &ch_ + 1);
        return traits_type::to_int_type(ch_);
    }

private:
    std::mutex              mu_;
    std::condition_variable cv_;
    std::deque<char>        q_;
    bool                    closed_ = false;
    char                    ch_ = 0;
};

// The other half of FeedBuf: an output buffer a second thread can read WHILE the
// server is still writing to it. A plain std::ostringstream cannot be -- reading
// out.str() while runMcp writes is a data race -- so a test that needs to know a
// reply has landed before it does the next thing has to have this. That is the only
// honest way to time a stop request: wait until the setup calls have actually ANSWERED,
// rather than guessing how long a guest boot takes on someone else's runner.
class SinkBuf : public std::streambuf {
public:
    std::string text() const {
        std::lock_guard<std::mutex> lk(mu_);
        return s_;
    }

protected:
    int_type overflow(int_type c) override {
        if (c != traits_type::eof()) {
            std::lock_guard<std::mutex> lk(mu_);
            s_.push_back(traits_type::to_char_type(c));
        }
        return c;
    }
    std::streamsize xsputn(const char* p, std::streamsize n) override {
        std::lock_guard<std::mutex> lk(mu_);
        s_.append(p, (size_t)n);
        return n;
    }

private:
    mutable std::mutex mu_;
    std::string        s_;
};

// The replies in a captured stream, by id -- runScript's tail end, for the tests that
// drive runMcp themselves instead of handing it a finished script.
std::map<int, Json> repliesById(const std::string& text) {
    std::map<int, Json> byId;
    std::istringstream  lines(text);
    std::string         line;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        Json        j;
        std::string err;
        if (Json::parse(line, j, err)) byId[(int)j.at("id").integer()] = j;
    }
    return byId;
}

std::map<int, Json> runScript(Machine& m, const std::string& script,
                              const std::string& mirror = "", const std::string& log = "") {
    std::istringstream in(script);
    std::ostringstream out;
    runMcp(m, in, out, mirror, log);

    return repliesById(out.str());
}

// A free TCP port the OS confirms unused -- bind port 0, read what it picked, drop it.
uint16_t freePort() {
    std::string err;
    if (auto probe = platform::listenTcp(0, err)) return probe->port();
    return 0;
}

// Poll `ready` for up to ~2 s of REAL time -- the loopback handshake and byte delivery
// are the kernel's to schedule (test_lines' lesson; sockettest's).
template <typename Fn>
bool waitFor(Fn ready, int ms = 2000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

// The MirrorStream now wrapping the console unit (and its inner ScriptedStream), if any.
// After runMcp with --mirror the console's line is Filter(Shared(Mirror(scripted))) -- the
// console transform filter is outermost (always), the SharedStream the console unit holds
// under it (#759), the mirror under that, the assistant drives the inner scripted. Peel the
// filter and the shared handle to reach the mirror.
MirrorStream* consoleMirror(Machine& m, ScriptedStream** innerOut = nullptr) {
    for (const auto& b : m.boards())
        for (const auto& u : b->units()) {
            if (u.kind != UnitKind::Serial) continue;
            ByteStream* s = b->unitStream(u.name);
            if (auto* f = dynamic_cast<FilterStream*>(s)) s = f->inner();  // peel the console filter
            if (auto* sh = dynamic_cast<SharedStream*>(s)) s = sh->inner(); // the shared handle
            if (auto* t = dynamic_cast<TeeStream*>(s)) s = t->inner();     // and a --log tap
            if (auto* mir = dynamic_cast<MirrorStream*>(s)) {
                if (innerOut) *innerOut = dynamic_cast<ScriptedStream*>(mir->inner());
                return mir;
            }
        }
    return nullptr;
}

// Load the altmon built-in -- a whole machine (8080, 2SIO, memory), no disk fixture --
// the way every section here does. False and a CHECK if it is not compiled in.
bool loadAltmon(Machine& m) {
    const BuiltinMachine* altmon = nullptr;
    for (const auto& b : builtinMachines())
        if (std::string(b.name) == "altmon") altmon = &b;
    CHECK(altmon != nullptr, "the altmon built-in is compiled in");
    if (!altmon) return false;
    std::string err;
    CHECK(loadMachine(*altmon, m, err), "altmon loads");
    return true;
}

// Wait until the worker either IS or IS NOT dispatched on a request, by asking the one
// tool that answers out of band: `status`, handled on the reader thread (#490).
//
// THIS IS WHAT REPLACES "sleep 200ms and assume the call under test is under way." A ^C or
// a `notifications/cancelled` is only honoured once the worker has dequeued the call it
// targets: runMcp calls Debugger::clearStopRequest() as it publishes each request id, so a
// signal landing a moment too early is wiped by the very dispatch that was about to run it,
// and the call goes on to serve its whole budget and answer `timeout`. A fixed sleep is a
// guess that the dequeue already happened -- the guess this file warns against two comments
// down, made anyway because there is no reply to wait for. On a loaded Windows CI runner it
// lost: PR #506's first leg, trial 4 of 6.
//
// `in_flight` is set under the same lock as clearStopRequest() and on the very next line, so
// observing it TRUE is proof the clear is already behind us -- a signal raised from here on
// is seen by the `Debugger::stopRequested()` check at the top of the run loop and cannot be
// swallowed. Waiting for FALSE first is what makes the TRUE mean anything: it pins the
// worker as idle before the call under test is fed, so the TRUE that follows can only be
// that call and not the tail of the setup.
//
// Probing cannot perturb what it measures. `status` is answered by the reader without ever
// entering `pending`, so it neither queues nor reorders; it deliberately never becomes
// `currentId`, so it cannot swallow a cancel meant for another id. Probe ids come from
// their own high range, well clear of the sequential ids each test indexes its replies by.
int g_probeId = 8000;

bool waitForWorker(FeedBuf& feedBuf, SinkBuf& sinkBuf, bool wantBusy, int ms = 30000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        const int          pid = ++g_probeId;
        std::ostringstream line;
        line << R"({"jsonrpc":"2.0","id":)" << pid
             << R"(,"method":"tools/call","params":{"name":"status","arguments":{}}})"
             << "\n";
        feedBuf.feed(line.str());

        std::map<int, Json> rep;
        if (waitFor([&] { rep = repliesById(sinkBuf.text()); return rep.count(pid) != 0; },
                    5000) &&
            rep[pid].at("result").at("structuredContent").at("in_flight").boolean() == wantBusy)
            return true;
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

// Drive a LIVE runMcp and interrupt the one call that is meant to be interrupted.
//
// The hard part is not the signal, it is knowing when to send it. A ^C that lands before
// the call under test has been dispatched is CORRECTLY discarded -- the server clears a
// stale flag as it publishes each request id (mcp/server.cpp) -- so a test that guesses
// "the setup is surely done by now" and sleeps is measuring the runner's mood, not the
// server: the guest boot in `setup` takes as long as it takes, and on a loaded Windows
// runner that was longer than the guess. So: feed `setup`, WAIT for its last reply to
// land, and only then feed the call under test. The dequeue of that call was left to a
// 200ms sleep on the grounds that one line read and a mutex is microseconds -- true, and
// still not a guarantee: a loaded Windows runner lost that race (PR #506). waitForWorker()
// above replaces it with the answer from the server itself.
//
// `after` is fed once the stop request has been answered (the stale-flag checks). The raise
// happens on this thread, between two feeds, so it can never escape runMcp's SigintGuard
// -- outside it SIGINT is back to its default disposition and would kill the test binary.
struct SigintRun {
    std::map<int, Json> replies;
    long long           elapsedMs = 0;  // feeding the call under test -> its reply
};

SigintRun runWithSigint(Machine& m, const std::vector<std::string>& setup,
                        const std::string& underTest, const std::vector<std::string>& after) {
    FeedBuf      feedBuf;
    std::istream in(&feedBuf);
    SinkBuf      sinkBuf;
    std::ostream out(&sinkBuf);

    std::thread worker([&] { runMcp(m, in, out, ""); });

    int  id   = 0;
    auto feed = [&](const std::string& params) {
        std::ostringstream line;
        line << R"({"jsonrpc":"2.0","id":)" << ++id
             << R"(,"method":"tools/call","params":)" << params << "}\n";
        feedBuf.feed(line.str());
        return id;
    };
    auto answered = [&](int want) {
        return waitFor([&] { return repliesById(sinkBuf.text()).count(want) != 0; }, 30000);
    };

    for (const auto& call : setup) feed(call);
    CHECK(answered(id), "the setup calls answered before the call under test was sent");
    CHECK(waitForWorker(feedBuf, sinkBuf, false),
          "the worker is idle again before the call under test is fed");

    const int runId = feed(underTest);
    CHECK(waitForWorker(feedBuf, sinkBuf, true),
          "the call under test is DISPATCHED -- so clearStopRequest() is behind us -- before "
          "the signal is raised");
    // Start the clock at DISPATCH, not at the feed: `timeout_ms` is counted from the moment
    // the worker enters the call, so that is the window "well inside the budget" is about.
    // Timing from the feed would charge the queue wait to the stop request.
    const auto t0 = std::chrono::steady_clock::now();
    std::raise(SIGINT);
    CHECK(answered(runId), "the interrupted run answered");
    SigintRun r;
    r.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();

    for (const auto& call : after) feed(call);
    feedBuf.close();
    worker.join();
    r.replies = repliesById(sinkBuf.text());
    return r;
}

// A LIVE runMcp a test calls into one request at a time, reading each reply as it lands --
// for the free run (#602), where the guest runs BETWEEN calls, so a finished script handed
// to runScript would be over before the guest had done anything. Destroying it closes the
// feed (EOF) and joins the server.
struct LiveMcp {
    FeedBuf      feedBuf;
    std::istream in{&feedBuf};
    SinkBuf      sinkBuf;
    std::ostream out{&sinkBuf};
    int          id = 0;
    std::thread  worker;  // last: everything it touches is built first

    explicit LiveMcp(Machine& m, std::string mirror = "")
        : worker([this, &m, sink = std::move(mirror)] { runMcp(m, in, out, sink); }) {}
    ~LiveMcp() { close(); }

    // The reply's `result`; null if none came within 30 s.
    Json call(const std::string& name, const std::string& args = "{}") {
        const int          want = ++id;
        std::ostringstream line;
        line << R"({"jsonrpc":"2.0","id":)" << want
             << R"(,"method":"tools/call","params":{"name":")" << name
             << R"(","arguments":)" << args << "}}\n";
        feedBuf.feed(line.str());
        std::map<int, Json> rep;
        if (!waitFor([&] { rep = repliesById(sinkBuf.text()); return rep.count(want) != 0; },
                     30000))
            return Json();
        return rep[want].at("result");
    }
    Json data(const std::string& name, const std::string& args = "{}") {
        return call(name, args).at("structuredContent");
    }
    static bool isError(const Json& result) { return result.at("isError").boolean(); }

    void close() {
        feedBuf.close();
        if (worker.joinable()) worker.join();
    }
};

std::string tmpPath(const char* leaf) {
    return (std::filesystem::temp_directory_path() / leaf).string();
}



// A board that drops a ^C into one exact spot: the gap between the --mcp run loop's
// "was a stop requested?" check and the slice it then runs. That loop reads the backplane's
// rxBytes() between the two, so this board raises the stop request from there. It is how a
// test reaches a window a few instructions wide WITHOUT timing: on Windows the real one
// was hit about once in 550 cancels, which no test can wait for.
//
// ONE-SHOT, and that is load-bearing. The loop also reads rxBytes() AFTER each slice, and
// a stop request raised there is caught by the next top-of-loop check whatever the slice
// does -- raise on every call and the test passes with or without the fix. Armed while the
// worker is idle, the first call after arming is the pre-slice one.
class GapBoard : public Board {
public:
    mutable std::atomic<bool> armed{false};
    std::string type() const override { return "test-gap"; }
    bool decodes(const BusCycle&) const override { return false; }
    std::vector<Property> properties() override { return {}; }
    uint64_t rxBytes() const override {
        if (armed.exchange(false)) Debugger::requestStop();
        return 0;
    }
};

} // namespace

void test_mcp() {
    SECTION("MCP: the encoder never emits invalid JSON for guest bytes");
    {
        // A serial terminal is 8-bit clean and can print ANY byte -- a lone 0xFF, a
        // control code -- while a host path really is UTF-8. The one must be escaped,
        // the other must survive.
        std::string raw = "HI\xff\x01 caf\xc3\xa9";  // lone FF, ^A, then a valid UTF-8 e-acute
        std::string d = Json(raw).dump();
        CHECK(d.find("\\u00ff") != std::string::npos, "a lone 0xFF byte is \\u-escaped");
        CHECK(d.find("\\u0001") != std::string::npos, "a control byte is \\u-escaped");
        CHECK(d.find("\xc3\xa9") != std::string::npos, "a valid UTF-8 sequence passes through");
        CHECK(d.find('\xff') == std::string::npos, "no raw high byte survives to break the line");

        // ...and the result is valid JSON that a client can actually parse (an escaped
        // byte comes back as its code point, not the raw byte -- that is what keeps the
        // line legal).
        Json back;
        std::string err;
        CHECK(Json::parse(d, back, err), "the escaped form is valid JSON");
        CHECK(back.str().find("caf\xc3\xa9") != std::string::npos, "and the UTF-8 in it survived the trip");
    }

    SECTION("MCP: a \\uXXXX control byte decodes to that one raw byte for the guest");
    {
        // The INPUT direction of the section above, and the reason issue #491 was filed:
        // `send`/`run` hand their string straight to the console, so what a client may
        // type at the guest is decided here, in the parser. ^C and ^Z must arrive as one
        // byte each -- they are how you break a BASIC program or end a PIP copy.
        Json j;
        std::string err;
        CHECK(Json::parse(R"("A\u0003B\u001aC")", j, err), "a \\u-escaped control byte parses");
        const std::string in = j.str();
        CHECK(in.size() == 5, "each escape decodes to exactly one byte");
        CHECK(in == std::string("A\x03" "B\x1a" "C", 5), "and it is the byte the escape names");

        // THE TRAP: JSON has no \x escape. We are lenient with an unknown one (drop the
        // backslash, keep the letter) rather than rejecting the request, so `\x03` types
        // three ordinary characters at the guest. That looks exactly like "control bytes
        // are filtered out and printable text gets through", which is what it was read as.
        CHECK(Json::parse(R"("\x03")", j, err), "\\x03 is not rejected -- we are lenient");
        CHECK(j.str() == "x03", "it is NOT a control byte: it types the characters x03");
    }

    SECTION("MCP: interactive tools drive a running guest (ALTMON)");
    {
        const BuiltinMachine* altmon = nullptr;
        for (const auto& b : builtinMachines())
            if (std::string(b.name) == "altmon") altmon = &b;
        CHECK(altmon != nullptr, "the altmon built-in is compiled in");
        if (!altmon) return;

        Machine m;
        std::string err;
        CHECK(loadMachine(*altmon, m, err), "altmon loads");

        std::ostringstream s;
        int id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/list", "{}");
        // ALTMON lives at F800 (63488). Boot it; its banner is "ALTMON".
        req("tools/call", R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req("tools/call", R"({"name":"regs","arguments":{}})");
        // Its own first bytes, dumped through itself: DF800F80F (no spaces -- ALTMON's
        // ahex cancels on one). No `until`: let it run to the next prompt so the whole
        // dump line lands. Proves send + run + recv move real bytes both ways.
        req("tools/call", R"({"name":"run","arguments":{"input":"DF800F80F\r","timeout_ms":4000}})");

        auto rep = runScript(m, s.str());

        // tools/list carries the interactive four alongside the builders.
        bool run = false, send = false, recv = false, regs = false;
        for (const auto& t : rep[2].at("result").at("tools").items()) {
            std::string n = t.at("name").str();
            run  |= (n == "run");
            send |= (n == "send");
            recv |= (n == "recv");
            regs |= (n == "regs");
        }
        CHECK(run && send && recv && regs, "tools/list advertises run, send, recv and regs");

        const Json& boot = rep[3].at("result").at("structuredContent");
        CHECK(boot.at("stopped").str() == "match", "run boots to the banner and stops on the match");
        CHECK(boot.at("output").str().find("ALTMON") != std::string::npos, "the ALTMON banner came out");

        const Json& r = rep[4].at("result").at("structuredContent");
        CHECK(r.has("pc") && r.has("registers"), "regs reports pc and the register file");

        const Json& dump = rep[5].at("result").at("structuredContent");
        CHECK(dump.at("output").str().find("3E 03 D3 10") != std::string::npos,
              "the DUMP command's output is ALTMON's own initialization bytes");
    }

    SECTION("MCP: the monitor TYPE command reaches the guest console (issue #427)");
    {
        // TYPE injects type-ahead into Console::instance(), but under --mcp the guest's
        // console line is rebound to a headless ScriptedStream -- so TYPE via the `monitor`
        // tool used to vanish silently while `run`/`send` worked. It must now feed the same
        // scripted line the guest actually reads.
        Machine m;
        if (!loadAltmon(m)) return;

        std::ostringstream s;
        int id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/call", R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        // Type ALTMON's own dump command through the MONITOR tool's TYPE, NOT run's input.
        req("tools/call", R"({"name":"monitor","arguments":{"command":"TYPE \"DF800F80F\\r\""}})");
        // Then advance the guest with a bare run -- nothing typed here. If TYPE reached the
        // scripted line, the guest reads it now and the dump comes out.
        req("tools/call", R"({"name":"run","arguments":{"timeout_ms":4000}})");

        auto rep = runScript(m, s.str());

        const Json& typed = rep[3].at("result");
        CHECK(!typed.has("isError"), "the TYPE command did not error");
        const Json& out = rep[4].at("result").at("structuredContent");
        CHECK(out.at("output").str().find("3E 03 D3 10") != std::string::npos,
              "the guest executed the TYPEd command -- its dump reached the scripted console");
    }

    SECTION("MCP: connecting a unit to console takes the console from its holder (#756, #759)");
    {
        // One unit holds the console, under --mcp as in the CLI. The holder shows `scripted`,
        // not `console`, so the monitor's CONNECT could not see it to take the console from
        // it: the new unit first went to the host console -- our JSON-RPC stdout (#756) --
        // and then joined the holder on the scripted line (#759). Each case wires one sio0
        // port to `console` and runs a program that writes 'Z' out of port B, then 'Y' out
        // of port A. Only the port that holds the console may reach the assistant.
        const std::string prog =
            R"({"name":"monitor","arguments":{"command":"DEPOSIT 100 3E 03 D3 12 3E 11 D3 12 )"
            R"(3E 5A D3 13 3E 03 D3 10 3E 11 D3 10 3E 59 D3 11 76"}})";
        const std::string run = R"({"name":"run","arguments":{"from":256,"timeout_ms":4000}})";
        auto connectB = [](const char* unit) {
            return std::string(R"({"name":"monitor","arguments":{"command":"CONNECT sio0:)") + unit +
                   R"( console"}})";
        };

        // The scripted line at the bottom of a unit's stack (Filter -> Shared -> Scripted).
        auto scriptedOf = [](Machine& mm, const char* unit) -> ScriptedStream* {
            Board* b = mm.find("sio0");
            ByteStream* st = b ? b->unitStream(unit) : nullptr;
            if (auto* f = dynamic_cast<FilterStream*>(st)) st = f->inner();
            if (auto* sh = dynamic_cast<SharedStream*>(st)) st = sh->inner();
            return dynamic_cast<ScriptedStream*>(st);
        };
        auto stateOf = [](Machine& mm, const char* unit) -> std::string {
            if (Board* b = mm.find("sio0"))
                for (const auto& u : b->units())
                    if (u.name == unit) return u.state;
            return std::string();
        };
        auto textOf = [](std::map<int, Json>& rep, int n) {
            return rep[n].at("result").at("content").items().at(0).at("text").str();
        };
        auto script = [](const std::vector<std::string>& calls) {
            std::ostringstream s;
            int id = 0;
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":"initialize","params":{}})" << "\n";
            for (const auto& c : calls)
                s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":"tools/call","params":)"
                  << c << "}\n";
            return s.str();
        };

        // The monitor tool's CONNECT: the CLI's "taken" line, then the CLI's own reply.
        {
            Machine m;
            if (!loadAltmon(m)) return;
            auto rep = runScript(m, script({prog, connectB("b"), run}));
            const std::string said = textOf(rep, 3);
            CHECK(said.rfind("console taken from sio0:a\n", 0) == 0,
                  "CONNECT says first who it took the console from, as the CLI does");
            CHECK(said.find("sio0:b: connected to console") != std::string::npos,
                  "and then that sio0:b is connected");
            const std::string out = rep[4].at("result").at("structuredContent").at("output").str();
            CHECK(out.find('Z') != std::string::npos, "port B, which holds the console, reaches run");
            CHECK(out.find('Y') == std::string::npos, "port A, which lost it, does not");
            CHECK(stateOf(m, "a") == "null", "sio0:a is on null");
            CHECK(scriptedOf(m, "b") != nullptr, "sio0:b holds the scripted line");
        }

        // The connect tool takes the console AT ONCE, not at the next run: a `step` right
        // after it must not write port B's byte onto stdout. RUN first, because the monitor
        // tool would move the console itself.
        {
            Machine m;
            if (!loadAltmon(m)) return;
            auto rep = runScript(
                m, script({prog, R"({"name":"monitor","arguments":{"command":"RUN 100"}})",
                           R"({"name":"connect","arguments":{"id":"sio0","unit":"b","endpoint":"console"}})",
                           R"({"name":"step","arguments":{"count":13}})",
                           R"({"name":"recv","arguments":{}})"}));
            CHECK(textOf(rep, 4).find("console taken from sio0:a") != std::string::npos,
                  "the connect tool says who it took the console from");
            const std::string out = rep[6].at("result").at("structuredContent").at("output").str();
            CHECK(out.find('Z') != std::string::npos, "a step's port B output reaches recv");
            CHECK(out.find('Y') == std::string::npos, "and port A's does not");
            CHECK(stateOf(m, "a") == "null", "sio0:a is on null");
        }

        // And back: the line moves again, and the unit that held it goes to null.
        {
            Machine m;
            if (!loadAltmon(m)) return;
            auto rep = runScript(
                m, script({prog, connectB("b"), connectB("a"), run,
                           R"({"name":"monitor","arguments":{"command":"SHOW CONSOLE"}})"}));
            CHECK(textOf(rep, 4).rfind("console taken from sio0:b\n", 0) == 0,
                  "the second CONNECT takes the console back from sio0:b");
            const std::string out = rep[5].at("result").at("structuredContent").at("output").str();
            CHECK(out.find('Y') != std::string::npos, "port A holds the console again");
            CHECK(out.find('Z') == std::string::npos, "and port B, on null, does not reach run");
            CHECK(stateOf(m, "b") == "null", "sio0:b is on null");
            const std::string show = textOf(rep, 6);
            CHECK(show.find("held by  sio0:a (--mcp)\n") != std::string::npos,
                  "SHOW CONSOLE names one holder, sio0:a");
        }
    }

    SECTION("MCP: CONNECT takes the console during the command, and SHOW CONSOLE names the holder (#779)");
    {
        // One routine takes the console, under --mcp as in the CLI. The holder shows
        // `scripted`, so CONNECT once missed it and the server moved the console only after
        // the command: a SHOW CONSOLE later in the same DO file named two holders. And a board
        // that cannot take a pre-built stream holds a bare line of its own, which SHOW CONSOLE
        // did not count at all.
        auto call = [](int id, const std::string& tool, const std::string& args) {
            return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
                   R"(,"method":"tools/call","params":{"name":")" + tool +
                   R"(","arguments":)" + args + "}}\n";
        };
        auto mon = [&](int id, const std::string& cmd) {
            return call(id, "monitor", R"({"command":")" + cmd + R"("})");
        };
        auto textOf = [](std::map<int, Json>& rep, int n) {
            return rep.count(n) ? rep[n].at("result").at("content").items().at(0).at("text").str()
                                : std::string();
        };
        auto stateOf = [](Machine& mm, const char* id, const char* unit) -> std::string {
            if (Board* b = mm.find(id))
                for (const auto& u : b->units())
                    if (u.name == unit) return u.state;
            return std::string();
        };
        const std::string init =
            std::string(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") + "\n";

        // In one monitor call: the CONNECT, then SHOW CONSOLE, from a DO file.
        {
            const std::string doFile = tmpPath("altair_mcp_take_console.do");
            {
                std::ofstream f(doFile);
                f << "CONNECT sio0:b console\nSHOW CONSOLE\n";
            }
            Machine m;
            if (!loadAltmon(m)) return;
            std::string path = doFile;
            for (auto& c : path)
                if (c == '\\') c = '/';
            auto rep = runScript(m, init + mon(2, "DO \\\"" + path + "\\\""));  // quoted: a space
            std::filesystem::remove(doFile);
            const std::string said = textOf(rep, 2);
            const size_t connect = said.find("do> CONNECT sio0:b console\n");
            const size_t taken   = said.find("console taken from sio0:a\n");
            CHECK(connect != std::string::npos && taken != std::string::npos && taken > connect,
                  "CONNECT takes the console from sio0:a during the command, not after it");
            CHECK(said.find("held by  sio0:b\n") != std::string::npos,
                  "and SHOW CONSOLE, in the same call, names sio0:b alone");
            CHECK(stateOf(m, "sio0", "a") == "null", "sio0:a is on null");
        }

        // The move keeps the line: output the guest wrote before it, and nobody has read yet,
        // is still there for `recv`. The program writes 'Z' out of port B, then 'Y' out of
        // port A, which holds the console; then sio0:b takes it.
        {
            Machine m;
            if (!loadAltmon(m)) return;
            auto rep = runScript(
                m, init + mon(2, "DEPOSIT 100 3E 03 D3 12 3E 11 D3 12 3E 5A D3 13 3E 03 D3 10 "
                                 "3E 11 D3 10 3E 59 D3 11 76") +
                       mon(3, "RUN 100") + call(4, "step", R"({"count":13})") +
                       mon(5, "CONNECT sio0:b console") + call(6, "recv", "{}"));
            const std::string out =
                rep.count(6) ? rep[6].at("result").at("structuredContent").at("output").str() : "";
            CHECK(out.find('Y') != std::string::npos,
                  "port A's unread output survives the move of the console to sio0:b");
        }

        // A board with no connectStream (the 88-PIO) holds a bare scripted line.
        {
            Machine m;
            if (!loadAltmon(m)) return;
            auto rep = runScript(m, init + mon(2, "BOARDS ADD pio pio0 port=4") +
                                        mon(3, "CONNECT pio0:out console") +
                                        mon(4, "SHOW CONSOLE") +
                                        mon(5, "CONNECT pio0:in console"));
            CHECK(textOf(rep, 3).rfind("console taken from sio0:a\n", 0) == 0,
                  "pio0:out takes the console from sio0:a");
            CHECK(textOf(rep, 4).find("held by  pio0:out (--mcp)\n") != std::string::npos,
                  "SHOW CONSOLE names pio0:out, on its own bare line");
            CHECK(textOf(rep, 5).rfind("console taken from pio0:out\n", 0) == 0,
                  "CONNECT takes the console from the bare line's holder");
            CHECK(stateOf(m, "pio0", "out") == "null", "pio0:out is on null");
            CHECK(stateOf(m, "pio0", "in") == "scripted", "pio0:in holds the line");
        }
    }

    SECTION("MCP: the no-console-line error gives advice that works (issue #783)");
    {
        // The console unit goes to another endpoint, then to a bare `scripted`. That does not
        // give it the console line back, and `run` says so. The error once told the client to
        // CONNECT a unit to 'scripted' -- what it had just done. It must name `console`, and
        // doing what it says must give `run` its line again.
        Machine m;
        if (!loadAltmon(m)) return;
        auto call = [](int id, const std::string& tool, const std::string& args) {
            return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
                   R"(,"method":"tools/call","params":{"name":")" + tool +
                   R"(","arguments":)" + args + "}}\n";
        };
        auto mon = [&](int id, const char* cmd) {
            return call(id, "monitor", std::string(R"({"command":")") + cmd + R"("})");
        };
        const std::string cr = R"({"input":"\r","until":"*","timeout_ms":2000})";
        auto rep = runScript(
            m, std::string(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") + "\n" +
                   call(2, "run", R"({"from":63488,"until":"*","timeout_ms":4000})") +
                   mon(3, "CONNECT sio0:a null") + mon(4, "CONNECT sio0:a scripted") +
                   call(5, "run", cr) + mon(6, "CONNECT sio0:a console") + call(7, "run", cr));
        CHECK(rep.count(5) && rep[5].at("result").has("isError"),
              "run has no console line after `scripted`");
        const std::string said =
            rep.count(5) ? rep[5].at("result").at("content").items().at(0).at("text").str() : "";
        CHECK(said.find("CONNECT a serial unit to 'console'") != std::string::npos,
              "the error says to connect a unit to console");
        CHECK(said.find("'scripted'") == std::string::npos,
              "and not to scripted, which is what the client had just done");
        CHECK(rep.count(7) && !rep[7].at("result").has("isError") &&
                  rep[7].at("result").at("structuredContent").at("output").str().find('*') !=
                      std::string::npos,
              "after the advised CONNECT sio0:a console, run reaches the ALTMON prompt");
    }

    SECTION("MCP: TYPE sends a quote and a control byte through the monitor tool (issue #624)");
    {
        // A client that cannot put a JSON escape in `input` sends its keys with TYPE. The
        // text used to stop at `\"`, so the guest got half a line. ALTMON does not echo a
        // key that is not a command; it gives a new prompt. So the quote shows as a second
        // `*` before the dump, and the dump shows the text after the quote arrived too.
        // (The bytes themselves are checked one by one in test_cli.)
        Machine m;
        if (!loadAltmon(m)) return;

        std::ostringstream s;
        int id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/call", R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req("tools/call", R"({"name":"monitor","arguments":{"command":"TYPE \"\\\"\\x44F800F80F\\^M\""}})");
        req("tools/call", R"({"name":"run","arguments":{"timeout_ms":4000}})");

        auto rep = runScript(m, s.str());

        CHECK(!rep[3].at("result").has("isError"), "the TYPE command did not error");
        const std::string out = rep[4].at("result").at("structuredContent").at("output").str();
        CHECK(out.find("*\r\n*DUMP") != std::string::npos,
              "the quote reached the guest: a new prompt for a key ALTMON does not know");
        CHECK(out.find("3E 03 D3 10") != std::string::npos,
              "and so did the text after it: \\x44 is D, \\^M is the carriage return");
    }

    SECTION("MCP: --mirror wraps the console so a socket client watches and takes over");
    {
        const BuiltinMachine* altmon = nullptr;
        for (const auto& b : builtinMachines())
            if (std::string(b.name) == "altmon") altmon = &b;
        CHECK(altmon != nullptr, "the altmon built-in is compiled in");
        if (!altmon) return;

        Machine m;
        std::string err;
        CHECK(loadMachine(*altmon, m, err), "altmon loads");

        uint16_t port = freePort();
        CHECK(port != 0, "the OS hands us a free port");
        const std::string mirror = "socket:" + std::to_string(port);

        // Drive a real MCP session WITH the mirror: initialize rebinds the console to
        // scripted|socket:PORT, and the boot run makes the guest print its banner.
        std::ostringstream s;
        int                id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/call",
            R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        auto rep = runScript(m, s.str(), mirror);

        // The assistant still sees the guest's output -- the inner scripted is driven
        // through the wrapper, transparently, so the run loop needed no change.
        const Json& boot = rep[2].at("result").at("structuredContent");
        CHECK(boot.at("output").str().find("ALTMON") != std::string::npos,
              "the assistant's run still sees the banner through the mirror");

        // The console line is now a mirror over scripted, describing the socket sink.
        ScriptedStream* inner = nullptr;
        MirrorStream*   mir   = consoleMirror(m, &inner);
        CHECK(mir != nullptr, "the console unit is wrapped in a MirrorStream");
        if (mir)
            CHECK(mir->describe() == "scripted|" + mirror,
                  "and it describes itself as scripted|socket:PORT");
        CHECK(inner != nullptr, "the inner line is the scripted stream the tools drive");

        // A human telnets in AFTER the assistant started -- the listener is still open on
        // the unit -- and takes over: they type a DUMP command, the guest EXECUTES it, and
        // the result comes back down the same socket. Proves both directions end to end,
        // through the exact wiring --mcp --mirror builds.
        if (mir && inner) {
            auto client = platform::connectTcp("127.0.0.1", port, err);
            CHECK(client != nullptr, ("a watcher dials in: " + err).c_str());
            bool up = waitFor([&] {
                m.pump();  // the run loop pumps every stream, the mirror among them
                if (client) client->poll();
                return client && client->established();
            });
            CHECK(up, "the watcher connects and the mirror accepts it mid-session");

            // The watcher types ALTMON's own dump command -- INJECTED as input the guest
            // reads (take-over), not fed through the assistant's channel.
            const char cmd[] = "DF800F80F\r";
            if (client) client->write((const uint8_t*)cmd, sizeof cmd - 1);

            std::string seen;
            waitFor([&] {
                if (client) client->poll();
                m.pump();
                m.debug.run(2000);  // give the guest cycles to read and execute
                m.pump();
                uint8_t b[256];
                size_t  r = client ? client->read(b, sizeof b) : 0;
                seen.append((const char*)b, r);
                return seen.find("3E 03 D3 10") != std::string::npos;
            });
            CHECK(seen.find("3E 03 D3 10") != std::string::npos,
                  "the watcher's typed command was executed by the guest, dump came back down the socket");
        }
    }

    SECTION("MCP: --mirror pty -- a queued tail reaches the watcher with no further request");
    if (platform::havePty()) {
        // #683. The watcher's wire is serviced from pump(). After a `run` returns nothing
        // called it, so output the kernel buffer had no room for stayed in the mirror's
        // queue until the NEXT request. A watcher that reads late must still get it all.
        const BuiltinMachine* altmon = nullptr;
        for (const auto& b : builtinMachines())
            if (std::string(b.name) == "altmon") altmon = &b;
        CHECK(altmon != nullptr, "the altmon built-in is compiled in");
        if (!altmon) return;

        auto        mp = std::make_unique<Machine>();
        Machine&    m  = *mp;
        std::string err;
        CHECK(loadMachine(*altmon, m, err), "altmon loads");

        namespace fs = std::filesystem;
        const std::string link = tmpPath("altairsim-test-mcp-pty");
        std::error_code   ec;
        fs::remove(link, ec);
        {
            LiveMcp mcp(m, "pty:" + link);
            CHECK(waitFor([&] { return fs::is_symlink(link, ec); }), "the link is made at start");
            auto watcher = platform::openPtyPeer(link, err);
            CHECK(watcher != nullptr, "a program opens the link");
            if (watcher) {
                Json boot = mcp.data("run", R"({"from":63488,"until":"*","timeout_ms":4000})");
                CHECK(boot.at("output").str().find("ALTMON") != std::string::npos,
                      "the assistant boots the monitor");
                // A 16K dump is about 74 KB of text -- far past a pseudo-terminal's buffer.
                // The watcher reads NOTHING while it prints.
                Json dump =
                    mcp.data("run", R"({"input":"D00003FFF","until":"3FF0 ","timeout_ms":20000})");
                const std::string want = dump.at("output").str();
                CHECK(want.size() > 60000, "the guest printed a long listing");

                // No more requests. Only the idle pump can move the rest.
                std::string seen;
                uint8_t     buf[4096];
                bool        all = waitFor(
                    [&] {
                        for (size_t r; (r = watcher->read(buf, sizeof buf)) != 0;)
                            seen.append((const char*)buf, r);
                        return seen.find("3FF0 ") != std::string::npos;
                    },
                    10000);
                CHECK(all, "the watcher receives the end of the listing");
                CHECK(seen.find("DUMP 0000 3FFF") != std::string::npos &&
                          seen.find("\r\n2000 ") != std::string::npos,
                      "and the start and the middle of it: nothing was dropped on the way");
            }
        }
        mp.reset();  // the line lives on the machine's board, and goes with it
        CHECK(!fs::is_symlink(link, ec), "the link is removed with the machine");
    }

    SECTION("MCP: CONFIG LOAD via the monitor tool re-adopts the console and --mirror at once");
    {
        // Issue #481: CONFIG LOAD replaces every board, destroying the scripted console line
        // and the --mirror listener riding on it. Only send/recv/run re-adopted the console,
        // so until one of those ran the mirror port was closed and the new console unit was
        // back on the host console -- the JSON-RPC stdin. The monitor tool must re-adopt it
        // itself, before the next tool call of any kind.
        Machine m;
        if (!loadAltmon(m)) return;
        std::string err;

        const std::string toml = tmpPath("altair_mcp_reload.toml");
        {
            Monitor mon(m);
            std::ostringstream os;
            mon.exec("CONFIG SAVE " + toml, os);
            CHECK(!mon.failed(), ("CONFIG SAVE writes the machine: " + os.str()).c_str());
        }

        uint16_t port = freePort();
        CHECK(port != 0, "the OS hands us a free port");
        const std::string mirror = "socket:" + std::to_string(port);

        std::ostringstream s;
        s << R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" << "\n";
        s << R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"monitor",)"
          << R"("arguments":{"command":)" << Json("CONFIG LOAD " + toml).dump() << "}}}\n";
        auto rep = runScript(m, s.str(), mirror);

        CHECK(rep.count(2) && !rep[2].at("result").has("isError"),
              "CONFIG LOAD through the monitor tool succeeded");

        // No send/recv/run was called, yet the console is ours again, mirror and all.
        ScriptedStream* inner = nullptr;
        MirrorStream*   mir   = consoleMirror(m, &inner);
        CHECK(mir != nullptr, "the reloaded machine's console is wrapped in the mirror again");
        CHECK(inner != nullptr, "over the scripted line the tools drive, not the host console");

        // And the port is listening: a watcher can dial in right away.
        if (mir) {
            auto client = platform::connectTcp("127.0.0.1", port, err);
            CHECK(client != nullptr, ("a watcher dials in after the reload: " + err).c_str());
            bool up = waitFor([&] {
                m.pump();
                if (client) client->poll();
                return client && client->established();
            });
            CHECK(up, "the mirror accepts a connection with no send/recv/run in between");
        }
        std::filesystem::remove(toml);
    }

    SECTION("MCP: --log writes what the guest prints to a text file, across a CONFIG LOAD");
    {
        // Issue #666. The log is a text tap on the console the tools drive. console() runs
        // again after a CONFIG LOAD, so it must open the file to APPEND: a truncating open
        // there would wipe the transcript of the session so far.
        const std::string toml = tmpPath("altair_mcp_log.toml");
        const std::string log  = tmpPath("altair_mcp_session.log");
        std::error_code   ec;
        std::filesystem::remove(log, ec);

        auto slurp = [](const std::string& p) {
            std::ifstream     f(p, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        };
        auto count = [](const std::string& hay, const std::string& needle) {
            int n = 0;
            for (size_t at = hay.find(needle); at != std::string::npos;
                 at        = hay.find(needle, at + needle.size()))
                ++n;
            return n;
        };
        const char* boot =
            R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})";

        {
            Machine m;
            if (!loadAltmon(m)) return;
            {
                Monitor            mon(m);
                std::ostringstream os;
                mon.exec("CONFIG SAVE " + toml, os);
                CHECK(!mon.failed(), ("CONFIG SAVE writes the machine: " + os.str()).c_str());
            }

            std::ostringstream s;
            int                id = 0;
            auto req = [&](const char* method, const std::string& params) {
                s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
                  << R"(","params":)" << params << "}\n";
            };
            req("initialize", "{}");
            req("tools/call", boot);
            req("tools/call", R"({"name":"monitor","arguments":{"command":)" +
                                  Json("CONFIG LOAD " + toml).dump() + "}}");
            req("tools/call", boot);
            auto rep = runScript(m, s.str(), "", log);

            const std::string first = rep[2].at("result").at("structuredContent").at("output").str();
            CHECK(first.find("ALTMON") != std::string::npos,
                  "the assistant's run still reads the banner through the tap");
            CHECK(rep.count(3) && !rep[3].at("result").has("isError"), "CONFIG LOAD succeeded");
            CHECK(rep.count(4) && rep[4].at("result").at("structuredContent").at("output").str().find(
                                      "ALTMON") != std::string::npos,
                  "and the reloaded machine boots under the same session");

            const std::string text = slurp(log);
            CHECK(text.find(first) != std::string::npos,
                  "the log holds the guest's output exactly as the run returned it");
            CHECK(count(text, "ALTMON") == 2,
                  "both boots are in the log: the reload did not wipe what came before");

            bool described = false;
            for (const auto& b : m.boards())
                for (const auto& u : b->units())
                    if (ByteStream* st = b->unitStream(u.name))
                        if (st->describe() == "scripted|" + log + "?fmt=text&append")
                            described = true;
            CHECK(described, "the console unit describes itself as scripted|FILE?fmt=text&append");
        }  // the machine closes the log here; only then can Windows delete it

        // With --mirror as well: the tap sits on the mirror, and both are there.
        std::filesystem::remove(log, ec);
        {
            Machine m;
            if (!loadAltmon(m)) return;
            uint16_t port = freePort();
            CHECK(port != 0, "the OS hands us a free port");
            const std::string mirror = "socket:" + std::to_string(port);

            std::ostringstream s;
            s << R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" << "\n";
            s << R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":)" << boot << "}\n";
            auto rep = runScript(m, s.str(), mirror, log);

            ScriptedStream* inner = nullptr;
            MirrorStream*   mir   = consoleMirror(m, &inner);
            CHECK(mir != nullptr && inner != nullptr,
                  "under the log tap the console is still a mirror over the scripted line");
            CHECK(rep[2].at("result").at("structuredContent").at("output").str().find("ALTMON") !=
                      std::string::npos,
                  "the assistant reads the banner through tap and mirror");
            CHECK(slurp(log).find("ALTMON") != std::string::npos, "and the log has it too");
        }
        std::filesystem::remove(log, ec);
        std::filesystem::remove(toml, ec);
    }

    SECTION("MCP: the console stand-in carries the machine's [console] transforms");
    {
        // The bug trgeuy hit on `ps2` (issue #381): under --mcp the console is a headless
        // scripted line, so strip7out/upper -- the console's transform chain -- were bypassed
        // and the assistant read bit-7 parity junk (0x4F 'O' came back 0xCF). The scripted
        // stand-in must wear the same filter a human console would.
        Machine m;
        if (!loadAltmon(m)) return;

        // Turn strip7out on the way a machine file's [console] block does -- the same property
        // seam SET CONSOLE and the TOML loader drive. Save/restore: the Console is a process
        // singleton shared with every other test.
        std::string err;
        bool        saved = false, savedUpper = false;
        auto setConsole = [&err](const char* name, bool on) {
            for (Property& p : Console::instance().properties())
                if (p.name == name) p.set(Value::ofBool(on), err);
        };
        for (Property& p : Console::instance().properties()) {
            if (p.name == "strip7out") saved = p.get().b();
            if (p.name == "upper") savedUpper = p.get().b();
        }
        setConsole("strip7out", true);
        setConsole("upper", false);

        std::ostringstream s;
        s << R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" << "\n";
        s << R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"monitor",)"
          << R"("arguments":{"command":"SHOW CONSOLE"}}})" << "\n";
        auto rep = runScript(m, s.str());  // binds the console to Filter(scripted)

        // SHOW CONSOLE names the stand-in as the console's holder (issue #529): it used to
        // say "(nobody", which read as the transforms being out of the path.
        const std::string show =
            rep.count(2) ? rep[2].at("result").at("content").items().at(0).at("text").str()
                         : std::string();
        CHECK(show.find("(--mcp)") != std::string::npos && show.find("(nobody") == std::string::npos,
              "SHOW CONSOLE under --mcp names the console stand-in as the holder");

        // The console unit's stream is a FilterStream (outermost) over the shared handle
        // (#756) to the scripted line.
        FilterStream*   filt = nullptr;
        ScriptedStream* sc   = nullptr;
        for (const auto& b : m.boards())
            for (const auto& u : b->units()) {
                if (u.kind != UnitKind::Serial) continue;
                if (auto* f = dynamic_cast<FilterStream*>(b->unitStream(u.name))) {
                    filt = f;
                    if (auto* sh = dynamic_cast<SharedStream*>(f->inner()))
                        sc = dynamic_cast<ScriptedStream*>(sh->inner());
                }
            }
        CHECK(filt != nullptr, "the console line is wrapped in the console's transform filter");
        CHECK(sc != nullptr, "and the inner line is the scripted stream the tools drive");

        // A guest byte with bit 7 set is stripped on its way to the assistant's out().
        if (filt && sc) {
            uint8_t hi = 0xCF;  // 'O' (0x4F) with the even-parity bit set -- the ps2 case
            filt->write(&hi, 1);
            CHECK(sc->out() == std::string(1, (char)0x4F),
                  "strip7out reaches the assistant: 0xCF arrives as 0x4F, not junk");

            // A SET CONSOLE made AFTER the binding reaches the guest (issue #529): the
            // stand-in follows the console's settings, it does not hold a copy of them.
            uint8_t c = 0;
            setConsole("upper", true);
            sc->feed("a");
            CHECK(filt->read(&c, 1) == 1 && c == 'A',
                  "SET CONSOLE upper=on mid-session folds what the assistant types");
            setConsole("upper", false);
            sc->feed("a");
            CHECK(filt->read(&c, 1) == 1 && c == 'a',
                  "and upper=off mid-session stops folding it");
        }

        // Restore the singleton for the tests that follow.
        setConsole("strip7out", saved);
        setConsole("upper", savedUpper);
    }

    SECTION("MCP: a RUN via the monitor tool parks instead of wedging the server");
    {
        // A bare RUN -- or the RUN a CONFIG LOAD startup ends in -- would enter the
        // unbounded run loop, and the single-threaded server has no keyboard to press
        // ATTN, so the whole connection would hang forever. Under MCP, RUN must set PC
        // and return. We plant an unconditional JMP-to-self at 0 so the run is genuinely
        // infinite: WITHOUT the fix this test never returns (a hang, not a failed CHECK).
        const BuiltinMachine* altmon = nullptr;
        for (const auto& b : builtinMachines())
            if (std::string(b.name) == "altmon") altmon = &b;
        CHECK(altmon != nullptr, "the altmon built-in is compiled in");
        if (!altmon) return;

        Machine m;
        std::string err;
        CHECK(loadMachine(*altmon, m, err), "altmon loads");

        std::ostringstream s;
        int id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/call", R"({"name":"mem_deposit","arguments":{"addr":0,"bytes":"C3 00 00"}})");
        req("tools/call", R"({"name":"monitor","arguments":{"command":"RUN 0"}})");
        // The server is still alive afterwards -- a later call gets a reply, which it
        // could not if RUN had wedged the read-eval loop.
        req("tools/call", R"({"name":"regs","arguments":{}})");

        auto rep = runScript(m, s.str());  // returns at all == the fix works

        const std::string run = rep[3].at("result").at("content").items().at(0).at("text").str();
        CHECK(run.find("PC set to 0000") != std::string::npos,
              "RUN under MCP parks the PC instead of entering the run loop");
        CHECK(run.find("run tool") != std::string::npos,
              "and it points the client at the non-blocking run tool");
        CHECK(rep.count(4) && rep[4].at("result").has("structuredContent"),
              "the server answered a later call -- RUN returned, it did not wedge");
    }

    SECTION("MCP: TRACE ON <file> via the monitor tool outlives the call that set it (#613)");
    {
        // The `monitor` tool makes a new Monitor for every call. The trace file used to
        // be a member of it, with the debugger holding a bare pointer -- so the file was
        // closed and destroyed as the TRACE ON call returned, and the next cycle wrote
        // through a dangling pointer: a segfault, or a trace file with nothing in it.
        // The debugger owns the file now. WITHOUT the fix this section crashes, or reads
        // an empty file.
        Machine m;
        if (!loadAltmon(m)) return;

        const std::string trace = tmpPath("altair_mcp_trace.txt");
        { std::error_code gone; std::filesystem::remove(trace, gone); }

        std::ostringstream s;
        int id = 0;
        auto tool = [&](const char* name, const std::string& args) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":{"name":")" << name
              << R"(","arguments":)" << args << "}}\n";
        };
        auto monitor = [&](const std::string& command) {
            tool("monitor", R"({"command":)" + Json(command).dump() + "}");
        };
        // MVI A,5A / STA 2000 / HLT
        tool("mem_deposit", R"({"addr":0,"bytes":"3E 5A 32 00 20 76"})");  // 1
        monitor("RUN 0");                                                  // 2: parks the PC
        monitor("TRACE ON " + trace);                                      // 3
        tool("step", R"({"count":2})");                                    // 4
        monitor("TRACE OFF");                                              // 5
        auto rep = runScript(m, s.str());

        auto text = [&](int i) {
            return rep[i].at("result").at("content").items().at(0).at("text").str();
        };
        auto isError = [&](int i) {
            const Json& r = rep[i].at("result");
            return r.has("isError") && r.at("isError").boolean();
        };
        CHECK(rep.count(3) && !isError(3) && text(3).find("trace on") != std::string::npos,
              "TRACE ON <file> is accepted");
        CHECK(rep.count(5), "the server lived through the traced step");

        std::ifstream     f(trace);
        std::stringstream got;
        got << f.rdbuf();
        CHECK(got.str().find("0000 = 3E") != std::string::npos,
              ("the first fetch is in the file: " + got.str()).c_str());
        CHECK(got.str().find("MW   2000 = 5A") != std::string::npos,
              "and the store, so the cycles of a LATER call reached the file");
        f.close();
        // TRACE OFF keeps the file open, and Windows will not delete an open file.
        m.debug.traceTo(nullptr, 0);
        std::error_code ec;
        CHECK(std::filesystem::remove(trace, ec), "the trace file is closed and removed");
    }

    SECTION("MCP: a trace to the console is refused -- there is no console to borrow (#613)");
    {
        // With no file, TRACE ON traces to the monitor's `out`. Under MCP that is the text
        // of one reply, gone when the call returns -- the same dangling pointer. So it is
        // refused, and so is a tracepoint that would default the sink the same way.
        Machine m;
        if (!loadAltmon(m)) return;

        const std::string trace = tmpPath("altair_mcp_tracepoint.txt");
        { std::error_code gone; std::filesystem::remove(trace, gone); }

        std::ostringstream s;
        int id = 0;
        auto tool = [&](const char* name, const std::string& args) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":{"name":")" << name
              << R"(","arguments":)" << args << "}}\n";
        };
        auto monitor = [&](const std::string& command) {
            tool("monitor", R"({"command":)" + Json(command).dump() + "}");
        };
        tool("mem_deposit", R"({"addr":0,"bytes":"3E 5A 32 00 20 76"})");  // 1
        monitor("RUN 0");                                                  // 2
        monitor("TRACE ON");                                               // 3: refused
        monitor("TRACE ON MASK=IN");                                       // 4: refused
        monitor("BREAK 0002 TRACE ON");                                    // 5: refused
        tool("breakpoints", R"({"action":"list"})");                       // 6: none was added
        tool("step", R"({"count":2})");                                    // 7: and nothing dangles
        monitor("TRACE ON " + trace);                                      // 8: name the file...
        monitor("TRACE OFF");                                              // 9
        monitor("BREAK 0002 TRACE ON");                                    // 10: ...and now it arms
        auto rep = runScript(m, s.str());

        auto text = [&](int i) {
            return rep[i].at("result").at("content").items().at(0).at("text").str();
        };
        auto isError = [&](int i) {
            const Json& r = rep[i].at("result");
            return r.has("isError") && r.at("isError").boolean();
        };
        CHECK(isError(3) && text(3).find("bus_trace") != std::string::npos,
              ("TRACE ON with no file is refused, and names bus_trace: " + text(3)).c_str());
        CHECK(isError(4), "a mask alone is still no file");
        CHECK(isError(5) && text(5).find("TRACE ON <file>") != std::string::npos,
              ("a tracepoint with no sink is refused, and says how: " + text(5)).c_str());
        CHECK(text(6).find("no breakpoints") != std::string::npos,
              "the refused tracepoint added no breakpoint");
        CHECK(rep.count(7) && rep[7].at("result").has("structuredContent"),
              "a step after the refusals runs");
        CHECK(!isError(10) && text(10).find("breakpoint") != std::string::npos,
              ("with a file named first, the tracepoint is accepted: " + text(10)).c_str());
        m.debug.traceTo(nullptr, 0);  // close the file so it can be removed (Windows)
        std::error_code ec;
        CHECK(std::filesystem::remove(trace, ec), "the trace file is closed and removed");
    }

    SECTION("MCP: tools/list advertises the structured wrappers");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        auto rep = runScript(m,
            R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})""\n");
        std::map<std::string, bool> seen;
        for (const auto& t : rep[1].at("result").at("tools").items())
            seen[t.at("name").str()] = true;
        for (const char* n : {"step", "disasm", "mem_fill", "mem_search", "mem_save",
                              "breakpoints", "snapshot", "restore", "bus_irq", "bus_trace",
                              "mount", "connect"})
            CHECK(seen[n], (std::string("tools/list carries ") + n).c_str());
    }

    SECTION("MCP: connect lists the endpoints CONNECT takes (#784)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        auto rep = runScript(m,
            R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})""\n");
        std::string desc;
        for (const auto& t : rep[1].at("result").at("tools").items())
            if (t.at("name").str() == "connect")
                desc = t.at("inputSchema").at("properties").at("endpoint").at("description").str();
        CHECK(desc.find(endpointHelp()) != std::string::npos,
              ("the endpoint description carries the CONNECT grammar: " + desc).c_str());
    }

    SECTION("MCP: disasm decodes through a non-invasive peek, with no CPU running");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        // ALTMON's own first bytes at F800 (63488): MVI A,03 / OUT 10. Decoded straight
        // out of ROM before a single instruction executes -- the stateless disassembler.
        auto rep = runScript(m,
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"disasm","arguments":{"addr":63488,"count":2,"cpu":"8080"}}})""\n");
        const Json& d = rep[1].at("result").at("structuredContent");
        const auto& lines = d.at("lines").items();
        CHECK(lines.size() == 2, "disasm returned two lines");
        CHECK(lines.at(0).at("addr").integer() == 63488, "first line is at F800");
        CHECK(lines.at(0).at("text").str() == "MVI A,03", "and decodes MVI A,03");
        CHECK(lines.at(0).at("len").integer() == 2, "a two-byte instruction");
        CHECK(lines.at(1).at("addr").integer() == 63490, "the next line follows by its length");
    }

    SECTION("MCP: step advances the CPU and reports where it rested");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req(R"({"name":"step","arguments":{"count":3}})");
        auto rep = runScript(m, s.str());
        const Json& st = rep[2].at("result").at("structuredContent");
        CHECK(st.at("steps").integer() == 3, "step ran three instructions");
        CHECK(st.has("registers") && st.has("pc") && st.has("t_states"),
              "and reported the register file, pc and T-states");
        CHECK(st.at("stopped").str() == "steps", "it stopped on the count, not a HLT/breakpoint");
    }

    // AN ARGUMENT OF THE WRONG JSON TYPE IS REFUSED, NOT READ AS 0 (#579). JSON has no hex,
    // so "from":"0xFF00" is the natural mistake -- and it used to run from PC 0 with no
    // word. Every tool's arguments are checked against its own inputSchema first.
    SECTION("MCP: an argument of the wrong type, or a missing one, is refused by name (#579)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        // Boot first, so the PC sits in the ROM: a `from` read as 0 would then show.
        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})"); // 1
        req(R"({"name":"run","arguments":{"from":"0xFF00","timeout_ms":100}})");       // 2
        req(R"({"name":"mem_dump","arguments":{"lo":"0x100","hi":271}})");            // 3
        req(R"({"name":"mem_dump","arguments":{"hi":15}})");                           // 4
        req(R"({"name":"mem_deposit","arguments":{"addr":256,"bytes":"00","rom":"true"}})"); // 5
        req(R"({"name":"mem_dump","arguments":{"lo":"256","hi":271}})");               // 6
        req(R"({"name":"regs","arguments":{}})");                                      // 7
        req(R"({"name":"mem_dump","arguments":{"lo":256,"hi":271}})");                 // 8
        auto rep = runScript(m, s.str());

        auto err = [&](int n) {
            const Json& r = rep[n].at("result");
            return r.at("isError").boolean() ? r.at("content").items()[0].at("text").str()
                                             : std::string();
        };
        const long long booted = rep[1].at("result").at("structuredContent").at("pc").integer();
        CHECK(err(2).find("`from` must be a JSON number") != std::string::npos &&
                  err(2).find("65280") != std::string::npos,
              "a hex string for `from` is refused, and the error gives the number to send");
        CHECK(rep[7].at("result").at("structuredContent").at("pc").integer() == booted,
              "...and nothing ran: the PC is where the boot left it, not 0 or FF00");
        CHECK(err(3).find("`lo` must be a JSON number") != std::string::npos &&
                  err(3).find("256") != std::string::npos,
              "mem_dump refuses a string `lo` instead of dumping address 0");
        CHECK(err(4).find("`lo` is required") != std::string::npos,
              "a missing required argument is refused, not read as 0");
        CHECK(err(5).find("`rom` must be true or false") != std::string::npos,
              "a boolean sent as a string is refused, not read as false");
        CHECK(err(6).find("write 256, not \"256\"") != std::string::npos,
              "a decimal string gets told to drop the quotes");
        CHECK(err(8).empty() && rep[8].at("result").at("structuredContent").at("lo").integer() == 256,
              "the same call with numbers works");
    }

    // A HALTED PROCESSOR UNDER MCP (#770), as at the monitor: `from` would move a PC the
    // front panel cannot move, and a run or step of a halt nothing can end would only
    // count idle T-states. Each is a tool error that says RESET ends the halt.
    SECTION("MCP: run, step and start refuse a halted processor until RESET (#770)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"monitor","arguments":{"command":"DEPOSIT 40 F3 76"}})");     // 1 DI;HLT
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":2000}})");          // 2
        req(R"({"name":"run","arguments":{"from":128,"timeout_ms":200}})");          // 3
        req(R"({"name":"run","arguments":{"timeout_ms":200}})");                     // 4
        req(R"({"name":"step","arguments":{"count":1}})");                           // 5
        req(R"({"name":"start","arguments":{"from":128}})");                         // 6
        req(R"({"name":"monitor","arguments":{"command":"RESET"}})");                // 7
        req(R"({"name":"step","arguments":{"count":1}})");                           // 8
        auto rep = runScript(m, s.str());

        CHECK(rep[2].at("result").at("structuredContent").at("stopped").str() == "halt",
              "set up: the run stops on the HLT");
        for (int n : {3, 4, 5, 6}) {
            const Json& r = rep[n].at("result");
            CHECK(r.has("isError") && r.at("isError").boolean() &&
                      r.at("content").items()[0].at("text").str() ==
                          "processor halted at PC=0042 -- RESET ends the halt.",
                  "a run, step or start on a halted processor is a tool error naming RESET");
        }
        CHECK(!rep[8].at("result").has("isError"), "after RESET, step works again");
    }

    // RUN TO AN ADDRESS (#822). `stop_at` is a one-shot PC stop for this call only: it stops
    // before the instruction there runs, a PC already there stops on its next arrival, a
    // real breakpoint met first still wins, and it is gone when the call returns.
    SECTION("MCP: run stop_at stops at an address, once (#822)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        // 0040: NOP ; NOP ; JMP 0040 -- a loop that never comes to the console.
        req(R"({"name":"monitor","arguments":{"command":"DEPOSIT 40 00 00 C3 40 00"}})");  // 1
        req(R"({"name":"run","arguments":{"from":64,"stop_at":66,"timeout_ms":2000}})");   // 2
        req(R"({"name":"run","arguments":{"stop_at":66,"timeout_ms":2000}})");             // 3
        req(R"({"name":"run","arguments":{"max_steps":1000,"timeout_ms":2000}})");         // 4
        req(R"({"name":"monitor","arguments":{"command":"BREAK 41"}})");                   // 5
        req(R"({"name":"run","arguments":{"from":64,"stop_at":66,"timeout_ms":2000}})");   // 6
        req(R"({"name":"monitor","arguments":{"command":"NOBREAK"}})");                    // 7
        req(R"({"name":"step","arguments":{"count":4}})");                                 // 8
        req(R"({"name":"run","arguments":{"from":64,"stop_at":"0x42"}})");                 // 9
        req(R"({"name":"run","arguments":{"from":64,"stop_at":70000}})");                  // 10
        req(R"({"name":"regs","arguments":{}})");                                          // 11
        auto rep = runScript(m, s.str());

        auto st = [&](int n) -> const Json& { return rep[n].at("result").at("structuredContent"); };
        auto err = [&](int n) {
            const Json& r = rep[n].at("result");
            return r.has("isError") && r.at("isError").boolean()
                       ? r.at("content").items()[0].at("text").str()
                       : std::string();
        };
        CHECK(st(2).at("stopped").str() == "stop-at" && st(2).at("pc").integer() == 66 &&
                  st(2).at("steps").integer() == 2,
              "run stops at stop_at, before the JMP there runs");
        CHECK(st(3).at("stopped").str() == "stop-at" && st(3).at("pc").integer() == 66 &&
                  st(3).at("steps").integer() == 3,
              "from the address itself, it stops on the next arrival, once round the loop");
        CHECK(st(4).at("stopped").str() == "steps",
              "the next run without stop_at goes past the old address");
        CHECK(st(6).at("stopped").str() == "breakpoint" && st(6).at("pc").integer() == 65,
              "a breakpoint met before stop_at stops the run first");
        CHECK(st(8).at("stopped").str() == "steps" && st(8).at("steps").integer() == 4,
              "the stop_at a breakpoint pre-empted is gone: a step runs through 0042");
        CHECK(err(9).find("`stop_at` must be a JSON number") != std::string::npos &&
                  err(9).find("66") != std::string::npos,
              "a hex string for stop_at is refused with the number to send");
        CHECK(err(10).find("`stop_at` must be an address from 0 to 65535") != std::string::npos,
              "an address past 64K is refused");
        CHECK(st(11).at("pc").integer() == st(8).at("pc").integer(),
              "...and a refused run moved nothing: `from` was not applied");
    }

    // SET BUS UNCLAIMED=WARN|HALT HOLDS UNDER MCP AS AT THE MONITOR (#580). HALT stops the
    // run with `unclaimed`; WARN returns the warning line in `warnings`; each run re-arms
    // the once-per-port de-dup, so a second run reports the port again.
    SECTION("MCP: run honours SET BUS UNCLAIMED=HALT and WARN (#580)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        // 0040: IN 50 ; JMP 0040 -- a guest polling a port no board in ALTMON decodes.
        req(R"({"name":"monitor","arguments":{"command":"DEPOSIT 40 DB 50 C3 40 00"}})"); // 1
        req(R"({"name":"monitor","arguments":{"command":"SET BUS UNCLAIMED=HALT"}})");    // 2
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":2000}})");               // 3
        req(R"({"name":"monitor","arguments":{"command":"SET BUS UNCLAIMED=WARN"}})");    // 4
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":200}})");                // 5
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":200}})");                // 6
        auto rep = runScript(m, s.str());

        const Json& h = rep[3].at("result").at("structuredContent");
        CHECK(h.at("stopped").str() == "unclaimed", "HALT stops the run as `unclaimed`");
        CHECK(h.at("warnings").items().size() == 1 &&
                  h.at("warnings").items()[0].str().find("IN 50") != std::string::npos,
              "...and returns the warning line naming the port");
        const std::string ht = rep[3].at("result").at("content").items()[0].at("text").str();
        CHECK(ht.find("stopped: IN from port 0x50, which no board decodes") != std::string::npos,
              "...and the text says which port, as the monitor does");

        for (int n : {5, 6}) {
            const Json& w = rep[n].at("result").at("structuredContent");
            CHECK(w.at("stopped").str() != "unclaimed", "WARN does not stop the run");
            CHECK(w.at("warnings").items().size() == 1 &&
                      w.at("warnings").items()[0].str().find("IN 50") != std::string::npos,
                  "WARN returns the line, and each run reports it again");
        }
    }

    // BREAK TAPE STOP stops an MCP run as it stops a monitor RUN -- the same slice-loop gap
    // as #580, found beside it.
    SECTION("MCP: run stops on BREAK TAPE STOP as `tape-stop`");
    {
        const auto tap = std::filesystem::temp_directory_path() / "altairsim-mcp-tapestop.tap";
        {
            std::ofstream f(tap, std::ios::binary);
            for (int i = 0; i < 300; ++i) f.put(char(i & 0xFF));
        }
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        auto mon = [&](const std::string& cmd) {
            req(R"({"name":"monitor","arguments":{"command":")" + cmd + R"("}})");
        };
        std::string path = tap.generic_string();
        mon("BOARDS ADD acr acr0");
        mon("MOUNT acr0:tape \\\"" + path + "\\\"");
        mon("SET acr0:tape stop=0:05");  // 150 bytes in, at 300 baud
        // 0040: IN 06 ; RRC ; JC 0040 ; IN 07 ; JMP 0040 -- read the tape forever.
        mon("DEPOSIT 40 DB 06 0F DA 40 00 DB 07 C3 40 00");
        mon("BREAK TAPE STOP");
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":4000}})");  // 6
        auto rep = runScript(m, s.str());
        std::error_code ec;
        std::filesystem::remove(tap, ec);

        for (int n = 1; n <= 5; ++n)
            CHECK(!rep[n].at("result").at("isError").boolean(), "the tape set-up is accepted");
        CHECK(rep[6].at("result").at("structuredContent").at("stopped").str() == "tape-stop",
              "the run stops at the tape's auto-stop mark");
    }

    SECTION("MCP: run stops on a prompt as idle, and SET cpu0 idle=off keeps it running");
    {
        // ALTMON boots to a monitor prompt and spins on console input. With the CPU card's
        // idle policy on (the default), `run` recognises that spin and hands control back
        // promptly with stopped=idle. `SET cpu0 idle=off` is how a hardware-in-the-loop run
        // says "do not park me" -- the same knob #424's guest set -- and then `run` runs the
        // whole timeout budget instead. Both prove the m.clock.idle() gate on the idle-stop.
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req(R"({"name":"run","arguments":{"timeout_ms":500}})");                 // at the prompt
        req(R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})");
        req(R"({"name":"run","arguments":{"timeout_ms":500}})");                 // no parking now
        auto rep = runScript(m, s.str());

        CHECK(rep[1].at("result").at("structuredContent").at("stopped").str() == "match",
              "the boot run stops on the banner");
        CHECK(rep[2].at("result").at("structuredContent").at("stopped").str() == "idle",
              "at the prompt, idle=on -> run hands back with stopped=idle");
        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "idle=off -> the same prompt spin runs the whole budget (no early idle-stop)");
    }

    SECTION("MCP: a live device on a line defers the idle-stop by a wall-clock grace");
    {
        // Same ALTMON prompt spin -- but with a real wire connected (loopback on the 2SIO's
        // second channel), "quiet" might just be the guest between a request and a reply that
        // lands hundreds of ms later. So the idle-stop is not taken until the wire has been
        // silent for a wall-clock grace (seconds), far longer than this run's 500 ms budget --
        // where the no-device run above stopped=idle almost at once, this one runs the budget
        // out. This grace is what stops a request/reply gap or a between-blocks pause on a real
        // device from being mistaken for a finished prompt (#424); it is measured in WALL time,
        // so it holds at any clock_hz. It is scoped to idle-stop ONLY -- since #487, it does NOT
        // extend timeout_ms itself; see the next section for that boundary.
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"connect","arguments":{"id":"sio0","unit":"b","endpoint":"loopback"}})");
        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req(R"({"name":"run","arguments":{"timeout_ms":500}})");  // at the prompt, device on line
        auto rep = runScript(m, s.str());

        CHECK(rep[3].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "with a device on a line the prompt spin is not called idle within the budget");
    }

    SECTION("MCP: timeout_ms is a strict wall-clock ceiling even while a live wire keeps "
            "talking (#487)");
    {
        // #487: a version of this loop let a live wire's traffic renew the DEADLINE itself, not
        // just defer idle-stop (the section above) -- so a peer that said anything at all,
        // however slowly, kept `run` going past its budget indefinitely, bounded only by a
        // 10-minute absolute cap. A real TCP peer here writes one byte every 150 ms for 1.5 s
        // straight -- comfortably inside the 5 s grace idle-stop still uses -- against a 400 ms
        // budget. Pre-fix this call would not have returned until ~5 s after the writer's last
        // byte (~6.5 s total); fixed, it must return at ~400 ms regardless of the traffic.
        Machine m;
        if (!loadAltmon(m)) return;
        std::string err;

        uint16_t port = freePort();
        CHECK(port != 0, "the OS hands us a free port");

        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"connect","arguments":{"id":"sio0","unit":"b","endpoint":"socket:)" +
            std::to_string(port) + R"("}})");
        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req(R"({"name":"run","arguments":{"timeout_ms":400}})");  // the call under test

        // The peer connects AFTER the "connect" request above binds the listener, which happens
        // mid-script inside the runScript() call below -- so it races that bind and retries.
        std::atomic<bool> stopFeed{false};
        std::thread feeder([&] {
            std::string ferr;
            std::unique_ptr<platform::TcpConn> client;
            for (int i = 0; i < 400 && !client && !stopFeed; ++i) {
                client = platform::connectTcp("127.0.0.1", port, ferr);
                if (!client) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!client) return;
            for (int i = 0; i < 200 && !client->established() && !stopFeed; ++i) {
                client->poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const uint8_t byte = 'X';
            for (int i = 0; i < 10 && !stopFeed; ++i) {
                client->poll();
                client->write(&byte, 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        });

        const auto t0  = std::chrono::steady_clock::now();
        auto       rep = runScript(m, s.str());
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
        stopFeed = true;
        feeder.join();

        CHECK(rep[3].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "the budget run stops on timeout_ms, not idle or anything else");
        CHECK(elapsedMs < 1200,
              ("timeout_ms:400 held under continuous live-wire traffic (took " +
               std::to_string(elapsedMs) +
               "ms; the pre-#487 grace would have run past 1500ms of writes plus a further "
               "5s of post-traffic silence)")
                  .c_str());
    }

    SECTION("MCP: a SIGINT to the process interrupts a wedged run (#488)");
    {
        // #488: `--mcp`'s stdin IS the JSON-RPC channel, so a caller has no in-band way to
        // stop a run that will not reach `until`/idle/timeout on its own -- unlike a piped
        // monitor RUN, which already had SigintGuard (core/debug.h). Proves the guard is now
        // installed for the whole of runMcp too: a stop request lands mid-call (simulated with
        // std::raise, the portable way to invoke the currently-installed handler without
        // depending on OS-level signal delivery -- the handler itself, and the atomic flag it
        // sets, are exactly what a real SIGINT would drive) and the run returns almost at once
        // with stopped:"interrupted", nowhere near its multi-second budget. The trailing run
        // also proves `Debugger::clearStopRequest()` at the top of each call means the flag does
        // not bleed into the next one.
        Machine m;
        if (!loadAltmon(m)) return;

        auto r = runWithSigint(
            m,
            {R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})",
             R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})"},
            R"({"name":"run","arguments":{"timeout_ms":4000}})",          // the call under test
            {R"({"name":"run","arguments":{"timeout_ms":500}})"});        // stale-flag check
        auto& rep = r.replies;

        CHECK(rep[3].at("result").at("structuredContent").at("stopped").str() == "interrupted",
              "the 4000ms-budget run stops on the SIGINT, not idle or timeout");
        CHECK(r.elapsedMs < 1500,
              ("the stop request landed well inside the 4000ms budget (took " +
               std::to_string(r.elapsedMs) + "ms)")
                  .c_str());
        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "clearStopRequest() means the next run() does not inherit a stale flag -- idle=off "
              "still set, so it runs its own budget out exactly like the no-stop case");
    }

    SECTION("MCP: a SIGINT is not lost in the pacing sleep between slices (#488)");
    {
        // The case the flat-out test above cannot see. With a clock_hz set, `run` sleeps
        // between slices to pace the crystal, so most of its wall time is spent OUTSIDE
        // Debugger::run() -- and run() clears the stop-request flag as it enters. Before the
        // Debugger::stopRequested() check at the top of the loop, a ^C landing in that sleep
        // was wiped by the next slice: five of eight trials here returned `timeout` having
        // ignored the signal outright. Repeat the trial, because "usually stops" is exactly
        // the bug -- a cancellation that works two times in three is not a cancellation.
        for (int trial = 0; trial < 6; ++trial) {
            Machine m;
            if (!loadAltmon(m)) return;

            auto rep = runWithSigint(
                m,
                {R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})",
                 R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})",
                 R"({"name":"monitor","arguments":{"command":"SET cpu0 clock_hz=2000000"}})"},
                R"({"name":"run","arguments":{"timeout_ms":3000}})",  // the call under test
                {}).replies;

            CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "interrupted",
                  ("a paced run stops on the SIGINT every time, not just when it happens to "
                   "land inside a slice (trial " + std::to_string(trial) + ")").c_str());
        }
    }

    SECTION("MCP: notifications/cancelled interrupts the matching in-flight run, and other "
            "requests queue behind it correctly (#488)");
    {
        // #488, part 2: with a static istringstream (runScript's usual approach) every line
        // is available to the reader thread at once, so a cancellation would race straight
        // past the `run` it targets before that call even starts -- nothing paces it against
        // real time the way a live client's stdin does. FeedBuf (above) fixes that: this test
        // controls exactly when each line becomes visible, matching how a real client would
        // actually drive this (send the call, then LATER send the cancellation once something
        // -- a user hitting Ctrl-C in their own tool -- decides to cancel it).
        //
        // Also proves the other half of the reader/worker split in the same flow: `regs` and
        // `recv`, fed WHILE the long run is still executing, must queue and still be answered
        // correctly once the worker frees up -- a busy worker must never block the reader from
        // accepting more requests, only from EXECUTING them out of order.
        Machine m;
        if (!loadAltmon(m)) return;

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);

        std::thread worker([&] { runMcp(m, feedIn, out, ""); });

        int  id  = 0;
        auto req = [&](const std::string& params) {
            std::ostringstream line;
            line << R"({"jsonrpc":"2.0","id":)" << ++id
                 << R"(,"method":"tools/call","params":)" << params << "}\n";
            feedBuf.feed(line.str());
        };

        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req(R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})");

        // Wait for the setup to ANSWER before sending the call to be cancelled: a cancel is
        // matched against the request in flight, so one that arrives while the guest is still
        // booting names an id that is not current yet and is dropped -- which is right, and
        // would make this a test of how fast the runner is rather than of the cancel.
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(2) != 0; }, 30000),
              "the setup calls answered before the run to be cancelled was sent");

        CHECK(waitForWorker(feedBuf, sinkBuf, false),
              "the worker is idle again before the run to be cancelled is fed");
        req(R"({"name":"run","arguments":{"timeout_ms":4000}})");  // id 3 -- will be cancelled
        // A cancel is matched against `currentId`, so id 3 must BE the in-flight request
        // before it is sent -- not merely fed. Waiting on that is the difference between
        // testing the cancel and testing how promptly the runner scheduled a thread.
        CHECK(waitForWorker(feedBuf, sinkBuf, true), "id 3 is the in-flight request");

        // Queued immediately behind the still-running id 3 -- proves the reader thread is not
        // blocked by a busy worker, regardless of when the worker actually gets to them.
        req(R"({"name":"regs","arguments":{}})");  // id 4
        req(R"({"name":"recv","arguments":{}})");  // id 5

        const auto t0 = std::chrono::steady_clock::now();
        feedBuf.feed(R"({"jsonrpc":"2.0","method":"notifications/cancelled",)"
                     R"("params":{"requestId":3}})"
                     "\n");

        // Stop the clock when id 3 ANSWERS. Measuring past here would bill the cancel for
        // id 6's own 500ms budget and the join, which have nothing to do with how promptly
        // the cancel landed.
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(3) != 0; }, 30000),
              "the cancelled run answered");
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();

        req(R"({"name":"run","arguments":{"timeout_ms":500}})");  // id 6 -- stale-flag check
        feedBuf.close();
        worker.join();

        std::map<int, Json> rep = repliesById(sinkBuf.text());

        CHECK(rep[3].at("result").at("structuredContent").at("stopped").str() == "interrupted",
              "notifications/cancelled for id 3 stops the matching in-flight run");
        CHECK(elapsedMs < 1500,
              ("the cancel landed well inside the 4000ms budget (took " +
               std::to_string(elapsedMs) + "ms)")
                  .c_str());
        CHECK(rep[4].at("result").at("structuredContent").has("pc"),
              "regs, queued behind the busy run, still answers once the worker is free");
        CHECK(rep[5].at("result").at("structuredContent").has("output"),
              "recv, queued right behind it, answers too -- in order, not dropped");
        CHECK(rep[6].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "id 6 runs its own budget out normally -- the cancel for id 3 does not leak "
              "into a later, unrelated request");
    }

    SECTION("MCP: status answers out of band, from a published slice-boundary snapshot, "
            "even while a run is mid-flight (#490)");
    {
        // #490: adding `status` as an ordinary tool would not make it non-blocking -- it
        // would just queue behind the very run it exists to report on, exactly like `regs`/
        // `recv` in the section above. This proves the actual fix: `status` is answered by
        // the READER thread, straight from the snapshot the `run` tool publishes at each
        // slice boundary (RunSnapshot, statusResult()), so it lands even while a long run
        // still sits busy in the worker.
        Machine m;
        if (!loadAltmon(m)) return;

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        // SinkBuf, not an ostringstream: waitForWorker() reads this WHILE the server is
        // writing it, and that is a data race on a bare stringstream.
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);

        std::thread worker([&] { runMcp(m, feedIn, out, ""); });

        int  id  = 0;
        auto req = [&](const std::string& params) {
            std::ostringstream line;
            line << R"({"jsonrpc":"2.0","id":)" << ++id
                 << R"(,"method":"tools/call","params":)" << params << "}\n";
            feedBuf.feed(line.str());
        };

        req(R"({"name":"status","arguments":{}})");  // id 1 -- before anything has run
        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req(R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})");

        // The setup has to FINISH before id 4 is fed. The boot (id 2) and the SET (id 3) are
        // calls too, so while they run `in_flight` is already true -- wait for "busy" with them
        // still queued and it is the boot that answers, not the run this section is about.
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(3) != 0; }, 30000),
              "the setup calls answered before the long run was fed");
        CHECK(waitForWorker(feedBuf, sinkBuf, false), "the worker is idle before id 4 is fed");

        // id 4: flat out, no `until`, idle disabled -- runs its full 1500ms budget, giving a
        // wide window (two 300ms-spaced polls below, with 900ms of margin left over) to poll
        // `status` while it is genuinely still executing. 1500ms proves the same thing as a
        // longer budget without adding several seconds to every run of this suite.
        req(R"({"name":"run","arguments":{"timeout_ms":1500}})");

        // Poll only once id 4 is genuinely the in-flight call. The 300ms BETWEEN the two
        // polls is real and deliberate -- `steps`/`generation` have to be given time to
        // advance -- but "id 4 has started" is a fact to read off the server, not to sleep
        // for (waitForWorker, above).
        CHECK(waitForWorker(feedBuf, sinkBuf, true), "id 4 is the in-flight call before it is polled");
        req(R"({"name":"status","arguments":{}})");  // id 5 -- id 4 is still running
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        req(R"({"name":"status","arguments":{}})");  // id 6 -- later still, id 4 still running

        feedBuf.close();
        worker.join();

        std::map<int, Json> rep;
        std::vector<int>    order;  // the order replies were WRITTEN, not the order requested
        std::istringstream  lines(sinkBuf.text());
        std::string         line;
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            Json        j;
            std::string err;
            if (Json::parse(line, j, err)) {
                int rid = (int)j.at("id").integer();
                rep[rid] = j;
                order.push_back(rid);
            }
        }

        const Json& s1 = rep[1].at("result").at("structuredContent");
        CHECK(!s1.at("in_flight").boolean() && s1.at("steps").integer() == 0,
              "status before any run has happened: idle, zero steps");
        CHECK(!s1.at("board").str().empty(),
              "status knows the CPU board's id even before the first run (#490)");

        const Json& s5 = rep[5].at("result").at("structuredContent");
        const Json& s6 = rep[6].at("result").at("structuredContent");
        CHECK(s5.at("in_flight").boolean() && s6.at("in_flight").boolean(),
              "status reports in_flight while the long run (id 4) is still executing");
        CHECK(s6.at("steps").integer() > s5.at("steps").integer(),
              "the published snapshot keeps advancing between two polls of the same live run");
        CHECK(s6.at("generation").integer() > s5.at("generation").integer(),
              "the generation counter advances too, so a poller can tell 'still running' from "
              "'stuck on the same slice'");

        auto posOf = [&](int rid) {
            return (size_t)(std::find(order.begin(), order.end(), rid) - order.begin());
        };
        CHECK(posOf(5) < posOf(4) && posOf(6) < posOf(4),
              "both mid-run status replies (id 5, id 6) are WRITTEN before id 4's own reply -- "
              "proof they were answered out of band, not queued behind it");

        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "the long run (idle=off, no until) still runs its own budget out normally -- "
              "status polling alongside it changes nothing about how it stops");
    }

    SECTION("MCP: status reports in_flight for a long NON-run tool too, not just run (#490)");
    {
        // The case `status` exists to catch, and the one a `run`-only in_flight was blind
        // to: the worker is wedged inside some OTHER tool. `monitor STEP <n>` is that tool
        // here -- it advances the guest inside the worker for about a second and publishes
        // no snapshot at all, so `generation` stays 0 while it runs. An `in_flight` sourced
        // from the run snapshot would say "idle" throughout.
        Machine m;
        if (!loadAltmon(m)) return;

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        // SinkBuf, not an ostringstream: waitForWorker() reads this WHILE the server is
        // writing it, and that is a data race on a bare stringstream.
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);

        std::thread worker([&] { runMcp(m, feedIn, out, ""); });

        int  id  = 0;
        auto req = [&](const std::string& params) {
            std::ostringstream line;
            line << R"({"jsonrpc":"2.0","id":)" << ++id
                 << R"(,"method":"tools/call","params":)" << params << "}\n";
            feedBuf.feed(line.str());
        };

        req(R"({"name":"status","arguments":{}})");  // id 1 -- nothing running yet
        // id 2: ~10M single steps through the debugger, roughly a second of worker time.
        req(R"({"name":"monitor","arguments":{"command":"STEP 10000000"}})");
        CHECK(waitForWorker(feedBuf, sinkBuf, true),
              "id 2 is the in-flight call -- read off the server, not slept for");
        req(R"({"name":"status","arguments":{}})");  // id 3 -- id 2 is still stepping

        feedBuf.close();
        worker.join();

        std::map<int, Json> rep;
        std::vector<int>    order;
        std::istringstream  lines(sinkBuf.text());
        std::string         line;
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            Json        j;
            std::string err;
            if (Json::parse(line, j, err)) {
                rep[(int)j.at("id").integer()] = j;
                order.push_back((int)j.at("id").integer());
            }
        }

        const Json& s1 = rep[1].at("result").at("structuredContent");
        const Json& s3 = rep[3].at("result").at("structuredContent");
        CHECK(!s1.at("in_flight").boolean(), "status is idle before the monitor call starts");
        CHECK(s3.at("in_flight").boolean(),
              "status reports in_flight while the worker is inside a long `monitor`, not a run");
        CHECK(s3.at("generation").integer() == 0,
              "and it says so with no run snapshot ever published -- in_flight cannot be "
              "coming from the `run` tool's own bookkeeping");

        auto posOf = [&](int rid) {
            return (size_t)(std::find(order.begin(), order.end(), rid) - order.begin());
        };
        CHECK(posOf(3) < posOf(2),
              "the mid-monitor status reply is written before the monitor's own -- answered "
              "out of band, not queued behind it");
    }

    SECTION("MCP: a stop request that lands between the loop's check and the slice is not "
            "erased");
    {
        // The slice used to clear the stop-request flag on entry, so a cancel or ^C landing just
        // after the loop's own check was wiped unseen and the run served its whole budget --
        // `timeout`, where the client had asked it to stop. Windows CI hit it on #506 and
        // #508; 5000 tries there lost 9 cancels before the fix and none after. GapBoard (above)
        // puts the stop request in that window every time, so this fails every time without the
        // fix rather than once in 550.
        Machine m;
        if (!loadAltmon(m)) return;
        auto gb = std::make_unique<GapBoard>();
        gb->id  = "gap0";
        GapBoard* gap = gb.get();
        m.adopt(std::move(gb));

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);
        std::thread  worker([&] { runMcp(m, feedIn, out, ""); });

        feedBuf.feed(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"run",)"
                     R"("arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}}})"
                     "\n");
        feedBuf.feed(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"monitor",)"
                     R"("arguments":{"command":"SET cpu0 idle=off"}}})"
                     "\n");
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(2) != 0; }, 30000),
              "the setup answered before the board was armed");
        CHECK(waitForWorker(feedBuf, sinkBuf, false), "the worker is idle when the board is armed");

        gap->armed = true;
        feedBuf.feed(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"run",)"
                     R"("arguments":{"timeout_ms":1000}}})"
                     "\n");
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(3) != 0; }, 30000),
              "the run answered");
        feedBuf.close();
        worker.join();

        CHECK(!gap->armed, "the board did fire -- the loop read rxBytes() during the run");
        const std::string st =
            repliesById(sinkBuf.text())[3].at("result").at("structuredContent").at("stopped").str();
        CHECK(st == "interrupted",
              ("a stop request raised between the check and the slice stops the run (stopped=" +
               st + ")")
                  .c_str());
    }

    SECTION("MCP: mem_fill, mem_search and mem_save round-trip through the bus");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::string hex = tmpPath("altair_mcp_dump.hex");
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"mem_fill","arguments":{"lo":8192,"hi":8207,"byte":171}})");        // AB x16
        req(R"({"name":"mem_deposit","arguments":{"addr":8200,"bytes":"DE AD BE EF"}})");
        req(R"({"name":"mem_search","arguments":{"lo":0,"hi":16384,"bytes":"DE AD BE EF"}})");
        req(R"({"name":"mem_save","arguments":{"path":")" + hex + R"(","lo":8192,"hi":8207}})");
        req(R"({"name":"mem_fill","arguments":{"lo":8192,"hi":8207,"byte":0}})");           // wipe
        req(R"({"name":"mem_load","arguments":{"path":")" + hex + R"("}})");                 // reload
        req(R"({"name":"mem_dump","arguments":{"lo":8200,"hi":8203}})");
        auto rep = runScript(m, s.str());

        CHECK(rep[1].at("result").at("structuredContent").at("written").integer() == 16,
              "mem_fill wrote sixteen cells");
        CHECK(rep[1].at("result").at("structuredContent").at("discarded").integer() == 0,
              "all sixteen landed (RAM, not ROM)");
        const Json& srch = rep[3].at("result").at("structuredContent");
        CHECK(srch.at("count").integer() == 1 && srch.at("matches").items().at(0).integer() == 8200,
              "mem_search finds the deposited pattern at 8200");
        CHECK(rep[4].at("result").at("structuredContent").at("format").str() == "HEX",
              "mem_save chose HEX from the .hex name");
        const auto& bytes = rep[7].at("result").at("structuredContent").at("bytes").items();
        CHECK(bytes.size() == 4 && bytes.at(0).integer() == 0xDE && bytes.at(3).integer() == 0xEF,
              "the saved HEX reloaded byte-for-byte after a wipe");
        std::filesystem::remove(hex);
    }

    SECTION("MCP: breakpoints add, list and remove");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"breakpoints","arguments":{"action":"add","kind":"pc","lo":256}})");
        req(R"({"name":"breakpoints","arguments":{}})");                     // list
        req(R"({"name":"breakpoints","arguments":{"action":"remove","id":1}})");
        req(R"({"name":"breakpoints","arguments":{}})");                     // list again
        auto rep = runScript(m, s.str());

        const Json& added = rep[1].at("result").at("structuredContent");
        CHECK(added.at("kind").str() == "pc" && added.at("lo").integer() == 256,
              "add returns the new PC breakpoint at 0100");
        CHECK(rep[2].at("result").at("structuredContent").at("breakpoints").items().size() == 1,
              "the list shows one breakpoint");
        CHECK(rep[4].at("result").at("structuredContent").at("breakpoints").items().empty(),
              "and none after remove");
    }

    SECTION("MCP: snapshot then restore round-trips machine state");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::string path = tmpPath("altair_mcp.state");
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"snapshot","arguments":{"path":")" + path + R"("}})");
        req(R"({"name":"restore","arguments":{"path":")" + path + R"("}})");
        auto rep = runScript(m, s.str());
        CHECK(rep[1].at("result").at("structuredContent").at("ok").boolean(), "snapshot wrote");
        CHECK(rep[2].at("result").at("structuredContent").at("ok").boolean(),
              "restore read it back into the matching machine");
        CHECK(rep[2].at("result").at("structuredContent").at("not_restored").items().empty(),
              "and restored every board");
        std::filesystem::remove(path);
    }

    SECTION("MCP: bus_irq and bus_trace are well-formed");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"run","arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}})");
        req(R"({"name":"bus_irq","arguments":{}})");
        req(R"({"name":"bus_trace","arguments":{"count":8}})");
        auto rep = runScript(m, s.str());

        const Json& irq = rep[2].at("result").at("structuredContent");
        CHECK(irq.has("int_pending") && irq.at("vi_lines").items().size() == 8,
              "bus_irq reports pINT and all eight VI wires");
        const Json& tr = rep[3].at("result").at("structuredContent");
        CHECK(!tr.at("cycles").items().empty(), "bus_trace holds cycles from the run");
        const Json& c0 = tr.at("cycles").items().at(0);
        CHECK(c0.has("addr") && c0.has("type") && c0.at("master").str() == "cpu",
              "a cycle carries an address, a type and its master");
    }

    SECTION("MCP: connect wires a serial unit; mount rejects an unknown board");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        // The 2SIO's second channel is 'b' -- wire it to a loopback endpoint.
        req(R"({"name":"connect","arguments":{"id":"sio0","unit":"b","endpoint":"loopback"}})");
        req(R"({"name":"mount","arguments":{"id":"nope","unit":"x","path":"/dev/null"}})");
        auto rep = runScript(m, s.str());
        const Json& con = rep[1].at("result").at("structuredContent");
        CHECK(con.at("id").str() == "sio0" && con.at("endpoint").str() == "loopback",
              "connect reports the wiring it made");
        CHECK(rep[2].at("result").has("isError") && rep[2].at("result").at("isError").boolean(),
              "mount on an unknown board is an error, not a silent no-op");
    }

    SECTION("MCP: mount takes media, for a blank disk that needs it (#777)");
    {
        setMediaResolver(openHostFile);
        const auto dir = std::filesystem::temp_directory_path() / "altairsim-mcp-media-777";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        // generic_string(): a Windows `C:\Users\...` is not a JSON string (`\U`).
        const std::string blank = (dir / "b.dsk").generic_string(),
                          bare  = (dir / "c.dsk").generic_string();

        Machine m;
        if (!loadAltmon(m)) return;
        std::ostringstream add;
        Monitor(m).exec("BOARDS ADD disk1a fd0", add);
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"mount","arguments":{"id":"fd0","unit":"Drive1","path":")" + blank +
            R"(","create":true,"media":"5ds"}})");
        req(R"({"name":"mount","arguments":{"id":"fd0","unit":"drive2","path":")" + bare +
            R"(","create":true}})");
        req(R"({"name":"mount","arguments":{"id":"sio0","unit":"a","path":")" + bare +
            R"(","media":"5ds"}})");
        auto rep = runScript(m, s.str());
        CHECK(!rep[1].at("result").has("isError"), "mount with media = 5ds mounts a blank");
        CHECK(rep[2].at("result").has("isError") &&
                  rep[2].at("result").at("content").items().at(0).at("text").str().find("set `media`") !=
                      std::string::npos,
              "...and without it the blank is refused, with the key to set");
        CHECK(rep[3].at("result").has("isError") &&
                  rep[3].at("result").at("content").items().at(0).at("text").str().find("media") !=
                      std::string::npos,
              "a unit that takes no media refuses it");
        std::string uerr;
        if (Board* fd = m.find("fd0")) fd->unmount("drive1", uerr);
        std::filesystem::remove_all(dir, ec);
    }

    SECTION("MCP: start leaves the guest running between calls; the other tools still work; "
            "stop ends it (#602)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        LiveMcp mcp(m);

        const Json st = mcp.data("start", R"({"from":63488})");
        CHECK(st.at("running").boolean(), "start reports the guest running");

        // No `run` from here on: the guest must reach its prompt by itself.
        std::string got;
        CHECK(waitFor([&] {
                  got += mcp.data("recv").at("output").str();
                  return got.find("ALTMON") != std::string::npos;
              }, 10000),
              "the guest printed its banner with no run call -- it ran between the recvs");

        const uint64_t s1 = (uint64_t)mcp.data("status").at("steps").integer();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const Json     status = mcp.data("status");
        CHECK(status.at("running").boolean(), "status says the guest is running");
        CHECK((uint64_t)status.at("steps").integer() > s1,
              "the guest went on running while no call was made at all");

        mcp.call("send", R"({"text":"?\r"})");
        std::string reply;
        CHECK(waitFor([&] {
                  reply += mcp.data("recv").at("output").str();
                  return reply.find('*') != std::string::npos;
              }, 10000),
              "a line typed with send is read and answered by the running guest");

        CHECK(LiveMcp::isError(mcp.call("run", R"({"timeout_ms":200})")),
              "run is refused while the guest free-runs");
        CHECK(LiveMcp::isError(mcp.call("step")), "step is refused while the guest free-runs");
        CHECK(LiveMcp::isError(mcp.call("start")), "a second start is refused");
        CHECK(mcp.data("regs").has("pc"), "regs answers between two slices");

        const Json stop = mcp.data("stop");
        CHECK(stop.at("stopped").str() == "requested" && stop.at("was_running").boolean(),
              "stop ends the free run and says so");
        CHECK(stop.at("steps").integer() > 0 && stop.at("t_states").integer() > 0,
              "stop reports what the whole free run did");
        const Json after = mcp.data("status");
        CHECK(!after.at("running").boolean() && after.at("stop_reason").str() == "requested",
              "status says the free run ended, and why");
        CHECK(!mcp.data("stop").at("was_running").boolean(),
              "a second stop reports the same stop, not a new one");
        CHECK(!LiveMcp::isError(mcp.call("run", R"({"timeout_ms":200})")),
              "run works again once the free run is stopped");
    }

    SECTION("MCP: a free run ends by itself at a HLT and at a breakpoint (#602)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        LiveMcp mcp(m);

        mcp.call("mem_deposit", R"({"addr":256,"bytes":"F3 76"})");  // DI; HLT
        mcp.call("start", R"({"from":256})");
        CHECK(waitFor([&] { return !mcp.data("status").at("running").boolean(); }),
              "the free run ended by itself");
        CHECK(mcp.data("status").at("stop_reason").str() == "halt", "... at the HLT");
        const Json stop = mcp.data("stop");
        CHECK(!stop.at("was_running").boolean() && stop.at("stopped").str() == "halt",
              "stop after the fact reports the HLT");

        // Only an interrupt or RESET ends a HLT -- setting the PC does not, for `start` as for
        // `run` and on the real 8080 -- so press RESET before the next program.
        mcp.call("reset", R"({"kind":"bus"})");
        mcp.call("mem_deposit", R"({"addr":512,"bytes":"00 00 C3 00 02"})");  // NOP NOP JMP 0200
        mcp.call("breakpoints", R"({"action":"add","kind":"pc","lo":513})");
        mcp.call("start", R"({"from":512})");
        CHECK(waitFor([&] { return !mcp.data("status").at("running").boolean(); }),
              "the free run ended by itself again");
        const Json s = mcp.data("status");
        CHECK(s.at("stop_reason").str() == "breakpoint" && s.at("pc").integer() == 513,
              "... at the breakpoint, with the PC on it");
    }

    SECTION("MCP: a SIGINT ends a free run, and is not left standing for the next call (#602)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        LiveMcp mcp(m);

        mcp.call("start", R"({"from":63488})");
        // raise() lands inside runMcp's SigintGuard: the server is up and answering.
        std::raise(SIGINT);
        CHECK(waitFor([&] { return !mcp.data("status").at("running").boolean(); }),
              "the ^C ended the free run");
        CHECK(mcp.data("status").at("stop_reason").str() == "interrupted",
              "... reported as interrupted");
        CHECK(mcp.data("run", R"({"timeout_ms":300})").at("stopped").str() != "interrupted",
              "the ^C was consumed -- the next run is not killed by it");

        // A ^C that lands just as a request is dispatched: the dispatch clears a STALE stop
        // flag, and must not clear this one, which is for the free run.
        mcp.call("start");
        std::raise(SIGINT);
        mcp.call("regs");
        CHECK(waitFor([&] { return !mcp.data("status").at("running").boolean(); }),
              "a ^C beside a dispatched request still ends the free run");
        CHECK(mcp.data("status").at("stop_reason").str() == "interrupted",
              "... as interrupted, not lost");
    }

    SECTION("MCP: a paced free run keeps to its crystal and still answers at once; EOF ends "
            "the session (#602)");
    {
        Machine m;
        if (!loadAltmon(m)) return;
        LiveMcp mcp(m);

        // At 1 kHz one 2,000-instruction slice is many SECONDS of crystal time, so after the
        // first slice the worker waits a long while before the next -- and must wait for a
        // request, not sleep through it.
        mcp.call("monitor", R"({"command":"SET cpu0 clock_hz=1000"})");
        mcp.call("start", R"({"from":63488})");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(mcp.data("regs").has("pc"), "regs answers during the pacing wait");
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        CHECK(ms < 1000, ("the request was not held for the pacing wait (took " +
                          std::to_string(ms) + "ms)").c_str());
        const Json stop = mcp.data("stop");
        CHECK(stop.at("t_states").integer() <= 2 * 2000 * 18,
              ("at 1 kHz the guest ran a slice or two, not flat out (" +
               std::to_string(stop.at("t_states").integer()) + " T)").c_str());

        mcp.call("monitor", R"({"command":"SET cpu0 clock_hz=0"})");
        mcp.call("start");
        std::atomic<bool> done{false};
        std::thread       closer([&] { mcp.close(); done = true; });
        CHECK(waitFor([&] { return done.load(); }, 5000),
              "EOF on stdin ends the session while the guest free-runs");
        closer.join();
    }

    SECTION("MCP: run and the free run report achieved_hz (#903)");
    {
        // SHOW cpu0's achieved_hz row, read through the monitor tool -- on the worker, so
        // nothing reads the card from another thread. -1 when the row is not there.
        auto achieved = [](LiveMcp& mcp) -> long long {
            const Json r = mcp.call("monitor", R"({"command":"SHOW cpu0"})");
            const std::string text = r.at("content").items().at(0).at("text").str();
            const size_t at = text.find("achieved_hz");
            if (at == std::string::npos) return -1;
            return std::atoll(text.c_str() + at + std::strlen("achieved_hz"));
        };
        const char* loop = R"({"addr":256,"bytes":"C3 00 01"})";  // JMP 0100

        {
            Machine m;
            if (!loadAltmon(m)) return;
            LiveMcp mcp(m);
            CHECK(achieved(mcp) == 0, "achieved_hz reads 0 before the guest runs");
            mcp.call("mem_deposit", loop);
            const Json run = mcp.data("run", R"({"from":256,"timeout_ms":300})");
            CHECK(run.at("stopped").str() == "timeout", "the loop runs until the timeout");
            const long long hz = achieved(mcp);
            CHECK(hz > 0, ("after a `run`, achieved_hz reports the rate (" +
                           std::to_string(hz) + ")").c_str());
        }
        {
            Machine m;
            if (!loadAltmon(m)) return;
            LiveMcp mcp(m);
            CHECK(achieved(mcp) == 0, "a new machine reads 0 again");
            mcp.call("mem_deposit", loop);
            mcp.call("start", R"({"from":256})");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            CHECK(mcp.data("stop").at("stopped").str() == "requested", "stop ends the free run");
            const long long hz = achieved(mcp);
            CHECK(hz > 0, ("after a free run, achieved_hz reports the rate (" +
                           std::to_string(hz) + ")").c_str());
        }
    }

    SECTION("MCP: CONFIG SAVE writes the console, not the session's line (#873)");
    {
        const std::string toml = tmpPath("altair_mcp_save.toml");
        const std::string log  = tmpPath("altair_mcp_save.log");
        auto slurp = [](const std::string& p) {
            std::ifstream     f(p, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        };
        auto monitor = [](int id, const std::string& command) {
            return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
                   R"(,"method":"tools/call","params":{"name":"monitor","arguments":{"command":)" +
                   Json(command).dump() + "}}}\n";
        };
        const std::string init = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" "\n";

        // Bare: the holder saves as `console`. A unit the operator set to `scripted` does not.
        {
            Machine m;
            if (!loadAltmon(m)) return;
            auto rep = runScript(m, init + monitor(2, "CONNECT sio0:b scripted") +
                                        monitor(3, "CONFIG SAVE " + toml));
            CHECK(rep.count(3) && !rep[3].at("result").has("isError"), "CONFIG SAVE succeeds");
            const std::string text = slurp(toml);
            CHECK(text.find("connect = \"console\"") != std::string::npos,
                  "the holder is saved on the console");
            CHECK(text.find("connect = \"scripted\"") != std::string::npos,
                  "a second unit on its own scripted line stays scripted");
        }

        // The saved file has a console: a session on it can run.
        {
            Machine m;
            if (!loadAltmon(m)) return;
            std::string script = init + monitor(2, "CONFIG LOAD " + toml) +
                R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"run",)"
                R"("arguments":{"from":63488,"until":"ALTMON","timeout_ms":4000}}})" "\n";
            auto rep = runScript(m, script);
            CHECK(rep.count(3) && !rep[3].at("result").has("isError"),
                  "the saved machine runs: its console is there");
        }

        // Mirror and log are the session's: neither reaches the file.
        {
            uint16_t port = freePort();
            CHECK(port != 0, "the OS hands us a free port");
            Machine m;
            if (!loadAltmon(m)) return;
            runScript(m, init + monitor(2, "CONFIG SAVE " + toml),
                      "socket:" + std::to_string(port), log);
            const std::string text = slurp(toml);
            CHECK(text.find("connect = \"console\"") != std::string::npos,
                  "the holder is saved on the console under --mirror and --log");
            CHECK(text.find("socket:") == std::string::npos, "no mirror in the file");
            CHECK(text.find(".log") == std::string::npos, "no log tap in the file");
        }
        std::error_code ec;
        std::filesystem::remove(toml, ec);
        std::filesystem::remove(log, ec);
    }

    SECTION("ScriptedStream::trimOut keeps the newest bytes (#602's held-output cap)");
    {
        ScriptedStream s;
        const std::string msg = "0123456789";
        s.write(reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
        CHECK(s.trimOut(4) == 6 && s.out() == "6789", "trimOut drops the oldest bytes");
        CHECK(s.trimOut(4) == 0 && s.out() == "6789", "and nothing when already under the cap");
    }
}
