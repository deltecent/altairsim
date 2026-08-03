#include "boards/cromemco-d7a.h"

#include "core/statefile.h"
#include "host/joystick.h"

namespace altair {
namespace {

// The injected host game-controller service (setJoystick), borrowed. Null on the bench;
// a NullJoystick headless -- pump() then reads every stick as centered with no buttons,
// which is a D+7A with no JS-1 plugged in.
Joystick* g_joystick = nullptr;

// Is `s` a well-formed non-negative decimal index? (The joystick straps accept a number
// or a keyword; this tells the two apart, at set-time and again when resolving.)
bool parseIndex(const std::string& s, int& out) {
    if (s.empty()) return false;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
        if (v > 32767) return false;  // no host has this many controllers; keep it sane
    }
    out = v;
    return true;
}

// A `joystick1`/`joystick2` strap is a keyword or a device index.
bool validJoySpec(const std::string& lowered) {
    if (lowered == "none" || lowered == "auto" || lowered == "keyboard" ||
        lowered == "kbd")
        return true;
    int idx = 0;
    return parseIndex(lowered, idx);
}

} // namespace

void D7aBoard::setJoystick(Joystick* j) { g_joystick = j; }

// ---------------------------------------------------------------------------
// Bus: eight consecutive I/O ports, no memory.
// ---------------------------------------------------------------------------
bool D7aBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= base_ && p < (uint8_t)(base_ + 8);
}

uint8_t D7aBoard::read(const BusCycle& c) {
    int off = c.port() - base_;
    if (off == 0) return parIn_;      // parallel input byte (JS-1 buttons)
    return analogIn_[off - 1];        // analog channel A/D
}

void D7aBoard::write(const BusCycle& c) {
    int off = c.port() - base_;
    if (off == 0) {
        parOut_ = c.data;             // parallel output latch
        return;
    }
    // Analog channel D/A latch. A JS-1 speaker port is written here in a timed loop;
    // the value is latched for a future audio path but nothing plays it yet.
    analogOut_[off - 1] = c.data;
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void D7aBoard::reset(Reset) {
    // Clear the D/A outputs to 0 V and the parallel output latch; the A/D shadows are
    // re-read from the host on the next pump(). Straps and stick assignments stay.
    for (auto& v : analogOut_) v = 0;
    parOut_ = 0;
}

void D7aBoard::power() {
    for (auto& v : analogIn_) v = 0;
    for (auto& v : analogOut_) v = 0;
    parIn_  = 0xFF;   // active-low buttons idle high (released); refreshed each pump()
    parOut_ = 0;
}

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------
void D7aBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    for (uint8_t v : analogIn_) w.u8(v);
    for (uint8_t v : analogOut_) w.u8(v);
    w.u8(parIn_);
    w.u8(parOut_);
}

void D7aBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    for (uint8_t& v : analogIn_) v = r.u8();
    for (uint8_t& v : analogOut_) v = r.u8();
    parIn_  = r.u8();
    parOut_ = r.u8();
}

// ---------------------------------------------------------------------------
// The host turn: read the joysticks and fold them into the A/D and parallel-input
// latches. Once per slice, on the main thread -- never inside a bus cycle.
// ---------------------------------------------------------------------------
void D7aBoard::pump() {
    if (!g_joystick) return;
    g_joystick->poll();
    // Console 1: X/Y -> analog channels 1/2 (0x19/0x1A), buttons -> parallel bits D0-D3;
    // `auto` prefers gamepad 0.
    applyConsole(js1_, 0, 1, 0, 0);
    // Console 2: X/Y -> analog channels 3/4 (0x1B/0x1C), buttons -> parallel bits D4-D7;
    // `auto` prefers gamepad 1, so two `auto` consoles drive two different sticks.
    applyConsole(js2_, 2, 3, 4, 1);
}

uint8_t D7aBoard::axis8(int16_t a) {
    return (uint8_t)(int8_t)((int)a >> 8);  // arithmetic shift (C++20): 0->0x00, +->0x7F, -->0x80
}

StickState D7aBoard::resolveStick(const std::string& spec, int autoIndex) const {
    if (!g_joystick) return {};
    std::string s = lowerAscii(spec);
    if (s == "none") return {};
    if (s == "keyboard" || s == "kbd") return g_joystick->keyboardStick();
    if (s == "auto")
        return g_joystick->count() > autoIndex ? g_joystick->stick(autoIndex)
                                               : g_joystick->keyboardStick();
    int idx = 0;
    if (parseIndex(s, idx)) return g_joystick->stick(idx);
    return {};
}

void D7aBoard::applyConsole(const std::string& spec, int xCh, int yCh,
                            int buttonShift, int autoIndex) {
    StickState s = resolveStick(spec, autoIndex);  // absent -> centered, no buttons
    // Positive (X-right, Y-up) full deflection is clamped to +126 (0x7E), not +127. The
    // real JS-1 pots never quite reach the rail, and the games treat 0x7F as an edge case;
    // capping at 126 keeps a hair of headroom and matches what the hardware delivers.
    int x = (int)(int8_t)axis8(s.x);
    if (x > 126) x = 126;
    analogIn_[xCh] = (uint8_t)(int8_t)x;
    // The period Dazzler games read SDL +Y (stick DOWN) as up, so invert Y: stick up -> a
    // positive byte, stick down -> negative. Shift to the byte FIRST (so a small rest drift
    // near center still floors to 0x00), THEN negate the signed byte -- negating the raw
    // axis first would floor a resting +drift to 0xFF and slide center off zero. Clamp the
    // -(-128) full-up case to +126 so it can't wrap back to 0x80.
    int y = -(int)(int8_t)axis8(s.y);
    if (y > 126) y = 126;
    analogIn_[yCh] = (uint8_t)(int8_t)y;
    // ACTIVE-LOW buttons: a bit reads 1 when the button is RELEASED and 0 when PRESSED
    // (the JS-1's pulled-up switches). So an idle/absent console reads its nibble all-1s,
    // and a press pulls its bit to 0. Sourced from David Hansel's Arduino Altair 8800
    // simulator firmware, which inverts exactly this way to drive the period Dazzler
    // games (reference/JS-1.md 3); StickState.buttons has bit = 1 for pressed.
    uint8_t nib  = (uint8_t)(~s.buttons & 0x0F);
    uint8_t mask = (uint8_t)(0x0F << buttonShift);
    parIn_ = (uint8_t)((parIn_ & ~mask) | (nib << buttonShift));
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> D7aBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base of the 8-port block (A7..A3 jumpers): parallel at BASE, analog at "
                  "BASE+1..7. A multiple of 8; default 18";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xF8;  // BASE+7 must still be a port
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() & 7) {
                err = "the D+7A block is 8 ports selected by A7..A3 -- the base must be a "
                      "multiple of 8";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    auto joyProp = [](const char* name, const char* console, std::string& slot) {
        Property x;
        x.name = name;
        x.help = std::string("Which host controller drives JS-1 console ") + console +
                 ": 'none', 'auto' (the matching gamepad -- console 1->pad 0, console "
                 "2->pad 1 -- or the keyboard), 'keyboard', or a device index like 0";
        x.kind = Kind::Str;
        x.get  = [&slot] { return Value::ofStr(slot); };
        x.set  = [&slot](const Value& v, std::string& err) {
            std::string s = lowerAscii(v.s());
            if (!validJoySpec(s)) {
                err = "expected 'none', 'auto', 'keyboard', or a device index (0, 1, ...)";
                return false;
            }
            slot = s;
            return true;
        };
        return x;
    };
    p.push_back(joyProp("joystick1", "1", js1_));
    p.push_back(joyProp("joystick2", "2", js2_));
    return p;
}

// The live picture for SHOW <id>: what each console's strap actually resolves to right
// now -- a named gamepad, the keyboard, or nothing -- which the property table (the strap
// STRING) cannot show. This is why "joystick1 = auto" is not the same question as "is a
// controller connected"; here we answer the second. Reads the host directly, so it polls
// first (count()/name() are cached by poll()); safe because SHOW runs on the main thread
// at a stopped prompt, the same place the display's idle hook pumps SDL.
std::vector<std::string> D7aBoard::statusLines() const {
    if (g_joystick) g_joystick->poll();

    // "-> ..." for one console's strap, given the gamepad `auto` prefers for it.
    auto resolve = [&](const std::string& spec, int autoIndex) -> std::string {
        if (!g_joystick) return "-> (no joystick service in this build)";
        std::string s = lowerAscii(spec);
        // Keyed on count() alone, exactly as resolveStick() decides what to READ, so the
        // reported source can never disagree with the source actually feeding the A/D.
        auto gamepad = [&](int i) -> std::string {
            if (g_joystick->count() > i) {
                std::string nm = g_joystick->name(i);
                return "-> gamepad " + std::to_string(i) + (nm.empty() ? "" : "  \"" + nm + "\"");
            }
            return "-> gamepad " + std::to_string(i) + "  (not present)";
        };
        if (s == "none") return "-> unwired";
        if (s == "keyboard" || s == "kbd")
            return g_joystick->keyboardStick().present ? "-> keyboard"
                                                       : "-> keyboard  (unavailable)";
        if (s == "auto") {
            if (g_joystick->count() > autoIndex) return gamepad(autoIndex);
            return g_joystick->keyboardStick().present
                       ? "-> keyboard  (no gamepad " + std::to_string(autoIndex) + ")"
                       : "-> nothing  (no gamepad " + std::to_string(autoIndex) +
                             ", no keyboard focus)";
        }
        int idx = 0;
        if (parseIndex(s, idx)) return gamepad(idx);
        return "-> (unrecognized)";
    };

    auto line = [&](const char* console, const std::string& strap, int autoIndex) {
        std::string s = "console " + std::string(console) + "  (" + strap + ")";
        while (s.size() < 24) s += ' ';
        return s + resolve(strap, autoIndex);
    };

    return {line("1", js1_, 0), line("2", js2_, 1)};
}

std::vector<MapEntry> D7aBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "D+7A -- parallel: input byte (JS-1 buttons) / output latch"},
        {(uint32_t)(base_ + 1), (uint32_t)(base_ + 7), "read/write",
         "D+7A -- 7 analog channels: A/D input / D/A output, two's-complement"},
    };
}

} // namespace altair
