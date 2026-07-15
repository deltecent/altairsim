#include "mcp/server.h"

#include "boards/s100-memory.h"
#include "boards/registry.h"
#include "cli/monitor.h"
#include "core/crc32.h"
#include "core/hex.h"
#include "core/roms.h"
#include "cpu/cpu.h"
#include "host/stream.h"
#include "util/json.h"

#include <chrono>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>

namespace altair {

namespace {

Json strSchema(const char* desc) {
    Json p = Json::obj();
    p["type"] = Json("string");
    p["description"] = Json(desc);
    return p;
}
Json intSchema(const char* desc) {
    Json p = Json::obj();
    p["type"] = Json("integer");
    p["description"] = Json(desc);
    return p;
}

Json tool(const char* name, const char* desc, Json props, std::vector<std::string> required) {
    Json t = Json::obj();
    t["name"] = Json(name);
    t["description"] = Json(desc);
    Json schema = Json::obj();
    schema["type"] = Json("object");
    schema["properties"] = props;
    Json req = Json::arr();
    for (auto& r : required) req.push(Json(r));
    schema["required"] = req;
    t["inputSchema"] = schema;
    return t;
}

// ---- The tool list. The interactive four -- run/send/recv/regs -- drive a RUNNING
// ---- guest: they type at its console, read what it prints, and advance it a bounded
// ---- slice at a time so a `tools/call` never blocks (unlike a bare RUN, which under a
// ---- pipe would wait for a stdin that is the JSON-RPC channel -- monitor.cpp:818).
Json toolList() {
    Json list = Json::arr();

    list.push(tool("board_types", "Every board type compiled in, with its properties.",
                   Json::obj(), {}));
    list.push(tool("board_list", "The boards in the machine: id, type, memory and I/O decode.",
                   Json::obj(), {}));

    {
        Json p = Json::obj();
        p["id"] = strSchema("Board id, e.g. mem0");
        list.push(tool("board_get",
                       "Every property of one board: value, legal range, and whether it is "
                       "settable at runtime. Schema comes from the board itself.",
                       p, {"id"}));
    }
    {
        Json p = Json::obj();
        p["type"] = strSchema("Board type, e.g. memory");
        p["id"] = strSchema("The id to give it");
        list.push(tool("board_add", "Add a board to the backplane.", p, {"type", "id"}));
    }
    {
        Json p = Json::obj();
        p["id"] = strSchema("Board id");
        p["key"] = strSchema("Property name (board_get lists them, with legal values)");
        p["value"] = strSchema("New value");
        list.push(tool("board_set",
                       "Set one property. Validated against the board's own metadata: illegal "
                       "enums, out-of-range ints, and config-time properties on a running "
                       "machine are REJECTED, never half-applied.",
                       p, {"id", "key", "value"}));
    }
    {
        Json p = Json::obj();
        p["addr"] = intSchema("Address 0-0xFFFF");
        list.push(tool("who",
                       "Who drives this address, for a read and for a write -- and whether "
                       "PHANTOM* is asserted. The reverse lookup for a decode you don't believe.",
                       p, {"addr"}));
    }
    list.push(tool("bus_map", "The memory decode map, plus the holes that float to 0xFF.",
                   Json::obj(), {}));
    list.push(tool("bus_io", "The I/O decode map.", Json::obj(), {}));
    list.push(tool("bus_contention",
                   "Every address two boards BOTH actually drive. A PHANTOM* overlay is not "
                   "contention and does not appear here -- the shadowed board switches itself "
                   "off, so only one board answers.",
                   Json::obj(), {}));
    {
        Json p = Json::obj();
        p["lo"] = intSchema("First address");
        p["hi"] = intSchema("Last address (inclusive)");
        p["raw"] = strSchema("Optional board id: read BEHIND the bus, from that board's store");
        list.push(tool("mem_dump",
                       "Read memory through the bus -- exactly what a CPU would see: live bank, "
                       "PHANTOM* overlays applied, unmapped addresses reading 0xFF. Pass `raw` "
                       "to bypass the bus and read one board's store directly.",
                       p, {"lo", "hi"}));
    }
    {
        Json p = Json::obj();
        p["addr"] = intSchema("Address");
        p["bytes"] = strSchema("Hex bytes, e.g. \"C3 00 2C\"");
        p["raw"] = strSchema("Optional board id: write BEHIND the bus (the PROM burner)");
        list.push(tool("mem_deposit",
                       "Write memory. Through the bus by default, which means a write to ROM "
                       "goes NOWHERE (the board never answers the cycle) and the result says so. "
                       "Pass `raw` to reach behind the bus into a board's store -- that is how "
                       "you write a ROM, and it is why the operator can and the guest cannot.",
                       p, {"addr", "bytes"}));
    }
    {
        Json p = Json::obj();
        p["path"] = strSchema("Path to an Intel HEX or flat binary file");
        p["at"] = intSchema("Load address (required for a flat binary; HEX places itself)");
        p["raw"] = strSchema("Optional board id: burn into that board's store");
        list.push(tool("mem_load", "Load a HEX or binary file. Every HEX checksum is verified.",
                       p, {"path"}));
    }
    list.push(tool("roms", "The ROMs compiled into the simulator: name, size, CRC32.",
                   Json::obj(), {}));
    {
        Json p = Json::obj();
        p["kind"] = strSchema("bus | power");
        list.push(tool("reset",
                       "bus = the front-panel RESET button. power = a power cycle. NEITHER "
                       "RESET CLEARS RAM -- only power does. A RAM chip has no reset pin.",
                       p, {"kind"}));
    }
    {
        Json p = Json::obj();
        p["from"]       = intSchema("Optional start address: set PC here first (like RUN <addr>). "
                                    "Omit to resume from the current PC.");
        p["input"]      = strSchema("Optional keystrokes to type at the console before running "
                                    "(raw bytes; add a trailing \\r to submit a CP/M line).");
        p["until"]      = strSchema("Optional: stop as soon as this substring appears in the "
                                    "output (e.g. a prompt like \"A0>\").");
        p["timeout_ms"] = intSchema("Wall-clock budget for this call in ms (default 2000). The "
                                    "guest runs flat out; this only bounds how long we wait.");
        p["max_steps"]  = intSchema("Optional instruction-count cap for this call.");
        list.push(tool("run",
                       "Advance the running guest a bounded slice and return what it printed to "
                       "the console. STOPS on: `until` matched, a prompt reached (the guest is "
                       "spinning on console input with nothing to say), timeout_ms, max_steps, a "
                       "HLT, or a breakpoint -- reported in `stopped`. This is the expect loop: "
                       "type a command with `input`, read the reply, call again. Never blocks.",
                       p, {}));
    }
    {
        Json p = Json::obj();
        p["text"] = strSchema("Keystrokes to type at the console (raw bytes). Does NOT run the "
                              "guest -- follow with `run` (or use run's own `input`).");
        list.push(tool("send", "Type at the guest console without running it.", p, {"text"}));
    }
    list.push(tool("recv",
                   "Drain and return everything the guest has printed to the console since the "
                   "last read, without running it.",
                   Json::obj(), {}));
    list.push(tool("regs",
                   "The CPU registers right now: every register the active core declares, plus "
                   "pc, halted and interrupts. Does not run the guest.",
                   Json::obj(), {}));
    {
        Json p = Json::obj();
        p["command"] = strSchema("A monitor command line");
        list.push(tool("monitor",
                       "Run one monitor command and return its text. The escape hatch: anything "
                       "the CLI can do, in one call.",
                       p, {"command"}));
    }
    return list;
}

Json boardJson(Board* b) {
    Json j = Json::obj();
    j["id"] = Json(b->id);
    j["type"] = Json(b->type());
    j["enabled"] = Json(b->enabled());
    Json mem = Json::arr();
    for (const auto& e : b->memMap()) {
        Json r = Json::obj();
        r["lo"] = Json((long long)e.lo);
        r["hi"] = Json((long long)e.hi);
        r["kind"] = Json(e.what);
        r["note"] = Json(e.note);
        mem.push(r);
    }
    j["memory"] = mem;
    Json io = Json::arr();
    for (const auto& e : b->ioMap()) {
        Json r = Json::obj();
        r["port"] = Json((long long)e.lo);
        r["dir"] = Json(e.what);
        r["note"] = Json(e.note);
        io.push(r);
    }
    j["io"] = io;
    return j;
}

// The whole argument for MCP-as-first-class, in one function: an agent asks a
// board what it can be told, and gets an answer generated from the board's own
// declaration -- enums, ranges, runtime-settability and all.
Json propsJson(Board* b) {
    Json arr = Json::arr();
    for (const auto& p : b->properties()) {
        Json j = Json::obj();
        j["name"] = Json(p.name);
        j["help"] = Json(p.help);
        j["value"] = Json(p.get().text(p.radix));
                switch (p.kind) {
        case Kind::Bool: j["kind"] = Json("bool"); break;
        case Kind::Int:
            j["kind"] = Json("int");
            if (!(p.min == 0 && p.max == 0)) {
                j["min"] = Json((long long)p.min);
                j["max"] = Json((long long)p.max);
            }
            if (p.radix == 16) j["radix"] = Json(16);
            break;
        case Kind::Str: j["kind"] = Json("string"); break;
        case Kind::Enum: {
            j["kind"] = Json("enum");
            Json c = Json::arr();
            for (const auto& x : p.choices) c.push(Json(x));
            j["choices"] = c;
            break;
        }
        }
        arr.push(j);
    }
    return arr;
}

// ...and the same argument, one level down: an agent asks a board what may be written in
// its [[board.drive]] / [[board.region]] tables, and gets an answer generated from the
// board's own declaration. Until subUnitProperties() existed there was nothing to answer
// from, so an agent writing a machine file could not discover `readonly`, `media` or `at`
// -- the keys that carry the disk and the ROM -- from the schema at all.
//
// NO "value" HERE, and that is the difference: these describe a drive that does not exist
// yet, so there is nothing to read. (Board::subUnitProperties.)
Json subUnitsJson(Board* b) {
    Json arr = Json::arr();
    for (const auto& table : b->subUnitTables()) {
        Json t = Json::obj();
        t["table"] = Json("[[board." + table + "]]");
        Json keys = Json::arr();
        for (const auto& p : b->subUnitProperties(table)) {
            Json j = Json::obj();
            j["name"] = Json(p.name);
            j["help"] = Json(p.help);
            switch (p.kind) {
            case Kind::Bool: j["kind"] = Json("bool"); break;
            case Kind::Str:  j["kind"] = Json("string"); break;
            case Kind::Int:
                j["kind"] = Json("int");
                if (!(p.min == 0 && p.max == 0)) {
                    j["min"] = Json((long long)p.min);
                    j["max"] = Json((long long)p.max);
                }
                if (p.radix == 16) j["radix"] = Json(16);
                break;
            case Kind::Enum: {
                j["kind"] = Json("enum");
                Json c = Json::arr();
                for (const auto& x : p.choices) c.push(Json(x));
                j["choices"] = c;
                break;
            }
            }
            keys.push(j);
        }
        t["keys"] = keys;
        arr.push(t);
    }
    return arr;
}

Json textResult(const std::string& s, bool isError = false) {
    Json r = Json::obj();
    Json content = Json::arr();
    Json c = Json::obj();
    c["type"] = Json("text");
    c["text"] = Json(s);
    content.push(c);
    r["content"] = content;
    if (isError) r["isError"] = Json(true);
    return r;
}

// Structured results still carry a text rendering, because an agent reads the
// text and a program reads the JSON, and neither should have to parse the other.
Json dataResult(const Json& data, const std::string& text) {
    Json r = textResult(text);
    r["structuredContent"] = data;
    return r;
}

bool parseBytes(const std::string& s, std::vector<uint8_t>& out) {
    std::istringstream in(s);
    std::string t;
    while (in >> t) {
        char* end = nullptr;
        long v = std::strtol(t.c_str(), &end, 16);
        if (end == t.c_str() || *end) return false;
        out.push_back((uint8_t)v);
    }
    return !out.empty();
}

// The interactive console the four live tools share. Non-owning: the chip owns the
// ScriptedStream; we remember only WHICH channel it is, and re-fetch the live pointer
// every call so a reconnect can never leave us holding a dangling one.
struct McpSession {
    std::string conBoard;
    std::string conUnit;
};

// Find the serial unit wired to the host console and REBIND it to an in-memory
// ScriptedStream. Under --mcp there is no terminal, so "console" would aim the guest's
// keyboard at the JSON-RPC pipe and hang the first run forever (monitor.cpp:818); a
// scripted line is one we can feed() and read out() instead. Idempotent: once a channel
// is ours, later calls just re-fetch it. Null + err if the machine has no such line.
ScriptedStream* console(Machine& m, McpSession& s, std::string& err) {
    if (!s.conBoard.empty())
        if (Board* b = m.find(s.conBoard))
            if (auto* ss = dynamic_cast<ScriptedStream*>(b->unitStream(s.conUnit)))
                return ss;

    for (const auto& b : m.boards())
        for (const auto& u : b->units()) {
            if (u.kind != UnitKind::Serial) continue;
            if (u.state != "console") continue;
            if (!b->connect(u.name, "scripted", err)) return nullptr;
            s.conBoard = b->id;
            s.conUnit  = u.name;
            if (auto* ss = dynamic_cast<ScriptedStream*>(b->unitStream(u.name))) return ss;
        }
    err = "no console line: CONNECT a serial unit to 'scripted' (one wired to 'console' "
          "is adopted automatically).";
    return nullptr;
}

Json callTool(Machine& m, McpSession& sess, const std::string& name, const Json& args) {
    char buf[256];

    if (name == "board_types") {
        Json a = Json::arr();
        for (const auto& t : boardTypes()) {
            Json j = Json::obj();
            j["name"] = Json(t.name);
            j["description"] = Json(t.description);
            auto b = makeBoard(t.name);
            j["properties"] = propsJson(b.get());
            j["sub_units"]  = subUnitsJson(b.get());   // what its [[board.x]] tables take
            a.push(j);
        }
        Json d = Json::obj();
        d["types"] = a;
        return dataResult(d, a.dump());
    }

    if (name == "board_list") {
        Json a = Json::arr();
        for (const auto& b : m.boards()) a.push(boardJson(b.get()));
        Json d = Json::obj();
        d["boards"] = a;
        return dataResult(d, a.items().empty() ? "(empty backplane)" : a.dump());
    }

    if (name == "board_get") {
        Board* b = m.find(args.at("id").str());
        if (!b) return textResult("no board '" + args.at("id").str() + "'", true);
        Json d = boardJson(b);
        d["properties"] = propsJson(b);
        d["sub_units"]  = subUnitsJson(b);
        if (auto* mem = dynamic_cast<MemoryBoard*>(b)) {
            Json rs = Json::arr();
            int i = 0;
            for (const auto& r : mem->regions()) {
                Json j = Json::obj();
                j["unit"] = Json(i++);
                j["type"] = Json(r.kind == RegionKind::Rom ? "rom" : "ram");
                j["at"] = Json((long long)r.at);
                j["size"] = Json((long long)r.size);
                if (!r.mount.empty()) j["mount"] = Json(r.mount);
                rs.push(j);
            }
            d["regions"] = rs;
        }
        return dataResult(d, d.dump());
    }

    if (name == "board_add") {
        std::string err;
        Board* b = m.add(args.at("type").str(), args.at("id").str(), err);
        if (!b) return textResult(err, true);
        return dataResult(boardJson(b), b->id + ": " + b->type() + " added");
    }

    if (name == "board_set") {
        Board* b = m.find(args.at("id").str());
        if (!b) return textResult("no board '" + args.at("id").str() + "'", true);
        std::string err;
        if (!setProperty(*b, args.at("key").str(), args.at("value").str(), err))
            return textResult(err, true);
        return dataResult(propsJson(b), b->id + ": " + args.at("key").str() + "=" +
                                            args.at("value").str());
    }

    if (name == "who") {
        uint16_t A = (uint16_t)args.at("addr").integer();
        Json d = Json::obj();
        d["addr"] = Json((long long)A);
        std::string text;
        for (Cycle t : {Cycle::MemRead, Cycle::MemWrite}) {
            BusCycle c;
            c.type = t;
            c.addr = A;
            bool ph = false;
            for (const auto& b : m.boards())
                if (b->enabled() && b->assertsPhantom(c)) ph = true;
            auto who = m.bus.respondersTo(c);
            Json j = Json::obj();
            Json ids = Json::arr();
            for (auto* b : who) ids.push(Json(b->id));
            j["boards"] = ids;
            j["phantom"] = Json(ph);
            j["floats"] = Json(who.empty());
            j["contention"] = Json(who.size() > 1);
            const char* k = (t == Cycle::MemRead) ? "read" : "write";
            d[k] = j;
            std::snprintf(buf, sizeof buf, "%04X %s: ", A, k);
            text += buf;
            if (who.empty()) text += "nobody -- floats to FF";
            for (auto* b : who) text += b->id + " ";
            if (who.size() > 1) text += "*** CONTENTION ***";
            if (ph) text += " [PHANTOM*]";
            text += "\n";
        }
        return dataResult(d, text);
    }

    if (name == "bus_map" || name == "bus_io") {
        Json a = Json::arr();
        for (const auto& b : m.boards())
            for (const auto& e : (name == "bus_map" ? b->memMap() : b->ioMap())) {
                Json j = Json::obj();
                j["board"] = Json(b->id);
                j["lo"] = Json((long long)e.lo);
                j["hi"] = Json((long long)e.hi);
                j["kind"] = Json(e.what);
                j["note"] = Json(e.note);
                a.push(j);
            }
        Json d = Json::obj();
        d["entries"] = a;
        return dataResult(d, a.dump());
    }

    if (name == "bus_contention") {
        Json a = Json::arr();
        for (uint32_t A = 0; A <= 0xFFFF; ++A) {
            for (Cycle t : {Cycle::MemRead, Cycle::MemWrite}) {
                BusCycle c;
                c.type = t;
                c.addr = (uint16_t)A;
                auto who = m.bus.respondersTo(c);
                if (who.size() < 2) continue;
                Json j = Json::obj();
                j["addr"] = Json((long long)A);
                j["cycle"] = Json(t == Cycle::MemRead ? "read" : "write");
                Json ids = Json::arr();
                for (auto* b : who) ids.push(Json(b->id));
                j["boards"] = ids;
                a.push(j);
            }
        }
        Json d = Json::obj();
        d["contention"] = a;
        return dataResult(d, a.items().empty() ? "none" : a.dump());
    }

    if (name == "mem_dump") {
        uint32_t lo = (uint32_t)args.at("lo").integer();
        uint32_t hi = (uint32_t)args.at("hi").integer();
        Board* raw = args.has("raw") ? m.find(args.at("raw").str()) : nullptr;
        if (args.has("raw") && !raw) return textResult("no board '" + args.at("raw").str() + "'", true);

        Json bytes = Json::arr();
        std::string text;
        for (uint32_t A = lo; A <= hi && A <= 0xFFFFF; ++A) {
            uint8_t v = raw ? raw->rawRead(A) : m.bus.memRead((uint16_t)A);
            bytes.push(Json((long long)v));
            std::snprintf(buf, sizeof buf, "%02X ", v);
            text += buf;
        }
        Json d = Json::obj();
        d["lo"] = Json((long long)lo);
        d["hi"] = Json((long long)hi);
        d["raw"] = Json(raw != nullptr);
        d["bytes"] = bytes;
        return dataResult(d, text);
    }

    if (name == "mem_deposit") {
        uint32_t A = (uint32_t)args.at("addr").integer();
        std::vector<uint8_t> bytes;
        if (!parseBytes(args.at("bytes").str(), bytes))
            return textResult("bytes must be hex, e.g. \"C3 00 2C\"", true);
        Board* raw = args.has("raw") ? m.find(args.at("raw").str()) : nullptr;
        if (args.has("raw") && !raw) return textResult("no board '" + args.at("raw").str() + "'", true);

        int discarded = 0;
        for (size_t k = 0; k < bytes.size(); ++k) {
            if (raw) {
                if (!raw->rawWrite(A + k, bytes[k]))
                    return textResult("offset past that board's store", true);
            } else {
                m.bus.memWrite((uint16_t)(A + k), bytes[k]);
                if (m.bus.lastUnclaimed()) ++discarded;
            }
        }
        Json d = Json::obj();
        d["addr"] = Json((long long)A);
        d["written"] = Json((long long)bytes.size());
        d["discarded"] = Json((long long)discarded);
        d["raw"] = Json(raw != nullptr);

        std::string text = std::to_string(bytes.size()) + " byte(s) written";
        if (discarded) {
            // The single most important thing this tool can tell an agent, and
            // it must never be silent about it.
            text += "; " + std::to_string(discarded) +
                    " landed NOWHERE -- no board decodes a write there. That address is ROM "
                    "or unmapped. A ROM does not reject the write; it never answers the cycle. "
                    "To write it anyway, use raw=<board id> (the PROM burner).";
        }
        return dataResult(d, text);
    }

    if (name == "mem_load") {
        std::string path = args.at("path").str();
        std::ifstream f(path, std::ios::binary);
        if (!f) return textResult("cannot open '" + path + "'", true);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        Image img;
        std::string err;
        if (looksLikeHex(data)) {
            if (!loadHex(data, img, err)) return textResult(path + ": " + err, true);
        } else {
            if (!args.has("at"))
                return textResult(path + " is a flat binary and carries no addresses -- pass `at`",
                                  true);
            loadBin(data, (uint32_t)args.at("at").integer(), img);
        }

        Board* raw = args.has("raw") ? m.find(args.at("raw").str()) : nullptr;
        if (args.has("raw") && !raw) return textResult("no board '" + args.at("raw").str() + "'", true);

        int discarded = 0;
        if (raw) {
            auto* mem = dynamic_cast<MemoryBoard*>(raw);
            if (!mem) return textResult(raw->id + ": no store to burn", true);
            if (!mem->blit(img, err)) return textResult(err, true);
        } else {
            for (const auto& [A, v] : img.bytes) {
                m.bus.memWrite((uint16_t)A, v);
                if (m.bus.lastUnclaimed()) ++discarded;
            }
        }
        Json d = Json::obj();
        d["bytes"] = Json((long long)img.size());
        d["lo"] = Json((long long)img.lo());
        d["hi"] = Json((long long)img.hi());
        d["discarded"] = Json((long long)discarded);
        std::snprintf(buf, sizeof buf, "loaded %zu bytes (%04X-%04X)", img.size(), img.lo(),
                      img.hi());
        std::string text = buf;
        if (discarded)
            text += "; WARNING: " + std::to_string(discarded) +
                    " byte(s) landed nowhere (ROM or unmapped). Use raw=<id> to burn a ROM.";
        return dataResult(d, text);
    }

    if (name == "roms") {
        Json a = Json::arr();
        std::string text;
        for (const auto& r : builtinRoms()) {
            Image img;
            std::string err;
            Json j = Json::obj();
            j["name"] = Json(r.name);
            j["file"] = Json(r.file);
            if (decodeRom(r, 0, img, err) && !img.empty()) {
                auto flat = img.flat();
                j["size"] = Json((long long)img.size());
                j["lo"] = Json((long long)img.lo());
                j["hi"] = Json((long long)img.hi());
                std::snprintf(buf, sizeof buf, "%08X", crc32(flat));
                j["crc32"] = Json(std::string(buf));
                text += std::string(r.name) + " (" + buf + ")\n";
            }
            j["mount"] = Json(std::string("builtin:") + r.name);
            a.push(j);
        }
        Json d = Json::obj();
        d["roms"] = a;
        return dataResult(d, text.empty() ? "(none compiled in)" : text);
    }

    if (name == "reset") {
        std::string k = args.at("kind").str("bus");
        if (k == "power") {
            m.power();
            return textResult("power cycled: RAM re-filled, ROM images re-read, POC* pulsed.");
        }
        m.reset(Reset::Bus);
        return textResult("RESET* pulsed. Memory is UNTOUCHED -- only power loses RAM.");
    }

    if (name == "send") {
        std::string err;
        ScriptedStream* con = console(m, sess, err);
        if (!con) return textResult(err, true);
        con->feed(args.at("text").str());
        return textResult("(typed)");
    }

    if (name == "recv") {
        std::string err;
        ScriptedStream* con = console(m, sess, err);
        if (!con) return textResult(err, true);
        std::string out = con->out();
        con->clearOut();
        Json d = Json::obj();
        d["output"] = Json(out);
        return dataResult(d, out.empty() ? "(nothing)" : out);
    }

    if (name == "regs") {
        CpuCore* c = m.cpu();
        if (!c) return textResult("no CPU in this machine", true);
        Json d = Json::obj();
        Json regs = Json::obj();
        std::string text;
        for (const RegDef& r : c->registers()) {
            uint32_t v = r.get();
            regs[r.name] = Json((long long)v);
            std::snprintf(buf, sizeof buf, "%s=%X ", r.shown().c_str(), v);
            text += buf;
        }
        d["registers"] = regs;
        d["pc"] = Json((long long)c->pc());
        d["halted"] = Json(c->halted());
        d["interrupts"] = Json(c->interruptsEnabled());
        return dataResult(d, text);
    }

    if (name == "run") {
        using clk = std::chrono::steady_clock;
        std::string err;
        ScriptedStream* con = console(m, sess, err);
        if (!con) return textResult(err, true);
        CpuCore* cpu = m.cpu();
        if (!cpu) return textResult("no CPU in this machine", true);

        if (args.has("from")) cpu->setPc((uint16_t)args.at("from").integer());
        if (args.has("input")) con->feed(args.at("input").str());

        const std::string until   = args.has("until") ? args.at("until").str() : std::string();
        long long         timeout = args.has("timeout_ms") ? args.at("timeout_ms").integer() : 2000;
        if (timeout < 0) timeout = 0;
        if (timeout > 600000) timeout = 600000;  // ten minutes is already a runaway
        const uint64_t maxSteps = args.has("max_steps") ? (uint64_t)args.at("max_steps").integer() : 0;

        // A prompt is a guest that ran, said nothing, received nothing, and came to the
        // console and found it empty at least once every 32 instructions -- the same
        // discrimination runMachine draws (guestIsWaiting, monitor.cpp), so a loader that
        // is merely quiet while it works is NOT mistaken for one. It must persist across a
        // couple of slices to be believed.
        static constexpr uint64_t kIdleRatio = 32;

        const auto deadline = clk::now() + std::chrono::milliseconds(timeout);
        std::string out;
        uint64_t    steps = 0;
        int         quiet = 0;
        std::string stopped;

        auto drain = [&] {
            const std::string& o = con->out();
            if (!o.empty()) { out += o; con->clearOut(); }
        };

        for (;;) {
            drain();
            if (!until.empty() && out.find(until) != std::string::npos) { stopped = "match"; break; }
            if (clk::now() >= deadline) { stopped = "timeout"; break; }
            if (maxSteps && steps >= maxSteps) { stopped = "steps"; break; }

            const uint64_t rxBefore     = m.rxBytes();
            const uint64_t hungryBefore = con->hungry();
            RunResult r = m.debug.run(2000);
            m.pump();
            steps += r.steps;

            const size_t wroteBefore = out.size();
            drain();
            const bool produced = out.size() != wroteBefore;
            const bool received = m.rxBytes() != rxBefore;
            const uint64_t hungry = con->hungry() - hungryBefore;

            if (r.why == StopReason::Halted)     { stopped = "halt";       break; }
            if (r.why == StopReason::Breakpoint) { stopped = "breakpoint"; break; }
            if (r.why == StopReason::NoCpu)      { stopped = "no-cpu";      break; }

            const bool waiting = con->drained() && !produced && !received &&
                                 r.steps != 0 && hungry * kIdleRatio >= r.steps;
            if (waiting) { if (++quiet >= 2) { stopped = "idle"; break; } }
            else quiet = 0;
        }

        Json d = Json::obj();
        d["output"]  = Json(out);
        d["stopped"] = Json(stopped);
        d["pc"]      = Json((long long)cpu->pc());
        d["steps"]   = Json((long long)steps);
        std::string text = out;
        if (!text.empty() && text.back() != '\n') text += '\n';
        text += "[stopped: " + stopped + "]";
        return dataResult(d, text);
    }

    if (name == "monitor") {
        std::ostringstream os;
        Monitor mon(m);
        mon.exec(args.at("command").str(), os);
        return textResult(os.str().empty() ? "(ok)" : os.str(), mon.failed());
    }

    return textResult("no such tool: " + name, true);
}

void reply(std::ostream& out, const Json& id, const Json& result) {
    Json r = Json::obj();
    r["jsonrpc"] = Json("2.0");
    r["id"] = id;
    r["result"] = result;
    out << r.dump() << "\n" << std::flush;
}

void replyError(std::ostream& out, const Json& id, int code, const std::string& msg) {
    Json r = Json::obj();
    r["jsonrpc"] = Json("2.0");
    r["id"] = id;
    Json e = Json::obj();
    e["code"] = Json(code);
    e["message"] = Json(msg);
    r["error"] = e;
    out << r.dump() << "\n" << std::flush;
}

} // namespace

int runMcp(Machine& m, std::istream& in, std::ostream& out) {
    // Take the console off "console" NOW, before anything runs: under --mcp stdin is the
    // JSON-RPC channel, not a keyboard, and a guest reading it would eat our next request.
    // A scripted line is one the interactive tools own. Quietly does nothing if the
    // machine has no console line (an empty backplane, a socket-only machine).
    McpSession sess;
    std::string bindErr;
    console(m, sess, bindErr);

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Json req;
        std::string err;
        if (!Json::parse(line, req, err)) {
            replyError(out, Json(), -32700, "parse error: " + err);
            continue;
        }
        std::string method = req.at("method").str();
        Json id = req.at("id");

        if (method == "initialize") {
            Json r = Json::obj();
            r["protocolVersion"] = Json("2024-11-05");
            Json caps = Json::obj();
            caps["tools"] = Json::obj();
            r["capabilities"] = caps;
            Json info = Json::obj();
            info["name"] = Json("altairsim");
            info["version"] = Json("0.1.0");
            r["serverInfo"] = info;
            reply(out, id, r);
            continue;
        }
        if (method == "notifications/initialized") continue;

        if (method == "tools/list") {
            Json r = Json::obj();
            r["tools"] = toolList();
            reply(out, id, r);
            continue;
        }
        if (method == "tools/call") {
            const Json& params = req.at("params");
            std::string name = params.at("name").str();
            reply(out, id, callTool(m, sess, name, params.at("arguments")));
            continue;
        }
        if (method == "ping") {
            reply(out, id, Json::obj());
            continue;
        }
        replyError(out, id, -32601, "method not found: " + method);
    }
    return 0;
}

} // namespace altair
