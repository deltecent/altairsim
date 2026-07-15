#include "test.h"

#include "core/machine.h"
#include "core/machines.h"
#include "mcp/server.h"
#include "util/json.h"

#include <map>
#include <sstream>
#include <string>

using namespace altair;

// The MCP server is driven by feeding JSON-RPC lines to runMcp and reading the
// replies back -- the same door an assistant uses, over a pair of stringstreams
// instead of a pipe. No mocks: a real built-in machine, a real 8080, a real 6850.
namespace {

std::map<int, Json> runScript(Machine& m, const std::string& script) {
    std::istringstream in(script);
    std::ostringstream out;
    runMcp(m, in, out);

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
}
