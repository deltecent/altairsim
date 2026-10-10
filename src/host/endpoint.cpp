#include "host/endpoint.h"

#include "core/value.h"
#include "host/console.h"
#include "host/file.h"
#include "host/hostserial.h"
#include "host/mirror_stream.h"
#include "host/pty_stream.h"
#include "host/tcp.h"
#include "host/tee_stream.h"
#include "host/telnet_stream.h"
#include "host/terminal/emulations.h"
#include "host/terminal/emulator.h"
#include "host/terminal/stream.h"
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

// THE ONE PLACE THAT KNOWS THE GRAMMAR (DESIGN.md 7.7). No board may parse an
// endpoint string, which is why `CONNECT sio0:a serial:/dev/tty.usbserial-AL009KFH`
// needed not one line of code in the 2SIO. These two are declared in endpoint.h so a
// board's property setter can VALIDATE a `host:port` string with the grammar's owner,
// still without the board learning the grammar (the PMMI's dial/answer settings).

bool parsePort(const std::string& s, uint16_t& out) {
    if (s.empty()) return false;
    char*         end = nullptr;
    unsigned long v   = std::strtoul(s.c_str(), &end, 10);
    if (*end || v == 0 || v > 65535) return false;
    out = (uint16_t)v;
    return true;
}

// The `socket:HOST:PORT` split, extracted so it has one home. rfind(':') on purpose:
// a bare host that happens to contain no colon is not a case we need, and a numeric
// IPv4 host never does. Empty host or a bad port is an error, not a silent default.
bool parseHostPort(const std::string& spec, std::string& host, uint16_t& port,
                   std::string& err) {
    size_t c = spec.rfind(':');
    if (c == std::string::npos) {
        err = "expected HOST:PORT, got '" + spec + "'";
        return false;
    }
    host = spec.substr(0, c);
    if (host.empty() || !parsePort(spec.substr(c + 1), port)) {
        err = "expected HOST:PORT, got '" + spec + "'";
        return false;
    }
    return true;
}

std::string endpointHelp(bool all) {
    std::vector<std::string> parts = {
        "console", "null", "loopback", "scripted", "socket:PORT[?banner]", "socket:HOST:PORT",
        "telnet:PORT[?banner=off]", "telnet:HOST:PORT", "serial:DEVICE",
    };
    // A pseudo-terminal only where the host has one (not Windows); the docs generator
    // lists it regardless, like `printer:` below.
    if (all || platform::havePty()) parts.emplace_back("pty[:LINK]");
    parts.insert(parts.end(), {"in:PATH", "out:PATH", "terminal[?emulation=vt100&size=80x24]"});
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
    if (all || havePrinter) parts.emplace_back("printer:QUEUE");
    // Any endpoint can be TAPPED: append `|FILE` to log the line to a hex file (a
    // poor man's protocol analyzer; host/tee_stream.h), or `|socket:PORT` to MIRROR it
    // live over TCP -- a human telnets in to watch and take over (host/mirror_stream.h,
    // issue #381). Advertised last so they read as a suffix on all of the above, not
    // peers of them.
    parts.emplace_back("<endpoint>|FILE");
    parts.emplace_back("<endpoint>|socket:PORT");
    // The pseudo-terminal mirror only where the host has pseudo-terminals (not Windows);
    // the docs generator lists it regardless, like `printer:` above.
    if (all || platform::havePty()) parts.emplace_back("<endpoint>|pty[:LINK]");
    // A real serial port as the mirror's sink (issue #683), on every host that has one.
    parts.emplace_back("<endpoint>|serial:DEVICE[?baud=N]");

    // Wrap the grammar so it does not run off the page -- the full list is one long line
    // that landed in the CONNECT help, the "no endpoint" error and the generated reference
    // all at once. The bar stays on the line it ends; a continuation is FLUSH LEFT, not
    // indented: the reference formatter fences any line that starts with two spaces (a
    // HELP example), and the live HELP shows this ahead of a two-space-indented gloss, so
    // an indent here would be read as a code block in one and as a gloss row in the other.
    const size_t width = 78;
    std::string  s     = parts[0];
    size_t       col   = parts[0].size();
    for (size_t i = 1; i < parts.size(); ++i) {
        if (col + 3 + parts[i].size() > width) {
            s += " |\n" + parts[i];
            col = parts[i].size();
        } else {
            s += " | " + parts[i];
            col += 3 + parts[i].size();
        }
    }
    return s;
}

std::string rebaseEndpointPaths(const std::string&                                    spec,
                                const std::function<std::string(const std::string&)>& rebase) {
    // A capture tap (ENDPOINT|FILE) carries a path on BOTH sides: the inner endpoint may
    // be an in:/out: file, and the log FILE itself is a path. Split on the `|` first,
    // rebase each independently, and rejoin -- otherwise a machine-file relative
    // `in:tape.tap|../logs/cap.hex` resolves its log against the shell cwd.
    // The LAST `|`: the right side is the outermost sink and the left side recurses, so a
    // tap on a mirror (`a|socket:2323|cap.log`) splits into `a|socket:2323` and `cap.log`.
    if (size_t bar = spec.rfind('|'); bar != std::string::npos) {
        std::string inner = spec.substr(0, bar);
        std::string file  = spec.substr(bar + 1);
        std::string path = file, opts;
        if (size_t q = file.find('?'); q != std::string::npos) {
            path = file.substr(0, q);
            opts = file.substr(q);  // keeps the leading '?'
        }
        // A `socket:` right side is a MIRROR sink, not a path -- rebasing it against the
        // config dir would corrupt the port spec. Only a FILE sink (the tee) is a path.
        // A `pty` right side is a mirror sink too; only the link after `pty:` is a path.
        if (path == "pty") {
        } else if (path.rfind("pty:", 0) == 0) {
            if (path.size() > 4) path = "pty:" + rebase(path.substr(4));
        } else if (!path.empty() && path.rfind("socket:", 0) != 0 &&
                   path.rfind("serial:", 0) != 0) {
            // (`serial:` is a mirror sink too: a device name, not a file in the config dir.)
            path = rebase(path);
        }
        return rebaseEndpointPaths(inner, rebase) + "|" + path + opts;
    }

    // `pty:LINK` names the link, and that is a path (issue #685). A bare `pty` has none.
    if (spec.rfind("pty:", 0) == 0)
        return spec.size() > 4 ? "pty:" + rebase(spec.substr(4)) : spec;

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
    return [baseFn = std::move(base), rebaseFn = std::move(rebase)](const std::string& spec,
                                                                    std::string&       err) {
        return baseFn(rebaseEndpointPaths(spec, rebaseFn), err);
    };
}

// The `socket:`/`telnet:` body, shared: `PORT` LISTENS, `HOST:PORT` CALLS OUT. The
// two schemes differ only in what wraps the result (telnet: adds the protocol layer),
// so the parse -- and every error message the operator sees -- has one home. `scheme`
// names the keyword for those messages; `spec` is the operator's full text, kept for
// describe() and the socket debug trace.
//
// `?banner` (a bare key is =true) greets each caller to a LISTEN with a one-line
// "Connected to ..." (host/tcp.h, ByteStream::greet). `bannerDefault` is the scheme's:
// on for telnet:, where a person is calling; off for socket:, the raw pipe another
// machine dials, where a banner would land in the far guest's input as data.
static std::unique_ptr<TcpStream> makeTcpStream(const std::string& body,
                                                const std::string& spec, const char* scheme,
                                                bool bannerDefault, std::string& err) {
    std::string rest = body, query;
    if (size_t q = body.find('?'); q != std::string::npos) {
        rest  = body.substr(0, q);
        query = body.substr(q + 1);
    }

    if (rest.empty()) {
        err = std::string(scheme) + ": needs a port (" + scheme + ":2323) or a host and port (" +
              scheme + ":bbs.example:23)";
        return nullptr;
    }

    // The options. `banner` is the only one; a bare key is =true, the convention the
    // mirror's `ro` and the tee use.
    bool banner = bannerDefault, bannerSet = false;
    for (size_t start = 0; start <= query.size();) {
        size_t      amp = query.find('&', start);
        std::string tok =
            query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!tok.empty()) {
            size_t      eq  = tok.find('=');
            std::string key = tok.substr(0, eq);
            std::string val = eq == std::string::npos ? "true" : tok.substr(eq + 1);
            if (key == "banner") {
                Value       v;
                std::string perr;
                if (!parseValue(val, Kind::Bool, v, perr)) {
                    err = std::string(scheme) + ": banner wants on or off: " + perr;
                    return nullptr;
                }
                banner    = v.b();
                bannerSet = true;
            } else {
                err = std::string(scheme) + ": unknown option '" + key +
                      "'. The only option is banner (greet each caller)";
                return nullptr;
            }
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }

    // A bare PORT is a LISTEN; HOST:PORT is a CALL. The colon is the whole of the
    // distinction, the same one every terminal program has used for forty years.
    size_t c = rest.rfind(':');
    if (c == std::string::npos) {
        uint16_t port = 0;
        if (!parsePort(rest, port)) {
            err = "'" + rest + "' is not a TCP port number (1..65535)";
            return nullptr;
        }
        auto l = platform::listenTcp(port, err);
        if (!l) return nullptr;
        return std::make_unique<TcpListenStream>(std::move(l), spec, banner);
    }

    // A dial-out is the CALLER: there is nobody for us to greet.
    if (bannerSet) {
        err = std::string(scheme) + ": banner greets callers to a listening port (" + scheme +
              ":PORT?banner), not a call out to " + rest;
        return nullptr;
    }

    std::string host = rest.substr(0, c);
    uint16_t    port = 0;
    if (host.empty() || !parsePort(rest.substr(c + 1), port)) {
        err = std::string("expected ") + scheme + ":HOST:PORT, got '" + spec + "'";
        return nullptr;
    }
    auto conn = platform::connectTcp(host, port, err);
    if (!conn) return nullptr;
    return std::make_unique<TcpConnectStream>(std::move(conn), spec);
}

std::unique_ptr<ByteStream> resolveEndpoint(const std::string& spec, std::string& err) {
    // ---- ENDPOINT|FILE -- a transparent HEX TAP on the line (host/tee_stream.h) ----
    //
    // Checked FIRST, before the prefix dispatch: a `|` appears nowhere else in the
    // grammar, so its mere presence means "capture", and `socket:23|cap.hex` must not be
    // handed to the socket: branch. Split on the FIRST `|`: the left is any endpoint
    // (recursed here, options and all), the right is the log path plus the tee's own
    // `?key=value&...`. The board never learns any of this -- it gets a ByteStream.
    //
    // SPLIT ON THE LAST `|`. Sinks stack: `scripted|socket:2323|session.log?fmt=text` is a
    // log tap on a mirror on a scripted line. The last sink is the outermost decorator and
    // everything to its left is the line it wraps, resolved by the recursion below -- so
    // describe(), which appends `|sink` to the inner's own, reads back in the same order.
    if (size_t bar = spec.rfind('|'); bar != std::string::npos) {
        std::string inner = spec.substr(0, bar);
        std::string file  = spec.substr(bar + 1);
        if (inner.empty()) {
            err = "capture needs ENDPOINT|FILE (an endpoint before '|'), "
                  "e.g. socket:2323|cap.hex";
            return nullptr;
        }

        std::string path = file, query;
        if (size_t q = file.find('?'); q != std::string::npos) {
            path  = file.substr(0, q);
            query = file.substr(q + 1);
        }
        if (path.empty()) {
            err = "capture needs a log file after '|', e.g. socket:2323|cap.hex";
            return nullptr;
        }

        // ---- ENDPOINT|socket:PORT -- a LIVE mirror, not a file capture (issue #381) ----
        //
        // The one place the two taps diverge: a `|` whose right side is a `socket:` is a
        // MirrorStream (bidirectional, a human watches and takes over over TCP), where a
        // `|` whose right side is a path is the TeeStream below (one-way hex to a file).
        // Everything up to here -- the split, the `?query` -- is shared; only the sink
        // differs. The mirror LISTENS (the watcher telnets in), so the right side is a
        // bare `socket:PORT`; a `?ro` option makes it read-only (watch, no take-over).
        // A `pty` or `pty:LINK` right side is the same mirror over a pseudo-terminal
        // (issue #683): the watcher opens the link with a terminal program.
        const bool ptySink    = path == "pty" || path.rfind("pty:", 0) == 0;
        const bool serialSink = path.rfind("serial:", 0) == 0;
        if (path.rfind("socket:", 0) == 0 || ptySink || serialSink) {
            uint16_t    port = 0;
            std::string link;  // "" = the first free /tmp/altairsim{n}
            std::string device;
            long long   baud = 9600;  // the serial sink's only line setting (8N1 fixed)
            if (serialSink) {
                device = path.substr(7);
                if (device.empty()) {
                    err = "mirror: serial: needs a device (|serial:/dev/tty.usbserial-XXXX, "
                          "|serial:COM3)";
                    for (const auto& p : platform::listSerialPorts()) err += "\n  " + p;
                    return nullptr;
                }
            } else if (ptySink) {
                link = path.size() > 4 ? path.substr(4) : "";
                if (path.size() == 4) {
                    err = "mirror: pty: wants a path for the link (or use a bare 'pty')";
                    return nullptr;
                }
            } else {
                std::string rest = path.substr(7);
                if (rest.find(':') != std::string::npos) {
                    err = "mirror listens for a watcher: use ENDPOINT|socket:PORT (a bare "
                          "port), got '" + path + "'";
                    return nullptr;
                }
                if (!parsePort(rest, port)) {
                    err = "'" + rest + "' is not a TCP port number (1..65535)";
                    return nullptr;
                }
            }

            // The mirror's only option is `ro` (read-only). A bare key is =true, the same
            // convention the tee and printer use; `ro=false` spells out take-over.
            bool readOnly = false;
            for (size_t start = 0; start <= query.size();) {
                size_t      amp = query.find('&', start);
                std::string tok =
                    query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
                if (!tok.empty()) {
                    size_t      eq  = tok.find('=');
                    std::string key = tok.substr(0, eq);
                    std::string val = eq == std::string::npos ? "true" : tok.substr(eq + 1);
                    if (key == "ro") {
                        Value       v;
                        std::string perr;
                        if (!parseValue(val, Kind::Bool, v, perr)) {
                            err = "mirror: ro wants a boolean: " + perr;
                            return nullptr;
                        }
                        readOnly = v.b();
                    } else if (key == "baud" && serialSink) {
                        // Only a serial sink has a rate: the socket and the pty are not
                        // clocked, and a board has nothing here to program it.
                        char*     end = nullptr;
                        long long b   = eq == std::string::npos ? 0
                                                                : std::strtoll(val.c_str(), &end, 10);
                        if (eq == std::string::npos || val.empty() || *end || b <= 0) {
                            err = "mirror: baud wants a positive number (baud=9600)";
                            return nullptr;
                        }
                        baud = b;
                    } else {
                        err = "mirror: unknown option '" + key + "'. The options are ro "
                              "(read-only)" + (serialSink ? " and baud" : "") +
                              " -- the port, the link or the device comes before '?'";
                        return nullptr;
                    }
                }
                if (amp == std::string::npos) break;
                start = amp + 1;
            }

            // Resolve the wrapped line first (a bad inner spec is the operator's error),
            // then bind the listener so a port in use REFUSES cleanly here -- a failed
            // CONNECT, not a half-built mirror. describe() echoes `inner|file`, so SHOW
            // and CONFIG SAVE round-trip the whole mirror (the socket: right side
            // re-selects this branch on reload).
            auto innerStream = resolveEndpoint(inner, err);
            if (!innerStream) return nullptr;
            if (serialSink) {
                // One port cannot be both the line and its mirror.
                if (innerStream->describe() == "serial:" + device) {
                    err = "mirror: " + device + " is already the line this mirrors";
                    return nullptr;
                }
                platform::SerialConfig cfg;
                cfg.baud = baud;
                auto sp  = platform::openSerialPort(device, cfg, err);
                if (!sp) {
                    // Name what is actually there, as the serial: endpoint does.
                    auto have = platform::listSerialPorts();
                    if (!have.empty()) {
                        err += "\nserial ports on this host:";
                        for (const auto& p : have) err += "\n  " + p;
                    }
                    return nullptr;
                }
                return std::make_unique<MirrorStream>(
                    std::move(innerStream), file,
                    std::make_unique<SerialMirrorSink>(std::move(sp), baud), readOnly);
            }
            if (ptySink) {
                auto pty = platform::openPty(link, err);
                if (!pty) return nullptr;
                return std::make_unique<MirrorStream>(
                    std::move(innerStream), file,
                    std::make_unique<PtyMirrorSink>(std::move(pty)), readOnly);
            }
            auto listener = platform::listenTcp(port, err);
            if (!listener) return nullptr;
            return std::make_unique<MirrorStream>(std::move(innerStream), file,
                                                  std::move(listener), readOnly);
        }

        // The tee's options ride the same `?key[=value][&key...]` grammar as in:/printer:,
        // and every value goes through the one parseValue -- no second convention.
        TeeStream::Params params;
        bool              append = false;  // open the log to add to it, not to start it over
        std::string       layoutOpt;       // the first hex-layout option given, for fmt=text
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
                if (key == "fmt") {
                    if (val == "dump") params.fmt = TeeStream::Fmt::Dump;
                    else if (val == "cols") params.fmt = TeeStream::Fmt::Cols;
                    else if (val == "jsonl") params.fmt = TeeStream::Fmt::Jsonl;
                    else if (val == "text") params.fmt = TeeStream::Fmt::Text;
                    else {
                        err = "capture: fmt is dump, cols, jsonl or text (got '" + val + "')";
                        return nullptr;
                    }
                } else if (key == "width") {
                    if (!parseValue(val, Kind::Int, v, perr) || v.i() < 1 || v.i() > 256) {
                        err = "capture: width wants 1..256 bytes per row: " + perr;
                        return nullptr;
                    }
                    params.width = (int)v.i();
                } else if (key == "gap") {
                    if (!parseValue(val, Kind::Int, v, perr) || v.i() < 0) {
                        err = "capture: gap wants milliseconds >= 0 (0 = never): " + perr;
                        return nullptr;
                    }
                    params.gapNs = (uint64_t)v.i() * 1'000'000ull;
                } else if (key == "ts") {
                    if (val == "elapsed") params.ts = TeeStream::Ts::Elapsed;
                    else if (val == "wall") params.ts = TeeStream::Ts::Wall;
                    else if (val == "none") params.ts = TeeStream::Ts::None;
                    else {
                        err = "capture: ts is elapsed, wall or none (got '" + val + "')";
                        return nullptr;
                    }
                } else if (key == "pins") {
                    if (!parseValue(val, Kind::Bool, v, perr)) {
                        err = "capture: pins wants a boolean: " + perr;
                        return nullptr;
                    }
                    params.pins = v.b();
                } else if (key == "append") {
                    if (!parseValue(val, Kind::Bool, v, perr)) {
                        err = "capture: append wants a boolean: " + perr;
                        return nullptr;
                    }
                    append = v.b();
                } else {
                    err = "capture: unknown option '" + key +
                          "'. Options are fmt, width, gap, ts, pins, append -- the log file "
                          "comes before '?'";
                    return nullptr;
                }
                if (layoutOpt.empty() &&
                    (key == "width" || key == "gap" || key == "ts" || key == "pins"))
                    layoutOpt = key;
            }
            if (amp == std::string::npos) break;
            start = amp + 1;
        }

        // fmt=text is the guest's bytes and nothing else, so the options that shape a hex
        // trace mean nothing there. Refuse one rather than accept it and ignore it.
        const bool text = params.fmt == TeeStream::Fmt::Text;
        if (text && !layoutOpt.empty()) {
            err = "capture: " + layoutOpt + " has no meaning with fmt=text, which writes the "
                  "guest's output as it is. The only option that applies is append";
            return nullptr;
        }

        // Recurse for the wrapped line; its error (a bad inner spec) is the operator's.
        auto innerStream = resolveEndpoint(inner, err);
        if (!innerStream) return nullptr;

        // The resolver opens the log so it can REFUSE cleanly here (a bad path is a
        // failed CONNECT, not a half-built tap). Truncate: a capture is a fresh trace --
        // unless `append` asks to add to what is there. A transcript is opened BINARY: its
        // bytes are the guest's, and a text-mode stream on Windows would turn each LF
        // into CR LF. The hex layouts are lines of our own text and stay text-mode.
        std::ios::openmode mode = std::ios::out | (append ? std::ios::app : std::ios::trunc);
        if (text) mode |= std::ios::binary;
        std::ofstream log(path, mode);
        if (!log) {
            err = "cannot open '" + path + "' for capture";
            return nullptr;
        }

        // describe() echoes `inner|file`, so SHOW / CONFIG SAVE round-trip the tap.
        return std::make_unique<TeeStream>(std::move(innerStream), file, params, std::move(log));
    }

    if (spec == "console") return std::make_unique<ConsoleRef>();
    if (spec == "null") return std::make_unique<NullStream>();
    if (spec == "loopback") return std::make_unique<LoopbackStream>();

    // A terminal with a caller instead of a human: the two directions are separate,
    // so whoever holds the stream feed()s keystrokes and reads out() what the guest
    // printed. It is what the MCP server binds the console to (the guest cannot tell
    // it from a person), and what a test types into with no tty in the picture.
    if (spec == "scripted") return std::make_unique<ScriptedStream>();

    // ---- terminal -- a built-in windowed terminal, the simulator's own (issue #244) ----
    //
    // `connect = "terminal"` is the whole of the common case: a VT100 in an 80x24 window.
    // Options ride the same `?key[=value][&...]` grammar the other endpoints use:
    // `terminal?emulation=vt100&size=80x24`. A leading ':' is accepted so
    // `terminal:?emulation=...` reads the same as every other scheme. GRAMMAR ERRORS ARE
    // REPORTED BEFORE the windowed-build check, so a bad emulation or size is caught even
    // on a headless build -- and a well-formed spec then refuses cleanly there, because a
    // terminal nobody can see or type at is not a serial line worth opening.
    if (spec == "terminal" || spec.rfind("terminal:", 0) == 0 ||
        spec.rfind("terminal?", 0) == 0) {
        std::string rest = spec.size() > 8 ? spec.substr(8) : "";  // after "terminal"
        if (!rest.empty() && rest[0] == ':') rest = rest.substr(1);
        std::string query;
        if (!rest.empty()) {
            if (rest[0] != '?') {
                err = "terminal takes options after '?': terminal?emulation=vt100&size=80x24";
                return nullptr;
            }
            query = rest.substr(1);
        }

        std::string emuName = "vt100";  // the first table row -- the default dialect
        int         cols = 80, rows = 24;  // the reporter's default geometry (issue #244)
        int         winWidth = 0;          // opening window width in px, 0 = auto (~half screen)
        auto        phosphor = TerminalStream::Phosphor::Green;  // tube colour (default green)

        auto parseSize = [&](const std::string& v) -> bool {
            size_t xp = v.find_first_of("xX");
            if (xp != std::string::npos) {
                std::string cs = v.substr(0, xp), rs = v.substr(xp + 1);
                char*         e1 = nullptr;
                char*         e2 = nullptr;
                unsigned long c  = std::strtoul(cs.c_str(), &e1, 10);
                unsigned long r  = std::strtoul(rs.c_str(), &e2, 10);
                if (!cs.empty() && !rs.empty() && !*e1 && !*e2 && c >= 16 && c <= 255 &&
                    r >= 2 && r <= 127) {
                    cols = (int)c;
                    rows = (int)r;
                    return true;
                }
            }
            err = "terminal: size is COLSxROWS -- cols 16..255, rows 2..127 (e.g. 80x24), "
                  "got '" + v + "'";
            return false;
        };

        for (size_t start = 0; start <= query.size();) {
            size_t      amp = query.find('&', start);
            std::string tok =
                query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
            if (!tok.empty()) {
                size_t      eq  = tok.find('=');
                std::string key = tok.substr(0, eq);
                std::string val = eq == std::string::npos ? "" : tok.substr(eq + 1);
                if (key == "emulation" || key == "emu") {
                    if (val.empty()) {
                        err = "terminal: emulation needs a name (" + terminalEmulationNames() + ")";
                        return nullptr;
                    }
                    emuName = val;
                } else if (key == "size" || key == "windowsize") {
                    // `windowsize` is the reporter's spelling; both mean COLSxROWS.
                    if (!parseSize(val)) return nullptr;
                } else if (key == "width") {
                    // Opening window width in device pixels (0/auto = ~half the screen).
                    char*         e = nullptr;
                    unsigned long v = std::strtoul(val.c_str(), &e, 10);
                    if (val.empty() || *e || v > 16384) {
                        err = "terminal: width is a pixel count 0..16384 (0 = auto), got '" +
                              val + "'";
                        return nullptr;
                    }
                    winWidth = (int)v;
                } else if (key == "phosphor" || key == "phosphour" || key == "color") {
                    if (val == "green") phosphor = TerminalStream::Phosphor::Green;
                    else if (val == "amber") phosphor = TerminalStream::Phosphor::Amber;
                    else {
                        err = "terminal: phosphor is green or amber, got '" + val + "'";
                        return nullptr;
                    }
                } else {
                    err = "terminal: unknown option '" + key +
                          "'. Options are emulation, size (alias windowsize), width, phosphor";
                    return nullptr;
                }
            }
            if (amp == std::string::npos) break;
            start = amp + 1;
        }

        // Grammar first: an unknown emulation is the operator's typo, caught everywhere.
        auto emu = makeTerminalEmulator(emuName);
        if (!emu) {
            err = "terminal: no emulation '" + emuName + "'. Try: " + terminalEmulationNames();
            return nullptr;
        }

        // Then capability: a terminal is only useful with a window on the screen.
        if (!TerminalStream::hasWindow()) {
            err = "terminal: this build has no window (headless / no SDL) -- a terminal has "
                  "nothing to draw on and no keyboard. Use socket:PORT with an external "
                  "terminal instead.";
            return nullptr;
        }

        auto term = std::make_unique<TerminalStream>(spec, rows, cols, std::move(emu));
        term->setWindowWidth(winWidth);
        term->setPhosphor(phosphor);
        return term;
    }

    // ---- socket: -- a RAW byte pipe: listen on a port, or call out to a host ----
    if (spec.rfind("socket:", 0) == 0)
        return makeTcpStream(spec.substr(7), spec, "socket", /*bannerDefault=*/false, err);

    // ---- telnet: -- like socket:, but speaks the Telnet protocol (host/telnet_stream.h) ----
    //
    // Same grammar as socket: (PORT listens, HOST:PORT calls out), wrapped so a human
    // with a stock `telnet` client gets the terminal-server handshake -- no local echo,
    // character-at-a-time -- instead of the raw pipe's double echo. LISTEN is the server
    // role (we offer to echo); a dial-out is the client role. socket: stays raw for
    // machine-to-machine CONNECT and the mirror.
    if (spec.rfind("telnet:", 0) == 0) {
        auto inner = makeTcpStream(spec.substr(7), spec, "telnet", /*bannerDefault=*/true, err);
        if (!inner) return nullptr;
        std::string body = spec.substr(7);
        body             = body.substr(0, body.find('?'));  // the options are not the address
        const bool server = body.rfind(':') == std::string::npos;  // bare PORT = listen
        return std::make_unique<TelnetStream>(std::move(inner), spec, server);
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

    // ---- pty / pty:LINK -- a pseudo-terminal, for a terminal program (issue #685) ----
    //
    // The line itself, not a mirror of one: `screen`, `minicom` or a file transfer
    // program opens the link as it opens a serial port (host/pty_stream.h). A bare
    // `pty` takes the first free /tmp/altairsim{n}; `pty:LINK` names the link, which is
    // the form a saved machine file wants. NOT behind havePty(): on Windows openPty()
    // refuses by name, and that is a better answer than "no endpoint 'pty'".
    if (spec == "pty" || spec.rfind("pty:", 0) == 0 || spec.rfind("pty?", 0) == 0) {
        if (spec.find('?') != std::string::npos) {
            err = "pty: has no options (pty, or pty:LINK)";
            return nullptr;
        }
        std::string link = spec.size() > 3 ? spec.substr(4) : std::string();
        if (spec != "pty" && link.empty()) {
            err = "pty: wants a path for the link (or use a bare 'pty')";
            return nullptr;
        }
        auto pty = platform::openPty(link, err);
        if (!pty) return nullptr;
        return std::make_unique<PtyStream>(std::move(pty), spec);
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
