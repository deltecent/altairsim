#include "cli/lineedit.h"

#include <cstdio>
#include <iostream>

#if defined(_WIN32)
#define ALTAIR_NO_TERMIOS 1
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace altair {

#ifdef ALTAIR_NO_TERMIOS

// No termios: behave exactly as before. Windows gets a line editor when someone
// with a Windows box wants one badly enough to test it, and not before.
bool LineEditor::interactive() { return false; }

#else

bool LineEditor::interactive() { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

namespace {

// Raw mode, restored on the way out no matter how we leave -- including through
// an exception or an early return. A monitor that leaves your terminal in raw
// mode when it exits is a monitor you only run once.
class RawMode {
public:
    RawMode() {
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        ok_ = true;
        termios raw = saved_;
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);  // we echo, and we edit
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }
    ~RawMode() {
        if (ok_) tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_);
    }
    bool ok() const { return ok_; }

private:
    termios saved_{};
    bool ok_ = false;
};

int readByte() {
    unsigned char c;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n != 1) return -1;
    return c;
}

// Repaint from the prompt: \r, prompt, line, clear to EOL, then park the cursor.
void redraw(const std::string& prompt, const std::string& buf, size_t cur) {
    std::string s = "\r" + prompt + buf + "\x1b[K";
    size_t back = buf.size() - cur;
    if (back) s += "\x1b[" + std::to_string(back) + "D";
    (void)!::write(STDOUT_FILENO, s.data(), s.size());
}

} // namespace

#endif // ALTAIR_NO_TERMIOS

bool LineEditor::read(const std::string& prompt, std::string& line, std::istream& in) {
    // Not a terminal: a script, a pipe, or the test suite. The driver is not
    // editing anything, nobody is typing, and raw mode would be actively wrong.
    if (!interactive()) {
        std::cout << prompt << std::flush;
        if (!std::getline(in, line)) return false;
        return true;
    }

#ifdef ALTAIR_NO_TERMIOS
    std::cout << prompt << std::flush;
    return (bool)std::getline(in, line);
#else
    RawMode raw;
    if (!raw.ok()) {
        std::cout << prompt << std::flush;
        return (bool)std::getline(in, line);
    }

    std::string buf;
    size_t cur = 0;
    size_t hpos = history_.size();  // one past the end == the line being typed
    std::string held;               // what was typed before we went browsing history

    redraw(prompt, buf, cur);

    for (;;) {
        int c = readByte();
        if (c < 0) {  // EOF on the tty
            (void)!::write(STDOUT_FILENO, "\n", 1);
            return false;
        }

        // ---- THE WHOLE POINT: BOTH of these are backspace. ----
        // BS (0x08, what a "backspace" key may send) and DEL (0x7F, what it may
        // ALSO send). We do not care which your terminal chose. Neither should you.
        if (c == 0x08 || c == 0x7F) {
            if (cur > 0) {
                buf.erase(cur - 1, 1);
                --cur;
                redraw(prompt, buf, cur);
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            (void)!::write(STDOUT_FILENO, "\n", 1);
            line = buf;
            if (!buf.empty() && (history_.empty() || history_.back() != buf))
                history_.push_back(buf);
            return true;
        }

        if (c == 0x03) {  // Ctrl-C -- abandon this line, keep the monitor
            (void)!::write(STDOUT_FILENO, "^C\n", 3);
            line.clear();
            return true;
        }
        if (c == 0x04) {  // Ctrl-D -- EOF, but only on an empty line
            if (buf.empty()) {
                (void)!::write(STDOUT_FILENO, "\n", 1);
                return false;
            }
            continue;
        }
        if (c == 0x15) {  // Ctrl-U
            buf.clear();
            cur = 0;
            redraw(prompt, buf, cur);
            continue;
        }
        if (c == 0x17) {  // Ctrl-W -- erase the word behind the cursor
            size_t e = cur;
            while (e > 0 && buf[e - 1] == ' ') --e;
            while (e > 0 && buf[e - 1] != ' ') --e;
            buf.erase(e, cur - e);
            cur = e;
            redraw(prompt, buf, cur);
            continue;
        }
        if (c == 0x01) { cur = 0; redraw(prompt, buf, cur); continue; }              // Ctrl-A
        if (c == 0x05) { cur = buf.size(); redraw(prompt, buf, cur); continue; }     // Ctrl-E

        if (c == 0x1B) {  // ESC [ ... -- arrows
            int a = readByte(), b = readByte();
            if (a != '[') continue;
            if (b == 'D' && cur > 0) { --cur; redraw(prompt, buf, cur); }
            else if (b == 'C' && cur < buf.size()) { ++cur; redraw(prompt, buf, cur); }
            else if (b == 'A' && hpos > 0) {          // up
                if (hpos == history_.size()) held = buf;
                buf = history_[--hpos];
                cur = buf.size();
                redraw(prompt, buf, cur);
            } else if (b == 'B' && hpos < history_.size()) {  // down
                ++hpos;
                buf = (hpos == history_.size()) ? held : history_[hpos];
                cur = buf.size();
                redraw(prompt, buf, cur);
            } else if (b == '3') {  // ESC[3~ -- the Delete key, forward-delete
                if (readByte() == '~' && cur < buf.size()) {
                    buf.erase(cur, 1);
                    redraw(prompt, buf, cur);
                }
            }
            continue;
        }

        if (c >= 0x20 && c < 0x7F) {  // printable
            buf.insert(buf.begin() + (long)cur, (char)c);
            ++cur;
            redraw(prompt, buf, cur);
        }
        // Anything else -- a stray control byte -- is DROPPED, not inserted. That
        // is how ^H got into the line in the first place.
    }
#endif
}

} // namespace altair
