#pragma once
//
// NullJoystick -- a Joystick that sees no controllers and no keyboard (the input
// analogue of NullDisplay / NullStream).
//
// This is what a board is given when there is no game-controller backend: in a
// headless build (no SDL, ALTAIRSIM_ENABLE_SDL off), and in EVERY test that does not
// install its own stub. A D+7A wired to one of these reports every stick centered
// (0x00 two's-complement, buttons up), which is precisely a board with no JS-1 console
// plugged in -- so the card runs identically to the shipping binary with nothing to
// read, and a test that DOES care drives it with a StubJoystick instead.

#include "host/joystick.h"

namespace altair {

class NullJoystick : public Joystick {
public:
    void        poll() override {}
    int         count() const override { return 0; }
    StickState  stick(int) const override { return {}; }        // centered, absent
    StickState  keyboardStick() const override { return {}; }
};

} // namespace altair
