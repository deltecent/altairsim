#include "host/terminal/adm3a.h"

#include "host/terminal/screen.h"

namespace altair {

void Adm3aEmulator::reset() {
    state_ = State::Ground;
    row_   = 0;
    clearReply();
}

void Adm3aEmulator::feed(uint8_t b, TerminalScreen& scr) {
    switch (state_) {
        case State::Ground:
            if (b == 0x1B) { state_ = State::Esc; return; }
            switch (b) {
                case 0x07: return;                       // BEL -- ignored
                case 0x08: scr.backspace(); return;      // ^H cursor left
                case 0x0A: scr.lineFeed(); return;       // ^J cursor down
                case 0x0B: scr.cursorUp(); return;       // ^K cursor up
                case 0x0C: scr.cursorForward(); return;  // ^L cursor right
                case 0x0D: scr.carriageReturn(); return; // ^M carriage return
                case 0x1A: scr.clearScreen(); return;    // ^Z clear + home
                case 0x1E: scr.home(); return;           // ^^ home
                default: break;
            }
            if (b < 0x20 || b == 0x7F) return;  // other controls / DEL -- ignored
            scr.putGlyph(b & 0x7F);
            return;

        case State::Esc:
            // The ADM-3A's one escape: ESC = begins a cursor load. Anything else after ESC
            // is not an ADM-3A sequence, so drop it and return to ground.
            state_ = (b == '=') ? State::LoadRow : State::Ground;
            return;

        case State::LoadRow:
            // Row is biased by 0x20 (a space means row 0). Hold it for the column byte.
            row_   = (uint8_t)(b - 0x20);
            state_ = State::LoadCol;
            return;

        case State::LoadCol:
            scr.place(row_, (uint8_t)(b - 0x20));  // place() clamps to the page
            state_ = State::Ground;
            return;
    }
}

void Adm3aEmulator::keySpecial(Key k) {
    switch (k) {
        case Key::Up:    emit((uint8_t)0x0B); break;  // ^K
        case Key::Down:  emit((uint8_t)0x0A); break;  // ^J
        case Key::Left:  emit((uint8_t)0x08); break;  // ^H
        case Key::Right: emit((uint8_t)0x0C); break;  // ^L
        case Key::Home:  emit((uint8_t)0x1E); break;  // ^^
    }
}

} // namespace altair
