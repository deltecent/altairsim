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

    // Pump SDL's event queue on the main thread (DESIGN.md 7.4 #2). We do not act on
    // input yet -- keystrokes will route through a ByteStream when the keyboard board
    // lands -- but draining the queue is what keeps the window from beach-balling, and
    // a close request is remembered for a future run loop to honor.
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) quit_ = true;
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
