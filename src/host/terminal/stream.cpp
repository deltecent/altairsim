#include "host/terminal/stream.h"

#include "host/display.h"
#include "host/terminal/emulator.h"
#include "host/terminal/font.h"

#include <utility>

namespace altair {

Display*            TerminalStream::s_display = nullptr;
const TerminalFont* TerminalStream::s_font    = nullptr;

TerminalStream::TerminalStream(std::string spec, int rows, int cols,
                               std::unique_ptr<TerminalEmulator> emu)
    : spec_(std::move(spec)), screen_(rows, cols), emu_(std::move(emu)) {
    renderer_.setFont(s_font);  // borrowed, session-lifetime; null just paints nothing
}

TerminalStream::~TerminalStream() = default;

bool TerminalStream::hasWindow() { return s_display && s_display->isWindowed(); }

size_t TerminalStream::read(uint8_t* buf, size_t n) { return emu_->takeReply(buf, n); }

size_t TerminalStream::write(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) emu_->feed(buf[i], screen_);
    return n;  // the emulated terminal consumes instantly -- it never back-pressures
}

bool TerminalStream::readable() const { return emu_->hasReply(); }

void TerminalStream::keyAscii(uint8_t b) { emu_->keyAscii(b); }

void TerminalStream::keySpecial(int key) {
    emu_->keySpecial((TerminalEmulator::Key)key);
}

void TerminalStream::pump() {
    Display* d = s_display;
    if (!d) return;
    if (!renderer_.frameChanged(screen_, d)) return;
    if (!d->wantsFrame()) return;
    renderer_.render(*d, screen_, videoWidth_);
}

} // namespace altair
