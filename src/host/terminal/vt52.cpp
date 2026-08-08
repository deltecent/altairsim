#include "host/terminal/vt52.h"

#include "host/terminal/screen.h"

namespace altair {

void Vt52Emulator::reset() {
    state_ = State::Ground;
    row_   = 0;
    clearReply();
}

void Vt52Emulator::feed(uint8_t b, TerminalScreen& scr) {
    switch (state_) {
        case State::Ground:
            if (b == 0x1B) { state_ = State::Esc; return; }
            switch (b) {
                case 0x08: scr.backspace(); return;       // BS
                case 0x09: scr.tab(); return;             // HT
                case 0x0A: scr.lineFeed(); return;        // LF
                case 0x0D: scr.carriageReturn(); return;  // CR
                default: break;
            }
            if (b < 0x20 || b == 0x7F) return;  // other controls / DEL -- ignored
            scr.putGlyph(b & 0x7F);
            return;

        case State::Esc:
            esc(b, scr);
            return;

        case State::AddrRow:
            row_   = (uint8_t)(b - 0x20);  // biased by 0x20, as ESC Y specifies
            state_ = State::AddrCol;
            return;

        case State::AddrCol:
            scr.place(row_, (uint8_t)(b - 0x20));  // place() clamps to the page
            state_ = State::Ground;
            return;
    }
}

void Vt52Emulator::esc(uint8_t b, TerminalScreen& scr) {
    switch (b) {
        case 'A': scr.cursorUp(); break;                             // cursor up
        case 'B': scr.place(scr.cursorRow() + 1, scr.cursorCol()); break;  // cursor down
        case 'C': scr.place(scr.cursorRow(), scr.cursorCol() + 1); break;  // cursor right
        case 'D': scr.place(scr.cursorRow(), scr.cursorCol() - 1); break;  // cursor left
        case 'H': scr.home(); break;                                // cursor home
        case 'I':                                                   // reverse line feed
            if (scr.cursorRow() > 0) scr.place(scr.cursorRow() - 1, scr.cursorCol());
            break;
        case 'J': scr.eraseToEos(); break;  // erase to end of screen
        case 'K': scr.eraseToEol(); break;  // erase to end of line
        case 'Y': state_ = State::AddrRow; return;  // direct cursor address -- two more bytes
        case 'Z':                                   // identify: a VT52 without copier
            emit("\x1b/K");
            break;
        case '=':   // enter alternate keypad -- accepted, no visible effect
        case '>':   // exit alternate keypad
        case 'F':   // enter graphics charset -- we render one font
        case 'G':   // exit graphics charset
            break;
        default:
            break;  // unknown escape -- abandon it
    }
    state_ = State::Ground;
}

void Vt52Emulator::keySpecial(Key k) {
    char letter = 0;
    switch (k) {
        case Key::Up:    letter = 'A'; break;
        case Key::Down:  letter = 'B'; break;
        case Key::Right: letter = 'C'; break;
        case Key::Left:  letter = 'D'; break;
        case Key::Home:  letter = 'H'; break;
    }
    emit((uint8_t)0x1B);
    emit((uint8_t)letter);
}

} // namespace altair
