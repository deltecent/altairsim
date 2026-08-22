#include "host/display_sdl.h"

#include <SDL3/SDL.h>

#include "platform/foreground.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>   // std::lround -- transitively present on libc++, not on libstdc++

namespace altair {

namespace {

// The fallback window multiple, used ONLY when the display's usable bounds cannot be read
// (SDL_GetDisplayUsableBounds failed) -- so neither the auto target nor the fit-loop has a
// screen size to work from. With bounds, a board's `width` decides: auto opens ~half the
// screen wide, a pixel width is honored and clamped to fit. See ensureWindow().
constexpr int kMaxScale = 3;

// What fraction of the usable screen WIDTH an auto-sized window aims for, as a percent. The
// frame is scaled up by the largest WHOLE multiple whose width stays under this -- so a
// 64x64 Dazzler frame opens about as wide as a 512-wide VDM-1 one, and neither swamps the
// desktop. Height then follows the frame's own aspect at that multiple.
constexpr int kDefaultWidthPercent = 50;

// A rough allowance for the title bar, in points, so a window sized against the
// display's usable height does not open with its title bar off the top of the screen.
constexpr int kChromeH = 40;

// A small inset around the picture so macOS's rounded window corners do not clip the
// bottom-left glyphs. This is the bezel we AIM FOR, in device pixels, at the opening size;
// ensureWindow() turns it into a margin in LOGICAL pixels (border_) that it folds into the
// logical presentation on every side, so the letterbox band is even on all four sides and
// STAYS even as the aspect-locked window is dragged (a fixed device-pixel bezel could not,
// since the window's aspect changes with size). Applied on every platform -- a thin bezel
// looks intentional, not clipped.
constexpr int kBorder = 8;  // device pixels per side, at the opening size

// The tube face a CRT-look window is stretched to fill: a classic 4:3 monitor. The period
// video boards scan a short, wide raster (VDM-1 512x208, a VDB 640x240) onto a 4:3 tube, so
// the pixels were never square; SET DISPLAY crt=on reproduces that by presenting a taller
// logical frame (crtDisplayHeight below). Easy to tune here without touching the mechanism.
constexpr double kCrtAspect = 4.0 / 3.0;

// The DISPLAY height a w x h frame is painted at: unchanged (h) in the crisp default, or the
// 4:3 tube height when crt is on -- always a stretch, never a squash, so a frame already taller
// than 4:3 is left alone. Pushing the extra height into the LOGICAL frame is what lets the one
// integer-scale + LETTERBOX path present non-square pixels; crt=on then switches the picture to
// LINEAR filtering so the stretched rows blend into a smooth tube face rather than a blocky one.
int crtDisplayHeight(int w, int h) {
    if (!Display::crt()) return h;
    return std::max(h, (int)std::lround((double)w / kCrtAspect));
}

}  // namespace

SdlDisplay::~SdlDisplay() {
    for (auto& [owner, win] : windows_) destroyWindow(win);
    if (inited_) SDL_Quit();
}

// Destroy one window's SDL resources but leave the struct reusable -- ensureWindow() rebuilds
// whenever a Window's renderer is null, so the next frame the board draws opens a fresh window.
// SDL itself is not torn down (inited_ stays true), so a rebuild needs no re-init.
void SdlDisplay::destroyWindow(Window& win) {
    if (win.texture)  { SDL_DestroyTexture(win.texture);   win.texture  = nullptr; }
    if (win.renderer) { SDL_DestroyRenderer(win.renderer); win.renderer = nullptr; }
    if (win.window)   { SDL_DestroyWindow(win.window);     win.window   = nullptr; }
    win.texW = win.texH = 0;
    win.pendingClose = false;
}

// Tear down only the windows the operator clicked shut at a stopped prompt (host/display.h),
// and drop them from the map so their Owner starts fresh if that board draws again. A machine
// with two video boards keeps the other picture up. Cheap and reversible: the next frame a
// board draws re-opens its window with no SDL re-init.
void SdlDisplay::closeWindow() {
    for (auto it = windows_.begin(); it != windows_.end();) {
        if (it->second.pendingClose) {
            destroyWindow(it->second);
            it = windows_.erase(it);
        } else {
            ++it;
        }
    }
}

// Close every window and forget every Owner -- CONFIG LOAD replaces the backplane wholesale
// (host/display.h), so every board that owned a window is about to die and its handle may be
// reused. The new machine's video boards re-open their windows on their first frame.
void SdlDisplay::closeAllWindows() {
    for (auto& [owner, win] : windows_) destroyWindow(win);
    windows_.clear();
    quitRequested_ = false;
}

// The window an event names. Small N (one per video board), so a linear scan is nothing.
SdlDisplay::Window* SdlDisplay::windowById(uint32_t id) {
    if (!id) return nullptr;
    for (auto& [owner, win] : windows_)
        if (win.window && SDL_GetWindowID(win.window) == id) return &win;
    return nullptr;
}

// Lazily bring up SDL, the window and the renderer on the first frame -- so
// constructing an SdlDisplay is free, and a machine that never runs a graphics
// board never opens a window. Returns false (and the display goes quiet) if SDL
// cannot start, rather than taking down the simulator: a missing display server is
// the host's problem, not a reason the guest cannot run.
bool SdlDisplay::ensureWindow(Window& win, int w, int h, int targetWidthPx) {
    if (win.renderer) return true;

    // Whether the operator is expected to type here or in the terminal (host/display.h).
    // Read once, at the moment the window is built, because that is when every hint
    // below has to be decided -- and taken again in yieldFocus(), which is the other
    // moment it means anything.
    const bool wantsFocus = Display::focusPolicy();

    if (!inited_) {
        // Do not come to the front just because a board drew a frame. This must be set
        // BEFORE SDL_Init -- it is read while the backend registers the application,
        // and setting it afterwards is too late. It suppresses the activation, and it
        // also suppresses the activation POLICY, which platform/foreground.h puts back
        // below; see that header for why the two have to be separated.
        //
        // Skipped entirely when the window is meant to be the console: then coming to
        // the front IS the wanted behavior, and SDL's own default -- register, set the
        // policy, activate -- is exactly right. This is the one place the two policies
        // diverge before SDL exists, which is why it is read this early.
        if (!wantsFocus) {
            SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");

            // Eligible for the foreground, but not asking for it: clicking the window
            // still focuses it, which it must, because this window is a real input
            // device.
            //
            // BEFORE SDL_Init, and that ordering is load-bearing. Measured 2026-07-19:
            // granting the policy after the backend has registered the application
            // brings the process to the front then and there -- the transition INTO the
            // regular policy is itself an activation -- so the window stole focus
            // exactly as it did with no fix at all. Granted first, there is no launched
            // application to activate, and SDL then declines to activate one because of
            // the hint above.
            platform::allowForegroundActivation();
        }

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
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, wantsFocus ? "1" : "0");

    // OPEN AT AN EXACT INTEGER MULTIPLE OF THE FRAME (plus a fixed kBorder inset on every
    // side, see below) -- and pick the multiple from a target WIDTH, rather than assuming
    // one and hoping. The height is not chosen separately; it is h*scale, so it comes off
    // the width through the frame's own aspect.
    //
    // 3x was assumed, and it does not fit a laptop. A 512-wide VDM-1 frame asks for a
    // 1536-point window on a panel 1470 points wide, so macOS clamps it -- and a
    // clamped window is no longer a multiple of anything. The integer-scale
    // presentation then drops to the largest whole multiple that fits BOTH axes, which
    // is 2x, and letterboxes everything left over. Measured on an M4 Air, 2026-07-19:
    // 1024x416 of picture inside a 1470x624 window, a 223-pixel border down each side
    // and 104 top and bottom. The WIDTH is what fails, but the border appears on all
    // four sides, because one scale has to serve both axes.
    //
    // We ADD a small bezel on every side deliberately (macOS rounds the window's corners and
    // would otherwise clip the bottom-left glyphs) -- but it is folded into the LOGICAL size,
    // not the device size. Below we pick a margin `m` in logical pixels worth about kBorder
    // device pixels at the opening scale, present a logical frame of (w+2m)x(h+2m), and draw
    // the picture into the centered [m,m,w,h] sub-rect (present()). Because the window is
    // locked to the (w+2m):(h+2m) aspect and LETTERBOX preserves exactly that ratio, the
    // picture fills the window with an even m-scaled band on ALL FOUR sides, and the band
    // stays even at every drag size. A fixed device-pixel bezel could not: the window's aspect
    // would change with size, so a single aspect lock would eat the border on the long axis.
    //
    // Ask the display how much room there is, and never ask for more than that. Usable
    // bounds already exclude the menu bar and the Dock, but not this window's own title
    // bar, and a window whose title bar is off-screen cannot be moved -- kChromeH is a
    // rough allowance for it, rough being enough because the answer is a whole number and
    // the next multiple is a long way away.
    SDL_Rect   usable{};
    const bool haveBounds = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    const int  requested  = targetWidthPx;  // 0 = auto, else this board's target pixels

    // The height the picture is PAINTED at -- taller than the raster when crt is on (the 4:3
    // tube stretch), otherwise h. The scale is still chosen from the WIDTH, but the height that
    // has to fit the screen, and the logical frame below, use this.
    const int hDisp = crtDisplayHeight(w, h);

    int scale;
    if (haveBounds) {
        // Grow to a target WIDTH: the pixels the drawing board asked for, or half the usable
        // screen width by default. The largest whole multiple whose width (inset included)
        // fits the target wins; small frames grow a lot, wide frames a little, and both land
        // near the same size -- which is why a fixed ceiling could not do this job.
        const int targetW = requested > 0 ? requested : usable.w * kDefaultWidthPercent / 100;
        scale = 1;
        while (w * (scale + 1) + 2 * kBorder <= targetW) ++scale;
        // Then bring it down if that width, or the (possibly stretched) height it implies, runs
        // off the screen.
        while (scale > 1 && (w * scale + 2 * kBorder > usable.w ||
                             hDisp * scale + 2 * kBorder + kChromeH > usable.h))
            --scale;
    } else if (requested > 0) {
        // No screen to measure against, but a width was asked for: honor it as best we can.
        scale = 1;
        while (w * (scale + 1) + 2 * kBorder <= requested) ++scale;
    } else {
        scale = kMaxScale;  // no bounds, no request: the plain fallback multiple
    }

    // The bezel as a whole number of LOGICAL pixels, worth about kBorder DEVICE pixels at this
    // opening scale (border * scale). Equal on both axes, so the displayed band is equal on
    // all four sides; folded into the logical size below and drawn as an inset in present().
    // At least 1 so there is always a hairline, even at large scales.
    win.border = std::max(1, (int)std::lround((double)kBorder / scale));
    const int logW = w + 2 * win.border, logH = hDisp + 2 * win.border;

    // A requested width (width= on the terminal, a board's width property) opens the window at
    // EXACTLY that many device pixels -- a non-integer multiple of the frame is fine BECAUSE the
    // crt look filters the picture linear (drawLastFrame), so the stretch stays smooth. Gated on
    // crt(): the crisp default keeps the whole-number multiple that makes nearest-neighbor pixels
    // square (the point of the integer scaling below), and only the soft tube look opens exact.
    // The height comes off the requested width at the frame aspect, and both stay on the screen.
    const bool exactSize = requested > 0 && Display::crt();
    int        createW = logW * scale, createH = logH * scale;
    if (exactSize) {
        const int maxW = haveBounds ? usable.w : requested;
        const int maxH = haveBounds ? std::max(1, usable.h - kChromeH) : (logH * scale);
        createW = std::min(requested, maxW);
        createH = (int)std::lround((double)createW * logH / logW);
        if (createH > maxH) {
            createH = maxH;
            createW = (int)std::lround((double)createH * logW / logH);
        }
    }

    // Named after the machine AND this board, and stamped with run/stop state, from the shared
    // parts the run loop set long before this window existed plus win.label acquire() just set.
    win.title = composedTitle(win);
    if (!SDL_CreateWindowAndRenderer(win.title.c_str(), createW, createH,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN, &win.window,
                                     &win.renderer)) {
        std::fprintf(stderr, "SDL: window/renderer failed: %s\n", SDL_GetError());
        return false;
    }

    // The border band is whatever SDL_RenderClear paints, so make it black -- a dark bezel
    // around the picture. present() clears every frame before drawing the texture.
    SDL_SetRenderDrawColor(win.renderer, 0, 0, 0, 255);

    // AND TAKE THE WINDOW WE ACTUALLY GOT, not the one we asked for. Any window manager
    // may clamp, tile or otherwise ignore a requested size, and a size that is not a
    // multiple of the frame is precisely the letterbox described above. So re-derive the
    // multiple from the real size and set it back.
    //
    // Self-correcting rather than macOS-specific: if nothing was clamped, this is
    // already the size, and the call does nothing. It is also why the fix is not simply
    // "ask for less" -- the display query above narrows the guess, this makes it true.
    int gotW = 0, gotH = 0;
    SDL_GetWindowSize(win.window, &gotW, &gotH);
    int wantW, wantH;
    if (exactSize) {
        // A literal width was asked for: take the window as it came, no integer re-snap. The
        // logical presentation scales the frame to fill it (letterboxed to the frame aspect).
        wantW = gotW;
        wantH = gotH;
    } else {
        // No request: re-derive the whole multiple from the real size (the WM may have clamped)
        // and set it back, so the crisp default stays an exact integer scale of the frame.
        const int fit = std::max(1, std::min(gotW / logW, gotH / logH));
        wantW = logW * fit;
        wantH = logH * fit;
        if (gotW != wantW || gotH != wantH) SDL_SetWindowSize(win.window, wantW, wantH);
    }

    // TILE, DON'T STACK. SDL opens every window at the same default spot, so a machine's second
    // video board would land squarely on top of the first (issue #234). Place each new window
    // just right of the last, wrapping to a fresh row when it would run off the usable desktop --
    // so two boards are two pictures side by side, not one hiding the other. Only with real
    // display bounds; without them the window manager's own placement stands.
    if (haveBounds) {
        const int margin = 40, gap = 20;
        const int left = usable.x + margin, right = usable.x + usable.w;
        if (!tilePlaced_) { tileX_ = left; tileY_ = usable.y + margin; tilePlaced_ = true; }
        // Wrap to the next row if this window would overflow -- unless it is already at the row
        // start (a window wider than the desktop just opens at the margin and overhangs).
        if (tileX_ > left && tileX_ + wantW > right) {
            tileX_ = left;
            tileY_ += tileRowH_ + gap;
            tileRowH_ = 0;
        }
        SDL_SetWindowPosition(win.window, tileX_, tileY_);
        tileX_ += wantW + gap;
        tileRowH_ = std::max(tileRowH_, wantH);
    }

    // Ask SDL for layout- and shift-resolved characters (SDL_EVENT_TEXT_INPUT), so a
    // '$' or a capital letter arrives correct without us reimplementing a keymap. The
    // control keys and Ctrl-combinations still come through SDL_EVENT_KEY_DOWN.
    SDL_StartTextInput(win.window);

    // Everything above is configured, so show it -- unfocused, per the hint set before
    // the window was created.
    SDL_ShowWindow(win.window);

    // Fit the logical presentation, the bezel and the aspect lock to this first frame. The
    // SAME call re-fits them when a board later changes resolution (applyPresentation, called
    // from acquire()) -- so the picture-plus-bezel keeps filling the window, even on all four
    // sides, at the opening size and after every mode switch. Done here, with the window shown
    // and sized, so the render output size it reads is the real one.
    applyPresentation(win, w, h);

    if (std::getenv("ALTAIRSIM_VIDEO_DEBUG")) {
        int ww = 0, wh = 0, pw = 0, ph = 0, ow = 0, oh = 0;
        SDL_GetWindowSize(win.window, &ww, &wh);
        SDL_GetWindowSizeInPixels(win.window, &pw, &ph);
        SDL_GetCurrentRenderOutputSize(win.renderer, &ow, &oh);
        SDL_FRect r{};
        SDL_GetRenderLogicalPresentationRect(win.renderer, &r);
        std::fprintf(stderr,
                     "[video] logical %dx%d  window %dx%d  pixels %dx%d  output %dx%d\n"
                     "[video] presentation rect x=%.1f y=%.1f w=%.1f h=%.1f\n",
                     logW, logH, ww, wh, pw, ph, ow, oh, r.x, r.y, r.w, r.h);
    }
    return true;
}

// FIT THE LOGICAL PRESENTATION TO A FRAME OF w x h, in the window as it stands now. Called
// once from ensureWindow() and again from acquire() every time a board changes resolution --
// which is the whole fix for the Dazzler. Without re-running it, the presentation stays frozen
// at the first frame's size (ensureWindow short-circuits on later frames), and present() then
// draws a larger picture, anchored at the top-left inset, off the bottom-right of the stale
// logical frame: SDL clips it, so only the top-left region shows and the bezel survives only on
// the top and left. Re-fitting here makes the picture fill the window whole with an even bezel.
//
// The WINDOW is not resized: the operator's size stands, and the new resolution is just
// rescaled into it (LETTERBOX, nearest-neighbor). A Dazzler flips modes rapidly, so a window
// that jumped size each time would be worse than a fractional-but-crisp rescale.
void SdlDisplay::applyPresentation(Window& win, int w, int h) {
    if (!win.renderer || !win.window || w <= 0 || h <= 0) return;

    // The bezel is worth about kBorder DEVICE pixels at the picture's current on-screen scale,
    // so it stays a thin hairline whatever resolution the board is in. Derive that scale from
    // how many whole picture-pixels fit the window's render output (falls back to 1 if the
    // output size is not readable yet, giving the kBorder default).
    // The painted height -- taller than the raster when crt is on (the 4:3 tube stretch). The
    // logical frame and the aspect lock are built from it, so LETTERBOX presents non-square
    // pixels; win.picW/picH still track the NATIVE w,h for change detection in acquire().
    const int hDisp = crtDisplayHeight(w, h);

    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(win.renderer, &ow, &oh);
    const int scale = std::max(1, std::min(ow / w, oh / hDisp));
    win.border = std::max(1, (int)std::lround((double)kBorder / scale));

    const int logW = w + 2 * win.border, logH = hDisp + 2 * win.border;

    // The picture PLUS its bezel is the logical frame; LETTERBOX scales that whole frame to the
    // window uniformly, keeping the bezel present() insets even on all four sides.
    SDL_SetRenderLogicalPresentation(win.renderer, logW, logH,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // Lock the window to the padded frame's aspect so a resize stays proportional -- but only
    // when the ratio actually moved. For the Dazzler's square modes it is always 1:1, and
    // re-asserting an unchanged ratio would nudge a window the operator has sized.
    const float aspect = (float)logW / (float)logH;
    if (aspect != win.aspect) {
        SDL_SetWindowAspectRatio(win.window, aspect, aspect);
        win.aspect = aspect;
    }

    win.picW = w;
    win.picH = h;

    // Remember which look this fit was built for, so present() can notice a live crt=on/off
    // flip (the property is a session-wide static with no handle on a window) and re-fit.
    win.crtFit = Display::crt();
}

// Name the window after the machine, not after the board that draws into it
// (host/display.h). Called before there is a window as often as after -- the run loop
// says it every time it starts the guest, and a machine that has never painted a frame
// has no window yet -- so it is recorded and ensureWindow() picks it up. Retitling a live
// window matters too: CONFIG LOAD swaps the machine underneath an open one.
void SdlDisplay::setTitle(const std::string& name) {
    machineName_ = name;
    for (auto& [owner, win] : windows_) applyTitle(win);
}

// Running or stopped, in the title bar (host/display.h). Recorded and composed with the
// machine name into every window's title; takes effect on live windows at once and is picked
// up by ensureWindow() on one not yet open. One machine, one run state -- so all windows agree.
void SdlDisplay::setRunning(bool running) {
    running_ = running;
    for (auto& [owner, win] : windows_) applyTitle(win);
}

// "AltairSim -- <machine> -- <board>" and, while the guest is stopped, the reminder that the
// picture on screen is frozen -- a window stays open across a stop (closeWindow), so without
// this a halted machine looks like a hung one. The board name is what tells one video board's
// window from another's; the machine name frames both.
std::string SdlDisplay::composedTitle(const Window& win) const {
    std::string t = machineName_.empty() ? "AltairSim" : "AltairSim -- " + machineName_;
    if (!win.label.empty()) t += " -- " + win.label;
    if (!running_) t += " -- simulator stopped";
    return t;
}

// Push the composed title onto one window, skipping SDL if it is unchanged.
void SdlDisplay::applyTitle(Window& win) {
    std::string t = composedTitle(win);
    if (t == win.title) return;
    win.title = std::move(t);
    if (win.window) SDL_SetWindowTitle(win.window, win.title.c_str());
}

// Hand the keyboard back to the terminal when the guest stops (host/display.h). SDL
// wraps no such call, so it goes through the platform seam, which knows that this is a
// macOS question and answers it nowhere else.
//
// Guarded on there being a window at all: the run loop asks on every stop of every
// machine, and most machines never open one.
//
// And declined outright when the window is the console (host/display.h). Handing the
// keyboard back at every stop is the right answer for a machine you drive from the
// altairsim> prompt and the wrong one for a Sol-20, where it would take the keyboard
// out of the window at each breakpoint and each close of the guest -- undoing, once a
// stop, exactly what the setting asked for.
void SdlDisplay::yieldFocus() {
    if (Display::focusPolicy()) return;
    for (auto& [owner, win] : windows_)
        if (win.window) { platform::yieldForeground(); return; }
}

Surface* SdlDisplay::acquire(Owner owner, const std::string& label, int w, int h, PixelFormat fmt,
                             int targetWidthPx) {
    Window& win = windows_[owner];  // find-or-create this board's window slot
    // The board's name, before the window is built so its first title carries it; retitle a
    // live window if the board ever renames itself (it does not today, so this is usually a no-op).
    if (win.label != label) {
        win.label = label;
        applyTitle(win);
    }
    if (!ensureWindow(win, w, h, targetWidthPx)) return nullptr;

    if (!win.surface || win.surface->width() != w || win.surface->height() != h ||
        win.surface->format() != fmt) {
        win.surface = std::make_unique<Surface>(w, h, fmt);
    }

    if (!win.texture || win.texW != w || win.texH != h) {
        if (win.texture) SDL_DestroyTexture(win.texture);
        win.texture = SDL_CreateTexture(win.renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, w, h);
        if (win.texture) SDL_SetTextureScaleMode(win.texture, SDL_SCALEMODE_NEAREST);
        win.texW = w;
        win.texH = h;
        win.rgba.assign((size_t)w * (size_t)h * 4, 0);
    }

    // A board that changed its frame resolution (a Dazzler switching video mode) needs the
    // logical presentation re-fit to the new size, or present() clips the larger picture to
    // the old, smaller logical frame. ensureWindow() did this for the first frame and then
    // short-circuits, so it falls to here. No-op in the steady state (win.picW/picH unchanged).
    if (w != win.picW || h != win.picH) applyPresentation(win, w, h);

    return win.surface.get();
}

void SdlDisplay::setPalette(Owner owner, std::span<const Color> colors) {
    windows_[owner].palette.assign(colors.begin(), colors.end());
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

    // Windows whose picture must be put back after the drain (one entry each, deduped). A raw
    // Window* is stable -- the map node does not move -- and only closeWindow()/closeAllWindows()
    // erase, neither of which runs mid-drain.
    std::vector<Window*> repaint;
    auto markRepaint = [&](Window* w) {
        if (w && std::find(repaint.begin(), repaint.end(), w) == repaint.end())
            repaint.push_back(w);
    };

    // The operator asked to close ONE window (its close box), routed by the running state at the
    // moment it happened: while the guest RUNS this stops the guest and keeps every window (RUN
    // resumes into them); while STOPPED it tears just that window down at the next idle tick.
    auto requestClose = [&](Window* w) {
        quitRequested_ = true;             // both loops act on takeQuitRequest()
        if (!running_ && w) w->pendingClose = true;  // stopped: closeWindow() takes the flagged one
    };

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            // App-level quit (Cmd-Q, or the platform's last-window signal): stop the guest, and
            // if already stopped, mark every window to go. No windowID -- it is the whole app.
            quitRequested_ = true;
            if (!running_)
                for (auto& [owner, win] : windows_) win.pendingClose = true;
            break;

        // One window's close box. SDL sends SDL_EVENT_QUIT only when the LAST window closes, so
        // with several windows this per-window event is the only notice that window X was shut.
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            requestClose(windowById(e.window.windowID));
            break;

        // The window needs its picture put back: a resize (the logical presentation rescales,
        // but nothing repaints the last frame), a drag onto another display, or the compositor
        // asking for a fresh paint. present() only runs when the GUEST's framebuffer changes, so
        // without this a resize of a still or paused screen would leave stale or torn content
        // until the guest next drew. Repainted once after the drain from the texture we still
        // hold. EXPOSED covers uncover/restore; RESIZED and PIXEL_SIZE_CHANGED cover both the
        // logical and the device-pixel size moving (a HiDPI move changes the latter alone).
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            markRepaint(windowById(e.window.windowID));
            break;
        case SDL_EVENT_TEXT_INPUT:
            // Printable characters, already shift/layout-resolved. The guest is a
            // 7-bit machine, so pass ASCII only; Ctrl-combos come via KEY_DOWN. Keyboard is
            // display-wide (one operator, one Console), so which window has focus does not route.
            if (!Display::keyboardToConsole()) break;  // display-only: keys are the joystick's
            for (const char* p = e.text.text; p && *p; ++p) {
                uint8_t c = (uint8_t)*p;
                if (c < 0x80) emitKeys(&c, 1);
            }
            break;
        case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode k = e.key.key;
            // DISPLAY-ONLY WINDOW (a Dazzler): its keystrokes drive the joystick, read
            // through SDL_GetKeyboardState (host/joystick_sdl.cpp), NOT the console -- so
            // they must not land at the CP/M or altairsim> prompt. Honor only ATTN
            // (Ctrl-E), mapped to the same guest-stop the close box raises, so the window
            // still hands the operator back the monitor. See Display::keyboardToConsole().
            if (!Display::keyboardToConsole()) {
                if ((e.key.mod & SDL_KMOD_CTRL) && k == 'e')
                    requestClose(windowById(e.key.windowID));
                break;
            }
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

                // The three no-ASCII Sol-20 keys a modern keyboard cannot supply and the
                // arrows do not cover -- function keys stand in for them (issue #59). Same
                // seam as the arrows: this only names the key, the byte is on Display.
                case SDLK_F1:    emitSpecialKey(SpecialKey::Mode);  break;  // MODE SELECT
                case SDLK_F2:    emitSpecialKey(SpecialKey::Clear); break;  // CLEAR
                case SDLK_F3:    emitSpecialKey(SpecialKey::Load);  break;  // LOAD (inert in SOLOS)

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

    // pollEvents() runs every run-loop slice AND every stopped-prompt idle tick, whether or not
    // a frame was drawn -- so it is the one place a host-side change is seen promptly even when
    // the guest's picture is static (present() is gated on the guest's framebuffer changing). A
    // crt toggle (SET DISPLAY crt=on/off) re-fits EVERY window, and re-fit or resize/expose alike
    // then repaint that window's last frame from the texture we still hold.
    for (auto& [owner, win] : windows_) {
        bool rp = std::find(repaint.begin(), repaint.end(), &win) != repaint.end();
        if (refitForCrt(win)) rp = true;
        if (rp) drawLastFrame(win);
    }
}

// SET DISPLAY crt=on/off flips a session-wide static (host/display.h) that has no handle on
// any window, so it cannot re-fit the picture itself. Catch the move here against the look this
// window's fit was built for and re-fit its logical frame + aspect lock. Returns true if it
// re-fit, so a caller with no fresh frame to draw (pollEvents) knows to repaint the last one.
// No-op until a texture exists (nothing has been drawn yet) and in the steady state.
bool SdlDisplay::refitForCrt(Window& win) {
    if (!win.renderer || !win.texture || Display::crt() == win.crtFit) return false;

    // Pin the window's WIDTH *before* touching the aspect lock. We want the picture to keep its
    // width and change HEIGHT (turn the tube on -> taller in place, off -> shorter), but setting
    // a new aspect ratio makes SDL resize the window on its own, and it does that by holding the
    // HEIGHT and moving the width -- the opposite of what we want. So read the width now, and
    // restore it below once we know the new aspect. Unlike a Dazzler mode switch (resolution
    // moves but the aspect barely does, so a resize would be jarring), crt swings the aspect from
    // a wide raster to a 4:3 tube; without a resize the aspect lock would just letterbox the
    // taller frame into the old wide window and shrink the picture.
    int ww = 0, wh = 0;
    SDL_GetWindowSize(win.window, &ww, &wh);
    const int keepW = ww;

    // Fit the logical frame + aspect lock to the new look (win.aspect becomes logW/logH for it).
    applyPresentation(win, win.picW, win.picH);

    // Restore the width SDL may have moved, and set the height the new aspect implies at that
    // width. Unconditional: applyPresentation's aspect change may already have resized the window
    // height-anchored, so keepW is what re-establishes the width-driven size we actually want.
    const int newH = win.aspect > 0.0f ? (int)std::lround((double)keepW / win.aspect) : wh;
    SDL_SetWindowSize(win.window, keepW, newH);
    return true;
}

// Paint whatever is in a window's texture into it: the bezel-inset sub-rect of the logical
// frame, the picture stretched to the painted height when crt is on. Split out of present() so a
// host-side presentation change (refitForCrt) can repaint the LAST frame with no new one in hand
// -- the board only calls present() when the guest's framebuffer changed, so a crt toggle at a
// still screen would otherwise not show until the guest redrew.
void SdlDisplay::drawLastFrame(Window& win) {
    if (!win.renderer || !win.texture) return;

    SDL_RenderClear(win.renderer);
    // Into the centered sub-rect of the logical frame: the logical size is the picture plus a
    // win.border margin on every side, so this inset leaves an even bezel that RenderClear (black)
    // fills. Coordinates are logical; the aspect-locked LETTERBOX scales the whole frame to the
    // window uniformly, keeping the bezel even at any size. The height is the painted height --
    // taller than the raster when crt is on -- so the w x h texture drawn into it is the tube
    // stretch, softened to a smooth phosphor face by the LINEAR filtering set just below.
    const int       w     = win.picW;
    const int       h     = win.picH;
    const int       hDisp = crtDisplayHeight(w, h);
    const SDL_FRect dst{ (float)win.border, (float)win.border, (float)w, (float)hDisp };

    // The tube look softens the stretch: crt=on scales the picture LINEAR so the 4:3
    // vertical stretch reads as a soft-phosphor bloom rather than the uneven row duplication
    // nearest-neighbor leaves; the crisp default keeps NEAREST so low-res pixels stay square.
    SDL_SetTextureScaleMode(win.texture,
                            Display::crt() ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_RenderTexture(win.renderer, win.texture, nullptr, &dst);
    SDL_RenderPresent(win.renderer);
}

void SdlDisplay::present(Owner owner, Surface* s) {
    if (!s) return;
    auto it = windows_.find(owner);
    if (it == windows_.end()) return;  // present without a prior acquire -- nothing to draw into
    Window& win = it->second;
    if (!win.renderer || !win.texture) return;

    // A crt toggle since the last frame -- re-fit before drawing so the logical frame matches
    // the painted height (a stale, shorter frame would clip the stretched picture).
    refitForCrt(win);

    // Resolve the indexed frame against the palette into RGBA32 (bytes R,G,B,A).
    auto px = s->pixels();
    const size_t n = px.size();
    for (size_t i = 0; i < n; ++i) {
        Color c{};
        uint8_t idx = px[i];
        if (idx < win.palette.size()) c = win.palette[idx];
        uint8_t* o = &win.rgba[i * 4];
        o[0] = c.r;
        o[1] = c.g;
        o[2] = c.b;
        o[3] = c.a;
    }

    SDL_UpdateTexture(win.texture, nullptr, win.rgba.data(), s->width() * 4);
    drawLastFrame(win);
}

} // namespace altair
