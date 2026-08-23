#include "test.h"

#include "core/board.h"
#include "core/machine.h"
#include "core/machines.h"
#include "host/mirror_stream.h"
#include "host/stream.h"
#include "mcp/server.h"
#include "platform/socket.h"
#include "util/json.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <thread>

using namespace altair;

// The MCP server is driven by feeding JSON-RPC lines to runMcp and reading the
// replies back -- the same door an assistant uses, over a pair of stringstreams
// instead of a pipe. No mocks: a real built-in machine, a real 8080, a real 6850.
namespace {

std::map<int, Json> runScript(Machine& m, const std::string& script,
                              const std::string& mirror = "") {
    std::istringstream in(script);
    std::ostringstream out;
    runMcp(m, in, out, mirror);

    std::map<int, Json> byId;
    std::istringstream lines(out.str());
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        Json j;
        std::string err;
        if (Json::parse(line, j, err)) byId[(int)j.at("id").integer()] = j;
    }
    return byId;
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
// After runMcp with --mirror, the console's line is `scripted|socket:PORT` -- a mirror
// the assistant drives via the inner scripted while a socket client shares it.
MirrorStream* consoleMirror(Machine& m, ScriptedStream** innerOut = nullptr) {
    for (const auto& b : m.boards())
        for (const auto& u : b->units()) {
            if (u.kind != UnitKind::Serial) continue;
            if (auto* mir = dynamic_cast<MirrorStream*>(b->unitStream(u.name))) {
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

std::string tmpPath(const char* leaf) {
    return (std::filesystem::temp_directory_path() / leaf).string();
}

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
}
