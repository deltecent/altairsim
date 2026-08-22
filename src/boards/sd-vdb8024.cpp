#include "boards/sd-vdb8024.h"

#include "boards/sd-vdb8024-font.h"
#include "core/statefile.h"
#include "host/display.h"
#include "host/endpoint.h"
#include "host/terminal/font.h"

#include <vector>

namespace altair {
namespace {

// The injected host video service (setDisplay), borrowed. Null on the bench and in a
// headless build with nothing wired -- pump() then simply does not draw.
Display* g_display = nullptr;

// The injected endpoint resolver (setResolver), borrowed. The board hands an endpoint
// string to it and gets back a stream; it never learns what a socket is (DESIGN.md 7.7).
EndpointResolver g_resolver;

// The VDB-8024's character generator behind the shared TerminalFont seam -- the
// authentic CGEN PROM (sd-vdb8024-font.h), 7 dots in an 8-wide cell over a 10-line
// field. Stateless, so one shared instance serves every board.
class Vdb8024Font : public TerminalFont {
public:
    int      cellCols() const override { return vdb8024font::kCols; }
    int      cellRows() const override { return vdb8024font::kRows; }
    uint16_t glyphRow(uint8_t code, int row) const override {
        // The PROM row is 8 bits with bit 7 leftmost; the seam wants bit 15 leftmost, so
        // MSB-align the byte into the top of the word (bit 7 -> bit 15).
        return (uint16_t)vdb8024font::glyphRow(code, row) << 8;
    }
};
const Vdb8024Font g_vdbFont;

} // namespace

void Vdb8024Board::setDisplay(Display* d) { g_display = d; }
void Vdb8024Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Vdb8024Board::Vdb8024Board() : kb_(std::make_unique<NullStream>()) {
    renderer_.setFont(&g_vdbFont);
    renderer_.setReverse(reverse_);
    renderer_.setCursorMode(cursorMode_);
}

// ---------------------------------------------------------------------------
// The terminal state machine -- one display byte from OUT base+1 (SD v1.6 dialect).
// ---------------------------------------------------------------------------
void SdVdb16Emulator::feed(uint8_t b, TerminalScreen& scr) {
    switch (parse_) {
        case Parse::Normal:
            if (b < 0x20) control(b, scr);
            else          scr.putGlyph(b & 0x7F);
            break;

        case Parse::Esc:
            escByte(b & 0x7F, scr);  // the firmware RES 7 before comparing
            break;

        case Parse::PosRow: {
            // ESC = <row+0x20> <col+0x20>. An out-of-range row aborts the sequence and
            // the next byte is a fresh character (as the v1.6 firmware's RET does).
            uint8_t v = b & 0x7F;
            if (v < 0x20 || (v - 0x20) >= scr.rows()) { parse_ = Parse::Normal; break; }
            pendRow_ = (uint8_t)(v - 0x20);
            parse_   = Parse::PosCol;
            break;
        }

        case Parse::PosCol: {
            uint8_t v = b & 0x7F;
            if (v < 0x20 || (v - 0x20) >= scr.cols()) { parse_ = Parse::Normal; break; }
            scr.place(pendRow_, v - 0x20);
            parse_ = Parse::Normal;
            break;
        }

        case Parse::Attr: {
            // ESC G n: '0' standard, '2' blink, '4' reverse, '6' reverse+blink. Half
            // intensity is a separate field (ESC & / '), so '0' leaves it alone.
            uint8_t v = b & 0x7F;
            uint8_t a = scr.currentAttr();
            constexpr uint8_t R = TerminalScreen::kAttrReverse, B = TerminalScreen::kAttrBlink;
            if      (v == '0') a &= (uint8_t)~(R | B);
            else if (v == '2') a = (uint8_t)((a & ~R) | B);
            else if (v == '4') a = (uint8_t)((a & ~B) | R);
            else if (v == '6') a |= (uint8_t)(R | B);
            scr.setCurrentAttr(a);
            parse_ = Parse::Normal;
            break;
        }
    }
}

void SdVdb16Emulator::control(uint8_t b, TerminalScreen& scr) {
    switch (b) {
        case 0x08: scr.backspace(); break;      // backspace -- non-destructive cursor left
        case 0x09: scr.tab(); break;            // tab to the next multiple of 8
        case 0x0A: scr.lineFeed(); break;       // line feed
        case 0x0B: scr.cursorUp(); break;       // cursor up
        case 0x0C: scr.cursorForward(); break;  // cursor right (forward space) -- wraps
        case 0x0D: scr.carriageReturn(); break; // carriage return
        case 0x1A: scr.clearScreen(); break;    // clear screen + home
        case 0x1B: parse_ = Parse::Esc; break;  // escape lead-in
        case 0x1E: scr.home(); break;           // home
        case 0x1F: scr.newline(); break;        // new line (LF + CR)
        default: break;  // every other C0 code is ignored, as in the firmware
    }
}

void SdVdb16Emulator::escByte(uint8_t b, TerminalScreen& scr) {
    switch (b) {
        case '=':  parse_ = Parse::PosRow; return;             // position cursor
        case '*':
        case ':':  scr.clearScreen(); break;                   // clear screen
        case 'T':  scr.eraseToEol(); break;                    // erase to end of line
        case 'Y':  scr.eraseToEos(); break;                    // erase to end of screen
        case 'G':  parse_ = Parse::Attr; return;               // video attribute
        case '&':  scr.setCurrentAttr(scr.currentAttr() | TerminalScreen::kAttrHalf); break;
        case 0x27: scr.setCurrentAttr(scr.currentAttr() & (uint8_t)~TerminalScreen::kAttrHalf);
                   break;                                       // ' -- full intensity
        case 'E':
        case 'R':  break;  // insert/delete line -- no-ops in the v1.6 firmware
        default:   break;  // unknown escape -- ignored
    }
    parse_ = Parse::Normal;
}

// ---------------------------------------------------------------------------
// Bus: two I/O ports. Status at base+0 (read), keyboard/display at base+1.
// ---------------------------------------------------------------------------
bool Vdb8024Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    switch (c.type) {
        case Cycle::IoRead:
        case Cycle::IoWrite:
            return c.port() == base_ || c.port() == (uint8_t)(base_ + 1);
        default:
            return false;
    }
}

uint8_t Vdb8024Board::read(const BusCycle& c) {
    if (c.type != Cycle::IoRead) return 0xFF;
    if (c.port() == base_) return statusByte();
    // base+1 IN -- the keyboard data byte. Reading it clears the keyboard-ready strobe
    // (status D1), exactly as the physical read pulse clears the latch. That also drops
    // the VI line if we are strapped for interrupts -- the ISR's IN 01H is the implicit
    // end-of-interrupt, so no daisy-chain EOI is needed for this single-source level.
    kbHave_ = false;
    intChanged();
    return kbData_;
}

// VI0-VI7. The keyboard-strobe interrupt strap (reference 6). While a byte is waiting
// (status D1) the board pulls its strapped VI line; `none` pulls nothing. The vector is
// NOT ours -- the SBC-200's CTC supplies it (mode-2 vector 0x02) by claiming the IntAck
// cycle; this card only asks, on the wire the jumper chose.
uint8_t Vdb8024Board::assertsVi() const {
    return kbHave_ ? viBit(kbIrq_) : 0;
}

void Vdb8024Board::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite) return;
    if (c.port() == (uint8_t)(base_ + 1)) emu_.feed(c.data, screen_);  // display / control
    // A write to the status port (base+0) does nothing -- it is read-only.
}

// Status: D1 = a keyboard byte is waiting; D2 = the display will accept a byte. Both
// active-HIGH. The emulated terminal consumes a byte instantly, so D2 is always set.
uint8_t Vdb8024Board::statusByte() const {
    uint8_t s = 0x04;  // D2 -- display ready
    if (kbHave_) s |= 0x02;  // D1 -- keyboard ready
    return s;
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void Vdb8024Board::power() {
    screen_.clearScreen();
    screen_.setCurrentAttr(0);
    emu_.reset();
    kbHave_  = false;
    kbData_  = 0;
    kbRx_    = 0;
    screen_.markDirty();
    intChanged();  // no byte waiting -> the VI line, if strapped, stands down
}

// S-100 RESET resets the onboard Z80, which re-runs its power-up path (INIT then CLEAR):
// the page blanks and the cursor homes. The keyboard strobe drops with it.
void Vdb8024Board::reset(Reset) {
    screen_.clearScreen();
    screen_.setCurrentAttr(0);
    emu_.reset();
    kbHave_  = false;
    intChanged();  // the keyboard strobe drops with the reset
}

// ---------------------------------------------------------------------------
// The host turn: drain the keyboard, then paint the frame. Once per time slice, on the
// main thread (DESIGN.md 7.4) -- never inside a bus cycle.
// ---------------------------------------------------------------------------
void Vdb8024Board::pump() {
    kb_->pump();
    latchKeyboard();

    if (!g_display) return;
    if (!renderer_.frameChanged(screen_, g_display)) return;
    if (!g_display->wantsFrame()) return;
    renderer_.render(this, id, *g_display, screen_, videoWidth_);
}

void Vdb8024Board::latchKeyboard() {
    if (kbHave_) return;                   // the strobe is still set: the line waits
    if (!kb_ || !kb_->readable()) return;
    uint8_t b = 0;
    if (kb_->read(&b, 1) != 1) return;
    kbData_ = (uint8_t)(b & 0x7F);         // a 7-bit ASCII keyboard
    kbHave_ = true;
    ++kbRx_;  // a keystroke crossed into the guest -- the run loop's live-traffic proof
    intChanged();  // the strobe -- raises status D1 and, if strapped, the VI line
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The page, the attribute plane, the cursor, the parser state and
// the keyboard holding register travel; the switches (port, video polarity, cursor mode)
// are straps restored from config, and the Display/keyboard streams are host resources.
// ---------------------------------------------------------------------------
void Vdb8024Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.raw(screen_.cellData(), screen_.planeBytes());
    w.raw(screen_.attrData(), screen_.planeBytes());
    w.u32((uint32_t)screen_.cursorRow());
    w.u32((uint32_t)screen_.cursorCol());
    w.u8(screen_.currentAttr());
    w.u8(emu_.parseByte());
    w.u8(emu_.pendRow());
    w.u8(kbData_);
    w.u8(kbHave_ ? 1 : 0);
    w.u64(kbRx_);
}

void Vdb8024Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    r.raw(screen_.cellData(), screen_.planeBytes());
    r.raw(screen_.attrData(), screen_.planeBytes());
    int cr = (int)r.u32();
    int cc = (int)r.u32();
    screen_.setCursor(cr, cc);
    screen_.setCurrentAttr(r.u8());
    emu_.setParseByte(r.u8());
    emu_.setPendRow(r.u8());
    kbData_  = r.u8();
    kbHave_  = r.u8() != 0;
    kbRx_    = r.u64();
    screen_.markDirty();  // the restored screen owes the host a full redraw
}

// ---------------------------------------------------------------------------
// Host connect / units.
// ---------------------------------------------------------------------------
bool Vdb8024Board::connect(const std::string& unit, const std::string& endpoint,
                           std::string& err) {
    if (unit != "keyboard") {
        err = "vdb8024 has no unit '" + unit + "' -- keyboard";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens (rebaseEndpointPaths knows the grammar). The `connect` unit property
    // routes here too, so both the CONNECT command and a declarative connect are covered.
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
    kb_ = std::move(s);
    return true;
}

bool Vdb8024Board::disconnect(const std::string& unit, std::string& err) {
    if (unit != "keyboard") {
        err = "vdb8024 has no unit '" + unit + "' -- keyboard";
        return false;
    }
    kb_ = std::make_unique<NullStream>();
    return true;
}

std::vector<UnitDef> Vdb8024Board::units() const {
    return {{"keyboard", UnitKind::Serial, kb_->describe()}};
}

std::vector<Property> Vdb8024Board::unitProperties(const std::string& unit) {
    if (unit != "keyboard") return {};
    std::vector<Property> p;
    // The keyboard is a line you CONNECT; `connect` is the endpoint on the far end, and
    // reading it round-trips through CONFIG SAVE. This is what makes `[board.unit.keyboard]
    // connect = "console"` in a machine file resolve.
    Property x;
    x.name = "connect";
    x.help = "The endpoint on the other end of the keyboard line (CONNECT sets this)";
    x.kind = Kind::Str;
    x.get  = [this] { return Value::ofStr(kb_->describe()); };
    x.set  = [this](const Value& v, std::string& err) { return connect("keyboard", v.s(), err); };
    p.push_back(std::move(x));
    return p;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> Vdb8024Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Low I/O port: status (IN base+0) / keyboard (IN base+1) / display "
                  "(OUT base+1). The real card is fixed at 00";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFE;  // base+1 must still be a valid port
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "cursor";
        x.help    = "Cursor at the current cell: off, blink, or steady (the board default "
                    "is a blinking cursor)";
        x.kind    = Kind::Enum;
        x.choices = {"off", "blink", "steady"};
        x.get     = [this] {
            return Value::ofStr(cursorMode_ == 0 ? "off"
                                : cursorMode_ == 2 ? "steady"
                                                   : "blink");
        };
        x.set = [this](const Value& v, std::string&) {
            cursorMode_ = (v.s() == "off") ? 0 : (v.s() == "steady") ? 2 : 1;
            renderer_.setCursorMode(cursorMode_);
            screen_.markDirty();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "video";
        x.help    = "Screen video polarity: normal (light on dark) or reverse";
        x.kind    = Kind::Enum;
        x.choices = {"normal", "reverse"};
        x.get     = [this] { return Value::ofStr(reverse_ ? "reverse" : "normal"); };
        x.set     = [this](const Value& v, std::string&) {
            reverse_ = (v.s() == "reverse");
            renderer_.setReverse(reverse_);
            screen_.markDirty();
            return true;
        };
        p.push_back(std::move(x));
    }
    // The keyboard-interrupt strap (E17 -> E13-E16). Default `none` = the host polls
    // status D1. Strapped to a VI line, each keystroke raises that line for an interrupt
    // controller (the SBC-200's CTC) to vector on -- what the SD video CBIOS needs.
    p.push_back(irqJumperProperty(
        "interrupt",
        "Keyboard-strobe interrupt strap: none = polled (default), or the S-100 VI line "
        "the keyboard raises while a byte waits (the SBC-200 CTC vectors it -- video CBIOS "
        "straps vi2)",
        kbIrq_));
    p.push_back(Display::widthProperty(videoWidth_));
    return p;
}

std::vector<MapEntry> Vdb8024Board::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read",
         "VDB-8024 -- status (D1 keyboard-ready, D2 display-ready)"},
        {(uint32_t)(base_ + 1), (uint32_t)(base_ + 1), "read/write",
         "VDB-8024 -- keyboard data / display data"},
    };
}

} // namespace altair
