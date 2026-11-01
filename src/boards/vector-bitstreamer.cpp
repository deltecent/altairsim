#include "boards/vector-bitstreamer.h"

#include "boards/vector-baud.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>

namespace altair {

namespace {

EndpointResolver g_resolver;

Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

} // namespace

void BitstreamerBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

BitstreamerBoard::BitstreamerBoard() {
    u_.disconnect();
    u_.dsrSrc = DsrSource::Line;
    vgSetBaud(u_, 9600);
}

BitstreamerBoard::~BitstreamerBoard() {
    if (clock_) clock_->cancel(wake_);
}

bool BitstreamerBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    return (uint8_t)(c.port() & 0xFC) == port_;
}

uint8_t BitstreamerBoard::read(const BusCycle& c) {
    const uint8_t reg = c.port() & 3;
    if (reg < 2) return 0xFF;  // the open J2 lines

    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint8_t v = (reg & 1) ? u_.readStatus(clk) : u_.readData(clk);
    refresh();
    return v;
}

void BitstreamerBoard::write(const BusCycle& c) {
    const uint8_t reg = c.port() & 3;
    if (reg == 0) {
        outA_ = c.data;
        return;
    }
    if (reg == 1) {
        outB_ = c.data;
        return;
    }
    const Clock& clk = clock_ ? *clock_ : deadCard();
    if (reg & 1) u_.writeControl(c.data, clk);
    else         u_.writeData(c.data, clk);
    refresh();
}

void BitstreamerBoard::refresh() {
    if (!clock_) return;

    u_.poll(*clock_);

    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

uint64_t BitstreamerBoard::nextEdge() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        if (when <= clk.now()) return;
        if (!best || when < best) best = when;
    };

    if (u_.rxFrameActive()) {
        consider(u_.rxDoneAt());
    } else if (u_.rxWaiting()) {
        consider(u_.rxNextAt());
    }
    return best;
}

void BitstreamerBoard::reset(Reset) {
    if (!clock_) return;
    u_.powerOn(*clock_);
    refresh();
}

void BitstreamerBoard::power() { reset(Reset::PowerOn); }

void BitstreamerBoard::pump() {
    u_.pump();
    refresh();
}

void BitstreamerBoard::configChanged() {
    decodeChanged();
    u_.programLine();
    refresh();
}

void BitstreamerBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    u_.serialize(w);
    w.u8(outA_);
    w.u8(outB_);
}

void BitstreamerBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    u_.deserialize(r);
    outA_ = r.u8();
    outB_ = r.u8();
    decodeChanged();
    refresh();
}

std::vector<Property> BitstreamerBoard::properties() {
    Property x;
    x.name  = "port";
    x.help  = "Base of the four ports, set by the pads beside U7: a multiple of 4";
    x.kind  = Kind::Int;
    x.radix = 16;
    x.min   = 0;
    x.max   = 0xFC;
    x.get   = [this] { return Value::ofInt(port_); };
    x.set   = [this](const Value& v, std::string& err) {
        if (v.i() & 3) {
            err = "port is a multiple of 4: the pads set A2 to A7";
            return false;
        }
        port_ = (uint8_t)v.i();
        return true;
    };
    return {std::move(x)};
}

std::vector<UnitDef> BitstreamerBoard::units() const {
    return {{"serial", UnitKind::Serial, u_.endpoint()}};
}

std::vector<Property> BitstreamerBoard::unitProperties(const std::string& unit) {
    if (unit != "serial") return {};
    std::vector<Property> p;
    p.push_back(vgBaudProperty(u_, "The baud switch, for a guest that selects a clock factor of "
                                   "16. The 2 MHz clock over 13 makes the label 9600 run at 9615"));
    Property x;
    x.name = "connect";
    x.help = "The endpoint on the serial line (CONNECT sets this)";
    x.kind = Kind::Str;
    x.get  = [this] { return Value::ofStr(u_.endpoint()); };
    x.set  = [this](const Value& v, std::string& err) { return connect("serial", v.s(), err); };
    p.push_back(std::move(x));
    return p;
}

std::vector<MapEntry> BitstreamerBoard::ioMap() const {
    const uint32_t b = port_;
    return {
        {b, b, "read/write", "J2 port A lines (R) / parallel latch A (W)"},
        {b + 1, b + 1, "read/write", "J2 port B lines (R) / parallel latch B (W)"},
        {b + 2, b + 2, "read/write", "8251 -- receive/transmit data"},
        {b + 3, b + 3, "read/write", "8251 -- status / mode+command"},
    };
}

bool BitstreamerBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "serial") {
        err = "vgbs has no unit '" + unit + "' to connect -- it has 'serial'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
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
    u_.connect(std::move(s));
    refresh();
    return true;
}

bool BitstreamerBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "serial") {
        err = "vgbs has no unit '" + unit + "' to disconnect -- it has 'serial'";
        return false;
    }
    u_.disconnect();
    refresh();
    return true;
}

bool BitstreamerBoard::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                                     std::string& err) {
    if (unit != "serial") {
        err = "vgbs has no unit '" + unit + "' to connect -- it has 'serial'";
        return false;
    }
    u_.connect(std::move(s));
    refresh();
    return true;
}

} // namespace altair
