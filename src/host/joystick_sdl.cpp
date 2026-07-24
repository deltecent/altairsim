#include "host/joystick_sdl.h"

#include <SDL3/SDL.h>

namespace altair {

SdlJoystick::~SdlJoystick() {
    for (auto& [id, gp] : open_)
        if (gp) SDL_CloseGamepad(gp);
    // Quit ONLY our subsystem, never the whole library -- the SdlDisplay owns video and
    // may still be alive (host/joystick_sdl.h). SDL_Quit() from the display, if it runs
    // after this, cleans up whatever is left.
    if (inited_) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void SdlJoystick::poll() {
    if (!inited_) {
        // Bring up just the gamepad subsystem. This works with no prior SDL_Init and no
        // video window, so a joystick is usable in a machine with no graphics board. If
        // it fails, stay quiet -- no controllers, not a crashed simulator.
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) return;
        inited_ = true;
    }

    SDL_UpdateGamepads();

    // Re-enumerate the connected gamepads: open ones we have not seen, carry over ones we
    // already hold, and close ones that have gone away -- so hot-plug just works and the
    // indices stay in SDL's stable enumeration order.
    int             n   = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    cache_.clear();
    names_.clear();
    std::unordered_map<uint32_t, SDL_Gamepad*> next;
    for (int i = 0; i < n; ++i) {
        SDL_JoystickID id = ids ? ids[i] : 0;
        SDL_Gamepad*   gp = nullptr;
        if (auto it = open_.find(id); it != open_.end()) {
            gp = it->second;
            open_.erase(it);
        } else {
            gp = SDL_OpenGamepad(id);
        }
        next[id] = gp;

        StickState s;
        if (gp) {
            s.x = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX);
            s.y = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
            uint8_t b = 0;
            if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 0x01;  // SW1
            if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_EAST))  b |= 0x02;  // SW2
            if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST))  b |= 0x04;  // SW3
            if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_NORTH)) b |= 0x08;  // SW4
            s.buttons = b;
            s.present = true;
            const char* nm = SDL_GetGamepadName(gp);
            names_.emplace_back(nm ? nm : "");
        } else {
            names_.emplace_back();
        }
        cache_.push_back(s);
    }
    // Close any gamepad that vanished since last poll.
    for (auto& [id, gp] : open_)
        if (gp) SDL_CloseGamepad(gp);
    open_.swap(next);
    if (ids) SDL_free(ids);

    // The keyboard as a synthetic stick. SDL up = negative Y, matching the gamepad axes.
    // With no focused window every key reads up, so the stick is simply centered.
    kbd_ = {};
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        int x = 0, y = 0;
        if (keys[SDL_SCANCODE_LEFT])  x -= 32768;
        if (keys[SDL_SCANCODE_RIGHT]) x += 32767;
        if (keys[SDL_SCANCODE_UP])    y -= 32768;
        if (keys[SDL_SCANCODE_DOWN])  y += 32767;
        kbd_.x = (int16_t)(x < -32768 ? -32768 : (x > 32767 ? 32767 : x));
        kbd_.y = (int16_t)(y < -32768 ? -32768 : (y > 32767 ? 32767 : y));
        uint8_t b = 0;
        if (keys[SDL_SCANCODE_SPACE]) b |= 0x01;  // SW1 (fire)
        if (keys[SDL_SCANCODE_Z])     b |= 0x02;  // SW2
        if (keys[SDL_SCANCODE_X])     b |= 0x04;  // SW3
        if (keys[SDL_SCANCODE_C])     b |= 0x08;  // SW4
        kbd_.buttons = b;
        kbd_.present = true;
    }
}

} // namespace altair
