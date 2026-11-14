#include "boards/cadzilla.h"

#include "core/statefile.h"
#include "host/display.h"

#include <algorithm>
#include <string>
#include <vector>

namespace altair {
namespace {

// The injected host video service (setDisplay), borrowed. Null on the bench and in a
// headless build with nothing wired -- pump() then simply does not draw.
Display* g_display = nullptr;

// The board's shift register: 8 words per display fetch, 16 pixels of 8 bits each.
constexpr int kWordsPerFetch  = 8;
constexpr int kPixelsPerFetch = 16;

// THE MONITOR MODES: the three primary VESA resolutions, timings reduced to the board's
// units -- memory cycles of 16 pixels horizontally, rasters vertically. Where a VESA porch
// is not a whole number of cycles it is rounded and the line total kept (800x600: 88 px back
// porch -> 6 cycles = 96, 40 px front porch -> 2 = 32; 1024x768: 136 px sync -> 8 cycles,
// 24 px front porch -> 2), which is what a timing PROM on the board would do.
//
//                name         w     h   hsw hbp hfp  vsw vbp vfp  pixel clock (60 Hz)
const CadzillaBoard::Mode kModes[] = {
    {"640x480",   640,  480,   6,  3,  1,   2, 33, 10, 25175000},
    {"800x600",   800,  600,   8,  6,  2,   4, 23,  1, 40000000},
    {"1024x768", 1024,  768,   8, 10,  2,   6, 29,  3, 65000000},
};

} // namespace

const CadzillaBoard::Mode& CadzillaBoard::mode(int i) {
    if (i < 0 || i >= modeCount()) i = 2;   // 1024x768, the default
    return kModes[i];
}
int CadzillaBoard::modeCount() { return (int)(sizeof kModes / sizeof kModes[0]); }

void CadzillaBoard::setDisplay(Display* d) { g_display = d; }

// 2 MB (1 M sixteen-bit words) fixed -- the reference design's SRAM fit, not a strap.
CadzillaBoard::CadzillaBoard() : acrtc_(kVramWords) {
    acrtc_.setCharSpace(this);   // the board decodes CHR: character-space drawing is the overlay
}

CadzillaBoard::~CadzillaBoard() {
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// Drawing time. The ACRTC counts 2CLK; the machine counts T-states. sync() carries the
// one into the other exactly -- the remainder travels, so no 2CLK is lost or invented
// however the accesses fall -- and at the rate in force since the last sync, which is why
// a change of rate (AMODE, `mode`) syncs first. The split division keeps every product
// inside 64 bits without a 128-bit type (MSVC has none).
// ---------------------------------------------------------------------------
long long CadzillaBoard::twoClkHz() const {
    return currentMode().pclk / ((modeReg_ & kModeAmode) ? 4 : 8);
}

void CadzillaBoard::sync() {
    if (!clock_) return;
    const uint64_t t = clock_->now();
    if (t > lastT_) {
        const uint64_t dT  = t - lastT_;
        const uint64_t hz  = (uint64_t)clock_->hz();
        const uint64_t f   = (uint64_t)twoClkHz();
        const uint64_t num = (dT % hz) * f + rem_;
        acc2clk_ += (dT / hz) * f + num / hz;
        rem_      = num % hz;
    }
    lastT_ = t;
    acrtc_.advance(acc2clk_);
}

// The one deadline: the T-state at which the 2CLK count reaches the end of what the ACRTC
// is drawing -- the least dT past the LAST SYNC (acc2clk_ and rem_ are as of lastT_, not
// necessarily now: a restored board has not synced yet) with dT x 2CLK + rem >= owed x
// CPU Hz. Nobody need touch the board for the command to end; an interrupt on CED or WFE
// is a wire, and a wire moves when it moves (DESIGN.md 7.5).
void CadzillaBoard::arm() {
    if (!clock_) return;
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    if (!acrtc_.busy()) return;
    const uint64_t hz = (uint64_t)clock_->hz();
    const uint64_t f  = (uint64_t)twoClkHz();
    uint64_t       dT = 0;
    if (acrtc_.busyUntil() > acc2clk_) {
        const uint64_t owed = (acrtc_.busyUntil() - acc2clk_) * hz - rem_;
        dT = (owed + f - 1) / f;
    }
    uint64_t when = lastT_ + dT;
    if (when <= clock_->now()) when = clock_->now() + 1;   // overdue: never AT now (a drain loop re-firing)
    wake_ = clock_->at(when, [this] {
        wake_ = Clock::kNone;
        sync();
        intChanged();   // CED (or WFE, or a flag the command raised) may have moved IRQ*
        arm();
    });
}

// A different Clock (a machine file is built in a scratch Machine, then moved): its time
// is not the old one's, and a handle from the old queue means nothing in the new.
void CadzillaBoard::clockAttached() {
    wake_  = Clock::kNone;
    lastT_ = clock_ ? clock_->now() : 0;
    arm();
}

// ---------------------------------------------------------------------------
// Bus: one 8-port block from BASE, no memory. BASE+3 (a 16-bit transfer's high byte, which an
// 8-bit host never makes) is not decoded; BASE+1 (MODE) is write-only -- both exactly like the
// Dazzler's format port floats on a read. The ACRTC's
// own RS=0/RS=1 are NOT adjacent: RS=0 is BASE+0, RS=1 is BASE+2, with MODE between them
// (see the header comment for the decode this reflects). Every ACRTC access ends by
// calling intChanged(), since acrtc_.irq() -- SR against CCR's enables -- can move on any
// register write, any FIFO push, or any FIFO pop.
// ---------------------------------------------------------------------------
bool CadzillaBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t off = (uint8_t)(c.port() - port_);
    if (off > 7) return false;
    if (off == 3) return false;                                 // 16-bit high byte: never on 8080/Z80
    if (off == 1) return c.type == Cycle::IoWrite;               // MODE: write-only
    return true;                                                 // 0,2: ACRTC; 4-7: Bt453
}

uint8_t CadzillaBoard::read(const BusCycle& c) {
    uint8_t off = (uint8_t)(c.port() - port_);
    uint8_t v;
    if (off == 0 || off == 2) {
        sync();
        v = acrtc_.read(off == 2);      // RS: BASE+0 = 0 (status), BASE+2 = 1 (data/FIFO)
        arm();                          // a read that makes room can start the next command
        intChanged();
    } else {
        v = dac_.read(off & 3);         // C1C0 = A1A0 (off is 4..7 here)
    }
    return v;
}

void CadzillaBoard::write(const BusCycle& c) {
    uint8_t off = (uint8_t)(c.port() - port_);
    if (off == 0 || off == 2) {
        sync();
        acrtc_.write(off == 2, c.data);
        arm();
        intChanged();
        return;
    }
    if (off == 1) {
        if (c.data != modeReg_) dirty_ = true;      // AMODE and OLEN move the picture; the rest does not
        sync();                                     // AMODE is also 2CLK: the time so far at the old rate
        modeReg_ = c.data;
        arm();
        return;
    }
    dac_.write(off & 3, c.data);
}

// ---------------------------------------------------------------------------
// Lifecycle. RESET* reaches the ACRTC's RES* pin; the Bt453 has no reset pin, so a warm
// reset keeps the palette a driver loaded (the picture blanks because the ACRTC stopped).
// ---------------------------------------------------------------------------
void CadzillaBoard::reset(Reset r) {
    if (r == Reset::Bus) {
        acrtc_.reset();
        // MODE is glue logic on the same RESET* line as the ACRTC, not a chip register with
        // its own reset behavior to cite -- clearing it deterministically (rather than
        // leaving it, which real flip-flops might or might not do) means a driver must
        // reprogram both the ACRTC's timing and the board's glue after a reset, which is
        // the simplest thing to be right about.
        sync();                                     // AMODE (2CLK) goes back to single
        modeReg_ = 0;
        dirty_   = true;
        arm();          // the ACRTC's command in flight went with RES*
        intChanged();   // RESET* clears CCR's enables (acrtc_.reset()) -- IRQ* stands down
    }
}

void CadzillaBoard::power() {
    acrtc_.power();
    dac_.reset();
    modeReg_ = 0;
    // The overlay SRAM, like the frame memory, comes up zero -- the one deterministic choice
    // for a part that powers up holding whatever it holds.
    std::fill(ovl_.begin(), ovl_.end(), uint16_t{0});
    dirty_   = true;   // the monitor shows its (black) frame from power-on, signal or not
    // 2CLK restarts with the Clock (powered first; its queue, and our deadline, are gone).
    lastT_   = clock_ ? clock_->now() : 0;
    acc2clk_ = 0;
    rem_     = 0;
    wake_    = Clock::kNone;
    intChanged();       // a fresh chip asserts nothing
}

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------
void CadzillaBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    acrtc_.serialize(w);
    dac_.serialize(w);
    w.u8(modeReg_);
    // The overlay SRAM, little-endian words, length-prefixed like the frame memory.
    std::vector<uint8_t> bytes;
    bytes.reserve(ovl_.size() * 2);
    for (uint16_t v : ovl_) {
        bytes.push_back((uint8_t)v);
        bytes.push_back((uint8_t)(v >> 8));
    }
    w.blob(bytes);
    w.u64(lastT_);
    w.u64(acc2clk_);
    w.u64(rem_);
}

void CadzillaBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    acrtc_.deserialize(r);
    dac_.deserialize(r);
    modeReg_ = r.u8();
    std::vector<uint8_t> bytes = r.blob();
    if (bytes.size() == ovl_.size() * 2) {
        for (size_t i = 0; i < ovl_.size(); ++i)
            ovl_[i] = (uint16_t)(bytes[2 * i] | (bytes[2 * i + 1] << 8));
    }
    lastT_   = r.u64();
    acc2clk_ = r.u64();
    rem_     = r.u64();
    dirty_   = true;  // the restored picture owes the host a full redraw
    arm();             // a Handle never travels: the command in flight re-arms its end
    intChanged();      // the restored SR/CCR may be mid-interrupt
}

// ---------------------------------------------------------------------------
// The CHR decode. A drawing access to a CHR screen enables no frame-memory bank (U56-U58
// decode DRAW & !CHR); the overlay SRAM answers instead, at MAD0-15 with OLSEL as A16.
// ---------------------------------------------------------------------------
uint16_t CadzillaBoard::charRead(uint16_t addr) { return ovl_[overlayAddr(addr)]; }

void CadzillaBoard::charWrite(uint16_t addr, uint16_t v) {
    uint16_t& w = ovl_[overlayAddr(addr)];
    if (w != v && (modeReg_ & kModeOlen)) dirty_ = true;   // a hidden overlay moves no pixel
    w = v;
}

// ---------------------------------------------------------------------------
// The programmed picture in the monitor's frame.
//
// Horizontally the ACRTC counts memory cycles from HSYNC's rising edge: its display starts
// HDS cycles after it (manual 5.8.3). The monitor's picture starts a fixed number of cycles
// after the same edge -- the mode's back porch -- so frame x of the ACRTC's memory cycle m
// is (HDS + m - hbp) cycles' worth of pixels. In interleaved mode a memory cycle is half a
// display cycle, so every register is in doubled units and a cycle is worth 8 pixels.
// Vertically: VDS rasters after VSYNC's rising edge against the mode's vertical back porch.
// ---------------------------------------------------------------------------
int CadzillaBoard::programmedWidth() const {
    return acrtc_.hdw() * kPixelsPerFetch / glueAccessMode();
}
int CadzillaBoard::programmedHeight() const {
    const uint16_t dcr = acrtc_.dcr();
    int h = acrtc_.reg(0x8A) & 0x0FFF;
    if (dcr & 0x2000) h += acrtc_.reg(0x8C) & 0x0FFF;
    if (dcr & 0x0800) h += acrtc_.reg(0x8E) & 0x0FFF;
    return h;
}
int CadzillaBoard::programmedX() const {
    const int acm = glueAccessMode();
    return (acrtc_.hds() - currentMode().hbp * acm) * (kPixelsPerFetch / acm);
}
int CadzillaBoard::programmedY() const { return acrtc_.vds() - currentMode().vbp; }

std::string CadzillaBoard::wiring() const {
    std::string s;
    if (acrtc_.bitsPerPixel() != 8)
        s += "CCR GBM is " + std::to_string(acrtc_.bitsPerPixel()) + " bpp, the board is wired for 8; ";
    if (acrtc_.gaiWords() != kWordsPerFetch)
        s += "OMR GAI is +" + std::to_string(acrtc_.gaiWords()) + " words, the board fetches 8; ";
    if ((acrtc_.omr() & 0x0C) == 0x0C) {
        s += "OMR ACM is superimposed, the board wires single and interleaved only; ";
    } else {
        // The chip's own OMR ACM bit and the board's MODE.AMODE strap must be programmed in
        // agreement -- neither can see the other's setting, so a driver has to set both.
        const bool acrtcInterleaved = (acrtc_.omr() & 0x08) != 0;
        const bool modeInterleaved  = (modeReg_ & kModeAmode) != 0;
        if (acrtcInterleaved != modeInterleaved)
            s += "OMR ACM is " + std::string(acrtcInterleaved ? "interleaved" : "single") +
                 ", MODE AMODE says " + std::string(modeInterleaved ? "interleaved" : "single") + "; ";
    }
    // A CHR screen is the overlay's drawing path and must never be displayed: the board
    // ignores CHR on a display cycle, so the screen would show frame memory at its address.
    const uint16_t dcr = acrtc_.dcr();
    const bool shown[4] = {(dcr & 0x3000) == 0x3000, (dcr & 0x4000) != 0,
                           (dcr & 0x0C00) == 0x0C00, (dcr & 0x0300) == 0x0300};
    for (int dn = 0; dn < 4; ++dn)
        if (shown[dn] && (acrtc_.reg((uint8_t)(0xC2 + dn * 8)) & 0x8000))
            s += "MWR" + std::to_string(dn) + " CHR is set on a displayed screen, the board has no "
                 "character display path; ";
    if (s.empty()) return "ok";
    s.erase(s.size() - 2);
    return s;
}

// ---------------------------------------------------------------------------
// The host turn: same three gates as the Dazzler. The chips say whether anything moved
// (a drawing command, a register, a palette entry); the host says whether it wants a
// frame yet; only then is the frame built.
// ---------------------------------------------------------------------------
void CadzillaBoard::pump() {
    if (!g_display) return;
    // Both consumed every time, so neither chip's flag is lost behind the other's.
    bool a = acrtc_.takeDirty();
    bool d = dac_.takeDirty();
    dirty_ = dirty_ || a || d;
    if (!dirty_) return;
    if (!g_display->wantsFrame()) return;
    render();
    dirty_ = false;
}

// THE MONITOR IS ALWAYS THERE. A fixed-frequency display with no signal shows a black frame,
// so the window opens on the first pump after power -- like the Dazzler's and the VDM-1's --
// at the mode's size, and the ACRTC's picture appears in it once a program starts the chip.
void CadzillaBoard::render() {
    const Mode& m = currentMode();
    // `this` keys this board's own window (issue #234); id titles it; videoWidth_ sizes it.
    Surface* s = g_display->acquire(this, id, m.width, m.height, PixelFormat::Rgb32, videoWidth_);
    if (!s) return;

    // The shift registers: the RAMDAC's input for every pixel of the frame.
    paintFrame(m.width, m.height);

    // THE RAMDAC: Table 3 for every input the bus can carry, once per frame -- P7..0 under
    // each OL1..0 -- and black for BLANK*, which overrides the look-up table.
    std::vector<Color> lut(1024);
    for (int ol = 0; ol < 4; ++ol)
        for (int p = 0; p < 256; ++p) lut[(size_t)(ol << 8 | p)] = dac_.lookup((uint8_t)p, (uint8_t)ol);
    auto out = s->pixels();
    for (size_t i = 0; i < bus_.size(); ++i) {
        const uint16_t b = bus_[i];
        const Color    c = (b & kBusBlank) ? Color{0, 0, 0, 255} : lut[b & 0x3FF];
        uint8_t*       o = &out[i * 4];
        o[0] = c.r;
        o[1] = c.g;
        o[2] = c.b;
        o[3] = c.a;
    }
    g_display->present(this, s);
}

uint16_t CadzillaBoard::busPixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= busW_ || y >= busH_) return 0;
    return bus_[(size_t)y * (size_t)busW_ + (size_t)x];
}

// The shift registers, run over the ACRTC's addresses for every raster of the frame: the
// frame memory's onto P7..0 and, when OLEN loads it, the overlay's onto OL1. Every pixel no
// display fetch reaches is BLANK -- the porches, and the whole frame when the chip is not
// displaying.
void CadzillaBoard::paintFrame(int w, int h) {
    busW_ = w;
    busH_ = h;
    bus_.assign((size_t)w * (size_t)h, kBusBlank);
    if (!acrtc_.displayOn()) return;

    const Mode& m    = currentMode();
    const int   acm  = glueAccessMode();          // the board's OWN glue, from MODE AMODE
    const int   ppmc = kPixelsPerFetch / acm;     // pixels one memory cycle is worth
    const int   gai  = acrtc_.gaiWords();         // the ACRTC's own step per display cycle
    const int   hbp  = m.hbp * acm;               // the porch, in this mode's memory cycles
    const bool  olen = (modeReg_ & kModeOlen) != 0;

    // One run of display cycles: `cycles` memory cycles starting at frame x `x0`, one fetch
    // at `addr` (advancing by GAI) on every `acm`-th memory cycle. A fetch reads row
    // addr >> 3 of all eight banks -- MA2..0 select no bank on a display cycle -- and the
    // overlay word of the same row.
    auto run = [&](uint16_t* dst, int x0, int cycles, uint32_t addr) {
        for (int mc = 0; mc < cycles; mc += acm) {
            const int      x    = x0 + mc * ppmc;
            const uint32_t row  = addr & ~(uint32_t)(kWordsPerFetch - 1);
            const uint16_t ovl  = olen ? ovl_[(addr >> 3) & kOvlMask] : 0;
            for (int i = 0; i < kWordsPerFetch; ++i) {
                const uint16_t word = acrtc_.peekWord(row + (uint32_t)i);
                for (int b = 0; b < 2; ++b) {                       // low byte first
                    const int k  = 2 * i + b;                       // pixel k of the fetch
                    const int xa = x + k;
                    if (xa < 0 || xa >= w) continue;
                    const uint16_t ol1 = (uint16_t)((ovl >> k) & 1);   // OL0 is tied low
                    dst[xa] = (uint16_t)(((word >> (8 * b)) & 0xFF) | (ol1 << 9));
                }
            }
            addr += (uint32_t)gai;
        }
    };

    const int vbp = m.vbp;
    for (int y = 0; y < h; ++y) {
        uint16_t* dst   = bus_.data() + (size_t)y * (size_t)w;
        const int vsync = y + vbp;                // this raster, counted from VSYNC's rise
        uint32_t  addr  = 0;
        const int r     = vsync - acrtc_.vds();   // which background raster, if any
        if (acrtc_.backgroundRaster(r, addr))
            run(dst, (acrtc_.hds() - hbp) * ppmc, acrtc_.hdw(), addr);
        if (acrtc_.windowRaster(vsync, addr))
            run(dst, (acrtc_.hws() - hbp) * ppmc, acrtc_.hww(), addr);
    }
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> CadzillaBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "I/O base -- one 8-port block: BASE the ACRTC address/status, BASE+1 the "
                  "MODE register, BASE+2 the ACRTC data/FIFO port, BASE+4..+7 the Bt453. A "
                  "multiple of 8; default 70";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xF8;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 7) {
                err = "the board occupies one 8-port block -- BASE must be a multiple of 8";
                return false;
            }
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "mode";
        x.help    = "The monitor: a fixed-frequency VESA raster the ACRTC's picture is placed in "
                    "by its HDS/VDS. 640x480, 800x600 or 1024x768 (default)";
        x.kind    = Kind::Enum;
        for (int i = 0; i < modeCount(); ++i) x.choices.push_back(mode(i).name);
        x.get     = [this] { return Value::ofStr(currentMode().name); };
        x.set     = [this](const Value& v, std::string& err) {
            for (int i = 0; i < modeCount(); ++i) {
                if (v.s() == mode(i).name) {
                    sync();                 // the pixel clock is 2CLK too: settle the old rate first
                    mode_  = i;
                    dirty_ = true;
                    arm();
                    return true;
                }
            }
            err = "mode is 640x480, 800x600 or 1024x768";
            return false;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "draw_rate";
        x.help    = "Drawing speed. full: each ACRTC command completes at once. real: each command "
                    "takes its HD63484 data-sheet time, and the write FIFO fills as on the real "
                    "board";
        x.kind    = Kind::Enum;
        x.choices = {"full", "real"};
        x.get     = [this] { return Value::ofStr(drawReal_ ? "real" : "full"); };
        x.set     = [this](const Value& v, std::string& err) {
            if (v.s() != "full" && v.s() != "real") {
                err = "draw_rate is full or real";
                return false;
            }
            sync();
            drawReal_ = v.s() == "real";
            acrtc_.setTimed(drawReal_);   // to full: what was in flight finishes now
            arm();
            intChanged();
            return true;
        };
        p.push_back(std::move(x));
    }
    p.push_back(Display::widthProperty(videoWidth_));
    // The interrupt strap (SW1-8: On connects IRQ* to the named line, Off -- the default,
    // `none` -- disconnects it). The same ten-choice vocabulary every interrupting board
    // uses; docs/devguide/adding-a-board.md.
    p.push_back(irqJumperProperty(
        "interrupt",
        "SW1-8: the S-100 line for the ACRTC's IRQ*. none (default) disconnects it. int "
        "(pin 73) or vi0..vi7 is asserted while an enabled status flag is set",
        irq_));

    // ---- LIVE STATUS (read-only: no setter, so SHOW says so and CONFIG SAVE skips it) ----
    {
        Property x;
        x.name = "video";
        x.help = "LIVE: whether the ACRTC is displaying -- OMR STR and DCR SE1 both set. Show-only";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(acrtc_.displayOn() ? "on" : "off"); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "picture";
        x.help = "LIVE: the size in pixels of the picture that the ACRTC is set to show, and the "
                 "position of its top-left corner in the monitor's frame. Show-only";
        x.kind = Kind::Str;
        x.get  = [this] {
            return Value::ofStr(std::to_string(programmedWidth()) + "x" +
                                std::to_string(programmedHeight()) + " at (" +
                                std::to_string(programmedX()) + "," +
                                std::to_string(programmedY()) + ")");
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "wiring";
        x.help = "LIVE: 'ok' when the ACRTC settings agree with the board's wiring (CCR GBM 8 "
                 "bpp, OMR GAI +8 words, OMR ACM the same as MODE AMODE, no displayed screen "
                 "with MWR CHR set). If not, it names the setting that is wrong, and the "
                 "picture is scrambled. Show-only";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(wiring()); };
        p.push_back(std::move(x));
    }
    // ---- MODE register (BASE+1), write-only on the wire: these are the board's own shadow
    // of what the guest last wrote there, reported the same way the Dazzler decodes its
    // write-only control/format bytes into readable status. ----
    {
        Property x;
        x.name = "hspol";
        x.help = "LIVE: MODE register bit 0, the horizontal sync polarity. The simulator shows "
                 "the value but does not use it. Show-only";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr((modeReg_ & kModeHspol) ? "negative" : "positive"); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "vspol";
        x.help = "LIVE: MODE register bit 1, the vertical sync polarity. The simulator shows "
                 "the value but does not use it. Show-only";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr((modeReg_ & kModeVspol) ? "negative" : "positive"); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "amode";
        x.help = "LIVE: MODE register bit 2, the access mode of the board's fetch logic: single "
                 "or interleaved. The ACRTC's OMR ACM bit must agree (see wiring). Show-only";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr((modeReg_ & kModeAmode) ? "interleaved" : "single"); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "olen";
        x.help = "LIVE: MODE register bit 3, overlay display enable. Off, the overlay is not "
                 "shown; drawing into it still works. Show-only";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool((modeReg_ & kModeOlen) != 0); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "olsel";
        x.help = "LIVE: MODE register bit 4, overlay select. Which half of the overlay memory "
                 "a CHR screen draws into: 0 the half for the lower 1 MB of frame memory, 1 the "
                 "upper. Show-only";
        x.kind = Kind::Int;
        x.get  = [this] { return Value::ofInt((modeReg_ & kModeOlsel) ? 1 : 0); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "status";
        x.help  = "LIVE: the ACRTC status register -- CER ARD CED LPD RFF RFR WFR WFE. Show-only";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.get   = [this] { return Value::ofInt(acrtc_.status()); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "irq";
        x.help = "LIVE: whether IRQ* is asserted right now -- an enabled status flag pending "
                 "AND the interrupt strap not 'none'. Show-only";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(irq_ != IrqJumper::None && acrtc_.irq()); };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> CadzillaBoard::ioMap() const {
    return {
        {(uint32_t)port_, (uint32_t)port_, "read/write",
         "ACRTC RS=0 -- status (CER ARD CED LPD RFF RFR WFR WFE) / address register"},
        {(uint32_t)(port_ + 1), (uint32_t)(port_ + 1), "write",
         "MODE -- board glue: HSPOL(0) VSPOL(1) AMODE(2) OLEN(3) OLSEL(4)"},
        {(uint32_t)(port_ + 2), (uint32_t)(port_ + 2), "read/write",
         "ACRTC RS=1 -- the register the address names, or the command FIFOs at AR=0"},
        {(uint32_t)(port_ + 4), (uint32_t)(port_ + 7), "read/write",
         "Bt453 RAMDAC -- address, palette RAM (R,G,B), address, overlay"},
    };
}

} // namespace altair
