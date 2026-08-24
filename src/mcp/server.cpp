#include "mcp/server.h"

#include "boards/s100-memory.h"
#include "boards/registry.h"
#include "cli/monitor.h"
#include "core/crc32.h"
#include "core/hex.h"
#include "core/roms.h"
#include "core/version.h"
#include "cpu/cpu.h"
#include "host/console.h"
#include "host/endpoint.h"
#include "host/filter.h"
#include "host/mirror_stream.h"
#include "host/stream.h"
#include "isa/isa.h"
#include "util/json.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <thread>

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
Json boolSchema(const char* desc) {
    Json p = Json::obj();
    p["type"] = Json("boolean");
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
        list.push(tool("mem_dump",
                       "Read memory through the bus -- exactly what a CPU would see: live bank, "
                       "PHANTOM* overlays applied, unmapped addresses reading 0xFF. A ROM reads "
                       "back like anything else. To see a bank that is not selected, or a board "
                       "that is phantomed out, SELECT it (board_set bank=/phantom=) and read "
                       "ordinary addresses -- the same thing the guest would have to do.",
                       p, {"lo", "hi"}));
    }
    {
        Json p = Json::obj();
        p["addr"] = intSchema("Address");
        p["bytes"] = strSchema("Hex bytes, e.g. \"C3 00 2C\"");
        p["rom"] = boolSchema("Program a ROM: write behind the bus, into whichever chip answers "
                              "reads at this address");
        list.push(tool("mem_deposit",
                       "Write memory. Through the bus by default, which means a write to ROM "
                       "goes NOWHERE (the board never answers the cycle) and the result says so. "
                       "Pass `rom` to program it anyway -- that is the operator pulling the chip "
                       "and putting it in a programmer, which is why the operator can and the "
                       "guest cannot. Addresses are always bus addresses, 0000-FFFF.",
                       p, {"addr", "bytes"}));
    }
    {
        Json p = Json::obj();
        p["path"] = strSchema("Path to an Intel HEX or flat binary file");
        p["at"] = intSchema("Load address. Required for a flat binary; a HEX file places itself, "
                            "and giving `at` anyway relocates it so its FIRST data record lands "
                            "here (wrapping modulo 64K)");
        p["format"] = strSchema("BIN or HEX. Overrides the sniffed content, which is otherwise "
                                "what decides");
        p["rom"] = boolSchema("Program a ROM: write behind the bus rather than through it");
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
        p["timeout_ms"] = intSchema("Wall-clock budget for this call in ms (default 2000). By "
                                    "default the guest runs flat out and this only bounds how "
                                    "long we wait; with `SET cpu0 clock_hz=N` set, the guest is "
                                    "paced to that crystal so a real serial/socket device has "
                                    "wall-clock time to reply within this budget.");
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
                       "the CLI can do, in one call. RUN does NOT block here -- it sets PC and "
                       "returns, so `CONFIG LOAD <bootable.toml>` is safe (its startup runs up "
                       "to the boot RUN, which parks); advance the guest with the `run` tool.",
                       p, {"command"}));
    }
    {
        Json p = Json::obj();
        p["count"] = intSchema("How many instructions to execute (default 1).");
        list.push(tool("step",
                       "Execute N instructions through the real decode and real bus cycles, then "
                       "report where the CPU came to rest. Unlike `run` this types nothing and "
                       "reads no console output -- it is the debugger's single-step, not the "
                       "expect loop. Stops early on a HLT or a breakpoint (see `stopped`).",
                       p, {"count"}));
    }
    {
        Json p = Json::obj();
        p["addr"]  = intSchema("First address to decode.");
        p["count"] = intSchema("How many instructions to decode (default 16). Ignored if `hi` "
                               "is given.");
        p["hi"]    = intSchema("Optional: decode from `addr` through this address inclusive, "
                               "instead of a fixed count.");
        p["cpu"]   = strSchema("Instruction set, e.g. 8080 or z80. Defaults to the machine's "
                               "active CPU; required if the backplane has no processor.");
        list.push(tool("disasm",
                       "Disassemble memory. Reads non-invasively through a peek (no UART byte is "
                       "consumed, no snoop latch tripped) and decodes with the stateless "
                       "disassembler -- so it works with no CPU running, and on a ROM.",
                       p, {"addr"}));
    }
    {
        Json p = Json::obj();
        p["lo"]   = intSchema("First address.");
        p["hi"]   = intSchema("Last address (inclusive).");
        p["byte"] = intSchema("The byte value to write into every cell of the range.");
        p["rom"]  = boolSchema("Program a ROM: write behind the bus, into whichever chip answers "
                               "reads there (the PROM burner). Otherwise a write to ROM or an "
                               "unmapped hole lands nowhere and is counted in `discarded`.");
        list.push(tool("mem_fill",
                       "Fill an address range with one byte, written through the bus like a "
                       "guest would -- so ROM and unmapped holes are reported, not silently "
                       "dropped. Same rules as mem_deposit.",
                       p, {"lo", "hi", "byte"}));
    }
    {
        Json p = Json::obj();
        p["lo"]    = intSchema("First address to search.");
        p["hi"]    = intSchema("Last address (inclusive).");
        p["bytes"] = strSchema("The pattern as hex bytes, e.g. \"C3 00 F8\".");
        p["text"]  = strSchema("The pattern as an ASCII string (an alternative to `bytes`).");
        list.push(tool("mem_search",
                       "Find every occurrence of a byte pattern in a memory range, read through "
                       "the bus. Give the pattern as `bytes` (hex) or `text` (ASCII). Returns "
                       "the start address of each match.",
                       p, {"lo", "hi"}));
    }
    {
        Json p = Json::obj();
        p["path"]   = strSchema("Where to write the file.");
        p["lo"]     = intSchema("First address to save.");
        p["hi"]     = intSchema("Last address (inclusive).");
        p["format"] = strSchema("BIN or HEX. Defaults to the filename (.hex -> HEX, else BIN). "
                                "HEX is Intel HEX and round-trips through mem_load.");
        list.push(tool("mem_save",
                       "Write a memory range to a host file, read through the bus. The mirror of "
                       "mem_load: a BIN is raw bytes, a HEX is Intel HEX with checksums.",
                       p, {"path", "lo", "hi"}));
    }
    {
        Json p = Json::obj();
        p["action"] = strSchema("list (default) | add | remove | clear.");
        p["kind"]   = strSchema("For add: pc | memread | memwrite | ioread | iowrite.");
        p["lo"]     = intSchema("For add: the address (a port, for io kinds), or the low end of "
                                "a range.");
        p["hi"]     = intSchema("For add: the high end of an address range (defaults to `lo`).");
        p["id"]     = intSchema("For remove: which breakpoint (from the list).");
        list.push(tool("breakpoints",
                       "List, add, remove or clear breakpoints -- the same CPU-agnostic "
                       "breakpoints the monitor sets, so a Z80 or a DMA transfer trips them too. "
                       "A run/step stops when one fires. Conditional breakpoints (BREAK ... IF) "
                       "are not exposed here; reach them through the `monitor` tool.",
                       p, {}));
    }
    {
        Json p = Json::obj();
        p["path"] = strSchema("Where to write the snapshot.");
        list.push(tool("snapshot",
                       "Write the whole machine's STATE to a file: CPU registers and hidden "
                       "micro-state, the clock's time, and every board's serialized state. It is "
                       "state, not topology -- restore it into a machine built from the same "
                       "config (DESIGN 13.1).",
                       p, {"path"}));
    }
    {
        Json p = Json::obj();
        p["path"] = strSchema("The snapshot file to read back.");
        list.push(tool("restore",
                       "Restore machine state from a snapshot. Refuses a snapshot whose topology "
                       "does not match this machine, with the reason.",
                       p, {"path"}));
    }
    list.push(tool("bus_irq",
                   "The interrupt bus, structured: pin 73 (pINT) and who pulls it, the eight "
                   "VI wires and which are asserted, CPU INTE, and which level an 88-VI would "
                   "acknowledge right now (with the RST opcode it jams). The read-only companion "
                   "to bus_map/bus_io for the interrupt lines.",
                   Json::obj(), {}));
    {
        Json p = Json::obj();
        p["count"] = intSchema("How many of the most recent cycles to return (default 64).");
        list.push(tool("bus_trace",
                       "The bus flight recorder: the last N cycles every board saw -- address, "
                       "data, who drove and who answered, DMA and contention flags, and the "
                       "T-state. Always recording WHILE the guest runs, so it holds the run-up to "
                       "wherever the last run/step stopped; empty before anything has run.",
                       p, {}));
    }
    {
        Json p = Json::obj();
        p["id"]             = strSchema("Board id, e.g. dsk.");
        p["unit"]           = strSchema("Which unit on the board (its drive/socket name).");
        p["path"]           = strSchema("Host path to the image or file to mount.");
        p["write_protect"]  = boolSchema("Mount read-only (a write-protect tab / a ROM socket).");
        p["create"]         = boolSchema("Create an empty file first if it does not exist -- for "
                                         "a blank disk you are about to FORMAT, or a fresh tape.");
        list.push(tool("mount",
                       "Mount a host file into a board's unit (a disk into a drive, a tape into a "
                       "deck, an image into a ROM socket). The board decides what it can take.",
                       p, {"id", "unit", "path"}));
    }
    {
        Json p = Json::obj();
        p["id"]       = strSchema("Board id.");
        p["unit"]     = strSchema("Which serial unit on the board.");
        p["endpoint"] = strSchema("The endpoint to wire it to, e.g. loopback, tcp:HOST:PORT, "
                                  "file:PATH, printer:QUEUE. (The console line is adopted onto a "
                                  "scripted stream automatically under --mcp.)");
        list.push(tool("connect",
                       "Wire a board's serial unit to a host endpoint -- the same schemes CONNECT "
                       "accepts at the prompt.",
                       p, {"id", "unit", "endpoint"}));
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
            // A key with more than one spelling says so, and says which one is real:
            // `name` is what an agent should WRITE (it is what CONFIG SAVE writes back),
            // `aliases` is what it must be able to READ in somebody's existing file.
            if (!p.aliases.empty()) {
                Json a = Json::arr();
                for (const auto& x : p.aliases) a.push(Json(x));
                j["aliases"] = a;
            }
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

// WHY a step/run came back, as one lowercase word -- the structured twin of the
// monitor's reportStop() prose (debug.h StopReason). Kept here rather than shared with
// the run tool's inline strings because those name things the STEP loop cannot see (a
// prompt is a console fact, not a StopReason) -- this covers exactly what m.debug.run
// reports.
const char* stopReasonName(StopReason w) {
    switch (w) {
    case StopReason::Steps:        return "steps";
    case StopReason::Breakpoint:   return "breakpoint";
    case StopReason::Halted:       return "halt";
    case StopReason::Attn:         return "attn";
    case StopReason::InputEnded:   return "input-ended";
    case StopReason::Interrupted:  return "interrupted";
    case StopReason::WindowClosed: return "window-closed";
    case StopReason::NoCpu:        return "no-cpu";
    case StopReason::StepTarget:   return "step-target";
    case StopReason::Unclaimed:    return "unclaimed";
    case StopReason::TapeStop:     return "tape-stop";
    }
    return "?";
}

// A bus cycle's type as a lowercase word, for bus_trace's structured rows. The
// monitor's cycleName is file-static in bus.cpp and human-spelled ("MEM R"); this is
// the machine-readable spelling, kept beside its consumer.
const char* cycleTypeName(Cycle t) {
    switch (t) {
    case Cycle::MemRead:  return "memread";
    case Cycle::MemWrite: return "memwrite";
    case Cycle::IoRead:   return "ioread";
    case Cycle::IoWrite:  return "iowrite";
    case Cycle::IntAck:   return "inta";
    }
    return "?";
}

// The BREAK kinds this tool can arm -- the address/port family only. A device-event
// kind (TAPE STOP) is not an address and is reached through the monitor tool. Null if
// `s` is not one we accept.
bool breakKindFromName(const std::string& s, BreakKind& out) {
    if (s == "pc")       { out = BreakKind::Pc;       return true; }
    if (s == "memread")  { out = BreakKind::MemRead;  return true; }
    if (s == "memwrite") { out = BreakKind::MemWrite; return true; }
    if (s == "ioread")   { out = BreakKind::IoRead;   return true; }
    if (s == "iowrite")  { out = BreakKind::IoWrite;  return true; }
    return false;
}

// The register file as {name: value}, plus a "H=xxxx " text rendering appended to
// `text`. Shared by the regs and step tools so the two cannot disagree about the shape.
Json regsObject(CpuCore* c, std::string& text) {
    Json regs = Json::obj();
    char buf[64];
    for (const RegDef& r : c->registers()) {
        uint32_t v = r.get();
        regs[r.name] = Json((long long)v);
        std::snprintf(buf, sizeof buf, "%s=%X ", r.shown().c_str(), v);
        text += buf;
    }
    return regs;
}

// One breakpoint as JSON, the shape breakpoints(list) and breakpoints(add) both return.
Json breakpointJson(const Breakpoint& b) {
    Json j = Json::obj();
    j["id"]      = Json((long long)b.id);
    j["kind"]    = Json(breakKindName(b.kind));
    j["lo"]      = Json((long long)b.lo);
    j["hi"]      = Json((long long)b.hi);
    j["enabled"] = Json(b.enabled);
    j["action"]  = Json(breakActionName(b.action));
    j["hits"]    = Json((long long)b.hits);
    j["describe"] = Json(b.describe());
    return j;
}

// The interactive console the four live tools share. Non-owning: the chip owns the
// ScriptedStream; we remember only WHICH channel it is, and re-fetch the live pointer
// every call so a reconnect can never leave us holding a dangling one.
struct McpSession {
    std::string conBoard;
    std::string conUnit;
    // `--mirror socket:PORT[?ro]`: when set, the console is `scripted` WRAPPED in a
    // MirrorStream so a human can telnet in and share the session (issue #381). Empty =
    // the bare scripted line. Set once at startup (runMcp), read by console().
    std::string mirror;
};

// The scripted line the interactive tools drive -- reached THROUGH whatever wraps it.
// Bare, the unit's stream IS the ScriptedStream; the console binding wraps it in the
// console's transform FilterStream (always) and, under --mirror, a MirrorStream too, so
// the stack is Filter -> [Mirror ->] Scripted. Peel any of those decorators to reach the
// ScriptedStream the guest ultimately talks to, which is the one we feed()/out().
ScriptedStream* asScripted(ByteStream* s) {
    while (s) {
        if (auto* ss = dynamic_cast<ScriptedStream*>(s)) return ss;
        if (auto* f = dynamic_cast<FilterStream*>(s)) { s = f->inner(); continue; }
        if (auto* mir = dynamic_cast<MirrorStream*>(s)) { s = mir->inner(); continue; }
        return nullptr;
    }
    return nullptr;
}

// Find the serial unit wired to the host console and REBIND it to an in-memory
// ScriptedStream. Under --mcp there is no terminal, so "console" would aim the guest's
// keyboard at the JSON-RPC pipe and hang the first run forever (monitor.cpp:818); a
// scripted line is one we can feed() and read out() instead. Under --mirror the scripted
// line is wrapped in a socket mirror (scripted|socket:PORT), transparently -- we still
// return the inner scripted, so the run loop is unchanged. Idempotent: once a channel is
// ours, later calls just re-fetch it. Null + err if the machine has no such line.
ScriptedStream* console(Machine& m, McpSession& s, std::string& err) {
    if (!s.conBoard.empty())
        if (Board* b = m.find(s.conBoard))
            if (auto* ss = asScripted(b->unitStream(s.conUnit)))
                return ss;

    // Bare `scripted`, or `scripted|socket:PORT` when a mirror was asked for. The mirror
    // rides the same tap grammar the resolver already knows (host/endpoint.cpp).
    const std::string baseSpec = s.mirror.empty() ? std::string("scripted")
                                                   : "scripted|" + s.mirror;

    for (const auto& b : m.boards())
        for (const auto& u : b->units()) {
            if (u.kind != UnitKind::Serial) continue;
            if (u.state != "console") continue;

            // Wrap the scripted (optionally mirrored) line in the CONSOLE's transform chain,
            // so the AI -- and any mirror watcher -- see what a terminal would, not the raw
            // 8-bit wire. Without it, a machine like `ps2` (strip7out=on, upper=on) reads
            // back as bit-7 parity junk (0x4F 'O' -> 0xCF). The transforms are the console's,
            // applied to the console's stand-in; the endpoint grammar deliberately cannot
            // express a filter (host/filter.h), so it is installed as a pre-built stream.
            auto base = resolveEndpoint(baseSpec, err);
            if (!base) return nullptr;
            auto filt = std::make_unique<FilterStream>(std::move(base));
            filt->copySettingsFrom(Console::instance().filter());

            // connectStream takes the pre-built, filtered stack. A board not taught the seam
            // refuses; fall back to the bare line -- no transforms, but no regression. (Every
            // shipped machine with console transforms consoles on a 2sio or sio, both taught.)
            if (!b->connectStream(u.name, std::move(filt), err)) {
                if (!b->connect(u.name, baseSpec, err)) return nullptr;
            }
            s.conBoard = b->id;
            s.conUnit  = u.name;
            if (auto* ss = asScripted(b->unitStream(u.name))) return ss;
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

        Json bytes = Json::arr();
        std::string text;
        for (uint32_t A = lo; A <= hi && A <= 0xFFFF; ++A) {
            uint8_t v = m.bus.memRead((uint16_t)A);
            bytes.push(Json((long long)v));
            std::snprintf(buf, sizeof buf, "%02X ", v);
            text += buf;
        }
        Json d = Json::obj();
        d["lo"] = Json((long long)lo);
        d["hi"] = Json((long long)hi);
        d["bytes"] = bytes;
        return dataResult(d, text);
    }

    if (name == "mem_deposit") {
        uint32_t A = (uint32_t)args.at("addr").integer();
        std::vector<uint8_t> bytes;
        if (!parseBytes(args.at("bytes").str(), bytes))
            return textResult("bytes must be hex, e.g. \"C3 00 2C\"", true);
        bool rom = args.has("rom") && args.at("rom").boolean();

        int discarded = 0;
        for (size_t k = 0; k < bytes.size(); ++k) {
            if (rom) {
                std::string why;
                if (!m.burn((uint16_t)(A + k), bytes[k], why)) return textResult(why, true);
            } else {
                m.bus.memWrite((uint16_t)(A + k), bytes[k]);
                if (m.bus.lastUnclaimed()) ++discarded;
            }
        }
        Json d = Json::obj();
        d["addr"] = Json((long long)A);
        d["written"] = Json((long long)bytes.size());
        d["discarded"] = Json((long long)discarded);
        d["rom"] = Json(rom);

        std::string text = std::to_string(bytes.size()) + " byte(s) written";
        if (discarded) {
            // The single most important thing this tool can tell an agent, and
            // it must never be silent about it.
            text += "; " + std::to_string(discarded) +
                    " landed NOWHERE -- no board decodes a write there. That address is ROM "
                    "or unmapped. A ROM does not reject the write; it never answers the cycle. "
                    "To program it anyway, pass rom=true (the PROM burner).";
        }
        return dataResult(d, text);
    }

    if (name == "mem_load") {
        std::string path = args.at("path").str();
        std::ifstream f(path, std::ios::binary);
        if (!f) return textResult("cannot open '" + path + "'", true);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        // Same rule as the prompt's LOAD, because it is the same operation: the file's
        // contents decide, and `format` overrides them.
        int forced = -1;  // -1 autodetect, 0 BIN, 1 HEX
        if (args.has("format")) {
            std::string want = args.at("format").str();
            for (char& c : want) c = (char)std::toupper((unsigned char)c);
            if (want == "HEX") forced = 1;
            else if (want == "BIN") forced = 0;
            else return textResult("format must be BIN or HEX", true);
        }

        Image img;
        std::string err;
        if (forced == 1 || (forced < 0 && looksLikeHex(data))) {
            if (!loadHex(data, img, err)) return textResult(path + ": " + err, true);
            // `at` was accepted and then IGNORED here for a HEX file, silently. It
            // relocates, exactly as it does at the prompt (hex.h).
            if (args.has("at")) relocateTo(img, (uint32_t)args.at("at").integer());
        } else if (forced < 0 && looksLikeSrec(data)) {
            // An S-record carries its own addresses like Intel HEX, so `at` relocates.
            if (!loadSrec(data, img, err)) return textResult(path + ": " + err, true);
            if (args.has("at")) relocateTo(img, (uint32_t)args.at("at").integer());
        } else {
            if (!args.has("at"))
                return textResult(path + " is a flat binary and carries no addresses -- pass `at`",
                                  true);
            loadBin(data, (uint32_t)args.at("at").integer(), img);
        }

        bool rom = args.has("rom") && args.at("rom").boolean();

        int discarded = 0;
        for (const auto& [A, v] : img.bytes) {
            if (rom) {
                std::string why;
                if (!m.burn((uint16_t)A, v, why)) {
                    std::snprintf(buf, sizeof buf, "%04X: ", A);
                    return textResult(buf + why, true);
                }
            } else {
                m.bus.memWrite((uint16_t)A, v);
                if (m.bus.lastUnclaimed()) ++discarded;
            }
        }
        Json d = Json::obj();
        d["bytes"] = Json((long long)img.size());
        d["lo"] = Json((long long)img.lo());
        d["hi"] = Json((long long)img.hi());
        d["discarded"] = Json((long long)discarded);
        d["rom"] = Json(rom);
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
        std::string text;
        d["registers"] = regsObject(c, text);
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

        // THROTTLE TO THE CRYSTAL, exactly as runMachine does (monitor.cpp). By default
        // clock_hz is 0 -- the clock is free() -- and this loop runs the guest FLAT OUT, which
        // is what a fast CP/M boot and interactive prompt-driving want. But `SET cpu0
        // clock_hz=N` under --mcp is a request to make the machine real-time, and the reason it
        // is asked for is a real serial device: a bench peer answers a read hundreds of ms
        // later, in WALL time, and a guest polling for that reply with a software timeout burns
        // that timeout in microseconds if we sprint. Paced, its emulated timeout spends real
        // wall-clock, m.pump() reads the port meanwhile, and the reply lands while it is still
        // waiting -- the same bargain the standalone monitor RUN already makes (#424). The
        // deadline below still bounds the call; pacing only declines to do all the emulated work
        // at once. Baseline is per-call: each run() re-bases, and there is no idle nap to rebase
        // against (this loop STOPS on idle rather than napping).
        const long long hz     = m.clock.hz();
        const uint64_t  startT = m.clock.now();
        const auto      start  = clk::now();

        // IS A REAL DEVICE ON A LINE? -- a serial cable, a socket, anything but the MCP console
        // and an empty jack. It changes what "the guest is doing nothing" means: on such a wire,
        // silence is usually the guest waiting on a reply that arrives hundreds of ms later, or
        // the gap between two blocks of a disk read, and a byte can land at any moment in WALL
        // time no matter how the CPU is clocked (#424).
        bool hasLiveWire = false;
        for (const auto& b : m.boards())
            for (const auto& u : b->units()) {
                if (u.kind != UnitKind::Serial) continue;
                if (b->id == sess.conBoard && u.name == sess.conUnit) continue;  // the MCP console
                if (u.state == "null") continue;                                 // nothing plugged in
                hasLiveWire = true;
            }

        // The wall-clock grace a live wire buys: idle is not declared, and an active transfer is
        // not cut off at the budget, until the wire has been silent this long. Zero with no
        // device -- there the instruction-count rule alone decides idle, exactly as before, so we
        // still "idle on instruction count" for a plain interactive prompt.
        const auto idleDwell = hasLiveWire ? std::chrono::milliseconds(5000)
                                           : std::chrono::milliseconds(0);

        const auto deadline     = start + std::chrono::milliseconds(timeout);
        const auto hardDeadline = start + std::chrono::milliseconds(600000);  // absolute 10-min cap
        std::string     out;
        uint64_t        steps = 0;
        int             quietSlices = 0;         // consecutive quiet slices -- the instruction-count rule
        clk::time_point idleSince{};             // when this unbroken run of quiet began; unset = busy
        clk::time_point lastRxAt{};              // wall time of the last byte in on any line; unset = none
        std::string     stopped;

        auto drain = [&] {
            const std::string& o = con->out();
            if (!o.empty()) { out += o; con->clearOut(); }
        };

        for (;;) {
            drain();
            if (!until.empty() && out.find(until) != std::string::npos) { stopped = "match"; break; }
            if (clk::now() >= deadline) {
                // The budget is up -- but do not cut off a transfer that is still streaming. On a
                // live wire, a byte received within idleDwell means the guest is mid-transaction
                // (a boot loader pulling 512-byte blocks over a 38.4k serial line runs many
                // seconds), so let it finish and return only once the wire has genuinely gone
                // quiet. The absolute ceiling still bounds a peer that never stops talking.
                const bool activeTransfer = hasLiveWire && lastRxAt != clk::time_point{} &&
                                            clk::now() - lastRxAt < idleDwell;
                if (!activeTransfer || clk::now() >= hardDeadline) { stopped = "timeout"; break; }
            }
            if (maxSteps && steps >= maxSteps) { stopped = "steps"; break; }

            const uint64_t rxBefore     = m.rxBytes();
            const uint64_t hungryBefore = con->hungry();
            RunResult r = m.debug.run(2000);
            m.pump();
            steps += r.steps;

            // Keep wall-clock in step with the crystal (see the baseline above). Only when a
            // clock_hz was asked for; free() is the flat-out default and never sleeps here.
            if (!m.clock.free()) {
                double want = (double)(m.clock.now() - startT) / (double)hz;
                double got  = std::chrono::duration<double>(clk::now() - start).count();
                if (want > got)
                    std::this_thread::sleep_for(std::chrono::duration<double>(want - got));
            }

            const size_t wroteBefore = out.size();
            drain();
            const bool produced = out.size() != wroteBefore;
            const bool received = m.rxBytes() != rxBefore;
            const uint64_t hungry = con->hungry() - hungryBefore;
            if (received) lastRxAt = clk::now();  // the wire is live -- keep the transfer going

            if (r.why == StopReason::Halted)     { stopped = "halt";       break; }
            if (r.why == StopReason::Breakpoint) { stopped = "breakpoint"; break; }
            if (r.why == StopReason::NoCpu)      { stopped = "no-cpu";      break; }

            // IDLE-STOP -- hand control back when the guest has nothing to do, so the AI is not
            // made to wait out timeout_ms for its next command. Gated on clock.idle() like
            // runMachine's nap (monitor.cpp): `SET cpu0 idle=off` turns it off entirely. A slice
            // counts as quiet when the guest said nothing, received nothing on ANY line (rxBytes,
            // the whole backplane), and kept coming to the console and finding it empty. It takes
            // BOTH signals to stop: the instruction-count spin (two quiet slices -- fast, and all
            // a plain interactive prompt needs) AND, when a device is on the wire, that the wire
            // has stayed silent for idleDwell of WALL time -- so a gap between a request and its
            // reply, or between two blocks of a disk read, is not mistaken for a prompt (#424).
            // idleDwell is 0 with no device, so there the two-slice rule alone decides, unchanged.
            const bool quiet = m.clock.idle() && con->drained() && !produced && !received &&
                               r.steps != 0 && hungry * kIdleRatio >= r.steps;
            if (!quiet) {
                quietSlices = 0;
                idleSince   = clk::time_point{};                   // it did something -- start over
            } else {
                ++quietSlices;
                if (idleSince == clk::time_point{}) idleSince = clk::now();  // first quiet slice
                if (quietSlices >= 2 && clk::now() - idleSince >= idleDwell) {
                    stopped = "idle";
                    break;
                }
            }
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
        mon.setMcpMode(true);  // RUN parks instead of blocking -- a bare RUN (or the RUN a
                               // CONFIG LOAD startup ends in) would otherwise wedge the server.
        mon.exec(args.at("command").str(), os);
        return textResult(os.str().empty() ? "(ok)" : os.str(), mon.failed());
    }

    if (name == "step") {
        CpuCore* c = m.cpu();
        if (!c) return textResult("no CPU in this machine", true);
        uint64_t n = args.has("count") ? (uint64_t)args.at("count").integer() : 1;
        if (n == 0) n = 1;

        RunResult r;
        uint64_t steps = 0, tStates = 0;
        for (uint64_t i = 0; i < n; ++i) {
            r = m.debug.run(1);
            steps += r.steps;
            tStates += r.tStates;
            if (r.why != StopReason::Steps) break;  // HLT, breakpoint -- stop early
        }
        m.pump();  // reflect the resting bus cycle on the panel, as monitor STEP does

        Json d = Json::obj();
        std::string text;
        d["registers"] = regsObject(c, text);
        d["steps"]    = Json((long long)steps);
        d["t_states"] = Json((long long)tStates);
        d["pc"]       = Json((long long)c->pc());
        d["halted"]   = Json(c->halted());
        d["stopped"]  = Json(stopReasonName(r.why));
        text += "[" + std::to_string(steps) + " insn, " + std::to_string(tStates) +
                " T; stopped: " + stopReasonName(r.why) + "]";
        return dataResult(d, text);
    }

    if (name == "disasm") {
        std::string isa = args.has("cpu") ? args.at("cpu").str() : m.isa();
        if (isa.empty())
            return textResult("no CPU in this machine -- pass cpu (e.g. 8080) to say how to "
                              "decode these bytes.", true);
        const Disassembler* dis = disassemblerFor(isa);
        if (!dis) {
            std::string known;
            for (const auto& s : instructionSets()) known += " " + s;
            return textResult("no instruction set '" + isa + "'. Known:" + known, true);
        }
        auto peek = [&](uint16_t a) { return m.bus.peek(a); };

        uint32_t at   = (uint32_t)args.at("addr").integer();
        bool     rng  = args.has("hi");
        uint32_t hi   = rng ? (uint32_t)args.at("hi").integer() : 0;
        uint32_t cnt  = args.has("count") ? (uint32_t)args.at("count").integer() : 16;

        Json lines = Json::arr();
        std::string text;
        for (uint32_t i = 0; (rng ? at <= hi : i < cnt) && at <= 0xFFFF; ++i) {
            Insn in = dis->at((uint16_t)at, peek, 16);
            Json line = Json::obj();
            line["addr"] = Json((long long)at);
            Json b = Json::arr();
            std::string hexbytes;
            for (int k = 0; k < in.len; ++k) {
                uint8_t v = peek((uint16_t)(at + (uint32_t)k));
                b.push(Json((long long)v));
                std::snprintf(buf, sizeof buf, "%02X ", v);
                hexbytes += buf;
            }
            line["bytes"] = b;
            line["text"]  = Json(in.text);
            line["len"]   = Json((long long)in.len);
            line["undocumented"] = Json(in.undocumented);
            lines.push(line);
            std::snprintf(buf, sizeof buf, "%04X  %-9s %s%s\n", at, hexbytes.c_str(),
                          in.text.c_str(), in.undocumented ? "   ; undocumented" : "");
            text += buf;
            at += in.len;
        }
        Json d = Json::obj();
        d["lines"] = lines;
        return dataResult(d, text);
    }

    if (name == "mem_fill") {
        uint32_t lo = (uint32_t)args.at("lo").integer();
        uint32_t hi = (uint32_t)args.at("hi").integer();
        uint8_t  v  = (uint8_t)args.at("byte").integer();
        bool     rom = args.has("rom") && args.at("rom").boolean();

        uint32_t written = 0, discarded = 0;
        for (uint32_t A = lo; A <= hi && A <= 0xFFFF; ++A) {
            if (rom) {
                std::string why;
                if (!m.burn((uint16_t)A, v, why)) {
                    std::snprintf(buf, sizeof buf, "%04X: ", A);
                    return textResult(buf + why, true);
                }
            } else {
                m.bus.memWrite((uint16_t)A, v);
                if (m.bus.lastUnclaimed()) ++discarded;
            }
            ++written;
        }
        Json d = Json::obj();
        d["lo"] = Json((long long)lo);
        d["hi"] = Json((long long)hi);
        d["byte"] = Json((long long)v);
        d["written"] = Json((long long)written);
        d["discarded"] = Json((long long)discarded);
        d["rom"] = Json(rom);
        std::snprintf(buf, sizeof buf, "filled %04X-%04X with %02X", lo, hi, v);
        std::string text = buf;
        if (discarded)
            text += "; " + std::to_string(discarded) +
                    " cell(s) landed NOWHERE (ROM or unmapped). Pass rom=true to burn a ROM.";
        return dataResult(d, text);
    }

    if (name == "mem_search") {
        uint32_t lo = (uint32_t)args.at("lo").integer();
        uint32_t hi = (uint32_t)args.at("hi").integer();
        std::vector<uint8_t> pat;
        if (args.has("bytes")) {
            if (!parseBytes(args.at("bytes").str(), pat))
                return textResult("bytes must be hex, e.g. \"C3 00 F8\"", true);
        } else if (args.has("text")) {
            for (char ch : args.at("text").str()) pat.push_back((uint8_t)ch);
            if (pat.empty()) return textResult("text is empty", true);
        } else {
            return textResult("give a pattern: bytes (hex) or text (ASCII)", true);
        }
        if (hi > 0xFFFF) hi = 0xFFFF;

        Json matches = Json::arr();
        std::string text;
        if (lo + pat.size() - 1 <= hi) {
            for (uint32_t A = lo; A + (uint32_t)pat.size() - 1 <= hi; ++A) {
                bool hit = true;
                for (size_t k = 0; k < pat.size(); ++k)
                    if (m.bus.peek((uint16_t)(A + k)) != pat[k]) { hit = false; break; }
                if (hit) {
                    matches.push(Json((long long)A));
                    std::snprintf(buf, sizeof buf, "%04X ", A);
                    text += buf;
                }
            }
        }
        Json d = Json::obj();
        d["matches"] = matches;
        d["count"] = Json((long long)matches.items().size());
        return dataResult(d, matches.items().empty() ? "no match" : text);
    }

    if (name == "mem_save") {
        std::string path = args.at("path").str();
        uint32_t lo = (uint32_t)args.at("lo").integer();
        uint32_t hi = (uint32_t)args.at("hi").integer();
        if (hi > 0xFFFF) hi = 0xFFFF;

        // The name decides, FORMAT overrides -- the same rule the monitor's SAVE uses,
        // and deliberately not mem_load's (a file that does not exist yet cannot be
        // sniffed, so a name is all there is to go on).
        bool asHex = false;
        std::string uname = path;
        for (char& ch : uname) ch = (char)std::toupper((unsigned char)ch);
        if (uname.size() > 4 && uname.rfind(".HEX") == uname.size() - 4) asHex = true;
        if (args.has("format")) {
            std::string want = args.at("format").str();
            for (char& ch : want) ch = (char)std::toupper((unsigned char)ch);
            if (want == "HEX") asHex = true;
            else if (want == "BIN") asHex = false;
            else return textResult("format must be BIN or HEX", true);
        }

        Image img;
        for (uint32_t A = lo; A <= hi; ++A) img.bytes[A] = m.bus.peek((uint16_t)A);

        std::ofstream f(path, std::ios::binary);
        if (!f) return textResult("cannot write '" + path + "'", true);
        if (asHex) {
            f << saveHex(img);
        } else {
            for (uint32_t A = lo; A <= hi; ++A) f.put((char)m.bus.peek((uint16_t)A));
        }
        if (!f) return textResult("write failed on '" + path + "'", true);

        Json d = Json::obj();
        d["path"] = Json(path);
        d["lo"] = Json((long long)lo);
        d["hi"] = Json((long long)hi);
        d["bytes"] = Json((long long)(hi - lo + 1));
        d["format"] = Json(asHex ? "HEX" : "BIN");
        std::snprintf(buf, sizeof buf, "saved %04X-%04X (%u bytes) as %s to %s", lo, hi,
                      hi - lo + 1, asHex ? "HEX" : "BIN", path.c_str());
        return dataResult(d, buf);
    }

    if (name == "breakpoints") {
        std::string action = args.has("action") ? args.at("action").str() : "list";

        if (action == "list") {
            Json a = Json::arr();
            std::string text;
            for (const Breakpoint& b : m.debug.breakpoints()) {
                a.push(breakpointJson(b));
                text += b.describe() + "\n";
            }
            Json d = Json::obj();
            d["breakpoints"] = a;
            return dataResult(d, a.items().empty() ? "(no breakpoints)" : text);
        }
        if (action == "add") {
            BreakKind kind{};  // set by breakKindFromName below; init keeps MSVC /W4 (C4701) quiet
            if (!args.has("kind") || !breakKindFromName(args.at("kind").str(), kind))
                return textResult("add needs kind: pc | memread | memwrite | ioread | iowrite",
                                  true);
            if (!args.has("lo")) return textResult("add needs lo (the address or port)", true);
            uint32_t lo = (uint32_t)args.at("lo").integer();
            uint32_t hi = args.has("hi") ? (uint32_t)args.at("hi").integer() : lo;
            int id = m.debug.add(kind, lo, hi);
            for (const Breakpoint& b : m.debug.breakpoints())
                if (b.id == id) return dataResult(breakpointJson(b), b.describe());
            return textResult("added", false);
        }
        if (action == "remove") {
            if (!args.has("id")) return textResult("remove needs id (see the list)", true);
            std::string err;
            if (!m.debug.remove((int)args.at("id").integer(), err))
                return textResult(err, true);
            Json d = Json::obj();
            d["removed"] = args.at("id");
            return dataResult(d, "breakpoint " + std::to_string(args.at("id").integer()) +
                                     " removed");
        }
        if (action == "clear") {
            m.debug.clear();
            return textResult("all breakpoints cleared");
        }
        return textResult("action is list, add, remove or clear", true);
    }

    if (name == "snapshot") {
        std::string path = args.at("path").str();
        std::string err;
        bool ok = m.snapshot(path, err);
        if (!ok) return textResult(err, true);
        Json d = Json::obj();
        d["path"] = Json(path);
        d["ok"] = Json(true);
        return dataResult(d, "snapshot written to " + path);
    }

    if (name == "restore") {
        std::string path = args.at("path").str();
        std::string err;
        bool ok = m.restore(path, err);
        if (!ok) return textResult(err, true);
        Json d = Json::obj();
        d["path"] = Json(path);
        d["ok"] = Json(true);
        return dataResult(d, "machine state restored from " + path);
    }

    if (name == "bus_irq") {
        Json d = Json::obj();
        std::string text;

        if (CpuCore* c = m.cpu()) {
            d["inte"] = Json(c->interruptsEnabled());
            text += std::string("INTE ") + (c->interruptsEnabled() ? "on" : "off") + "\n";
        }
        d["int_pending"] = Json(m.bus.intPending());

        // pin 73: who is pulling it right now.
        Json pin73boards = Json::arr();
        for (const auto& b : m.boards())
            if (b->enabled() && b->assertsInt()) pin73boards.push(Json(b->id));
        Json pin73 = Json::obj();
        pin73["asserted"] = Json(m.bus.intPending());
        pin73["boards"] = pin73boards;
        d["pin73"] = pin73;
        text += m.bus.intPending() ? "pINT ASSERTED" : "pINT idle";
        for (const auto& id : pin73boards.items()) text += " " + id.str();
        text += "\n";

        // The eight VI wires.
        uint8_t lines = m.bus.viLines();
        Json vi = Json::arr();
        for (int i = 0; i < 8; ++i) {
            Json j = Json::obj();
            j["line"] = Json((long long)i);
            j["pulling"] = Json((bool)((lines >> i) & 1));
            vi.push(j);
        }
        d["vi_lines"] = vi;
        d["vi_mask"] = Json((long long)lines);

        // Which level an 88-VI would acknowledge, and the RST opcode it jams.
        for (const auto& b : m.boards()) {
            int win = b->enabled() ? b->intWinner() : -1;
            if (win >= 0) {
                Json w = Json::obj();
                w["level"] = Json((long long)win);
                w["encoder"] = Json(b->id);
                uint8_t rst = (uint8_t)(0xC7 | ((win & 7) << 3));  // RST n
                w["vector"] = Json((long long)rst);
                d["winner"] = w;
                std::snprintf(buf, sizeof buf, "winner: level %d via %s -> RST %d (%02X)\n",
                              win, b->id.c_str(), win, rst);
                text += buf;
                break;
            }
        }
        return dataResult(d, text);
    }

    if (name == "bus_trace") {
        size_t n = args.has("count") ? (size_t)args.at("count").integer() : 64;
        auto recs = m.debug.history(n);
        const auto& handles = m.debug.boardHandles();

        Json a = Json::arr();
        std::string text;
        for (const auto& r : recs) {
            Json j = Json::obj();
            j["t"] = Json((long long)r.t);
            j["type"] = Json(cycleTypeName(r.type));
            j["addr"] = Json((long long)r.addr);
            j["data"] = Json((long long)r.data);
            j["dma"] = Json(r.dma);
            j["contended"] = Json(r.contended);
            j["master"] = Json(r.master < 0 ? std::string("cpu")
                                            : handles[(size_t)r.master]);
            if (r.responder < 0) j["responder"] = Json();  // floating bus -- nobody drove
            else j["responder"] = Json(handles[(size_t)r.responder]);
            a.push(j);
            text += Debugger::formatCycle(r, handles) + "\n";
        }
        Json d = Json::obj();
        d["cycles"] = a;
        d["count"] = Json((long long)a.items().size());
        return dataResult(d, a.items().empty()
                                 ? "(no cycles recorded -- run or step the guest first)"
                                 : text);
    }

    if (name == "mount") {
        Board* b = m.find(args.at("id").str());
        if (!b) return textResult("no board '" + args.at("id").str() + "'", true);
        std::string unit = args.at("unit").str();
        std::string path = args.at("path").str();
        bool wp = args.has("write_protect") && args.at("write_protect").boolean();

        if (args.has("create") && args.at("create").boolean()) {
            std::ifstream exists(path, std::ios::binary);
            if (!exists) {
                std::ofstream mk(path, std::ios::binary);  // touch it empty
                if (!mk) return textResult("cannot create '" + path + "'", true);
            }
        }
        std::string err;
        if (!b->mount(unit, path, wp, err)) return textResult(err, true);
        Json d = Json::obj();
        d["id"] = Json(b->id);
        d["unit"] = Json(unit);
        d["path"] = Json(path);
        d["write_protect"] = Json(wp);
        return dataResult(d, b->id + ":" + unit + " <- " + path + (wp ? " (WP)" : ""));
    }

    if (name == "connect") {
        Board* b = m.find(args.at("id").str());
        if (!b) return textResult("no board '" + args.at("id").str() + "'", true);
        std::string unit = args.at("unit").str();
        std::string endpoint = args.at("endpoint").str();
        std::string err;
        if (!b->connect(unit, endpoint, err)) return textResult(err, true);
        Json d = Json::obj();
        d["id"] = Json(b->id);
        d["unit"] = Json(unit);
        d["endpoint"] = Json(endpoint);
        return dataResult(d, b->id + ":" + unit + " <-> " + endpoint);
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

int runMcp(Machine& m, std::istream& in, std::ostream& out, const std::string& mirror) {
    // Take the console off "console" NOW, before anything runs: under --mcp stdin is the
    // JSON-RPC channel, not a keyboard, and a guest reading it would eat our next request.
    // A scripted line is one the interactive tools own. Quietly does nothing if the
    // machine has no console line (an empty backplane, a socket-only machine).
    McpSession sess;
    sess.mirror = mirror;
    std::string bindErr;
    console(m, sess, bindErr);

    // A mirror that could not bind (its port is in use) is worth saying out loud -- but
    // to STDERR, never `out`, which is the JSON-RPC channel a stray line would corrupt.
    // The session still runs; the console just falls back to being un-rebound until a
    // tool call retries and surfaces the same error to the client.
    if (!mirror.empty() && !bindErr.empty())
        std::cerr << "altairsim: --mirror " << mirror << " failed: " << bindErr << "\n";

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
            // The version number alone, not the commit: this field is a SemVer string
            // by protocol, and a `git describe` is not one. SHOW VERSION is where the
            // commit lives, and an MCP client can run it.
            info["version"] = Json(versionNumber());
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
