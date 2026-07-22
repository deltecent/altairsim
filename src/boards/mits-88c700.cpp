#include "boards/mits-88c700.h"

#include "core/statefile.h"

namespace altair {
namespace {

// ---------------------------------------------------------------------------
// The status word (reference Table 1). ACTIVE HIGH -- unlike the 88-SIO's
// inverted ready bits, the C700's status reads true-sense, so a set bit means the
// named condition IS so. Bit 5 is unused.
// ---------------------------------------------------------------------------
constexpr uint8_t kAcknowledge = 0x01;  // bit 0: HIGH = printer will accept new data
constexpr uint8_t kBusy        = 0x02;  // bit 1: HIGH = print/return/line-feed occurring
constexpr uint8_t kPaperEmpty  = 0x04;  // bit 2: HIGH = out of paper
constexpr uint8_t kNotSelected = 0x08;  // bit 3: HIGH = NOT selected (select is active-low)
constexpr uint8_t kFault       = 0x10;  // bit 4: HIGH = fault
constexpr uint8_t kIntEnable   = 0x40;  // bit 6: HIGH = interrupts enabled
constexpr uint8_t kIntRequest  = 0x80;  // bit 7: HIGH = interrupt requested (not modeled)

// The control word (reference Table 2). Only two bits exist; the rest are ignored.
constexpr uint8_t kPrimeLow   = 0x01;  // D0: LOW = reset buffer + home the head
constexpr uint8_t kIntControl = 0x02;  // D1: HIGH = enable the interrupt structure

C700Board::EndpointResolver g_resolver;

} // namespace

void C700Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

C700Board::C700Board() : stream_(std::make_unique<NullStream>()) {
    // No null pointer in the stream path, ever: a card with nothing plugged into it
    // is a card with a DEAD line (a printer that is switched off), not a dangling one.
}

// ---------------------------------------------------------------------------
// Status: everything the printer tells the guest about whether it can take a byte.
// For a byte-sink line (a file, a socket, the console) the answer is "yes" as long
// as the stream will take a write -- writable() is what a full transmit buffer
// negates, and it is the honest analogue of a printer that has gone BUSY.
// ---------------------------------------------------------------------------
uint8_t C700Board::statusByte() const {
    uint8_t s = 0;
    if (stream_->writable()) {
        s |= kAcknowledge;  // ready for the next character
    } else {
        s |= kBusy;         // the line will not take a byte right now
    }
    // Always has paper, always selected (kNotSelected clear = selected), never faults --
    // a file or a socket cannot run out of paper. The bits are here so the day a real
    // parallel port reports one, it lands where the manual says it lands.
    (void)kPaperEmpty;
    (void)kNotSelected;
    (void)kFault;

    if (intEnabled_) s |= kIntEnable;
    // kIntRequest stays clear: the interrupt request/vector path is not modeled in
    // the polled card (see the header, and issue #26).
    (void)kIntRequest;
    return s;
}

// ---------------------------------------------------------------------------
// The bus interface. Two ports: Control/Status at BASE (even), Data at BASE+1 (odd).
// ---------------------------------------------------------------------------
// A0 PICKS THE CHANNEL, AND THE TWO CHANNELS FACE DIFFERENT WAYS. The reference is
// exact about it (§2): Control/Status is the even port and takes BOTH an IN (status)
// and an OUT (control); Data Transfer is the odd port and takes an OUT ONLY. There is
// no `IN` at the odd address anywhere in the manual, because there is nothing on this
// card to read there -- a printer sends nothing back up the ribbon cable.
//
// So the DIRECTION is part of the decode, not an afterthought inside read(). A real
// card's status buffer is enabled by the port compare AND sINP AND A0=0; on `IN <odd>`
// nothing turns on and the card leaves the data bus alone. Saying that here -- rather
// than claiming the cycle and returning 0xFF -- is what makes us do the same.
//
// AND THAT IS THE BUG THIS FIXES (issue #26). We used to decode the read and hand back
// an 0xFF of our own making, which is the one thing a board may never do (DESIGN.md
// 4.6.1): 0xFF is the BUS's word and it means NOBODY DROVE THIS CYCLE. The value an
// operator saw was right and its provenance was a lie, and the lie had a cost exactly
// where it hurt -- the monitor annotates an unclaimed IN, so `IN 04` explained itself
// ("nobody answered -- the bus floated it") and `IN 03`, the port actually under
// investigation, printed a bare FF. A board impersonating the bus silences the bus.
//
// Found via CP/M SURVEY, which probes every port and reads FF as "nothing there", so it
// does not list 03. That is CORRECT, and it is what real hardware does -- the point of
// this change is that it is now correct for the real reason.
bool C700Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    uint8_t p = c.port();
    if (c.type == Cycle::IoWrite) return p == base_ || p == (uint8_t)(base_ + 1);
    if (c.type == Cycle::IoRead) return p == base_;  // status only -- see above
    return false;
}

uint8_t C700Board::read(const BusCycle& c) {
    // Only the even channel is ever read: decodes() does not claim the other one, so
    // the bus floats it and we are never asked. No 0xFF is manufactured here.
    (void)c;
    return statusByte();
}

void C700Board::write(const BusCycle& c) {
    if ((c.port() - base_) & 1) {
        // DATA -- the character goes out on the line, verbatim.
        stream_->writeByte(c.data);
        return;
    }

    // CONTROL -- the two bits that exist (reference Table 2).
    //
    // PRIME is active-low: D0 = 0 resets the printer's buffer and homes the head. We
    // have no buffer to reset (bytes go straight out), so the honest equivalent is to
    // flush the line -- push out anything a buffered endpoint is still holding.
    if ((c.data & kPrimeLow) == 0) stream_->flush();

    // D1 arms/disarms the interrupt structure. Stored so the status byte reports it;
    // the request itself is not raised in the polled card.
    intEnabled_ = (c.data & kIntControl) != 0;
}

void C700Board::reset(Reset) {
    // POC/RESET disables the interrupt structure. The line STAYS CONNECTED -- a warm
    // reset does not unplug the printer.
    intEnabled_ = false;
    stream_->flush();
}

void C700Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(intEnabled_);
}

void C700Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    intEnabled_ = r.boolean();
}

// The one door to the outside world (DESIGN.md 7.1): let a socket accept/drain, and
// flush a file so a capture is visible while the machine runs rather than only at
// DISCONNECT. Both are no-ops on a NullStream.
void C700Board::pump() {
    stream_->pump();
    stream_->flush();
}

// ---------------------------------------------------------------------------
// Properties, units, and the connector.
// ---------------------------------------------------------------------------
std::vector<Property> C700Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            // A0 is not decoded -- it picks the channel -- so an odd base is not a card
            // you could build. The manual: "The Control/Status address is always even."
            if (v.i() & 1) {
                err = "the 88-C700 decodes an even/odd PAIR -- the base must be even";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(connectSpec_); };
        x.set  = [this](const Value& v, std::string& err) { return applyEndpoint(v.s(), err); };
        p.push_back(std::move(x));
    }
    return p;
}

// ONE serial unit, named for what it is: the printer. CONNECT names it.
std::vector<UnitDef> C700Board::units() const {
    return {{"prn", UnitKind::Serial, connectSpec_}};
}

std::vector<MapEntry> C700Board::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "C700 -- status (read) / control: PRIME + interrupt enable (write)"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "write", "C700 -- data (to the printer)"},
    };
}

bool C700Board::applyEndpoint(const std::string& endpoint, std::string& err) {
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }

    // A file: PATH written in a machine file is relative to that file, and typed at
    // the prompt is relative to the shell -- the same rule every path in the sim
    // follows. resolvePath() is where a mount rebases (mits-hardsector.cpp); a file
    // endpoint's PATH is a path too. We rebase it here (the board is the only thing
    // that knows its config dir) and REMEMBER the original spec, so a relative path
    // does not double-rebase when CONFIG SAVE writes it back and a reload rebases
    // again. Nothing else in the endpoint grammar is a path.
    std::string spec = endpoint;
    if (endpoint.rfind("file:", 0) == 0) {
        std::string path = endpoint.substr(5);
        if (!path.empty()) spec = "file:" + resolvePath(path);
    }

    auto s = g_resolver(spec, err);
    if (!s) {
        if (endpoint.rfind("file:", 0) == 0) err += pathNote(endpoint.substr(5));
        return false;
    }
    stream_      = std::move(s);
    connectSpec_ = endpoint;  // as written -- what SHOW and CONFIG SAVE echo
    return true;
}

bool C700Board::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "prn") {
        err = "c700 has no unit '" + unit + "' -- it has one, and it is called 'prn'";
        return false;
    }
    return applyEndpoint(ep, err);
}

bool C700Board::disconnect(const std::string& unit, std::string& err) {
    if (unit != "prn") {
        err = "c700 has no unit '" + unit + "' -- it has one, and it is called 'prn'";
        return false;
    }
    stream_      = std::make_unique<NullStream>();
    connectSpec_ = "null";
    return true;
}

} // namespace altair
