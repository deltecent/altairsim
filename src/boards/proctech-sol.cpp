#include "boards/proctech-sol.h"

#include "boards/proctech-vdm1.h"  // VdmBoard::setScroll -- the OUT 0xFE target
#include "core/bus.h"              // bus_->boards(), to find the VDM

namespace altair {
namespace {

// The injected endpoint resolver (setResolver), borrowed. The board hands an endpoint
// string to it and gets a stream back; it never learns what a socket is (DESIGN.md 7.7).
SolBoard::EndpointResolver g_resolver;

}  // namespace

void SolBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// A stopped clock, for the bench and for a card with no crystal attached. The serial
// UART needs a Clock& to answer "is the transmit buffer empty yet?"; with no clock,
// now() never advances, so TBMT (a deadline) never comes due -- exactly right.
Clock& SolBoard::deadCard() {
    static Clock dead;
    return dead;
}

SolBoard::SolBoard()
    : kb_(std::make_unique<NullStream>()),
      printer_(std::make_unique<NullStream>()),
      tape_(std::make_unique<NullStream>()) {
    // -> NullStream. There is no null pointer in the stream path: a UART with nothing
    // plugged in has a DEAD line, not a dangling one (as SioBoard does for its own).
    serial_.disconnect();
}

// ---------------------------------------------------------------------------
// Bus decode: the seven contiguous ports F8..FE. FF (sense) is the fp board; the
// VDM screen RAM is the vdm1 board. Both are elsewhere on the backplane.
// ---------------------------------------------------------------------------
bool SolBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= base_ && p <= (uint8_t)(base_ + 6);
}

uint8_t SolBoard::read(const BusCycle& c) {
    switch ((uint8_t)(c.port() - base_)) {
        case 0:  // F8 -- serial status
            if (clock_) serial_.poll(*clock_);  // the receiver runs on its own clock
            return serialStatus();
        case 1:  // F9 -- serial data (the read strobe clears Data Available)
            if (clock_) serial_.poll(*clock_);
            return serial_.readData();
        case 2:  // FA -- general/tape status
            return generalStatus();
        case 3:  // FB -- tape (CUTS) data. Deferred: no tape, nothing arrives.
            return 0x00;
        case 4: {  // FC -- keyboard data. Return the latched char and clear the strobe.
            uint8_t v = kbData_;
            kbHave_ = false;
            return v;
        }
        case 5:  // FD -- parallel data IN. No parallel input source; float.
            return 0xFF;
        case 6:  // FE -- DSTAT is write-only (scroll). A read floats.
            return 0xFF;
    }
    return 0xFF;
}

void SolBoard::write(const BusCycle& c) {
    switch ((uint8_t)(c.port() - base_)) {
        case 1:  // F9 -- serial data out
            serial_.writeData(c.data, clock_ ? *clock_ : deadCard());
            break;
        case 2:  // FA (OUT) -- tape motors (D7/D6) + 300-baud select (D5). Held; the
                 // tape is deferred, so nothing spins yet.
            tapeCtl_ = c.data;
            break;
        case 3: {  // FB -- tape data out. Deferred: sink it so SAVE does not hang.
            uint8_t b = c.data;
            tape_->write(&b, 1);
            break;
        }
        case 5: {  // FD -- parallel data out -> the printer line
            uint8_t b = c.data;
            printer_->write(&b, 1);
            printer_->flush();
            break;
        }
        case 6:  // FE -- VDM display parameter. Forward the scroll row to the VDM.
            if (VdmBoard* v = vdm()) v->setScroll(c.data);
            break;
        default:  // F8 (status) and FC (keyboard) are read-only; a write is ignored.
            break;
    }
}

// ---------------------------------------------------------------------------
// Status words.
// ---------------------------------------------------------------------------

bool SolBoard::serialTxEmpty() const {
    return serial_.txBufferEmpty(clock_ ? *clock_ : deadCard());
}

// IN 0xF8 -- serial status, ACTIVE HIGH (ready = 1). SOLOS's drivers test only D6
// (data ready) and D7 (transmitter empty); the modem-handshake and error bits are
// present in the manual but unused, so they read 0 (there is no line to have noise on).
uint8_t SolBoard::serialStatus() const {
    uint8_t s = 0;
    if (serial_.dataAvailable()) s |= 0x40;  // D6 SDR  -- a character is waiting
    if (serialTxEmpty())         s |= 0x80;  // D7 STBE -- OK to send the next one
    return s;
}

// IN 0xFA -- the shared status register, MIXED polarity (reference/Sol-20.md). The
// keyboard and parallel readies are active LOW (0 = ready), the tape flags active HIGH.
uint8_t SolBoard::generalStatus() const {
    uint8_t s = 0;
    if (!kbHave_)           s |= 0x01;  // D0 KDR  active low: 0 = a key is waiting
    s |= 0x02;                          // D1 PDR  active low: no parallel input source
    if (!printerWritable()) s |= 0x04;  // D2 PXDR active low: 0 = printer can take a byte
    // D3 TFE / D4 TOE -- tape errors, none. D6 TDR active high: 0 = no tape byte ready.
    s |= 0x80;                          // D7 TTBE active high: 1 = tape TX empty (idle)
    return s;
}

// ---------------------------------------------------------------------------
// The host turn. Serial line and keyboard both come in here, once per slice, on the
// main thread -- never inside a bus cycle (DESIGN.md 7.1).
// ---------------------------------------------------------------------------
void SolBoard::pump() {
    serial_.pump();
    if (clock_) serial_.poll(*clock_);
    kb_->pump();
    latchKeyboard();
    printer_->pump();
    tape_->pump();
}

void SolBoard::latchKeyboard() {
    if (kbHave_) return;                 // the strobe is still set: the line waits
    if (!kb_ || !kb_->readable()) return;
    uint8_t b = 0;
    if (kb_->read(&b, 1) != 1) return;
    kbData_ = b;
    kbHave_ = true;
    ++kbRx_;  // a keystroke crossed into the guest -- the run loop's live-traffic proof
}

void SolBoard::reset(Reset) {
    if (clock_) serial_.masterReset(*clock_);
    kbHave_ = false;
    tapeCtl_ = 0;
}

void SolBoard::power() { reset(Reset::PowerOn); }

VdmBoard* SolBoard::vdm() const {
    if (!bus_) return nullptr;
    for (Board* b : bus_->boards())
        if (b->type() == "vdm1") return static_cast<VdmBoard*>(b);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------

std::vector<Property> SolBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "Base I/O port (decodes BASE+0..6). Fixed at F8 on a real Sol-PC";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xF8;  // BASE+6 must clear FF (the sense switches)
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> SolBoard::unitProperties(const std::string& unit) {
    if (unit != "serial" && unit != "printer" && unit != "tape" && unit != "keyboard")
        return {};

    std::vector<Property> p;
    {
        // Every unit is a line you CONNECT. `connect` is the endpoint on the far end;
        // reading it round-trips through CONFIG SAVE.
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of this line (CONNECT sets this)";
        x.kind = Kind::Str;
        std::string u = unit;  // by value -- the lambda outlives this call
        x.get = [this, u] { return Value::ofStr(endpointOf(u)); };
        x.set = [this, u](const Value& v, std::string& err) { return connect(u, v.s(), err); };
        p.push_back(std::move(x));
    }
    if (unit == "serial") {
        {
            Property x;
            x.name = "baud";
            x.help = "Serial line speed (the strap on the Sol-PC's serial UART)";
            x.kind = Kind::Int;
            x.min  = 1;
            x.max  = 1000000;
            x.get  = [this] { return Value::ofInt(serial_.baud); };
            x.set  = [this](const Value& v, std::string&) {
                serial_.baud = v.i();
                serial_.programLine();
                return true;
            };
            p.push_back(std::move(x));
        }
        {
            Property x;
            x.name  = "data_bits";
            x.help  = "Serial word length: 8, 7, or 6 (the Sol-PC DIP)";
            x.kind  = Kind::Int;
            x.min   = 5;
            x.max   = 8;
            x.get   = [this] { return Value::ofInt(serial_.dataBits); };
            x.set   = [this](const Value& v, std::string&) {
                serial_.dataBits = (int)v.i();
                serial_.programLine();
                return true;
            };
            p.push_back(std::move(x));
        }
    }
    return p;
}

std::string SolBoard::endpointOf(const std::string& unit) const {
    if (unit == "serial")   return serial_.endpoint();
    if (unit == "printer")  return printer_->describe();
    if (unit == "tape")     return tape_->describe();
    if (unit == "keyboard") return kb_->describe();
    return "null";
}

std::vector<UnitDef> SolBoard::units() const {
    return {
        {"serial",   UnitKind::Serial, serial_.endpoint()},
        {"printer",  UnitKind::Serial, printer_->describe()},
        {"tape",     UnitKind::Serial, tape_->describe()},
        {"keyboard", UnitKind::Serial, kb_->describe()},
    };
}

std::vector<MapEntry> SolBoard::ioMap() const {
    const uint32_t b = base_;
    return {
        {b + 0, b + 0, "read",       "Sol serial status (D6 RX-rdy, D7 TX-empty)"},
        {b + 1, b + 1, "read/write", "Sol serial data"},
        {b + 2, b + 2, "read/write", "Sol status: kbd/parallel/tape (in) / tape motor+baud (out)"},
        {b + 3, b + 3, "read/write", "Sol tape (CUTS) data -- deferred"},
        {b + 4, b + 4, "read",       "Sol keyboard data"},
        {b + 5, b + 5, "read/write", "Sol parallel (printer) data"},
        {b + 6, b + 6, "write",      "Sol VDM display parameter (scroll) -> vdm1"},
    };
}

// ---------------------------------------------------------------------------
// Units: four connectable lines. Serial rides the UART; the other three are bare
// streams the card holds directly.
// ---------------------------------------------------------------------------

bool SolBoard::connect(const std::string& unit, const std::string& endpoint,
                       std::string& err) {
    if (unit != "serial" && unit != "printer" && unit != "tape" && unit != "keyboard") {
        err = "sol has no unit '" + unit + "' -- serial, printer, tape, keyboard";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    auto s = g_resolver(endpoint, err);
    if (!s) return false;

    if (unit == "serial")        serial_.connect(std::move(s));
    else if (unit == "printer")  printer_ = std::move(s);
    else if (unit == "tape")     tape_ = std::move(s);
    else                         kb_ = std::move(s);  // keyboard
    return true;
}

bool SolBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit == "serial")        serial_.disconnect();
    else if (unit == "printer")  printer_ = std::make_unique<NullStream>();
    else if (unit == "tape")     tape_ = std::make_unique<NullStream>();
    else if (unit == "keyboard") kb_ = std::make_unique<NullStream>();
    else {
        err = "sol has no unit '" + unit + "' -- serial, printer, tape, keyboard";
        return false;
    }
    return true;
}

ByteStream* SolBoard::unitStream(const std::string& unit) {
    if (unit == "serial")   return &serial_.stream();
    if (unit == "printer")  return printer_.get();
    if (unit == "tape")     return tape_.get();
    if (unit == "keyboard") return kb_.get();
    return nullptr;
}

}  // namespace altair
