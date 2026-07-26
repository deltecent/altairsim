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
    // Console 1: X/Y -> analog channels 1/2 (0x19/0x1A), buttons -> parallel bits D0-D3.
    applyConsole(js1_, 0, 1, js1InvertY_, 0);
    // Console 2: X/Y -> analog channels 3/4 (0x1B/0x1C), buttons -> parallel bits D4-D7.
    applyConsole(js2_, 2, 3, js2InvertY_, 4);
}

uint8_t D7aBoard::axis8(int16_t a, bool invert) {
    int v = invert ? -(int)a : (int)a;
    if (v > 32767) v = 32767;      // guard -(-32768) after inversion
    if (v < -32768) v = -32768;
    return (uint8_t)(int8_t)(v >> 8);  // arithmetic shift (C++20): 0->0x00, +->0x7F, -->0x80
}

StickState D7aBoard::resolveStick(const std::string& spec) const {
    if (!g_joystick) return {};
    std::string s = lowerAscii(spec);
    if (s == "none") return {};
    if (s == "keyboard" || s == "kbd") return g_joystick->keyboardStick();
    if (s == "auto")
        return g_joystick->count() > 0 ? g_joystick->stick(0) : g_joystick->keyboardStick();
    int idx = 0;
    if (parseIndex(s, idx)) return g_joystick->stick(idx);
    return {};
}

void D7aBoard::applyConsole(const std::string& spec, int xCh, int yCh, bool invertY,
                            int buttonShift) {
    StickState s = resolveStick(spec);  // absent -> centered, no buttons
    analogIn_[xCh] = axis8(s.x, false);
    analogIn_[yCh] = axis8(s.y, invertY);
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
                 ": 'none', 'auto' (gamepad 0 or the keyboard), 'keyboard', or a device "
                 "index like 0";
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
    {
        Property x;
        x.name = "js1_invert_y";
        x.help = "Flip console 1's Y axis (a stick whose pot opposes the host's up=negative)";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(js1InvertY_); };
        x.set  = [this](const Value& v, std::string&) { js1InvertY_ = v.b(); return true; };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "js2_invert_y";
        x.help = "Flip console 2's Y axis";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(js2InvertY_); };
        x.set  = [this](const Value& v, std::string&) { js2InvertY_ = v.b(); return true; };
        p.push_back(std::move(x));
    }
    return p;
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
