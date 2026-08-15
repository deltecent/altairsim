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

// How dark each scan-line gap is, as an alpha over the picture (0 = invisible, 255 = solid
// black). A moderate value reads as a raster without swallowing the glyphs; tune with taste.
constexpr uint8_t kScanlineAlpha = 90;

// The DISPLAY height a w x h frame is painted at: unchanged (h) in the crisp default, or the
// 4:3 tube height when crt is on -- always a stretch, never a squash, so a frame already taller
// than 4:3 is left alone. Pushing the extra height into the LOGICAL frame is what lets the one
// integer-scale + LETTERBOX path present non-square pixels; the scan lines mask the row
// duplication that nearest-neighbor upscaling would otherwise show as a vertical blur.
int crtDisplayHeight(int w, int h) {
    if (!Display::crt()) return h;
    return std::max(h, (int)std::lround((double)w / kCrtAspect));
}

}  // namespace

SdlDisplay::~SdlDisplay() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (inited_) SDL_Quit();
}

// Tear the window down but leave SDL itself initialized, so the operator closing the
// window at a stopped prompt (host/display.h) is cheap and reversible: ensureWindow()
// rebuilds whenever renderer_ is null, so the next frame a board draws -- after a RUN --
// opens a fresh window with no re-init. pollEvents() keeps working meanwhile (inited_
// stays true) with simply nothing to drain, and there is no window for the OS to declare
// unresponsive.
void SdlDisplay::closeWindow() {
    if (texture_)  { SDL_DestroyTexture(texture_);   texture_  = nullptr; }
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_);     window_   = nullptr; }
    texW_ = texH_ = 0;
    quit_ = false;
}

// Lazily bring up SDL, the window and the renderer on the first frame -- so
// constructing an SdlDisplay is free, and a machine that never runs a graphics
// board never opens a window. Returns false (and the display goes quiet) if SDL
// cannot start, rather than taking down the simulator: a missing display server is
// the host's problem, not a reason the guest cannot run.
bool SdlDisplay::ensureWindow(int w, int h) {
    if (renderer_) return true;

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
    const int  requested  = Display::windowWidth();  // 0 = auto, else target pixels

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
    // opening scale (border_ * scale). Equal on both axes, so the displayed band is equal on
    // all four sides; folded into the logical size below and drawn as an inset in present().
    // At least 1 so there is always a hairline, even at large scales.
    border_ = std::max(1, (int)std::lround((double)kBorder / scale));
    const int logW = w + 2 * border_, logH = hDisp + 2 * border_;

    if (!SDL_CreateWindowAndRenderer(title_.c_str(), logW * scale, logH * scale,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN, &window_,
                                     &renderer_)) {
        std::fprintf(stderr, "SDL: window/renderer failed: %s\n", SDL_GetError());
        return false;
    }

    // The border band is whatever SDL_RenderClear paints, so make it black -- a dark bezel
    // around the picture. present() clears every frame before drawing the texture.
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);

    // AND TAKE THE WINDOW WE ACTUALLY GOT, not the one we asked for. Any window manager
    // may clamp, tile or otherwise ignore a requested size, and a size that is not a
    // multiple of the frame is precisely the letterbox described above. So re-derive the
    // multiple from the real size and set it back.
    //
    // Self-correcting rather than macOS-specific: if nothing was clamped, this is
    // already the size, and the call does nothing. It is also why the fix is not simply
    // "ask for less" -- the display query above narrows the guess, this makes it true.
    int gotW = 0, gotH = 0;
    SDL_GetWindowSize(window_, &gotW, &gotH);
    const int fit = std::max(1, std::min(gotW / logW, gotH / logH));
    const int wantW = logW * fit, wantH = logH * fit;
    if (gotW != wantW || gotH != wantH) SDL_SetWindowSize(window_, wantW, wantH);

    // Ask SDL for layout- and shift-resolved characters (SDL_EVENT_TEXT_INPUT), so a
    // '$' or a capital letter arrives correct without us reimplementing a keymap. The
    // control keys and Ctrl-combinations still come through SDL_EVENT_KEY_DOWN.
    SDL_StartTextInput(window_);

    // Everything above is configured, so show it -- unfocused, per the hint set before
    // the window was created.
    SDL_ShowWindow(window_);

    // Fit the logical presentation, the bezel and the aspect lock to this first frame. The
    // SAME call re-fits them when a board later changes resolution (applyPresentation, called
    // from acquire()) -- so the picture-plus-bezel keeps filling the window, even on all four
    // sides, at the opening size and after every mode switch. Done here, with the window shown
    // and sized, so the render output size it reads is the real one.
    applyPresentation(w, h);

    if (std::getenv("ALTAIRSIM_VIDEO_DEBUG")) {
        int ww = 0, wh = 0, pw = 0, ph = 0, ow = 0, oh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
        SDL_FRect r{};
        SDL_GetRenderLogicalPresentationRect(renderer_, &r);
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
void SdlDisplay::applyPresentation(int w, int h) {
    if (!renderer_ || !window_ || w <= 0 || h <= 0) return;

    // The bezel is worth about kBorder DEVICE pixels at the picture's current on-screen scale,
    // so it stays a thin hairline whatever resolution the board is in. Derive that scale from
    // how many whole picture-pixels fit the window's render output (falls back to 1 if the
    // output size is not readable yet, giving the kBorder default).
    // The painted height -- taller than the raster when crt is on (the 4:3 tube stretch). The
    // logical frame and the aspect lock are built from it, so LETTERBOX presents non-square
    // pixels; picW_/picH_ still track the NATIVE w,h for change detection in acquire().
    const int hDisp = crtDisplayHeight(w, h);

    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
    const int scale = std::max(1, std::min(ow / w, oh / hDisp));
    border_ = std::max(1, (int)std::lround((double)kBorder / scale));

    const int logW = w + 2 * border_, logH = hDisp + 2 * border_;

    // The picture PLUS its bezel is the logical frame; LETTERBOX scales that whole frame to the
    // window uniformly, keeping the bezel present() insets even on all four sides.
    SDL_SetRenderLogicalPresentation(renderer_, logW, logH,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // Lock the window to the padded frame's aspect so a resize stays proportional -- but only
    // when the ratio actually moved. For the Dazzler's square modes it is always 1:1, and
    // re-asserting an unchanged ratio would nudge a window the operator has sized.
    const float aspect = (float)logW / (float)logH;
    if (aspect != aspect_) {
        SDL_SetWindowAspectRatio(window_, aspect, aspect);
        aspect_ = aspect;
    }

    picW_ = w;
    picH_ = h;

    // Remember which look this fit was built for, so present() can notice a live crt=on/off
    // flip (the property is a session-wide static with no handle on this window) and re-fit.
    crtFit_ = Display::crt();
}

// Name the window after the machine, not after the board that draws into it
// (host/display.h). Called before there is a window as often as after -- the run loop
// says it every time it starts the guest, and a machine that has never painted a frame
// has no window yet -- so it is recorded and ensureWindow() picks it up. Retitling a live
// window matters too: CONFIG LOAD swaps the machine underneath an open one.
void SdlDisplay::setTitle(const std::string& name) {
    machineName_ = name;
    applyTitle();
}

// Running or stopped, in the title bar (host/display.h). Recorded and composed with the
// machine name; takes effect on a live window at once and is picked up by ensureWindow()
// on one not yet open.
void SdlDisplay::setRunning(bool running) {
    running_ = running;
    applyTitle();
}

// Build "AltairSim -- <machine>" and, while the guest is stopped, append the reminder that
// the picture on screen is frozen -- the window stays open across a stop (closeWindow),
// so without this a halted machine looks like a hung one. Cached in title_ so an unchanged
// title is not pushed at SDL every run/stop.
void SdlDisplay::applyTitle() {
    std::string t = machineName_.empty() ? "AltairSim" : "AltairSim -- " + machineName_;
    if (!running_) t += " -- simulator stopped";
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
//
// And declined outright when the window is the console (host/display.h). Handing the
// keyboard back at every stop is the right answer for a machine you drive from the
// altairsim> prompt and the wrong one for a Sol-20, where it would take the keyboard
// out of the window at each breakpoint and each close of the guest -- undoing, once a
// stop, exactly what the setting asked for.
void SdlDisplay::yieldFocus() {
    if (window_ && !Display::focusPolicy()) platform::yieldForeground();
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

    // A board that changed its frame resolution (a Dazzler switching video mode) needs the
    // logical presentation re-fit to the new size, or present() clips the larger picture to
    // the old, smaller logical frame. ensureWindow() did this for the first frame and then
    // short-circuits, so it falls to here. No-op in the steady state (picW_/picH_ unchanged).
    if (w != picW_ || h != picH_) applyPresentation(w, h);

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

    bool repaint = false;  // coalesce a burst of resize/expose events into one redraw below

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            quit_ = true;
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
            repaint = true;
            break;
        case SDL_EVENT_TEXT_INPUT:
            // Printable characters, already shift/layout-resolved. The guest is a
            // 7-bit machine, so pass ASCII only; Ctrl-combos come via KEY_DOWN.
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
                if ((e.key.mod & SDL_KMOD_CTRL) && k == 'e') quit_ = true;
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
    // crt toggle (SET DISPLAY crt=on/off) re-fits, and re-fit or resize/expose alike then repaint
    // the last frame from the texture we still hold.
    if (refitForCrt()) repaint = true;
    if (repaint) drawLastFrame();
}

// SET DISPLAY crt=on/off flips a session-wide static (host/display.h) that has no handle on
// this window, so it cannot re-fit the picture itself. Catch the move here against the look the
// current fit was built for and re-fit the logical frame + aspect lock. Returns true if it
// re-fit, so a caller with no fresh frame to draw (pollEvents) knows to repaint the last one.
// No-op until a texture exists (nothing has been drawn yet) and in the steady state.
bool SdlDisplay::refitForCrt() {
    if (!renderer_ || !texture_ || Display::crt() == crtFit_) return false;

    // Pin the window's WIDTH *before* touching the aspect lock. We want the picture to keep its
    // width and change HEIGHT (turn the tube on -> taller in place, off -> shorter), but setting
    // a new aspect ratio makes SDL resize the window on its own, and it does that by holding the
    // HEIGHT and moving the width -- the opposite of what we want. So read the width now, and
    // restore it below once we know the new aspect. Unlike a Dazzler mode switch (resolution
    // moves but the aspect barely does, so a resize would be jarring), crt swings the aspect from
    // a wide raster to a 4:3 tube; without a resize the aspect lock would just letterbox the
    // taller frame into the old wide window and shrink the picture.
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    const int keepW = ww;

    // Fit the logical frame + aspect lock to the new look (aspect_ becomes logW/logH for it).
    applyPresentation(picW_, picH_);

    // Restore the width SDL may have moved, and set the height the new aspect implies at that
    // width. Unconditional: applyPresentation's aspect change may already have resized the window
    // height-anchored, so keepW is what re-establishes the width-driven size we actually want.
    const int newH = aspect_ > 0.0f ? (int)std::lround((double)keepW / aspect_) : wh;
    SDL_SetWindowSize(window_, keepW, newH);
    return true;
}

// Paint whatever is in the texture into the window: the bezel-inset sub-rect of the logical
// frame, the picture stretched to the painted height when crt is on, and the scan lines over it.
// Split out of present() so a host-side presentation change (refitForCrt) can repaint the LAST
// frame with no new one in hand -- the board only calls present() when the guest's framebuffer
// changed, so a crt toggle at a still screen would otherwise not show until the guest redrew.
void SdlDisplay::drawLastFrame() {
    if (!renderer_ || !texture_) return;

    SDL_RenderClear(renderer_);
    // Into the centered sub-rect of the logical frame: the logical size is the picture plus a
    // border_ margin on every side, so this inset leaves an even bezel that RenderClear (black)
    // fills. Coordinates are logical; the aspect-locked LETTERBOX scales the whole frame to the
    // window uniformly, keeping the bezel even at any size. The height is the painted height --
    // taller than the raster when crt is on -- so the w x h texture drawn into it is the tube
    // stretch (nearest-neighbor, hence the scan lines below to mask the row duplication).
    const int       w     = picW_;
    const int       h     = picH_;
    const int       hDisp = crtDisplayHeight(w, h);
    const SDL_FRect dst{ (float)border_, (float)border_, (float)w, (float)hDisp };
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);

    // The scan lines: one dark, alpha-blended horizontal line at the bottom edge of each native
    // raster row's stretched band, so the gaps read as a raster rather than a blur. Only worth
    // drawing when the picture is actually stretched (hDisp > h); at 1:1 there is no band to gap.
    if (Display::crt() && hDisp > h) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, kScanlineAlpha);
        for (int i = 0; i < h; ++i) {
            const float y = (float)border_ + (float)std::lround((double)(i + 1) * hDisp / h) - 1;
            SDL_RenderLine(renderer_, (float)border_, y, (float)(border_ + w), y);
        }
        // Back to opaque black so the next frame's RenderClear paints a solid bezel.
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    }
    SDL_RenderPresent(renderer_);
}

void SdlDisplay::present(Surface* s) {
    if (!renderer_ || !texture_ || !s) return;

    // A crt toggle since the last frame -- re-fit before drawing so the logical frame matches
    // the painted height (a stale, shorter frame would clip the stretched picture).
    refitForCrt();

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
    drawLastFrame();
}

} // namespace altair
