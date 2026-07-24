#pragma once
//
// Joystick -- the host game-controller service a board reads stick positions and
// buttons from (the input analogue of host/display.h).
//
// THE BOARD NEVER CALLS SDL. A Cromemco D+7A maps one or two JS-1 joystick consoles
// onto its analog-input and parallel-input ports (reference/JS-1.md); where those
// axes and buttons actually come from -- a USB gamepad, the keyboard, or nothing --
// is the host's business and lives behind this interface. So the board compiles and
// RUNS with no game-controller library at all: against a NullJoystick
// (joystick_null.h) every stick reads centered with no buttons, which is exactly a
// D+7A with no console plugged in, and a headless test drives it with a stub.
//
// This is the joystick analogue of the Display seam. A board's read()/write() are
// pure computation over state; anything that reaches the outside world happens
// through an injected service at a known point in emulated time -- Board::pump(),
// which is where poll() is called, NEVER from inside a bus cycle. poll() refreshes
// the cached state once per slice; stick()/keyboardStick() are cheap cached reads the
// board's pump() copies into its A/D and parallel latches.
//
// AXES ARE SDL-NATIVE (-32768..32767), on purpose. The 8-bit two's-complement byte a
// D+7A A/D returns is the board's to compute (an arithmetic >>8), so the mapping stays
// in the board where it is testable with no controller -- this seam carries the raw
// host reading, not a period byte.

#include <cstdint>
#include <string>

namespace altair {

// One physical (or synthetic) stick, as the host last saw it. Cached by poll().
struct StickState {
    int16_t x = 0;            // left/right, SDL range: -32768 (left) .. +32767 (right)
    int16_t y = 0;            // up/down,   SDL range: -32768 (up)   .. +32767 (down)
    uint8_t buttons = 0;      // bit0..bit3 = up to four buttons (JS-1 SW1..SW4)
    bool    present = false;  // is a device actually behind this stick?
};

class Joystick {
public:
    virtual ~Joystick() = default;

    // Refresh every cached reading from the host. Called once per run-loop slice from
    // a board's pump(), on the main thread -- the same contract as Display::pollEvents.
    // A no-op on a headless host.
    virtual void poll() = 0;

    // How many physical game controllers the host currently sees. Zero headless. Used
    // to validate a `joystick1 = <index>` strap and to resolve `auto`.
    virtual int count() const = 0;

    // The cached state of physical controller `index` (0-based, in host enumeration
    // order). Out of range -> a centered, button-less, absent stick.
    virtual StickState stick(int index) const = 0;

    // The keyboard driven as a synthetic stick (arrow keys -> axes, a few keys ->
    // buttons), for a player with no USB controller. Absent/centered when the host has
    // no focused keyboard (a headless host, or a video window that does not hold the
    // keyboard). Kept SEPARATE from stick() so selecting it is explicit -- see the
    // D+7A's `joystick1 = keyboard`.
    virtual StickState keyboardStick() const { return {}; }

    // A human name for physical controller `index` ("" if none), for diagnostics.
    virtual std::string name(int index) const { (void)index; return {}; }
};

} // namespace altair
