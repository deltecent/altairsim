#include "boards/mits-frontpanel.h"

#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <algorithm>
#include <utility>

namespace altair {

namespace {

using namespace std::chrono_literals;

// One resolver for every fp board, injected in main() (DESIGN.md 7.7). A board that
// could reach a socket itself would be a board that knows what a socket is.
FrontPanelBoard::EndpointResolver g_resolver;

// THE REDIAL BACKOFF. A quarter second to start -- a bridge relaunching feels instant --
// doubling to a four-second ceiling while it stays away, so a panel left disconnected
// overnight is not hammering connect() thousands of times. Reset to the floor the moment
// carrier rises (a working line owes nothing to the last failure).
constexpr auto kReconnectMin = 250ms;
constexpr auto kReconnectMax = 4000ms;

// A hard cap on one accumulated inbound line, so a bridge that never sends '\n' (broken,
// or hostile) cannot grow rxLine_ without bound. Real frames are ~16 bytes; this is a
// wide margin, and a run longer than it is dropped until the next newline resynchronises.
constexpr size_t kMaxLine = 256;

} // namespace

void FrontPanelBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// -> NullStream. There is no null pointer in the stream path, ever: a card with
// nothing plugged into it holds a DEAD line, not a dangling one.
FrontPanelBoard::FrontPanelBoard() : stream_(std::make_unique<NullStream>()) {
    backoff_ = kReconnectMin;
}

// Out of line: ByteStream is complete here, so ~unique_ptr<ByteStream> can run.
FrontPanelBoard::~FrontPanelBoard() = default;

// The lamps are wired to the bus, so they show WHATEVER WENT BY LAST -- including a
// cycle this card did not answer, which is every cycle but one. That is the whole
// reason wantsSnoop() is true: a card with a flip-flop on the address bus (board.h).
//
// A PURE FORWARDER. Address, data AND status are all bus signals now -- the status
// word is latched onto BusCycle by Bus::settle() (core/bus.h), so this card copies
// c.status verbatim, exactly as it copies the address and data lines. No switch on
// c.type, no lookup table: the emitter is the bus, not this pipe.
void FrontPanelBoard::snoop(const BusCycle& c) {
    addrLeds_ = c.addr;
    dataLeds_ = c.data;    // back-filled on reads by Bus::settle()
    status_   = c.status;  // the 8080 status word, WO* active low (Status8080 in bus.h)
}

// POWER OFF, LAMPS OUT. The switches do NOT move -- they are toggles, and a toggle
// with no power is still wherever the operator left it. That asymmetry is the
// hardware's, not ours, and it is the one thing power() has to say.
void FrontPanelBoard::power() {
    addrLeds_ = 0;
    dataLeds_ = 0;
    status_   = 0;
    // A freshly powered Altair sits stopped, WAIT lit -- the operator has not run it
    // yet. The monitor clears this the instant a RUN session begins (setRunning).
    running_  = false;
}

void FrontPanelBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u16(sw_);
    w.u16(addrLeds_);
    w.u8(dataLeds_);
    w.u8(status_);
}

void FrontPanelBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    sw_       = r.u16();
    addrLeds_ = r.u16();
    dataLeds_ = r.u8();
    status_   = r.u8();
}

std::vector<Property> FrontPanelBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "sense";
        x.help  = "The SENSE switches, SA8..SA15 -- what IN 0FFH reads";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(sense()); };
        x.set   = [this](const Value& v, std::string&) {
            setSense((uint8_t)v.i());
            return true;
        };
        p.push_back(std::move(x));
    }
    // AND NOTHING ELSE. There used to be a second property, `data`, for the low half of
    // the switch row -- SA0..SA7, the byte DEPOSIT writes on a real panel. It was there
    // for a graphical panel to bind a toggle to, and no such panel exists. Nothing in the
    // machine reads it: no port is wired to those switches (schematic 880-106), and the
    // monitor's DEPOSIT takes its byte from the command line. So it was a knob that
    // changed nothing, sitting in the reference next to one that changes everything.
    //
    // There is no front panel to reach out and flip, so what this board owes an operator
    // is one thing: a way to say what IN 0FFH returns. That is `sense`, and that is all.
    // (The low half of sw_ stays -- it is the same physical row, it travels in the
    // snapshot, and a panel that ever wants it will find it here.)
    //
    // ...and now ONE more, because there IS a panel to reach out to: a graphical bridge
    // on the far end of the `gui` unit. These two -- how fast we feed it, and what it is
    // plugged into -- are the operator's knobs on that line.
    {
        Property x;
        x.name  = "fps";
        x.help  = "Panel refresh cap -- L status frames per second sent to the bridge";
        x.kind  = Kind::Int;
        x.radix = 10;  // never on the wire: decimal (DESIGN.md 10.0.1)
        x.min   = 1;
        x.max   = 1024;
        x.unit  = "fps";
        x.get   = [this] { return Value::ofInt(fps_); };
        x.set   = [this](const Value& v, std::string&) {
            fps_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint the graphical panel bridge lives at (CONNECT sets this)";
        x.kind = Kind::Str;
        // describe(), NOT endpoint_ -- see units(). An idle line reads "null", which
        // resolves straight back, so CONFIG SAVE's output loads as a fixed point.
        x.get  = [this] { return Value::ofStr(stream_->describe()); };
        // Route through connect() so a declarative `[fp0.unit.gui] connect = "socket:..."`
        // rebases its PATH the same way the CONNECT command does -- one path, one rule.
        x.set  = [this](const Value& v, std::string& err) { return connect("gui", v.s(), err); };
        p.push_back(std::move(x));
    }
    return p;
}

// ONE unit, and it is CONNECTed, not mounted: the graphical panel bridge. The card
// has one connector, so like the 88-SIO there is nothing to disambiguate.
std::vector<UnitDef> FrontPanelBoard::units() const {
    // describe(), NOT endpoint_: an idle connector reads "null" (a resolvable endpoint
    // -> NullStream), so CONFIG SAVE writes `connect = "null"` and CONFIG LOAD reads it
    // straight back -- the same fixed point every serial card round-trips through
    // (Uart1602::endpoint()). endpoint_ is the operator's raw words, kept for pump()'s
    // redial (Checkpoint ③), and it can diverge from the canonical describe().
    // consoleCapable = false: the guest never does character I/O over the panel bridge,
    // so the RUN banner must not name it as "the console" -- a machine with its console
    // on a `terminal:` window used to read "(console on socket)" off this line (#295).
    UnitDef u{"gui", UnitKind::Serial, stream_->describe()};
    u.consoleCapable = false;
    return {u};
}

bool FrontPanelBoard::connect(const std::string& unit, const std::string& ep,
                              std::string& err) {
    if (unit != "gui") {
        err = "fp has no unit '" + unit + "' -- it has one, and it is called 'gui'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens, exactly as the serial cards do. socket: endpoints have no path and
    // pass through untouched. The `connect` unit property routes here too, so a
    // declarative connect and the CONNECT command share this one rule.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(ep, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    stream_   = std::move(s);
    endpoint_ = ep;  // the operator's own words -- pump() redials THIS

    // A FRESH DIAL. Give the new stream one backoff interval to answer before pump()
    // would replace it (a dial in flight is a phone still ringing, not a failure), and
    // wipe any state from the line that was here before -- the greeting, the diff gate,
    // the "bridge is gone" latch all belong to the connection, not the card.
    backoff_    = kReconnectMin;
    nextDial_   = std::chrono::steady_clock::now() + backoff_;
    wasUp_      = false;
    loggedDown_ = false;
    rxLine_.clear();
    lastFrame_.clear();
    return true;
}

bool FrontPanelBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "gui") {
        err = "fp has no unit '" + unit + "' -- it has one, and it is called 'gui'";
        return false;
    }
    // The explicit stop. pump() reconnects a line that DROPPED on its own, but never one
    // an operator pulled -- so clearing endpoint_ is what tells it to stay unplugged.
    stream_   = std::make_unique<NullStream>();
    endpoint_.clear();
    wasUp_    = false;  // a NullStream reports carrier UP; the empty endpoint_ is the real gate
    return true;
}

// THE ONE HOST TURN (DESIGN.md 7.1). One non-blocking pass: redial if the line is down,
// let the session breathe, greet on a fresh carrier, read switch frames, send a lamp
// frame. No thread anywhere -- the panel is a peripheral like any other, and the outside
// world comes through this door and no other.
void FrontPanelBoard::pump() {
    // NOTHING PLUGGED IN is the empty endpoint_, NOT a carrier check: a NullStream (the
    // idle connector) reports carrier UP by default (host/stream.h), so keying off carrier
    // here would have every panel-less machine "handshake" with a bridge that is not there.
    if (endpoint_.empty()) return;

    const auto now = std::chrono::steady_clock::now();

    // ---- Persistent reconnect. The bridge is launched, closed and relaunched out of
    // band, so a line that is down is a phone to redial, not an error. We cannot see the
    // socket -- only its carrier -- so we redial on a capped backoff whenever carrier is
    // down and the timer is due; a dial in flight gets its interval to answer before we
    // replace it. Only DISCONNECT (endpoint_ cleared) ends this.
    if (!stream_->status().carrier && now >= nextDial_) {
        if (g_resolver) {
            std::string err;
            std::string spec = rebaseEndpointPaths(
                endpoint_, [&](const std::string& p) { return resolvePath(p); });
            if (auto s = g_resolver(spec, err)) {
                stream_ = std::move(s);
            } else if (!loggedDown_) {
                // Said ONCE, here at the first failure -- not on every retry, or a bridge
                // that is simply not running would scroll the operator's screen forever.
                log_.push_back("cannot reach the panel bridge at " + endpoint_ + " -- " + err);
                loggedDown_ = true;
            }
        }
        backoff_  = std::min(backoff_ * 2, kReconnectMax);
        nextDial_ = now + backoff_;
    }

    stream_->pump();  // finish dialling, drain the socket -- the stream's own host turn

    // ---- The carrier EDGE, not its level. A rise is the bridge answering; a fall is it
    // going away. Everything per-connection hangs off these two moments.
    const bool up = stream_->status().carrier;
    if (up && !wasUp_) {
        // Answered. Greet, and let the bridge greet back -- parseLine() adopts min(ours,
        // theirs) when its HELLO arrives. Blank the diff gate so the first frame always
        // ships, reset the backoff (a good line owes nothing to the last failure), and
        // say so once.
        wireVer_ = fplink::kProtocolVersion;
        rxLine_.clear();
        lastFrame_.clear();
        std::string hello = fplink::encodeHello();
        stream_->write(reinterpret_cast<const uint8_t*>(hello.data()), hello.size());
        backoff_    = kReconnectMin;
        loggedDown_ = false;
        log_.push_back("panel bridge connected at " + endpoint_);
    } else if (!up && wasUp_) {
        // Dropped. Redial on the SHORT backoff first -- this was a working line a moment
        // ago -- growing only if the redials themselves keep failing.
        backoff_  = kReconnectMin;
        nextDial_ = now + backoff_;
    }
    wasUp_ = up;

    if (!up) return;  // still ringing, or down between redials: nothing to read or send

    // ---- Inbound: W/S switch frames (and the bridge's HELLO). Split on '\n'; a malformed
    // or unknown line parses to Kind::None and is ignored (forward compatibility).
    uint8_t buf[256];
    for (size_t n; (n = stream_->read(buf, sizeof buf)) > 0;) {
        for (size_t i = 0; i < n; ++i) {
            char c = static_cast<char>(buf[i]);
            if (c == '\n') {
                fplink::PanelMsg m = fplink::parseLine(rxLine_);
                switch (m.kind) {
                case fplink::PanelMsg::Kind::Switches: setSwitches(m.value); break;
                case fplink::PanelMsg::Kind::Sense:    setSense((uint8_t)m.value); break;
                case fplink::PanelMsg::Kind::Hello:
                    wireVer_ = std::min<int>(fplink::kProtocolVersion, m.value);
                    break;
                case fplink::PanelMsg::Kind::None: break;  // ignore -- forward compat
                }
                rxLine_.clear();
            } else if (rxLine_.size() < kMaxLine) {
                rxLine_.push_back(c);
            }
            // else: a line past the cap -- drop the run until the next '\n' resyncs us
        }
    }

    // ---- Outbound: the L status frame. Throttled to fps_ (host wall time, so a flat-out
    // guest is no faster on the wire than an idle one), sent only when the lamps actually
    // CHANGED and the socket can take it -- an idle guest and a backpressured one both
    // cost nothing. The bridge repaints at its own rate from the last frame it holds.
    if (now - lastSend_ >= std::chrono::microseconds(1000000 / fps_)) {
        lastSend_ = now;
        // WAIT is the only machine-control flag we model: lit when the operator has
        // the machine stopped, dark while a RUN session turns (see setRunning). The
        // rest of the flags group is still 0.
        uint8_t flags = running_ ? 0 : fplink::FlWait;
        std::string frame = fplink::encodeL(addrLeds_, dataLeds_, status_, flags);
        if (frame != lastFrame_ && stream_->writable()) {
            stream_->write(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
            lastFrame_ = std::move(frame);
        }
    }
}

// The base pulls the far end of every serial line (the `socket` layer's own connect/
// disconnect trace); this adds the board's own edge log. Both reach the operator through
// the monitor's drain after every command and run.
std::vector<std::string> FrontPanelBoard::drainLog() {
    std::vector<std::string> out = Board::drainLog();
    for (auto& l : log_) out.push_back(id + ": " + std::move(l));
    log_.clear();
    return out;
}

std::vector<MapEntry> FrontPanelBoard::ioMap() const {
    // READ ONLY, and SHOW BUS IO says so. An OUT 0FFH is not this card's: the
    // buffer bank's enable is gated with sINP, and there is no sOUT in the gate.
    return {{0xFF, 0xFF, "read", "SENSE switches SA8..SA15 -> D0..D7"}};
}

} // namespace altair
