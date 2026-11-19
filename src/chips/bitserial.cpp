#include "chips/bitserial.h"

#include "core/statefile.h"
#include "core/value.h"

#include <cstdio>

namespace altair {

namespace {

// Half bit times from the start edge to the middle of each bit the far end samples.
constexpr int kStartMid = 1;       // 0.5
constexpr int kStopMid  = 19;      // 9.5
constexpr int kFrame    = 20;      // 10: start, 8 data, stop

constexpr int dataMid(int i) { return 3 + 2 * i; }  // 1.5 + i

} // namespace

BitSerialLine::BitSerialLine() { disconnect(); }

BitSerialLine::~BitSerialLine() {
    if (clock_) {
        clock_->cancel(wake_);
        clock_->unwatch(&clock_);
    }
}

void BitSerialLine::attachClock(Clock* c) {
    if (clock_) {
        clock_->cancel(wake_);
        clock_->unwatch(&clock_);
    }
    wake_  = Clock::kNone;
    clock_ = c;
    if (clock_) clock_->watch(&clock_);
    arm();
}

uint64_t BitSerialLine::at(int halfBits) const {
    if (!clock_ || baud <= 0) return 0;
    return (uint64_t)halfBits * (uint64_t)clock_->hz() / (2 * (uint64_t)baud);
}

// ---------------------------------------------------------------------------
// The connector. The line is taken as it is: no transform chain (DESIGN.md 7.2).
// ---------------------------------------------------------------------------
void BitSerialLine::connect(std::unique_ptr<ByteStream> s) {
    stream_ = std::move(s);
    programLine();
}

void BitSerialLine::disconnect() { connect(std::make_unique<NullStream>()); }

// A real serial port is opened at the frame the far end uses. If the host refuses, say so
// and go on: the guest's delay loop is what sets the rate it can measure.
void BitSerialLine::programLine() {
    LineParams p;
    p.baud = baud;
    std::string err;
    if (!stream_->setParams(p, err)) log_.push_back("serial: " + err);
}

// ---------------------------------------------------------------------------
// Transmit.
// ---------------------------------------------------------------------------
bool BitSerialLine::txLevelAt(uint64_t t) const {
    bool level = true;  // a frame starts from MARK
    for (const auto& e : txEdges_) {
        if (e.first > t) break;
        level = e.second;
    }
    return level;
}

void BitSerialLine::arm() {
    if (!clock_) return;
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (txBusy_) wake_ = clock_->at(txStart_ + at(kStopMid), [this] {
        wake_ = Clock::kNone;
        finishFrame();
    });
}

void BitSerialLine::finishFrame() {
    txBusy_ = false;
    if (txLevelAt(txStart_ + at(kStartMid))) {  // the start bit was gone by its middle
        txEdges_.clear();
        return;
    }
    uint8_t b = 0;
    for (int i = 0; i < 8; ++i)
        if (txLevelAt(txStart_ + at(dataMid(i)))) b |= (uint8_t)(1u << i);
    const bool stop = txLevelAt(txStart_ + at(kStopMid));
    txEdges_.clear();
    if (stop) {
        stream_->write(&b, 1);
        stream_->flush();
        return;
    }
    char msg[96];
    if (b == 0)
        std::snprintf(msg, sizeof msg, "serial: a break on the line (no stop bit); nothing sent");
    else
        std::snprintf(msg, sizeof msg, "serial: framing error, no stop bit after %02Xh; byte dropped",
                      b);
    log_.push_back(msg);
}

// A frame whose stop bit is already behind us is finished before anything new happens on
// the wire. The deadline does this on time; this covers a write that lands on the same
// T-state.
void BitSerialLine::catchUp() {
    if (txBusy_ && clock_ && clock_->now() >= txStart_ + at(kStopMid)) {
        clock_->cancel(wake_);
        wake_ = Clock::kNone;
        finishFrame();
    }
}

void BitSerialLine::setTx(bool mark) {
    if (!clock_) {
        txMark_ = mark;
        return;
    }
    catchUp();
    if (mark == txMark_) return;
    txMark_          = mark;
    const uint64_t t = clock_->now();
    if (txBusy_) {
        txEdges_.emplace_back(t, mark);
        return;
    }
    if (mark) return;  // SPACE to MARK on an idle line: the end of a break, nothing to send
    txBusy_  = true;
    txStart_ = t;
    txEdges_.assign(1, {t, false});
    arm();
}

// ---------------------------------------------------------------------------
// Receive.
// ---------------------------------------------------------------------------
bool BitSerialLine::rx() {
    if (!clock_ || baud <= 0) return true;
    const uint64_t now = clock_->now();
    if (rxBusy_ && now >= rxStart_ + at(kFrame)) rxBusy_ = false;
    if (!rxBusy_) {
        if (!stream_->readable()) {
            ++emptyPolls_;  // the guest read an idle wire with nothing coming: it is waiting
            return true;
        }
        uint8_t b = 0;
        if (stream_->read(&b, 1) != 1) return true;
        rxByte_  = b;
        rxStart_ = now;
        rxBusy_  = true;
        ++rxCount_;
    }
    const uint64_t bit = (now - rxStart_) * (uint64_t)baud / (uint64_t)clock_->hz();
    if (bit == 0) return false;                    // the start bit
    if (bit <= 8) return (rxByte_ >> (bit - 1)) & 1;
    return true;                                   // the stop bit
}

void BitSerialLine::powerOn() {
    if (clock_) clock_->cancel(wake_);
    wake_   = Clock::kNone;
    txMark_ = true;
    txBusy_ = false;
    txEdges_.clear();
    rxBusy_ = false;
}

// ---------------------------------------------------------------------------
// Properties: the serial unit's.
// ---------------------------------------------------------------------------
std::vector<Property> BitSerialLine::properties(EndpointResolver resolve) {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate: the far end's setting, and what the guest's delay loop must "
                  "match. 8 data bits, no parity, 1 stop bit";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 50;
        x.max   = 115200;
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(baud); };
        x.set   = [this](const Value& v, std::string&) {
            baud = v.i();
            programLine();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(stream_->describe()); };
        x.set  = [this, resolve](const Value& v, std::string& err) {
            if (!resolve) {
                err = "no endpoint resolver installed";
                return false;
            }
            auto s = resolve(v.s(), err);
            if (!s) return false;
            connect(std::move(s));
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE. The frame in flight travels with its absolute times, which stay valid
// because the Clock's time travels too; the deadline is re-armed from them.
// ---------------------------------------------------------------------------
void BitSerialLine::serialize(StateWriter& w) const {
    w.boolean(txMark_);
    w.boolean(txBusy_);
    w.u64(txStart_);
    w.u32((uint32_t)txEdges_.size());
    for (const auto& e : txEdges_) {
        w.u64(e.first);
        w.boolean(e.second);
    }
    w.boolean(rxBusy_);
    w.u64(rxStart_);
    w.u8(rxByte_);
    w.u64(rxCount_);
}

void BitSerialLine::deserialize(StateReader& r) {
    txMark_          = r.boolean();
    txBusy_          = r.boolean();
    txStart_         = r.u64();
    const uint32_t n = r.u32();
    txEdges_.clear();
    for (uint32_t i = 0; i < n; ++i) {
        const uint64_t t = r.u64();
        txEdges_.emplace_back(t, r.boolean());
    }
    rxBusy_  = r.boolean();
    rxStart_ = r.u64();
    rxByte_  = r.u8();
    rxCount_ = r.u64();
    arm();
}

} // namespace altair
