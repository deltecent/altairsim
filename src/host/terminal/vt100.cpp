#include "host/terminal/vt100.h"

#include "host/terminal/screen.h"

#include <cstdio>

namespace altair {

void Vt100Emulator::reset() {
    state_      = State::Ground;
    params_.clear();
    cur_        = 0;
    haveDigits_ = false;
    priv_       = false;
    saved_      = false;
    appCursor_  = false;
    ansi_       = true;
    clearReply();  // a reset drops any report in flight
}

void Vt100Emulator::feed(uint8_t b, TerminalScreen& scr) {
    switch (state_) {
        case State::Ground:
            if (b == 0x1B) { state_ = State::Esc; return; }
            if (b < 0x20)  { c0(b, scr); return; }
            if (b == 0x7F) return;             // DEL -- ignored, as on a real VT100
            scr.putGlyph(b & 0x7F);            // printable (the grid renders 7-bit glyphs)
            return;

        case State::Esc:
            esc(b, scr);
            return;

        case State::Csi:
            csi(b, scr);
            return;

        case State::EscInter:
            // The byte after ESC ( / ) / * / + is a character-set designator. We render a
            // single font, so consume it and drop back to ground.
            state_ = State::Ground;
            return;
    }
}

void Vt100Emulator::c0(uint8_t b, TerminalScreen& scr) {
    switch (b) {
        case 0x08: scr.backspace(); break;       // BS
        case 0x09: scr.tab(); break;             // HT
        case 0x0A:                               // LF
        case 0x0B:                               // VT -- treated as LF
        case 0x0C: scr.lineFeed(); break;        // FF -- treated as LF
        case 0x0D: scr.carriageReturn(); break;  // CR
        default:   break;                        // BEL and the rest -- ignored
    }
}

void Vt100Emulator::esc(uint8_t b, TerminalScreen& scr) {
    switch (b) {
        case '[':  // CSI
            params_.clear();
            cur_        = 0;
            haveDigits_ = false;
            priv_       = false;
            state_      = State::Csi;
            return;

        case '(':  // charset designators -- consume the next byte, then ignore
        case ')':
        case '*':
        case '+':
            state_ = State::EscInter;
            return;

        case '7': saveCursor(scr); break;     // DECSC
        case '8': restoreCursor(scr); break;  // DECRC
        case 'D': scr.lineFeed(); break;      // IND -- index (down, scroll at bottom)
        case 'E': scr.newline(); break;       // NEL -- next line (CR + LF)
        case 'M':                             // RI -- reverse index (up; no scroll-down here)
            if (scr.cursorRow() > 0) scr.place(scr.cursorRow() - 1, scr.cursorCol());
            break;
        case 'c': ris(scr); break;            // RIS -- reset
        case '=':                             // DECKPAM / DECKPNM -- keypad modes, ignored
        case '>': break;
        default:  break;                      // unknown escape -- abandon it
    }
    state_ = State::Ground;
}

void Vt100Emulator::csi(uint8_t b, TerminalScreen& scr) {
    if (b >= '0' && b <= '9') {
        cur_        = cur_ * 10 + (b - '0');
        haveDigits_ = true;
        return;
    }
    if (b == ';') {
        params_.push_back(haveDigits_ ? cur_ : 0);
        cur_        = 0;
        haveDigits_ = false;
        return;
    }
    if (b >= 0x3C && b <= 0x3F) {  // '<' '=' '>' '?' -- private-parameter lead-in
        priv_ = true;
        return;
    }
    if (b >= 0x20 && b <= 0x2F) {  // intermediate bytes -- collected but unused here
        return;
    }
    // A final byte (0x40-0x7E) ends the sequence: flush the pending parameter and act.
    params_.push_back(haveDigits_ ? cur_ : 0);
    dispatchCsi(b, scr);
    state_ = State::Ground;
}

void Vt100Emulator::dispatchCsi(uint8_t final, TerminalScreen& scr) {
    const int row = scr.cursorRow(), col = scr.cursorCol();
    switch (final) {
        case 'A': scr.place(row - arg(0, 1), col); break;  // CUU
        case 'B': scr.place(row + arg(0, 1), col); break;  // CUD
        case 'C': scr.place(row, col + arg(0, 1)); break;  // CUF
        case 'D': scr.place(row, col - arg(0, 1)); break;  // CUB
        case 'H':                                          // CUP
        case 'f': scr.place(arg(0, 1) - 1, arg(1, 1) - 1); break;  // HVP

        case 'J':  // ED -- erase in display
            switch (argRaw(0)) {
                case 0:  scr.eraseToEos(); break;
                case 1:  scr.eraseFromTop(); break;
                case 2:  scr.eraseAll(); break;
                default: break;
            }
            break;

        case 'K':  // EL -- erase in line
            switch (argRaw(0)) {
                case 0:  scr.eraseToEol(); break;
                case 1:  scr.eraseFromBol(); break;
                case 2:  scr.eraseLine(); break;
                default: break;
            }
            break;

        case 'm': sgr(scr); break;  // SGR

        case 'n':  // DSR -- device status report
            if (argRaw(0) == 6) {
                char buf[32];  // two %d ints + ESC[ ;R + NUL -- GCC's worst case is 27
                std::snprintf(buf, sizeof buf, "\x1b[%d;%dR", row + 1, col + 1);
                emit(buf);
            } else if (argRaw(0) == 5) {
                emit("\x1b[0n");  // "terminal OK"
            }
            break;

        case 's': saveCursor(scr); break;     // ANSI.SYS save cursor
        case 'u': restoreCursor(scr); break;  // ANSI.SYS restore cursor

        case 'h':  // SM / DECSET
        case 'l':  // RM / DECRST
            // The private modes we honor: DECCKM (ESC[?1h/l), which decides whether the
            // arrow keys send ESC[A or ESC O A, and DECANM (ESC[?2h/l), which selects ANSI
            // vs VT52 mode (the H19 uses the latter to fall back to Heath mode). Everything
            // else (autowrap, cursor visibility) is accepted and ignored.
            if (priv_ && argRaw(0) == 1) appCursor_ = (final == 'h');
            if (priv_ && argRaw(0) == 2) ansi_ = (final == 'h');
            break;

        default: break;  // DECSTBM 'r' and the rest -- not modeled
    }
}

void Vt100Emulator::sgr(TerminalScreen& scr) {
    constexpr uint8_t R = TerminalScreen::kAttrReverse;
    constexpr uint8_t B = TerminalScreen::kAttrBlink;
    constexpr uint8_t H = TerminalScreen::kAttrHalf;
    uint8_t a = scr.currentAttr();
    for (int p : params_) {
        switch (p) {
            case 0:  a = 0; break;                    // reset
            case 1:  a = (uint8_t)(a & ~H); break;    // bold -> full intensity (no bold plane)
            case 2:  a = (uint8_t)(a | H); break;     // faint -> half intensity
            case 5:  a = (uint8_t)(a | B); break;     // blink
            case 7:  a = (uint8_t)(a | R); break;     // reverse
            case 22: a = (uint8_t)(a & ~H); break;    // normal intensity
            case 25: a = (uint8_t)(a & ~B); break;    // blink off
            case 27: a = (uint8_t)(a & ~R); break;    // reverse off
            default: break;                           // underline, colors -- not modeled
        }
    }
    scr.setCurrentAttr(a);
}

void Vt100Emulator::keySpecial(Key k) {
    char letter = 0;
    switch (k) {
        case Key::Up:    letter = 'A'; break;
        case Key::Down:  letter = 'B'; break;
        case Key::Right: letter = 'C'; break;
        case Key::Left:  letter = 'D'; break;
        case Key::Home:  letter = 'H'; break;
    }
    emit((uint8_t)0x1B);
    emit((uint8_t)(appCursor_ ? 'O' : '['));
    emit((uint8_t)letter);
}

void Vt100Emulator::saveCursor(const TerminalScreen& scr) {
    savedRow_  = scr.cursorRow();
    savedCol_  = scr.cursorCol();
    savedAttr_ = scr.currentAttr();
    saved_     = true;
}

void Vt100Emulator::restoreCursor(TerminalScreen& scr) {
    if (!saved_) return;
    scr.place(savedRow_, savedCol_);
    scr.setCurrentAttr(savedAttr_);
}

void Vt100Emulator::ris(TerminalScreen& scr) {
    reset();
    scr.clearScreen();
    scr.setCurrentAttr(0);
}

} // namespace altair
