#include "host/terminal/h19.h"

#include "host/terminal/screen.h"

namespace altair {

void H19Emulator::reset() {
    state_    = State::Ground;
    row_      = 0;
    saved_    = false;
    ansiMode_ = false;
    vt100_.reset();
    clearReply();
}

void H19Emulator::drainInner() {
    uint8_t b;
    while (vt100_.hasReply() && vt100_.takeReply(&b, 1) == 1) emit(b);
}

void H19Emulator::reverseVideo(TerminalScreen& scr, bool on) {
    uint8_t a = scr.currentAttr();
    scr.setCurrentAttr(on ? (uint8_t)(a | TerminalScreen::kAttrReverse)
                          : (uint8_t)(a & ~TerminalScreen::kAttrReverse));
}

void H19Emulator::feed(uint8_t b, TerminalScreen& scr) {
    if (ansiMode_) {
        // In ANSI mode the H19 is a VT100. Delegate, forward any report it makes, and drop
        // back to Heath mode the moment the guest resets DECANM (ESC[?2l).
        vt100_.feed(b, scr);
        drainInner();
        if (!vt100_.ansiMode()) ansiMode_ = false;
        return;
    }

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
            row_   = (uint8_t)(b - 0x20);  // biased by 0x20, like the VT52's ESC Y
            state_ = State::AddrCol;
            return;

        case State::AddrCol:
            scr.place(row_, (uint8_t)(b - 0x20));  // place() clamps to the page
            state_ = State::Ground;
            return;
    }
}

void H19Emulator::esc(uint8_t b, TerminalScreen& scr) {
    switch (b) {
        // ---- cursor motion ----
        case 'A': scr.cursorUp(); break;
        case 'B': scr.place(scr.cursorRow() + 1, scr.cursorCol()); break;
        case 'C': scr.place(scr.cursorRow(), scr.cursorCol() + 1); break;
        case 'D': scr.place(scr.cursorRow(), scr.cursorCol() - 1); break;
        case 'H': scr.home(); break;
        case 'I':  // reverse index
            if (scr.cursorRow() > 0) scr.place(scr.cursorRow() - 1, scr.cursorCol());
            break;
        case 'Y': state_ = State::AddrRow; return;  // direct cursor address -- two more bytes
        case 'n': {                                  // report cursor position -> ESC Y r c
            uint8_t rc[4] = {0x1B, 'Y', (uint8_t)(scr.cursorRow() + 0x20),
                             (uint8_t)(scr.cursorCol() + 0x20)};
            for (uint8_t x : rc) emit(x);
            break;
        }

        // ---- save / restore cursor ----
        case 'j': savedRow_ = scr.cursorRow(); savedCol_ = scr.cursorCol(); saved_ = true; break;
        case 'k': if (saved_) scr.place(savedRow_, savedCol_); break;

        // ---- erasing ----
        case 'E': scr.clearScreen(); break;   // clear display and home
        case 'J': scr.eraseToEos(); break;    // to end of screen
        case 'b': scr.eraseFromTop(); break;  // to start of screen
        case 'K': scr.eraseToEol(); break;    // to end of line
        case 'o': scr.eraseFromBol(); break;  // to start of line
        case 'l': scr.eraseLine(); break;     // entire line

        // ---- reverse video ----
        case 'p': reverseVideo(scr, true); break;
        case 'q': reverseVideo(scr, false); break;

        // ---- modes ----
        case '<': ansiMode_ = true; vt100_.reset(); break;  // enter ANSI (VT100) mode
        case 'z': scr.clearScreen(); scr.setCurrentAttr(0); saved_ = false; break;  // reset
        case '=':  // enter keypad-shifted mode -- accepted, no visible effect
        case '>':  // exit keypad-shifted mode
        case 'F':  // enter graphics charset -- one font here
        case 'G':  // exit graphics charset
            break;

        default:
            break;  // ESC L/M/N, ESC @/O (insert-delete) and the rest -- not modeled
    }
    state_ = State::Ground;
}

void H19Emulator::keyAscii(uint8_t b) {
    if (ansiMode_) { vt100_.keyAscii(b); drainInner(); return; }
    emit(b);
}

void H19Emulator::keySpecial(Key k) {
    if (ansiMode_) { vt100_.keySpecial(k); drainInner(); return; }
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
