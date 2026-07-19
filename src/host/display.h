#pragma once
//
// Display -- the host video service a graphics board draws into (DESIGN.md 7.4).
//
// THE BOARD NEVER CALLS SDL. A VDM-1 or a Dazzler renders its picture into a
// Surface (an indexed-color pixel buffer) and hands it to present(); what turns
// that into a window, and how it scales, is the host's business and lives behind
// this interface. So the board compiles and RUNS with no graphics library at all
// -- against a NullDisplay (display_null.h) it renders into memory and a test
// reads the pixels back, which is exactly how a headless CI build proves the card.
//
// This is the display analogue of host/stream.h's ByteStream: the seam that keeps
// a board pure. A board's read()/write() are pure computation over state; anything
// that has to reach the outside world happens through an injected service, at a
// known point in emulated time (Board::pump(), DESIGN.md 7.1) -- never from inside
// a bus cycle, and never by owning the host's frame rate (DESIGN.md 7.4 #1: the
// SDL event loop does not own the main loop; the display is pumped once per slice).
//
// KEYSTROKES DO NOT GO INTO A BOARD FROM HERE. A display is output only. The
// VDM-1's keyboard is a SEPARATE parallel board, and a Dazzler game's joystick is an
// input port -- both take their bytes from a ByteStream/endpoint (host/endpoint.h),
// never from the window directly. But a windowed host DOES capture keystrokes when
// its window has focus, and those must reach the guest the same way the terminal's
// do: through the one Console (host/console.h), which is the recorded input queue
// (DESIGN.md 7.4). So a Display has an optional KEY SINK -- a callback the
// composition root points at Console::inject -- and the SDL window drains its key
// events into it. The board still reads only its ByteStream; window and terminal
// keys merge in the Console before any board sees them, so RECORD/REPLAY is intact.

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace altair {

// One palette entry. Alpha is carried for the host's benefit (a real color TV has
// none); a board leaves it 255. The Dazzler builds 16 of these from its RGBI
// nibble; the VDM-1 uses two (background, foreground).
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

// How a Surface stores its pixels.
//
//   Indexed8   -- one byte per pixel, an index into the palette set with
//                 setPalette(). This is what BOTH period boards want: the Dazzler
//                 is natively a palette machine (a 4-bit nibble per element) and
//                 the VDM-1 is two colors. The host resolves index->Color when it
//                 uploads the frame, so normal/reverse video and a Dazzler color
//                 change are a setPalette() away, with no re-render.
enum class PixelFormat {
    Indexed8,
};

// A drawable buffer the board fills and present()s. Concrete and owns its pixels --
// a board does not allocate host memory, it asks the Display to acquire() one and
// then writes into pixels(). The Display may hand back the SAME Surface every
// frame (it does: acquire() is idempotent for a given w,h,format), so a board must
// treat the contents as undefined and paint the whole frame each pump().
class Surface {
public:
    Surface(int w, int h, PixelFormat fmt)
        : w_(w), h_(h), fmt_(fmt), pixels_((size_t)w * (size_t)h, 0) {}

    int width() const { return w_; }
    int height() const { return h_; }
    PixelFormat format() const { return fmt_; }

    // Bytes per row. Indexed8 is tightly packed, so pitch == width; kept explicit
    // so a future format (or a host that wants row alignment) does not force every
    // board's inner loop to change.
    int pitch() const { return w_; }

    // The pixel bytes, row-major, top-left origin. Mutable for the board to paint;
    // const for the host to upload.
    std::span<uint8_t> pixels() { return pixels_; }
    std::span<const uint8_t> pixels() const { return pixels_; }

    // One pixel, bounds-checked to a no-op off the edge -- a glyph or a sprite that
    // runs past the margin clips instead of corrupting the next row.
    void put(int x, int y, uint8_t index) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        pixels_[(size_t)y * (size_t)w_ + (size_t)x] = index;
    }

    void clear(uint8_t index = 0) {
        for (auto& p : pixels_) p = index;
    }

private:
    int w_, h_;
    PixelFormat fmt_;
    std::vector<uint8_t> pixels_;
};

class Display {
public:
    virtual ~Display() = default;

    // Get the buffer to draw this frame's picture into, at the board's logical
    // resolution. The Display owns it; the board must not delete it and must not
    // keep the pointer past the next acquire() (the host may resize or reuse it).
    // Called once per pump() by the board -- cheap on the steady state, since the
    // dimensions do not change frame to frame.
    virtual Surface* acquire(int w, int h, PixelFormat fmt) = 0;

    // Show the frame. On a windowed host this uploads the Surface to a texture and
    // scales it (nearest-neighbor, integer where it fits) so low-res pixels stay
    // crisp; on a NullDisplay it does nothing. present() is also where the host
    // pumps its own event queue on the main thread (DESIGN.md 7.4 #2) -- but it
    // never blocks on vsync, because emulated time, not the monitor, owns the clock.
    virtual void present(Surface* s) = 0;

    // The palette an Indexed8 Surface resolves against. Up to 256 entries; a board
    // sets only as many as it uses (2 for the VDM-1, 16 for the Dazzler). Entries a
    // board never sets stay black. Cheap enough to call every frame or only on a
    // change -- the host caches it.
    virtual void setPalette(std::span<const Color> colors) = 0;

    // The keyboard sink (see the header note). A windowed host delivers focus
    // keystrokes here as ASCII; the composition root wires it to Console::inject so
    // they join the terminal's on the one recorded input queue. Left null on a
    // headless host -- a NullDisplay never captures a key, so it never calls it.
    using KeySink = std::function<void(const uint8_t*, size_t)>;
    void setKeySink(KeySink s) { keySink_ = std::move(s); }

    // KEYS WITH NO ASCII CODE. A host keyboard has arrows and a Home key; ASCII has
    // no character for either, so a windowed host has to be told what byte to send
    // and the answer is the guest's, not the terminal's.
    //
    // The defaults are the Processor Technology codes (reference/Sol-20.md, Table
    // 7-4), because the machines with a window are the ones with a VDM in them: a
    // Sol-20's own keyboard sends these, and SOLOS's display driver masks the top
    // bit, so the same byte drives CUTER and a bare VDM-1 the same way. A guest that
    // wants something else -- or nothing, which is what `0` means -- can be given it
    // here rather than by editing the host. That knob is not exposed to the operator
    // yet; see the mapping design in issue #59.
    //
    // THE TABLE LIVES HERE, NOT IN THE SDL BACK END. The window's job is to say
    // WHICH key was pressed; deciding what that key is worth in bytes is the same
    // kind of call as RETURN sending 0D, and belongs where it can be read, tested
    // and overridden without a graphics library.
    enum class SpecialKey { Up, Down, Left, Right, Home, Count_ };

    uint8_t specialKey(SpecialKey k) const { return special_[(size_t)k]; }
    void    setSpecialKey(SpecialKey k, uint8_t code) { special_[(size_t)k] = code; }

    // IS THE HOST READY FOR ANOTHER FRAME? A board asks BEFORE it paints, because
    // painting is the expensive half: a VDM-1 frame is 106,496 pixels cleared and
    // 106,496 glyph bits walked, and the run loop pumps every 2000 instructions. Left
    // unchecked that is ~106 pixel operations per emulated instruction, which measured
    // as a 94x slowdown on any machine with a video card in it -- the card, not the
    // CPU, was the emulator's speed limit.
    //
    // THE FRAME RATE IS THE HOST'S BUSINESS, NOT THE BOARD'S. A real VDM-1 scanned at
    // the monitor's rate no matter what the 8080 was doing, and the guest could not
    // observe the difference -- nothing on the S-100 side reads back a pixel. So this
    // is a pure host-side economy: it changes what is DRAWN, never what is COMPUTED,
    // and the status bits a guest CAN time (D0's one-shot, D1's scan-advance) come off
    // the Clock and are untouched by it.
    //
    // DEFAULT IS UNLIMITED, AND THAT IS DELIBERATE. Wall-clock rate limiting is
    // nondeterministic, so a test must be able to opt out and get "paint every time I
    // ask". tests/main.cpp leaves the limit at 0; src/main.cpp sets 60. The board's
    // own change detection is the deterministic half and is always on.
    void setFrameLimitHz(double hz) { frameMinPeriod_ = hz > 0 ? 1.0 / hz : 0.0; }

    bool wantsFrame() {
        if (frameMinPeriod_ <= 0.0) return true;  // unlimited -- tests, and headless
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> since = now - lastFrame_;
        if (since.count() < frameMinPeriod_) return false;
        lastFrame_ = now;
        return true;
    }

    // SECONDS OF WALL TIME, monotonic, zero at the first call. FOR A BOARD'S OWN
    // OSCILLATOR, AND FOR NOTHING ELSE.
    //
    // Emulated time is the Clock's, and a board must never take a duration a guest
    // can OBSERVE from anywhere else (DESIGN.md 7.5). But some of the metal on a
    // video card is not on the CPU's crystal and never was: the VDM-1's cursor blink
    // runs off its own oscillator at about 1 Hz (reference/Processor Technology
    // VDM-1.md), asynchronous to the 8080, and nothing on the S-100 side can read its
    // phase back. Driving it from the Clock made it a function of how fast the HOST
    // retires instructions -- so at `clock_hz = 0` the cursor strobed, which is not
    // what the card does and not something a guest could have caused.
    //
    // It lives HERE, behind the injected service, for the same reason wantsFrame()
    // does: the board stays pure, a headless host answers deterministically, and a
    // test can say what time it is instead of racing one (display_null.h).
    virtual double hostSeconds() {
        auto now = std::chrono::steady_clock::now();
        if (!epochSet_) { epoch_ = now; epochSet_ = true; }
        return std::chrono::duration<double>(now - epoch_).count();
    }

    // WHAT MACHINE THIS WINDOW BELONGS TO. A windowed host puts it in the title bar; a
    // headless one drops it on the floor.
    //
    // THE BOARD DOES NOT SAY THIS, AND THAT IS THE WHOLE POINT. The same VDM-1 is the
    // screen of a bare `vdm1`, of `cuter`, and of a Sol-20 -- so a title the board chose
    // could only ever say "VDM-1", which is what it used to say, and which is wrong on
    // the machine most likely to have a window open. The machine's name is the machine's
    // to publish.
    //
    // PUBLISHED, NOT WIRED. Nothing here holds a pointer to a Machine: CONFIG LOAD
    // replaces the machine wholesale (machine.h, replaceWith), so a borrowed name would
    // go stale exactly when the window is still open and still showing the old one. The
    // run loop pushes the current name each time it starts the guest instead, which is
    // the one moment the answer is both known and settled -- the same shape as a board
    // republishing its clock rate on re-attach (core/board.h) rather than being fixed up.
    //
    // May be called before there is a window; a host that has not opened one yet is
    // expected to remember it and use it when it does.
    virtual void setTitle(const std::string&) {}

    // TAKE WHATEVER THE OPERATOR HAS DONE TO THE WINDOW SINCE LAST TIME: keys pressed,
    // the close box clicked. Keystrokes go to the key sink, a close request is
    // remembered for takeQuitRequest(), and the host's own event queue is emptied,
    // which is also what keeps a window from being declared unresponsive.
    //
    // SEPARATE FROM present() ON PURPOSE, AND THAT SEPARATION IS THE POINT. Draining
    // input used to live inside present(), which a board reaches only after passing
    // frameChanged() and wantsFrame() -- so keys arrived at the rate FRAMES were
    // produced. At a static prompt the only thing producing frames was the VDM-1's
    // ~1 Hz cursor blink, which measured as ~200 ms of typing lag (2026-07-19); with a
    // non-blinking cursor it was a deadlock, because no frame meant no key meant
    // nothing changed meant still no frame. Reading the operator is not drawing, it
    // costs nothing when there is nothing to read, and it must not be behind a gate
    // that asks whether the picture would look different.
    //
    // The run loop calls it once a slice, for every host, alongside the console's own
    // poll. The base does nothing and NullDisplay inherits that, so headless builds and
    // tests are unaffected.
    virtual void pollEvents() {}

    // HAS THE OPERATOR ASKED TO CLOSE THE WINDOW SINCE WE LAST ASKED? A windowed
    // host sets this from its own event queue; the run loop asks once a slice and
    // stops the guest, which is the same place ATTN lands you (DESIGN.md 7.4).
    //
    // CONSUMING, exactly like Console::takeAttn(): asking clears it, so one click
    // stops one run and cannot stop the next one too. And it is the DISPLAY that is
    // asked, not the display that stops the machine -- the seam runs one way, and a
    // board's pump() must never be able to halt the backplane it is sitting in.
    //
    // A headless host never fires it, so the base answers no and NullDisplay
    // inherits that: a test and a no-SDL build see this as if it did not exist.
    virtual bool takeQuitRequest() { return false; }

protected:
    // A subclass that captures keystrokes calls this to hand them off; a no-op when
    // no sink is wired.
    void emitKeys(const uint8_t* p, size_t n) {
        if (keySink_ && n) keySink_(p, n);
    }

    // One of the no-ASCII keys, resolved through the table above. A code of `0` is
    // "this key sends nothing", so it is dropped rather than injected as a NUL --
    // which on a Sol would be MODE SELECT, and a key that quietly did that would be
    // worse than a key that does nothing.
    void emitSpecialKey(SpecialKey k) {
        uint8_t c = specialKey(k);
        if (c) emitKeys(&c, 1);
    }

private:
    KeySink keySink_;

    // Sol-20 Table 7-4: up 97, down 9A, left 81, right 93, HOME CURSOR 8E.
    uint8_t special_[(size_t)SpecialKey::Count_] = {0x97, 0x9A, 0x81, 0x93, 0x8E};

    // Minimum seconds between accepted frames; 0 = no limit. See wantsFrame().
    double frameMinPeriod_ = 0.0;
    std::chrono::steady_clock::time_point lastFrame_{};

    // Where hostSeconds() counts from -- fixed at its first call rather than at
    // construction, so the number stays small and a board that never asks pays
    // nothing.
    std::chrono::steady_clock::time_point epoch_{};
    bool epochSet_ = false;
};

} // namespace altair
