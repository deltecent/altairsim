#pragma once
//
// SdlJoystick -- the SDL3-backed Joystick (host/joystick.h). Compiled ONLY when SDL3
// is found (ALTAIRSIM_ENABLE_SDL); the headless build uses NullJoystick instead.
//
// THE ONLY JOYSTICK FILE THAT INCLUDES SDL. A D+7A reads StickState out of the
// Joystick seam and knows nothing of this; the composition root (src/main.cpp) creates
// one of these and injects it. It uses SDL's GAMEPAD API, so any modern USB controller
// maps to a JS-1 with no per-device configuration: the left stick is the joystick, the
// four face buttons are SW1-SW4. A player with no controller can use the keyboard
// (arrow keys + Space/Z/X/C), read here as a synthetic stick.
//
// SELF-CONTAINED AND SELF-INITIALIZING. It brings up its OWN SDL subsystem
// (SDL_INIT_GAMEPAD) lazily on the first poll() and tears down only that subsystem in
// its destructor -- it does NOT call the global SDL_Quit(), so it never disturbs the
// SdlDisplay's video subsystem (and vice versa). That also means a joystick works in a
// machine with NO graphics board: it does not wait for a video window to exist. The
// keyboard-as-a-stick path is the exception -- it needs a focused SDL window to receive
// keys (a Dazzler with `SET DISPLAY focus=on`, or a click on the window).
//
// MAIN-THREAD, POLLED. poll() runs from the D+7A's pump() on the main thread once per
// slice, exactly like the Display is pumped; it never reads the host from inside a bus
// cycle. See host/joystick.h.

#include "host/joystick.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Gamepad;

namespace altair {

class SdlJoystick : public Joystick {
public:
    SdlJoystick() = default;
    ~SdlJoystick() override;

    SdlJoystick(const SdlJoystick&) = delete;
    SdlJoystick& operator=(const SdlJoystick&) = delete;

    void       poll() override;  // lazy: no SDL work until the first call
    int        count() const override { return (int)cache_.size(); }
    StickState stick(int i) const override {
        return (i >= 0 && i < (int)cache_.size()) ? cache_[i] : StickState{};
    }
    StickState  keyboardStick() const override { return kbd_; }
    std::string name(int i) const override {
        return (i >= 0 && i < (int)names_.size()) ? names_[i] : std::string{};
    }

private:
    bool inited_ = false;

    // Open gamepads keyed by their SDL_JoystickID (a Uint32). Reconciled every poll():
    // new controllers are opened, departed ones closed.
    std::unordered_map<uint32_t, SDL_Gamepad*> open_;

    // Cached readings, in SDL enumeration order -- what count()/stick()/name() return.
    std::vector<StickState>  cache_;
    std::vector<std::string> names_;
    StickState               kbd_;  // the keyboard as a synthetic stick
};

} // namespace altair
