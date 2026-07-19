#include "boards/proctech-vdm1.h"

#include "boards/proctech-vdm1-font.h"
#include "host/display.h"

namespace altair {
namespace {

// The injected host video service (setDisplay), borrowed. Null on the bench and in
// a headless build with nothing wired -- pump() then simply does not draw.
Display* g_display = nullptr;

// Glyph cell on screen: one MCM6574 character box. The ROM glyphs are 8 dots wide
// (bit 7 leftmost, unused, so a 1-dot left gap) by 13 scan rows (lowercase
// descenders reach the bottom rows) -- see proctech-vdm1-font.h.
constexpr int kCellW = vdm1font::kCols;   // 8
constexpr int kCellH = vdm1font::kRows;   // 13

// Emulated-time constants, in T-states at the 2 MHz the Altair crystal runs (the
// card's own dot clock is asynchronous to the CPU; these are the OBSERVABLE
// durations a guest can time). Deterministic and replay-safe -- see the reference.
constexpr uint64_t kTimerTStates = 750000;    // ~0.375 s status one-shot (D0)
constexpr uint64_t kBlinkTStates = 1000000;   // ~0.5 s cursor blink half-period
constexpr uint64_t kLineTStates  = 128;       // ~64 us per scan line
constexpr uint64_t kMarginTStates = 16;       // width of the right-margin (D1) window

// The two-color palette. Period VDM-1s were white or green phosphor; green reads as
// "a terminal" and is easy on a modern panel. Index 0 = background, 1 = foreground.
constexpr Color kBg = {0x00, 0x00, 0x00, 0xFF};
constexpr Color kFg = {0x33, 0xFF, 0x66, 0xFF};

} // namespace

void VdmBoard::setDisplay(Display* d) { g_display = d; }

VdmBoard::VdmBoard() {
    for (auto& b : screen_) b = 0x00;  // blank (control code -> no glyph)
}

// ---------------------------------------------------------------------------
// Bus: memory (screen RAM) OR the one I/O port.
// ---------------------------------------------------------------------------
bool VdmBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    switch (c.type) {
        case Cycle::MemRead:
        case Cycle::MemWrite:
            return c.addr >= base_ && c.addr < base_ + kBytes;
        case Cycle::IoRead:
        case Cycle::IoWrite:
            return c.port() == port_;
        default:
            return false;
    }
}

uint8_t VdmBoard::read(const BusCycle& c) {
    if (c.type == Cycle::IoRead) return statusByte();
    return screen_[c.addr - base_];  // MemRead -- decodes() guaranteed the range
}

void VdmBoard::write(const BusCycle& c) {
    if (c.type == Cycle::IoWrite) {
        // Load the scroll (top character row) and fire the status one-shot. Only the
        // low 4 bits are a row number; the rest of the latch does not change what a
        // guest can observe (reference 3.1).
        uint8_t row = c.data & 0x0F;
        if (row != scroll_) dirty_ = true;  // a different row at the top: repaint
        scroll_ = row;
        timerExpiry_ = (clock_ ? clock_->now() : 0) + kTimerTStates;
        return;
    }
    // MemWrite. Only a byte that actually CHANGES needs a repaint -- a guest that
    // rewrites the same character (a cursor walking over unchanged text, a redraw of a
    // static field) is common, and costs nothing here.
    uint8_t& cell = screen_[c.addr - base_];
    if (cell != c.data) dirty_ = true;
    cell = c.data;
}

bool VdmBoard::peek(uint16_t addr, uint8_t& out) const {
    if (addr < base_ || addr >= base_ + kBytes) return false;
    out = screen_[addr - base_];
    return true;
}

// ---------------------------------------------------------------------------
// Status (IN): D0 = the one-shot timer, D1 = SCAN ADVANCE (right-margin). Both are
// readings off the Clock, never a poll-driven counter -- a spin loop must see them
// move because emulated time advanced, so a recorded session replays identically.
// ---------------------------------------------------------------------------
uint8_t VdmBoard::statusByte() const {
    uint8_t s = 0;
    if (clock_) {
        uint64_t now = clock_->now();
        if (now < timerExpiry_) s |= 0x01;                       // D0: one-shot busy
        if (now % kLineTStates >= kLineTStates - kMarginTStates)  // D1: right margin
            s |= 0x02;
    }
    return s;
}

void VdmBoard::power() {
    for (auto& b : screen_) b = 0x00;
    scroll_ = 0;
    timerExpiry_ = 0;
    dirty_ = true;  // the blanked screen still owes the host one frame
}

// ---------------------------------------------------------------------------
// The host turn: paint the frame and show it. Once per time slice, on the main
// thread (DESIGN.md 7.4) -- never inside a bus cycle.
// ---------------------------------------------------------------------------
// TWO GATES, AND THEY ANSWER DIFFERENT QUESTIONS. The board asks "would this frame
// look any different?" -- deterministic, emulated state only, so headless and windowed
// builds skip identically. The host then asks "do I want a frame right now?" -- wall
// clock, and only a windowed back end has an opinion. Both are checked BEFORE
// render(), because the 213,000 pixel operations are the cost being avoided.
//
// Neither gate clears `dirty_`: a change deferred by the frame limiter is still owed,
// and render() is what finally discharges it.
void VdmBoard::pump() {
    if (!g_display) return;
    if (!frameChanged()) return;
    if (!g_display->wantsFrame()) return;
    render();
}

bool VdmBoard::frameChanged() const {
    if (dirty_) return true;

    // Nothing was written -- but a blinking cursor repaints itself on its own clock.
    if (cursorMode_ == 1 && hasCursorCell_ && clock_) {
        bool blinkOn = ((clock_->now() / kBlinkTStates) & 1) == 0;
        if (blinkOn != lastBlinkOn_) return true;
    }
    return false;
}

void VdmBoard::render() {
    const int w = kCols * kCellW, h = kRows * kCellH;  // 512 x 208
    Surface* s = g_display->acquire(w, h, PixelFormat::Indexed8);
    if (!s) return;

    const Color pal[2] = {reverse_ ? kFg : kBg, reverse_ ? kBg : kFg};
    g_display->setPalette(pal);

    // Cursor visibility this frame (D7 marks the cell; SW3/SW4 pick the behavior).
    bool blinkOn = true;
    if (clock_) blinkOn = ((clock_->now() / kBlinkTStates) & 1) == 0;
    bool cursorShown =
        cursorMode_ == 2 /*steady*/ || (cursorMode_ == 1 /*blink*/ && blinkOn);

    s->clear(0);  // background

    // Whether ANY cell carries the cursor bit, set as we go: it is what decides
    // whether a later blink-phase flip can change the picture at all (frameChanged).
    bool sawCursorCell = false;

    for (int dr = 0; dr < kRows; ++dr) {
        int srcRow = (scroll_ + dr) & (kRows - 1);  // hardware scroll wraps mod 16
        for (int col = 0; col < kCols; ++col) {
            uint8_t byte = screen_[srcRow * kCols + col];
            uint8_t code = byte & 0x7F;
            if (byte & 0x80) sawCursorCell = true;
            bool cursor = (byte & 0x80) && cursorShown;

            int px = col * kCellW, py = dr * kCellH;
            uint8_t bgIdx = cursor ? 1 : 0;  // invert the whole cell for the cursor
            uint8_t fgIdx = cursor ? 0 : 1;

            if (cursor) {
                for (int y = 0; y < kCellH; ++y)
                    for (int x = 0; x < kCellW; ++x) s->put(px + x, py + y, bgIdx);
            }
            for (int ry = 0; ry < vdm1font::kRows; ++ry) {
                uint8_t bits = vdm1font::glyphRow(code, ry);  // bit 7 = leftmost dot
                for (int rx = 0; rx < vdm1font::kCols; ++rx)
                    if (bits & (0x80 >> rx)) s->put(px + rx, py + ry, fgIdx);
            }
        }
    }

    // The frame is on the host. Remember what it was painted FROM, so the next pump
    // can tell whether anything has moved since.
    dirty_         = false;
    lastBlinkOn_   = blinkOn;
    hasCursorCell_ = sawCursorCell;

    g_display->present(s);
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> VdmBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "Screen-RAM base address -- 1 KB-aligned (16x64 = 1024 bytes)";
        x.kind  = Kind::Int;
        x.radix = 16;  // an address on the wire -> hex
        x.min   = 0;
        x.max   = 0xFC00;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x3FF) {
                err = "the VDM-1 screen is 1 KB -- base must be a multiple of 0x400";
                return false;
            }
            base_ = (uint32_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "port";
        x.help  = "I/O port -- scroll (OUT) / status (IN). Low two bits are zero";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFC;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x3) {
                err = "the VDM-1 port decodes on a 4-port boundary -- it must be a "
                      "multiple of 4";
                return false;
            }
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "video";
        x.help    = "Video polarity (SW1/SW2): normal (light on dark) or reverse";
        x.kind    = Kind::Enum;
        x.choices = {"normal", "reverse"};
        x.get     = [this] { return Value::ofStr(reverse_ ? "reverse" : "normal"); };
        x.set     = [this](const Value& v, std::string&) {
            reverse_ = (v.s() == "reverse");
            dirty_   = true;  // the palette is only re-sent by render()
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "cursor";
        x.help    = "Cursor for a byte with bit 7 set (SW3/SW4): off, blink, or steady";
        x.kind    = Kind::Enum;
        x.choices = {"off", "blink", "steady"};
        x.get     = [this] {
            return Value::ofStr(cursorMode_ == 0 ? "off"
                                : cursorMode_ == 2 ? "steady"
                                                   : "blink");
        };
        x.set = [this](const Value& v, std::string&) {
            cursorMode_ = (v.s() == "off") ? 0 : (v.s() == "steady") ? 2 : 1;
            dirty_      = true;
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> VdmBoard::memMap() const {
    return {{base_, base_ + kBytes - 1, "read/write", "VDM-1 screen RAM (16x64)"}};
}

std::vector<MapEntry> VdmBoard::ioMap() const {
    return {{(uint32_t)port_, (uint32_t)port_, "read/write",
             "VDM-1 -- status (read: D0 timer, D1 scan-advance) / scroll (write)"}};
}

} // namespace altair
