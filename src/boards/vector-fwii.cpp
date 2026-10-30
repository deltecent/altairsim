#include "boards/vector-fwii.h"
#include "boards/vector-fwii-font.h"

#include "core/statefile.h"
#include "host/display.h"
#include "host/endpoint.h"

#include <cstring>
#include <random>

namespace altair {
namespace {

// The injected host video service (setDisplay), borrowed. Null on the bench and in a
// headless build with nothing wired.
Display* g_display = nullptr;

// The injected endpoint resolver (setResolver), borrowed.
EndpointResolver g_resolver;

// The raster, off Y1 (reference §6). 14.3347 MHz is the 60 Hz assembly in Table I.
constexpr uint64_t kDotHz       = 14334700;
constexpr uint64_t kDotsPerChar = 8;
constexpr uint64_t kCharsPerLine = 114;
constexpr uint64_t kLinesPerField = 262;
constexpr uint64_t kDotsPerLine  = kDotsPerChar * kCharsPerLine;
constexpr uint64_t kDotsPerField = kDotsPerLine * kLinesPerField;
constexpr uint64_t kNsPerSecond  = 1000000000;

constexpr int kFirstBlankLine = 240;  // V BLANK runs from here to the last line, 261

// Each dot line is drawn twice. 240 lines of 640 dots would be a wide, flat picture, and
// the tube showed them 4:3.
constexpr int kLinesPerDotLine = 2;

// Index 0 = background, 1 = foreground. The board sends one video signal and the monitor
// gave it a color. The manual names none, so this is the VDM-1's green.
constexpr Color kPalette[2] = {{0x00, 0x00, 0x00, 0xFF}, {0x33, 0xFF, 0x66, 0xFF}};

} // namespace

void FwiiBoard::setDisplay(Display* d) { g_display = d; }
void FwiiBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

FwiiBoard::FwiiBoard() : kb_(std::make_unique<NullStream>()) { fillRam(); }

// The same policy, and the same wording, as MemoryBoard's `fill`/`seed`.
void FwiiBoard::fillRam() {
    std::mt19937_64 rng(seed_ ^ 0x9E3779B97F4A7C15ULL);
    for (auto& b : vram_) b = (fill_ == Fill::Zero) ? 0x00 : (uint8_t)(rng() & 0xFF);
}

// ---------------------------------------------------------------------------
// Bus: the 2K of RAM, and the two input ports. No IoWrite: the port decode needs
// sINP (reference §3), so an OUT reaches nothing on this board.
// ---------------------------------------------------------------------------
bool FwiiBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    switch (c.type) {
        case Cycle::MemRead:
        case Cycle::MemWrite:
            return c.addr >= base_ && c.addr < (uint32_t)base_ + kBytes;
        case Cycle::IoRead:
            return (c.port() & 0xFE) == port_;
        default:
            return false;
    }
}

uint8_t FwiiBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead) return vram_[c.addr - base_];
    if ((c.port() & 1) == 0) {
        // A guest waiting for a key polls here. The line counts each time it is found
        // empty, and that count is how the run loop tells a prompt from a busy guest. The
        // key still comes in through pump(): this only asks.
        if (!kbHave_) kb_->readable();
        return statusByte();
    }

    // INPD selects U46, and the read clears the flag. That is also the only thing that
    // releases pin 73. The latch keeps its byte.
    kbHave_ = false;
    intChanged();
    return kbData_;
}

void FwiiBoard::write(const BusCycle& c) {
    if (c.type != Cycle::MemWrite) return;
    // Only a byte that changes a cell on the screen owes the host a frame. The guest may
    // keep what it likes in the last 128 bytes.
    const uint16_t off = (uint16_t)(c.addr - base_);
    if (off < kShown && vram_[off] != c.data) dirty_ = true;
    vram_[off] = c.data;
}

bool FwiiBoard::peek(uint16_t addr, uint8_t& out) const {
    if (addr < base_ || addr >= (uint32_t)base_ + kBytes) return false;
    out = vram_[addr - base_];
    return true;
}

// The vertical counter, read off host time.
//
// Y1 IS THE BOARD'S OWN CRYSTAL. The processor's clock never reaches it, so the count
// must not come from clock_->now(): flat out, that would run the 60 Hz field at the
// host's instruction rate (#825). The time comes through the bus's host clock, the one
// a test sets, and the field needs no deadline, so an idle machine can still stand down.
//
// This reads host time inside a bus cycle, where a board normally does not. Reading it
// once a slice in pump() would make a slice the raster's unit, and the 1.4 ms of blanking
// that a guest polls bit 5 for can fall between two slices.
//
// Whole seconds and the rest go through the crystal apart, because the host's nanosecond
// count times 14334700 does not fit 64 bits.
int FwiiBoard::lineNow() const {
    if (!bus_) return 0;
    const uint64_t ns = bus_->busClock().hostNs();
    const uint64_t dots =
        (ns / kNsPerSecond) * kDotHz + (ns % kNsPerSecond) * kDotHz / kNsPerSecond;
    return (int)((dots % kDotsPerField) / kDotsPerLine);
}

// U21 drives bits 6, 5 and 0. Nothing drives the rest, so they read as the bus floats.
uint8_t FwiiBoard::statusByte() const {
    uint8_t s = 0x9E;
    s |= kbHave_ ? 0x40 : 0x01;
    if (lineNow() < kFirstBlankLine) s |= 0x20;
    return s;
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void FwiiBoard::power() {
    fillRam();
    kbRx_  = 0;
    dirty_ = true;  // the new RAM shows
}

// POC* is the only reset wire on the board: it drives the 8212's CLR, which clears the
// latch and the flag. RESET* goes nowhere, and neither one reaches the video RAM.
void FwiiBoard::reset(Reset r) {
    if (r != Reset::PowerOn) return;
    kbData_ = 0;
    kbHave_ = false;
    intChanged();
}

// ---------------------------------------------------------------------------
// The host turn. Once per time slice, on the main thread -- never inside a bus cycle.
// ---------------------------------------------------------------------------
//
// The keyboard comes first: neither of the two gates below may hold back a key. The board
// asks if the picture would differ, and the host then asks if it wants a frame now. Both
// come before render(), and neither clears `dirty_`, so a deferred change is still owed.
void FwiiBoard::pump() {
    kb_->pump();
    latchKeyboard();

    if (!g_display) return;
    if (!frameChanged()) return;
    if (!g_display->wantsFrame()) return;
    render();
}

// The whole picture, every frame: a Surface's contents are undefined on entry. The dots
// come out of U22 with D7 at the left, and U7 reverses them for a byte with bit 7 set.
void FwiiBoard::render() {
    const int w = kCols * fwiifont::kCols;
    const int h = kRows * fwiifont::kRows * kLinesPerDotLine;  // 640 x 480
    // `this` keys this board's own window (issue #234); id titles it; videoWidth_ sizes it.
    Surface* s = g_display->acquire(this, id, w, h, PixelFormat::Indexed8, videoWidth_);
    if (!s) return;
    g_display->setPalette(this, kPalette);

    uint8_t* px = s->pixels().data();
    for (int row = 0; row < kRows; ++row) {
        const uint8_t* text = vram_ + row * kCols;
        for (int dotLine = 0; dotLine < fwiifont::kRows; ++dotLine) {
            uint8_t* line = px + (size_t)(row * fwiifont::kRows + dotLine) * kLinesPerDotLine * w;
            uint8_t* p    = line;
            for (int col = 0; col < kCols; ++col) {
                uint8_t dots = fwiifont::glyphRow(text[col], dotLine);
                if (text[col] & 0x80) dots = (uint8_t)~dots;
                for (int x = 0; x < fwiifont::kCols; ++x) *p++ = (dots >> (7 - x)) & 1;
            }
            std::memcpy(line + w, line, (size_t)w);
        }
    }

    dirty_ = false;
    g_display->present(this, s);
}

// The real latch takes a new key over an old one. Here the line holds the byte back until
// the flag is clear, so text the host sends ahead of the guest is not lost.
void FwiiBoard::latchKeyboard() {
    if (kbHave_) return;
    if (!kb_->readable()) return;
    uint8_t b = 0;
    if (kb_->read(&b, 1) != 1) return;
    kbData_ = b;  // all eight bits: the board does not mask bit 7
    kbHave_ = true;
    ++kbRx_;
    intChanged();
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE.
// ---------------------------------------------------------------------------
void FwiiBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.raw(vram_, kBytes);
    w.u8(kbData_);
    w.u8(kbHave_ ? 1 : 0);
    w.u64(kbRx_);
}

void FwiiBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    r.raw(vram_, kBytes);
    kbData_ = r.u8();
    kbHave_ = r.u8() != 0;
    kbRx_   = r.u64();
    dirty_  = true;  // the restored screen owes the host a full redraw
}

// ---------------------------------------------------------------------------
// Host connect / units.
// ---------------------------------------------------------------------------
bool FwiiBoard::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    if (unit != "keyboard") {
        err = "fwii has no unit '" + unit + "' -- keyboard";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens. The `connect` unit property routes here too.
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

bool FwiiBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "keyboard") {
        err = "fwii has no unit '" + unit + "' -- keyboard";
        return false;
    }
    kb_ = std::make_unique<NullStream>();
    return true;
}

std::vector<UnitDef> FwiiBoard::units() const {
    return {{"keyboard", UnitKind::Serial, kb_->describe()}};
}

std::vector<Property> FwiiBoard::unitProperties(const std::string& unit) {
    if (unit != "keyboard") return {};
    std::vector<Property> p;
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
std::vector<Property> FwiiBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "Video RAM base address: a 2K block from C000 to F800 (area F)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0xC000;
        x.max   = 0xF800;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 0x7FF) {
                err = "the Flashwriter II decodes a 2K block -- base must be a multiple "
                      "of 0x800";
                return false;
            }
            base_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "port";
        x.help  = "Status port (IN). The data port is the next one up. Even, 00 to 0E (area J)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0x0E;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 1) {
                err = "the Flashwriter II selects a pair of ports -- port must be even";
                return false;
            }
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // The board's two positions of the strap vocabulary. It has no VI connector.
        Property x = irqJumperProperty(
            "interrupt",
            "Key flag to pin 73 (area L): int = fitted, or none. No vector, so the "
            "acknowledge reads as RST 7",
            irq_);
        x.choices = {"none", "int"};
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "fill";
        x.help    = "Video RAM at power-on: zero | random (real RAM is not zeroed)";
        x.kind    = Kind::Enum;
        x.choices = {"zero", "random"};
        x.get     = [this] { return Value::ofStr(fill_ == Fill::Zero ? "zero" : "random"); };
        x.set     = [this](const Value& v, std::string&) {
            fill_ = (v.s() == "zero") ? Fill::Zero : Fill::Random;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "seed";
        x.help = "Seed for fill=random. The same seed fills the RAM the same way at every "
                 "POWER, so a run is repeatable; change it for a different pattern";
        x.kind = Kind::Int;
        x.get  = [this] { return Value::ofInt((long long)seed_); };
        x.set  = [this](const Value& v, std::string&) {
            seed_ = (uint64_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(Display::widthProperty(videoWidth_));
    return p;
}

std::vector<MapEntry> FwiiBoard::memMap() const {
    return {{(uint32_t)base_, (uint32_t)base_ + kBytes - 1, "read/write",
             "Flashwriter II video RAM (24x80)"}};
}

std::vector<MapEntry> FwiiBoard::ioMap() const {
    return {
        {(uint32_t)port_, (uint32_t)port_, "read",
         "Flashwriter II -- status (D6 key waiting, D5 not blanking, D0 no key)"},
        {(uint32_t)(port_ + 1), (uint32_t)(port_ + 1), "read",
         "Flashwriter II -- keyboard data"},
    };
}

} // namespace altair
