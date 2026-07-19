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

#include <cstdint>
#include <functional>
#include <span>
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

private:
    KeySink keySink_;
};

} // namespace altair
