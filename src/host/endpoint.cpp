#include "host/endpoint.h"

#include "core/value.h"
#include "host/console.h"
#include "host/file.h"
#include "host/hostserial.h"
#include "host/tcp.h"
#include "platform/serial.h"
#include "platform/socket.h"

#ifdef ALTAIRSIM_ENABLE_PRINTER
#include "host/printer_stream.h"
#include "platform/printer.h"
#endif

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace altair {
namespace {

// THE ONE PLACE THAT KNOWS THE GRAMMAR (DESIGN.md 7.7). No board may parse an
// endpoint string, which is why `CONNECT sio0:a serial:/dev/tty.usbserial-AL009KFH`
// needed not one line of code in the 2SIO.

bool parsePort(const std::string& s, uint16_t& out) {
    if (s.empty()) return false;
    char*         end = nullptr;
    unsigned long v   = std::strtoul(s.c_str(), &end, 10);
    if (*end || v == 0 || v > 65535) return false;
    out = (uint16_t)v;
    return true;
}

} // namespace

std::string endpointHelp(bool all) {
    std::string s = "console | null | loopback | scripted | socket:PORT | "
                    "socket:HOST:PORT | serial:DEVICE | in:PATH | out:PATH";
    // `printer:` only where a host print system was found at build time -- absent, the
    // grammar does not advertise a door it cannot open (docs/printing.md 3.1). The docs
    // generator passes all=true: the committed manual is one document for every platform,
    // so it lists the full grammar (the CONNECT gloss says "only where the build found
    // one") and the committed ref stays byte-identical whatever the build's printer flag.
#ifdef ALTAIRSIM_ENABLE_PRINTER
    const bool havePrinter = true;
#else
    const bool havePrinter = false;
#endif
    if (all || havePrinter) s += " | printer:QUEUE";
    return s;
}

std::string rebaseEndpointPaths(const std::string&                                    spec,
                                const std::function<std::string(const std::string&)>& rebase) {
    // Only in:/out: name a path; everything else is returned byte-for-byte.
    if (spec.rfind("in:", 0) != 0 && spec.rfind("out:", 0) != 0) return spec;

    std::string out;
    for (size_t start = 0; start <= spec.size();) {
        size_t      comma = spec.find(',', start);
        std::string part =
            spec.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

        // Rewrite the PATH inside a `<prefix>PATH[?opts]` part, leaving prefix and
        // options alone. A part that is neither in: nor out: is passed through -- the
        // resolver will reject it, with the operator's original text intact.
        size_t colon = part.find(':');
        bool   isIn  = part.rfind("in:", 0) == 0;
        bool   isOut = part.rfind("out:", 0) == 0;
        if ((isIn || isOut) && colon != std::string::npos) {
            std::string prefix = part.substr(0, colon + 1);
            std::string rest   = part.substr(colon + 1);
            std::string path   = rest, opts;
            if (size_t q = rest.find('?'); q != std::string::npos) {
                path = rest.substr(0, q);
                opts = rest.substr(q);  // keeps the leading '?'
            }
            if (!path.empty()) path = rebase(path);
            part = prefix + path + opts;
        }

        out += part;
        if (comma == std::string::npos) break;
        out += ',';
        start = comma + 1;
    }
    return out;
}

std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)> rebasingResolver(
    std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)> base,
    std::function<std::string(const std::string&)>                              rebase) {
    if (!rebase) return base;
    return [base = std::move(base), rebase = std::move(rebase)](const std::string& spec,
                                                                std::string&       err) {
        return base(rebaseEndpointPaths(spec, rebase), err);
    };
}

std::unique_ptr<ByteStream> resolveEndpoint(const std::string& spec, std::string& err) {
    if (spec == "console") return std::make_unique<ConsoleRef>();
    if (spec == "null") return std::make_unique<NullStream>();
    if (spec == "loopback") return std::make_unique<LoopbackStream>();

    // A terminal with a caller instead of a human: the two directions are separate,
    // so whoever holds the stream feed()s keystrokes and reads out() what the guest
    // printed. It is what the MCP server binds the console to (the guest cannot tell
    // it from a person), and what a test types into with no tty in the picture.
    if (spec == "scripted") return std::make_unique<ScriptedStream>();

    // ---- socket: -- listen on a port, or call out to a host ----
    if (spec.rfind("socket:", 0) == 0) {
        std::string rest = spec.substr(7);
        if (rest.empty()) {
            err = "socket: needs a port (socket:2323) or a host and port (socket:bbs.example:23)";
            return nullptr;
        }

        // `socket:2323` is a LISTEN; `socket:host:2323` is a CALL. The colon is the
        // whole of the distinction, and it is the same one every terminal program has
        // used for forty years.
        size_t c = rest.rfind(':');
        if (c == std::string::npos) {
            uint16_t port = 0;
            if (!parsePort(rest, port)) {
                err = "'" + rest + "' is not a TCP port number (1..65535)";
                return nullptr;
            }
            auto l = platform::listenTcp(port, err);
            if (!l) return nullptr;
            return std::make_unique<TcpListenStream>(std::move(l), spec);
        }

        std::string host = rest.substr(0, c);
        uint16_t    port = 0;
        if (host.empty() || !parsePort(rest.substr(c + 1), port)) {
            err = "expected socket:HOST:PORT, got '" + spec + "'";
            return nullptr;
        }
        auto conn = platform::connectTcp(host, port, err);
        if (!conn) return nullptr;
        return std::make_unique<TcpConnectStream>(std::move(conn), spec);
    }

    // ---- serial: -- a real port on this host ----
    if (spec.rfind("serial:", 0) == 0) {
        std::string dev = spec.substr(7);
        if (dev.empty()) {
            err = "serial: needs a device (serial:/dev/tty.usbserial-XXXX, serial:COM3)";
            for (const auto& p : platform::listSerialPorts()) err += "\n  " + p;
            return nullptr;
        }

        // Opened at a default 9600 8N1 -- and then IMMEDIATELY re-programmed by the
        // card, which is the only thing that knows what it is strapped to. See
        // ByteStream::setParams(): the 6850 calls it on connect, on a baud change, and
        // whenever the guest rewrites the control register, because those bits ARE the
        // frame that goes on the wire.
        platform::SerialConfig cfg;
        auto port = platform::openSerialPort(dev, cfg, err);
        if (!port) {
            // NAME WHAT IS ACTUALLY THERE. "cannot open /dev/ttyUSB0" with no further
            // help, on a machine where the cable enumerated under a different name, is
            // ten minutes of a person doubting the simulator.
            auto have = platform::listSerialPorts();
            if (!have.empty()) {
                err += "\nserial ports on this host:";
                for (const auto& p : have) err += "\n  " + p;
            }
            return nullptr;
        }
        return std::make_unique<HostSerialStream>(std::move(port), spec);
    }

    // ---- in:PATH / out:PATH -- a host file as a reader and/or a punch ----
    //
    // Direction is the keyword, never a flag: `in:` is a reader (a byte source),
    // `out:` a punch (a byte sink). One spec may carry BOTH, comma-joined, for a
    // unit whose single line is bidirectional (a 4PIO section, a 2SIO channel):
    // `in:reader.tap,out:punch.tap`. Two files, two positions -- which is exactly
    // why the eager-UART "one head" hazard (host/file.h, host/tape.h) cannot arise.
    //
    // A reader may be PACED: `in:tape.tap?cps=300` is the 88-HSR (issue #152).
    // Options ride the printer:'s `?key[=value][&key...]` grammar; `cps` is bytes
    // (characters) per second, `baud` the same rate as a line speed (10 bits per
    // character). Full speed is the default -- no option, no wall clock consulted.
    //
    // The PATH is opened as handed to us. A relative path is rebased against the
    // board's config dir BEFORE we see it (rebaseEndpointPaths) and the board keeps
    // the ORIGINAL spec for round-trip, so `spec` -- not any resolved path -- is
    // what describe() echoes. Typed at the prompt, the path is the shell's.
    if (spec.rfind("in:", 0) == 0 || spec.rfind("out:", 0) == 0) {
        std::optional<std::vector<uint8_t>> input;
        std::string inPath, outPath;
        uint64_t    nsPerByte = 0;
        bool        sawIn = false, sawOut = false, sawRate = false;

        // Parse one `?key[=value][&key...]` option string into the reader's rate.
        auto parseInOpts = [&](const std::string& query) -> bool {
            for (size_t start = 0; start <= query.size();) {
                size_t      amp = query.find('&', start);
                std::string tok =
                    query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
                if (!tok.empty()) {
                    size_t      eq  = tok.find('=');
                    std::string key = tok.substr(0, eq);
                    std::string val = eq == std::string::npos ? "" : tok.substr(eq + 1);
                    Value       v;
                    std::string perr;
                    if (key == "cps" || key == "baud") {
                        if (sawRate) {
                            err = "in: cps and baud are two spellings of one rate -- give one";
                            return false;
                        }
                        if (!parseValue(val, Kind::Int, v, perr) || v.i() <= 0) {
                            err = "in: " + key + " wants a positive rate: " + perr;
                            return false;
                        }
                        // cps is bytes/sec; baud is a line rate at 10 bits/character.
                        nsPerByte = key == "cps" ? 1000000000ull / (uint64_t)v.i()
                                                 : 1000000000ull * 10 / (uint64_t)v.i();
                        sawRate = true;
                    } else {
                        err = "in: unknown option '" + key + "'. Options are cps, baud.";
                        return false;
                    }
                }
                if (amp == std::string::npos) break;
                start = amp + 1;
            }
            return true;
        };

        for (size_t start = 0; start <= spec.size();) {
            size_t      comma = spec.find(',', start);
            std::string part =
                spec.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

            if (part.rfind("in:", 0) == 0) {
                if (sawIn) {
                    err = "in: given twice in '" + spec + "'";
                    return nullptr;
                }
                sawIn            = true;
                std::string rest = part.substr(3);
                std::string path = rest, query;
                if (size_t q = rest.find('?'); q != std::string::npos) {
                    path  = rest.substr(0, q);
                    query = rest.substr(q + 1);
                }
                if (path.empty()) {
                    err = "in: needs a path (in:reader.tap)";
                    return nullptr;
                }
                if (!query.empty() && !parseInOpts(query)) return nullptr;
                inPath = path;
            } else if (part.rfind("out:", 0) == 0) {
                if (sawOut) {
                    err = "out: given twice in '" + spec + "'";
                    return nullptr;
                }
                sawOut           = true;
                std::string path = part.substr(4);
                if (path.find('?') != std::string::npos) {
                    err = "out: takes no options (a punch runs at the line's speed)";
                    return nullptr;
                }
                if (path.empty()) {
                    err = "out: needs a path (out:punch.tap)";
                    return nullptr;
                }
                outPath = path;
            } else if (!part.empty()) {
                err = "'" + part + "' is not in: or out:. A file endpoint is in:PATH, "
                                   "out:PATH, or in:PATH,out:PATH";
                return nullptr;
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        // A reader slurps its file once (8-bit clean, binary), so readable() is a
        // pure question of "bytes left" and the position needs no live file handle.
        if (sawIn) {
            std::ifstream f(inPath, std::ios::binary);
            if (!f) {
                err = "cannot open '" + inPath + "' for reading";
                return nullptr;
            }
            input = std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                         std::istreambuf_iterator<char>());
        }

        // A punch opens WITHOUT truncating and seeks to 0: it overwrites forward and
        // extends past the end, never blanking the file up front. An absent file is
        // created (out|binary makes an empty one to reopen for update).
        std::fstream out;
        if (sawOut) {
            out.open(outPath, std::ios::in | std::ios::out | std::ios::binary);
            if (!out.is_open()) {
                std::ofstream create(outPath, std::ios::binary);  // make it, then reopen
                create.close();
                out.open(outPath, std::ios::in | std::ios::out | std::ios::binary);
            }
            if (!out.is_open()) {
                err = "cannot open '" + outPath + "' for writing";
                return nullptr;
            }
            out.seekp(0);
        }

        return std::make_unique<FileStream>(spec, std::move(input), std::move(out), nsPerByte);
    }

#ifdef ALTAIRSIM_ENABLE_PRINTER
    // ---- printer: -- a host print QUEUE, write-only, buffered into jobs ----
    //
    // The one genuinely new grammar this file has grown in a while, so it is spelled
    // out. `printer:QUEUE[?key[=value][&key...]]`: the FIRST '?' ends the queue name
    // (a queue name may contain spaces and colons, so nothing before it is special),
    // and everything after is '&'-separated key[=value]. A bare key is `=true`, so the
    // common case never types `=1`; the `=value` form still parses, because it is the
    // only way to write a boolean OFF. Values go through parseValue -- the same
    // true/yes/on/1 booleans and radix-aware integers every other setting accepts, so
    // no operator meets a second convention here. The buffering, the boundaries and
    // the "never submit an empty buffer" rule all live in PrinterStream.
    if (spec.rfind("printer:", 0) == 0) {
        std::string rest  = spec.substr(8);
        std::string queue = rest;
        std::string query;
        if (size_t q = rest.find('?'); q != std::string::npos) {
            queue = rest.substr(0, q);
            query = rest.substr(q + 1);
        }
        if (queue.empty()) {
            err = "printer: needs a queue name (printer:linewriter)";
            for (const auto& p : platform::listQueues()) err += "\n  " + p;
            return nullptr;
        }

        PrinterStream::Params params;  // defaults per docs/printing.md 2.3
        for (size_t start = 0; start <= query.size();) {
            size_t      amp = query.find('&', start);
            std::string tok =
                query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
            if (!tok.empty()) {
                size_t      eq  = tok.find('=');
                std::string key = tok.substr(0, eq);
                std::string val = eq == std::string::npos ? "true" : tok.substr(eq + 1);

                Value       v;
                std::string perr;
                if (key == "idle") {
                    if (!parseValue(val, Kind::Int, v, perr) || v.i() < 0) {
                        err = "printer: idle wants host seconds >= 0 (0 = never): " + perr;
                        return nullptr;
                    }
                    params.idleSeconds = (uint64_t)v.i();
                } else if (key == "max") {
                    if (!parseValue(val, Kind::Int, v, perr) || v.i() <= 0) {
                        err = "printer: max wants a positive byte ceiling: " + perr;
                        return nullptr;
                    }
                    params.maxBytes = (uint64_t)v.i();
                } else if (key == "onff") {
                    if (!parseValue(val, Kind::Bool, v, perr)) {
                        err = "printer: onff wants a boolean: " + perr;
                        return nullptr;
                    }
                    params.onFormFeed = v.b();
                } else {
                    err = "printer: unknown option '" + key +
                          "'. Options are idle, onff, max -- and the queue name comes before '?'";
                    return nullptr;
                }
            }
            if (amp == std::string::npos) break;
            start = amp + 1;
        }

        // The board never learns what a print queue is: it is handed a submit sink
        // already bound to this queue, and PrinterStream calls it at a job boundary.
        auto submit = [queue](const std::vector<uint8_t>& data, std::string& e) {
            return platform::printRaw(queue, data, e);
        };
        // describe() echoes `spec` verbatim, so SHOW and CONFIG SAVE round-trip what
        // the operator typed, options and all.
        return std::make_unique<PrinterStream>(spec, params, std::move(submit));
    }
#endif

    err = "no endpoint '" + spec + "'. Try: " + endpointHelp();
    return nullptr;
}

} // namespace altair
