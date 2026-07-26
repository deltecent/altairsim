#include "boards/mits-88lpc.h"

#include "core/statefile.h"
#include "host/endpoint.h"

namespace altair {
namespace {

// ---------------------------------------------------------------------------
// The status word (reference §4). ACTIVE HIGH -- a set bit means the named
// condition IS so. The manual defines bits 0..3; 4..7 are undefined.
// ---------------------------------------------------------------------------
constexpr uint8_t kBufferEmpty = 0x01;  // bit 0: HIGH = buffer has room / ready for a char
constexpr uint8_t kNotPrinting = 0x02;  // bit 1: HIGH = print head idle (LOW = in motion)
constexpr uint8_t kPaperOk     = 0x04;  // bit 2: HIGH = paper feeding normally (LOW = jammed)
constexpr uint8_t kLineFeedOk  = 0x08;  // bit 3: HIGH = a line feed would be accepted

// The control word (reference §3). ACTIVE-HIGH COMMAND STROBES -- unlike the C700's
// active-low PRIME, a set bit here PERFORMS the action, and more than one may be set.
constexpr uint8_t kPrint     = 0x01;  // D0: print the buffer (commit the line)
constexpr uint8_t kLineFeed  = 0x02;  // D1: advance the paper one line, no printing
constexpr uint8_t kClear     = 0x04;  // D2: discard the buffer
constexpr uint8_t kIntEnable = 0x08;  // D3: HIGH = enable interrupts, LOW = disable

// The 6-bit data code -> printer glyph (reference §5). The 64-char set is ASCII
// 0x20..0x5F, packed into six bits with bit 6 = complement of bit 5. Inferred packing
// (the Okidata chart is not in the LPC manual) but consistent with its test program,
// whose space is 100000 octal = 0x20. So 0x20->' ', 0x00->'@', 0x01->'A', 0x1F->'_'.
inline uint8_t decodeGlyph(uint8_t data) {
    uint8_t code = data & 0x3F;
    return (code & 0x20) ? code : (uint8_t)(code | 0x40);
}

LpcBoard::EndpointResolver g_resolver;

} // namespace

void LpcBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

LpcBoard::LpcBoard() : stream_(std::make_unique<NullStream>()) {
    // No null pointer in the stream path, ever: a card with nothing plugged into it is
    // a card with a DEAD line (a printer switched off), not a dangling one.
}

// ---------------------------------------------------------------------------
// Status: what the printer tells the guest about whether it can take a byte and
// whether a line is in flight. A byte-sink line (a file, a socket, the console) is
// essentially always ready -- writable() is the honest analogue of BUFFER EMPTY, and
// the mechanical states (printing, paper, line feed) are always "OK" because a file
// cannot be mid-stroke or out of paper. The bits are here so the day a real parallel
// port reports one, it lands where the manual says.
// ---------------------------------------------------------------------------
uint8_t LpcBoard::statusByte() const {
    uint8_t s = 0;
    if (stream_->writable()) s |= kBufferEmpty;  // ready for the next character
    s |= kNotPrinting;                            // never mid-stroke: printing is instant here
    s |= kPaperOk;                                // a file cannot jam
    s |= kLineFeedOk;                             // ...and it will always take a line feed
    // Bits 4..7 are undefined by the manual and stay clear. The interrupt-enable bit
    // software wrote is NOT reported in status (the LPC status table stops at bit 3);
    // intEnabled_ is stored for the interrupt structure, which is not modeled.
    return s;
}

// ---------------------------------------------------------------------------
// The bus interface. Two ports: Control at BASE (even), Data at BASE+1 (odd).
// ---------------------------------------------------------------------------
// A0 PICKS THE CHANNEL, AND THE TWO CHANNELS FACE DIFFERENT WAYS. The Control channel
// (even) takes BOTH an IN (status) and an OUT (command). The Data channel (odd) takes
// an OUT ONLY -- a printer sends nothing back, so there is nothing to read there. The
// direction is part of the decode, exactly as on the C700: on `IN <odd>` nothing on
// the card turns on and it leaves the bus alone. We must NOT claim the read and hand
// back an 0xFF of our own making -- 0xFF is the BUS's word for "nobody drove this
// cycle" (DESIGN.md 4.6.1, issue #26). Saying so here is what makes us do the same.
bool LpcBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    uint8_t p = c.port();
    if (c.type == Cycle::IoWrite) return p == base_ || p == (uint8_t)(base_ + 1);
    if (c.type == Cycle::IoRead) return p == base_;  // status only -- see above
    return false;
}

uint8_t LpcBoard::read(const BusCycle& c) {
    // Only the even channel is ever read: decodes() does not claim the odd one, so the
    // bus floats it and we are never asked. No 0xFF is manufactured here.
    (void)c;
    return statusByte();
}

void LpcBoard::write(const BusCycle& c) {
    if ((c.port() - base_) & 1) {
        // DATA -- a 6-bit code into the printer's buffer, decoded to its glyph. When
        // the buffer fills to 80, the real printer starts printing on its own.
        lineBuf_.push_back((char)decodeGlyph(c.data));
        if (lineBuf_.size() >= kLineWidth) printLine();
        return;
    }

    // CONTROL -- the active-high command strobes (reference §3). Independent bits; a
    // write may set more than one. CLEAR first (discard), then PRINT (commit), then a
    // bare LINE FEED, so a "clear and feed" does the sensible thing.
    if (c.data & kClear)    lineBuf_.clear();
    if (c.data & kPrint)    printLine();
    if (c.data & kLineFeed) stream_->writeByte('\n');

    // D3 arms/disarms the interrupt structure. Stored; the request itself is not raised
    // in the polled card.
    intEnabled_ = (c.data & kIntEnable) != 0;
}

// Commit the pending line: the decoded characters, then the paper advance. Printing a
// line advances the paper on the real printer (its test program prints 64 lines with no
// explicit LINE FEED between them), so a printed line becomes a text line + '\n'.
// Flushing stays in pump(), as on the C700 -- never from write().
void LpcBoard::printLine() {
    for (char ch : lineBuf_) stream_->writeByte((uint8_t)ch);
    stream_->writeByte('\n');
    lineBuf_.clear();
}

void LpcBoard::reset(Reset) {
    // POC/RESET disables the interrupt structure and clears the pending line. The line
    // STAYS CONNECTED -- a warm reset does not unplug the printer.
    intEnabled_ = false;
    lineBuf_.clear();
    stream_->flush();
}

void LpcBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(intEnabled_);
    w.str(lineBuf_);  // the printer's pending line is software-visible state
}

void LpcBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    intEnabled_ = r.boolean();
    lineBuf_    = r.str();
}

// The one door to the outside world (DESIGN.md 7.1): let a socket accept/drain, and
// flush a file so a capture is visible while the machine runs rather than only at
// DISCONNECT. Both are no-ops on a NullStream.
void LpcBoard::pump() {
    stream_->pump();
    stream_->flush();
}

// ---------------------------------------------------------------------------
// Properties, units, and the connector.
// ---------------------------------------------------------------------------
std::vector<Property> LpcBoard::properties() {
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
            // you could build. The manual: the control address is even, data the odd
            // above it.
            if (v.i() & 1) {
                err = "the 88-LPC decodes an even/odd PAIR -- the base must be even";
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
std::vector<UnitDef> LpcBoard::units() const {
    return {{"prn", UnitKind::Serial, connectSpec_}};
}

std::vector<MapEntry> LpcBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "LPC -- status / control: PRINT + LINE FEED + CLEAR + interrupt enable"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "write", "LPC -- data (a 6-bit char to the printer)"},
    };
}

bool LpcBoard::applyEndpoint(const std::string& endpoint, std::string& err) {
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }

    // An in:/out: PATH written in a machine file is relative to that file, and typed at
    // the prompt is relative to the shell -- the same rule every path in the sim follows.
    // rebaseEndpointPaths() is the one place that knows the grammar; we rebase only the
    // copy handed to the resolver and REMEMBER the original spec, so a relative path does
    // not double-rebase when CONFIG SAVE writes it back and a reload rebases again. The
    // lambda also collects each PATH, so a failed open can name the rule that applied.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(endpoint, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });

    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    stream_      = std::move(s);
    connectSpec_ = endpoint;  // as written -- what SHOW and CONFIG SAVE echo
    return true;
}

bool LpcBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "prn") {
        err = "lpc has no unit '" + unit + "' -- it has one, and it is called 'prn'";
        return false;
    }
    return applyEndpoint(ep, err);
}

bool LpcBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "prn") {
        err = "lpc has no unit '" + unit + "' -- it has one, and it is called 'prn'";
        return false;
    }
    stream_      = std::make_unique<NullStream>();
    connectSpec_ = "null";
    return true;
}

} // namespace altair
