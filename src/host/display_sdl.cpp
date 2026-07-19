#include "host/display_sdl.h"

#include <SDL3/SDL.h>

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
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::fprintf(stderr, "SDL: video init failed: %s\n", SDL_GetError());
            return false;
        }
        inited_ = true;
    }

    // Open at an integer multiple so the pixels are visible; the user can resize and
    // the integer-scale logical presentation keeps the aspect and the crisp edges.
    const int scale = 3;
    if (!SDL_CreateWindowAndRenderer("altairsim -- VDM-1", w * scale, h * scale,
                                     SDL_WINDOW_RESIZABLE, &window_, &renderer_)) {
        std::fprintf(stderr, "SDL: window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderLogicalPresentation(renderer_, w, h,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    // Ask SDL for layout- and shift-resolved characters (SDL_EVENT_TEXT_INPUT), so a
    // '$' or a capital letter arrives correct without us reimplementing a keymap. The
    // control keys and Ctrl-combinations still come through SDL_EVENT_KEY_DOWN.
    SDL_StartTextInput(window_);
    return true;
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

void SdlDisplay::present(Surface* s) {
    if (!renderer_ || !texture_ || !s) return;

    // Pump SDL's event queue on the main thread (DESIGN.md 7.4 #2). Keystrokes go to
    // the injected key sink (host/display.h), which the composition root wires to the
    // Console -- so a key typed in this window joins the terminal's on the one
    // recorded input queue, and no board is touched from here. Draining the queue is
    // also what keeps the window from beach-balling, and a close request is remembered.
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
