#include "host/endpoint.h"

#include "host/console.h"
#include "host/hostserial.h"
#include "host/tcp.h"
#include "platform/serial.h"
#include "platform/socket.h"

#include <cstdlib>
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
           "serial:DEVICE";
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

    if (spec.rfind("file:", 0) == 0) {
        err = "file: endpoints are not implemented yet. " + endpointHelp();
        return nullptr;
    }

    err = "no endpoint '" + spec + "'. Try: " + endpointHelp();
    return nullptr;
}

} // namespace altair
