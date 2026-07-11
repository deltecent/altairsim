#include "mcp/server.h"

#include "boards/memory.h"
#include "boards/registry.h"
#include "cli/monitor.h"
#include "core/crc32.h"
#include "core/hex.h"
#include "core/roms.h"
#include "util/json.h"

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

// ---- The tool list. No CPU yet, so no run/step/regs -- and we say so rather
// ---- than shipping a `run` that quietly does nothing.
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
        j["runtime"] = Json(p.runtime);
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

Json callTool(Machine& m, const std::string& name, const Json& args) {
    char buf[256];

    if (name == "board_types") {
        Json a = Json::arr();
        for (const auto& t : boardTypes()) {
            Json j = Json::obj();
            j["name"] = Json(t.name);
            j["description"] = Json(t.description);
            auto b = makeBoard(t.name);
            j["properties"] = propsJson(b.get());
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
        if (!setProperty(*b, args.at("key").str(), args.at("value").str(), m.running, err))
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
            reply(out, id, callTool(m, name, params.at("arguments")));
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
