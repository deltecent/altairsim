#include "host/display_sdl.h"

#include <SDL3/SDL.h>

#include "platform/foreground.h"

#include <cstdio>

namespace altair {

SdlDisplay::~SdlDisplay() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (inited_) SDL_Quit();
}

// Lazily bring up SDL, the window and the renderer on the first frame -- so
// constructing an SdlDisplay is free, and a machine that never runs a graphics
// board never opens a window. Returns false (and the display goes quiet) if SDL
// cannot start, rather than taking down the simulator: a missing display server is
// the host's problem, not a reason the guest cannot run.
bool SdlDisplay::ensureWindow(int w, int h) {
    if (renderer_) return true;
    if (!inited_) {
        // Do not come to the front just because a board drew a frame. This must be set
        // BEFORE SDL_Init -- it is read while the backend registers the application,
        // and setting it afterwards is too late. It suppresses the activation, and it
        // also suppresses the activation POLICY, which platform/foreground.h puts back
        // below; see that header for why the two have to be separated.
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");

        // Eligible for the foreground, but not asking for it: clicking the window still
        // focuses it, which it must, because this window is a real input device.
        //
        // BEFORE SDL_Init, and that ordering is load-bearing. Measured 2026-07-19:
        // granting the policy after the backend has registered the application brings
        // the process to the front then and there -- the transition INTO the regular
        // policy is itself an activation -- so the window stole focus exactly as it did
        // with no fix at all. Granted first, there is no launched application to
        // activate, and SDL then declines to activate one because of the hint above.
        platform::allowForegroundActivation();

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::fprintf(stderr, "SDL: video init failed: %s\n", SDL_GetError());
            return false;
        }
        inited_ = true;
    }

    // The window half of the same thing: do not activate the window when it is shown.
    // The hint is consulted on the SHOW path, so it only bites if the window is created
    // hidden and shown deliberately below -- which also means it never appears
    // half-configured, before the renderer and the logical presentation are set.
    //
    // Not SDL_WINDOW_NOT_FOCUSABLE: that would make the window permanently unable to
    // take focus, and this window is a real input device. Unfocused is also not the
    // same as behind -- the terminal stays active, but this window may still be ordered
    // in front of it (on macOS the non-activating show is orderFront:), and SDL wraps
    // no "order back". Outside macOS this is a hint a window manager is free to ignore,
    // so the whole arrangement is best-effort and cannot be asserted in a test.
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");

    // Open at an integer multiple so the pixels are visible; the user can resize and
    // the integer-scale logical presentation keeps the aspect and the crisp edges.
    const int scale = 3;
    if (!SDL_CreateWindowAndRenderer(title_.c_str(), w * scale, h * scale,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN, &window_,
                                     &renderer_)) {
        std::fprintf(stderr, "SDL: window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderLogicalPresentation(renderer_, w, h,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    // Ask SDL for layout- and shift-resolved characters (SDL_EVENT_TEXT_INPUT), so a
    // '$' or a capital letter arrives correct without us reimplementing a keymap. The
    // control keys and Ctrl-combinations still come through SDL_EVENT_KEY_DOWN.
    SDL_StartTextInput(window_);

    // Everything above is configured, so show it -- unfocused, per the hint set before
    // the window was created.
    SDL_ShowWindow(window_);
    return true;
}

// Name the window after the machine, not after the board that draws into it
// (host/display.h). Called before there is a window as often as after -- the run loop
// says it every time it starts the guest, and a machine that has never painted a frame
// has no window yet -- so it is recorded and ensureWindow() picks it up. Retitling a live
// window matters too: CONFIG LOAD swaps the machine underneath an open one.
void SdlDisplay::setTitle(const std::string& name) {
    std::string t = name.empty() ? "altairsim" : "altairsim -- " + name;
    if (t == title_) return;
    title_ = std::move(t);
    if (window_) SDL_SetWindowTitle(window_, title_.c_str());
}

// Hand the keyboard back to the terminal when the guest stops (host/display.h). SDL
// wraps no such call, so it goes through the platform seam, which knows that this is a
// macOS question and answers it nowhere else.
//
// Guarded on there being a window at all: the run loop asks on every stop of every
// machine, and most machines never open one.
void SdlDisplay::yieldFocus() {
    if (window_) platform::yieldForeground();
}

Surface* SdlDisplay::acquire(int w, int h, PixelFormat fmt) {
    if (!ensureWindow(w, h)) return nullptr;

    if (!surface_ || surface_->width() != w || surface_->height() != h ||
        surface_->format() != fmt) {
        surface_ = std::make_unique<Surface>(w, h, fmt);
    }

    if (!texture_ || texW_ != w || texH_ != h) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
        if (texture_) SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        texW_ = w;
        texH_ = h;
        rgba_.assign((size_t)w * (size_t)h * 4, 0);
    }
    return surface_.get();
}

void SdlDisplay::setPalette(std::span<const Color> colors) {
    palette_.assign(colors.begin(), colors.end());
}

// Pump SDL's event queue on the main thread (DESIGN.md 7.4 #2). Keystrokes go to the
// injected key sink (host/display.h), which the composition root wires to the Console --
// so a key typed in this window joins the terminal's on the one recorded input queue,
// and no board is touched from here. Draining the queue is also what keeps the window
// from beach-balling, and a close request is remembered.
//
// Called once a slice by the run loop, NOT from present(): see host/display.h for why
// reading the operator must not be gated on whether a frame was drawn.
void SdlDisplay::pollEvents() {
    // Nothing to drain before there is a window, and SDL_PollEvent must not be called
    // before SDL_Init. The run loop asks every slice, including on machines that have
    // no video board at all and will never open one.
    if (!inited_) return;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            quit_ = true;
            break;
        case SDL_EVENT_TEXT_INPUT:
            // Printable characters, already shift/layout-resolved. The guest is a
            // 7-bit machine, so pass ASCII only; Ctrl-combos come via KEY_DOWN.
            for (const char* p = e.text.text; p && *p; ++p) {
                uint8_t c = (uint8_t)*p;
                if (c < 0x80) emitKeys(&c, 1);
            }
            break;
        case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode k = e.key.key;
            uint8_t c = 0;
            if (e.key.mod & SDL_KMOD_CTRL) {
                // Ctrl-A..Ctrl-Z -> C0 control codes (SOLOS reads Ctrl-C and kin this
                // way; SDL does not deliver these as TEXT_INPUT). The keycode carries
                // the unshifted ASCII letter, so map off its value, not a SDLK_ name.
                if (k >= 'a' && k <= 'z') c = (uint8_t)(k - 'a' + 1);
                else if (k == '[') c = 0x1B;   // Ctrl-[  = ESC
                else if (k == '\\') c = 0x1C;
                else if (k == ']') c = 0x1D;
            } else {
                switch (k) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER:  c = 0x0D; break;  // CR -- SOLOS's line terminator
                case SDLK_BACKSPACE: c = 0x08; break;
                case SDLK_TAB:       c = 0x09; break;
                case SDLK_ESCAPE:    c = 0x1B; break;
                case SDLK_DELETE:    c = 0x7F; break;

                // The keys ASCII has no code for. What byte each is worth is the
                // guest's business, so the table is on Display, not here -- this
                // back end only says which key the operator pressed.
                case SDLK_UP:    emitSpecialKey(SpecialKey::Up);    break;
                case SDLK_DOWN:  emitSpecialKey(SpecialKey::Down);  break;
                case SDLK_LEFT:  emitSpecialKey(SpecialKey::Left);  break;
                case SDLK_RIGHT: emitSpecialKey(SpecialKey::Right); break;
                case SDLK_HOME:  emitSpecialKey(SpecialKey::Home);  break;

                default:             break;
                }
            }
            if (c) emitKeys(&c, 1);
            break;
        }
        default:
            break;
        }
    }
}

void SdlDisplay::present(Surface* s) {
    if (!renderer_ || !texture_ || !s) return;

    // Resolve the indexed frame against the palette into RGBA32 (bytes R,G,B,A).
    auto px = s->pixels();
    const size_t n = px.size();
    for (size_t i = 0; i < n; ++i) {
        Color c{};
        uint8_t idx = px[i];
        if (idx < palette_.size()) c = palette_[idx];
        uint8_t* o = &rgba_[i * 4];
        o[0] = c.r;
        o[1] = c.g;
        o[2] = c.b;
        o[3] = c.a;
    }

    SDL_UpdateTexture(texture_, nullptr, rgba_.data(), s->width() * 4);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

} // namespace altair
