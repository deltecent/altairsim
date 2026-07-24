#include "boards/cromemco-dazzler.h"

#include "core/statefile.h"
#include "host/display.h"

#include <array>

namespace altair {
namespace {

// The injected host video service (setDisplay), borrowed. Null on the bench and in a
// headless build with nothing wired -- pump() then simply does not draw.
Display* g_display = nullptr;

// Emulated-time constants for the status bits, in T-states at the 2 MHz the Altair
// crystal runs (the card's video is on its own 3.579545 MHz subcarrier and is
// asynchronous to the CPU; these are the OBSERVABLE durations a guest can time off IN
// BASE). Same shape as the VDM-1's -- read off the Clock, never a poll counter.
constexpr uint64_t kLineTStates   = 128;    // ~64 us per scan line -> D7 odd/even
constexpr uint64_t kFrameTStates  = 33333;  // ~1/60 s frame
constexpr uint64_t kVblankTStates = 8000;   // ~4 ms end-of-frame window -> D6

// The two intensity levels of the RGBI nibble: HI (full) and LO (~2/3). The Dazzler's
// 16 colors are intensity x primary mix (reference 4.3).
constexpr uint8_t kHi = 0xFF;
constexpr uint8_t kLo = 0xAA;

// Build the 16-entry palette an Indexed8 element resolves against. In color mode the
// 4-bit element is R(bit0) G(bit1) B(bit2) HI/LO(bit3); in B&W it is a 16-level grey
// ramp (reference 4.1/4.3).
std::array<Color, 16> buildPalette(bool color) {
    std::array<Color, 16> pal{};
    for (int i = 0; i < 16; ++i) {
        if (color) {
            uint8_t level = (i & 0x08) ? kHi : kLo;
            pal[i] = {(uint8_t)((i & 0x01) ? level : 0),
                      (uint8_t)((i & 0x02) ? level : 0),
                      (uint8_t)((i & 0x04) ? level : 0), 0xFF};
        } else {
            uint8_t g = (uint8_t)(i * 255 / 15);  // linear 16-step grey
            pal[i] = {g, g, g, 0xFF};
        }
    }
    return pal;
}

} // namespace

void DazzlerBoard::setDisplay(Display* d) { g_display = d; }

DazzlerBoard::DazzlerBoard() = default;

// ---------------------------------------------------------------------------
// Bus: two I/O ports, no memory. (The framebuffer is RAM owned by a memory board.)
// ---------------------------------------------------------------------------
bool DazzlerBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    switch (c.type) {
        case Cycle::IoRead:
        case Cycle::IoWrite:
            return c.port() == port_ || c.port() == (uint8_t)(port_ | 1);
        default:
            return false;
    }
}

uint8_t DazzlerBoard::read(const BusCycle& c) {
    if (c.port() == port_) return statusByte();  // IN BASE -- status
    return 0xFF;                                  // IN BASE+1 -- format is write-only, floats
}

void DazzlerBoard::write(const BusCycle& c) {
    if (c.port() == port_) {
        // OUT BASE -- control: D7 on/off, D6-D0 = A15-A9 of the framebuffer base.
        bool     newOn   = c.data & 0x80;
        uint32_t newBase = (uint32_t)(c.data & 0x7F) << 9;
        if (newOn != on_ || newBase != base_) dirty_ = true;
        on_   = newOn;
        base_ = newBase;
        return;
    }
    // OUT BASE+1 -- format.
    if (c.data != format_) dirty_ = true;
    format_ = c.data;
}

// ---------------------------------------------------------------------------
// Status (IN BASE): D7 = ODD/EVEN scan line, D6 = END OF FRAME (0 during vblank).
// Both read off the Clock so a spin loop pacing to the frame sees them move because
// emulated time advanced.
// ---------------------------------------------------------------------------
uint8_t DazzlerBoard::statusByte() const {
    uint8_t s = 0;
    if (clock_) {
        uint64_t now = clock_->now();
        if ((now / kLineTStates) % 2 == 0) s |= 0x80;                 // D7: 1 on even lines
        if (now % kFrameTStates < kFrameTStates - kVblankTStates)     // D6: 1 outside vblank
            s |= 0x40;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void DazzlerBoard::reset(Reset r) {
    // Front-panel CLEAR/RESET forces the Dazzler off (reference 2.1).
    if (r == Reset::Bus && on_) {
        on_    = false;
        dirty_ = true;
    } else if (r == Reset::Bus) {
        on_ = false;
    }
}

void DazzlerBoard::power() {
    on_     = false;
    base_   = 0;
    format_ = 0;
    dirty_  = true;  // the blank screen still owes the host one frame
    for (auto& b : shadow_) b = 0;
    shadowOn_ = false;
    shadowBase_ = 0;
    shadowFormat_ = 0;
}

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------
void DazzlerBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(on_);
    w.u32(base_);
    w.u8(format_);
}

void DazzlerBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    on_     = r.boolean();
    base_   = r.u32();
    format_ = r.u8();
    dirty_  = true;  // the restored picture owes the host a full redraw
}

// ---------------------------------------------------------------------------
// The host turn: read the framebuffer out of main RAM and paint it. Once per slice,
// on the main thread -- never inside a bus cycle. Same two-gate economy as the VDM-1.
// ---------------------------------------------------------------------------
void DazzlerBoard::pump() {
    if (!g_display) return;
    if (!frameChanged()) return;
    if (!g_display->wantsFrame()) return;
    render();
}

int DazzlerBoard::elementsPerSide() const {
    int quad = x4() ? 64 : 32;      // elements per side within one 512-byte quadrant
    return twoK() ? quad * 2 : quad;
}

// WOULD THE PICTURE LOOK DIFFERENT FROM THE ONE ON SCREEN? Unlike the VDM-1 the
// framebuffer is not on this card, so there is no write to latch a dirty flag on -- we
// poll the live bytes through the side-effect-free Bus::peek and compare to what we
// last painted.
bool DazzlerBoard::frameChanged() {
    if (dirty_) return true;
    if (on_ != shadowOn_) return true;
    if (!on_) return false;  // off, and was off: the picture is black and cannot move
    if (base_ != shadowBase_ || format_ != shadowFormat_) return true;

    uint16_t n = twoK() ? kMaxBytes : kQuadrant;
    for (uint16_t i = 0; i < n; ++i) {
        uint8_t live = bus_ ? bus_->peek((uint16_t)(base_ + i)) : 0xFF;
        if (live != shadow_[i]) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Paint the framebuffer into the injected Display. The scan order is the manual's
// "Memory Map Of Dazzler Picture": up to four 512-byte quadrants tiled 2x2, 16 bytes
// per row within a quadrant (reference 3, 4).
// ---------------------------------------------------------------------------
void DazzlerBoard::render() {
    const int side = elementsPerSide();
    Surface* s = g_display->acquire(side, side, PixelFormat::Indexed8);
    if (!s) return;

    std::array<Color, 16> pal = buildPalette(color());
    g_display->setPalette(pal);

    s->clear(0);
    for (auto& b : shadow_) b = 0;

    const bool     is2k   = twoK();
    const bool     isX4   = x4();
    const int      quads  = is2k ? 4 : 1;
    const int      qSide  = isX4 ? 64 : 32;   // elements per side of one quadrant
    const uint8_t  x4Idx  = (uint8_t)(format_ & 0x0F);  // whole-picture color/grey in X4

    if (on_) {
        for (int q = 0; q < quads; ++q) {
            const int ox = (q & 1) * qSide;       // Q0 TL, Q1 TR, Q2 BL, Q3 BR
            const int oy = (q >> 1) * qSide;
            for (int byteRow = 0; byteRow < 32; ++byteRow) {
                for (int byteCol = 0; byteCol < 16; ++byteCol) {
                    uint16_t off  = (uint16_t)(q * kQuadrant + byteRow * 16 + byteCol);
                    uint8_t  byte = bus_ ? bus_->peek((uint16_t)(base_ + off)) : 0xFF;
                    shadow_[off]  = byte;

                    if (isX4) {
                        // 8 on/off bits: two side-by-side 2x2 cells (reference 4.2).
                        int cx = ox + byteCol * 4, cy = oy + byteRow * 2;
                        if (byte & 0x01) s->put(cx + 0, cy + 0, x4Idx);
                        if (byte & 0x02) s->put(cx + 1, cy + 0, x4Idx);
                        if (byte & 0x04) s->put(cx + 0, cy + 1, x4Idx);
                        if (byte & 0x08) s->put(cx + 1, cy + 1, x4Idx);
                        if (byte & 0x10) s->put(cx + 2, cy + 0, x4Idx);
                        if (byte & 0x20) s->put(cx + 3, cy + 0, x4Idx);
                        if (byte & 0x40) s->put(cx + 2, cy + 1, x4Idx);
                        if (byte & 0x80) s->put(cx + 3, cy + 1, x4Idx);
                    } else {
                        // Two nibble-elements: low nibble left, high nibble right.
                        int ex = ox + byteCol * 2, ey = oy + byteRow;
                        s->put(ex + 0, ey, (uint8_t)(byte & 0x0F));
                        s->put(ex + 1, ey, (uint8_t)((byte >> 4) & 0x0F));
                    }
                }
            }
        }
    }

    shadowOn_     = on_;
    shadowBase_   = base_;
    shadowFormat_ = format_;
    dirty_        = false;

    g_display->present(s);
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> DazzlerBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "I/O base port -- control/status (BASE) and format (BASE+1). Even; default 0E";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFE;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 1) {
                err = "the Dazzler occupies BASE and BASE+1 -- BASE must be even";
                return false;
            }
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> DazzlerBoard::ioMap() const {
    return {
        {(uint32_t)port_, (uint32_t)port_, "read/write",
         "Dazzler -- status (read: D7 odd/even, D6 end-of-frame) / control (write: on + base)"},
        {(uint32_t)(port_ | 1), (uint32_t)(port_ | 1), "write",
         "Dazzler -- format (write: resolution/size/color)"},
    };
}

} // namespace altair
