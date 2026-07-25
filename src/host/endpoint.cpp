#include "host/endpoint.h"

#include "host/console.h"
#include "host/file.h"
#include "host/hostserial.h"
#include "host/tcp.h"
#include "platform/serial.h"
#include "platform/socket.h"

#ifdef ALTAIRSIM_ENABLE_PRINTER
#include "core/value.h"
#include "host/printer_stream.h"
#include "platform/printer.h"
#endif

#include <cstdlib>
#include <fstream>
#include <string>

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

std::string endpointHelp() {
    return "console | null | loopback | scripted | socket:PORT | socket:HOST:PORT | "
           "serial:DEVICE | file:PATH"
#ifdef ALTAIRSIM_ENABLE_PRINTER
           // Only where a host print system was found at build time. Absent, the
           // grammar does not advertise a door it cannot open (docs/printing.md 3.1).
           " | printer:QUEUE"
#endif
        ;
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

    // ---- file: -- a host file, write-only. A printout, or a capture of a line ----
    //
    // The path is opened exactly as handed to us. A RELATIVE path written in a
    // machine file is config-relative, but THAT rebasing is the board's job (it is
    // the only thing that knows its config dir; see Board::resolvePath and the mount
    // path in mits-hardsector.cpp) -- and the board keeps the ORIGINAL spec for
    // round-trip, which is why `spec` below, not the resolved path, is what the
    // stream describes. Typed at the prompt, the path is the shell's, which is just
    // the path as-is.
    if (spec.rfind("file:", 0) == 0) {
        std::string path = spec.substr(5);
        if (path.empty()) {
            err = "file: needs a path (file:printout.txt)";
            return nullptr;
        }
        // BINARY + trunc: 8-bit clean (a CR stays a CR on every host), opened fresh.
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot open '" + path + "' for writing";
            return nullptr;
        }
        return std::make_unique<FileStream>(std::move(out), spec);
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
