#include "cli/lineedit.h"

#include "platform/terminal.h"

#include <iostream>

namespace altair {

// NO CONDITIONAL COMPILATION IN THIS FILE, and it used to be the only one in the tree
// that had any. Raw mode is src/platform/terminal.h's job now (DESIGN.md 2.1), and the
// editor below is the SAME editor on every OS -- Windows included, where it used to be
// switched off wholesale on the grounds that nobody had tested it.
//
// (Which means Windows now gets a line editor for the first time, out of exactly the
// same code. It has never been run there -- src/platform/win32/ has never been
// compiled -- but the fallback is honest: if the console cannot be put in raw mode,
// interactive() is false and this degrades to std::getline, as it always did.)

namespace {

// Raw mode, given back on the way out no matter how we leave -- including through an
// exception, an early return, or a signal (terminal.h arms the handlers). A monitor
// that leaves your terminal in raw mode when it exits is a monitor you run once.
class RawLine {
public:
    RawLine() : ok_(platform::enterTermMode(platform::TermMode::LineEdit)) {}
    ~RawLine() {
        if (ok_) platform::restoreTerm();
    }
    bool ok() const { return ok_; }

private:
    bool ok_;
};

void put(const std::string& s) { platform::writeOutput((const uint8_t*)s.data(), s.size()); }

// Repaint from the prompt: \r, prompt, line, clear to EOL, then park the cursor.
void redraw(const std::string& prompt, const std::string& buf, size_t cur) {
    std::string s = "\r" + prompt + buf + "\x1b[K";
    size_t back = buf.size() - cur;
    if (back) s += "\x1b[" + std::to_string(back) + "D";
    put(s);
}

} // namespace

bool LineEditor::interactive() { return platform::stdinIsTty() && platform::stdoutIsTty(); }

bool LineEditor::read(const std::string& prompt, std::string& line, std::istream& in) {
    // Not a terminal: a script, a pipe, or the test suite. The driver is not
    // editing anything, nobody is typing, and raw mode would be actively wrong.
    if (!interactive()) {
        std::cout << prompt << std::flush;
        if (!std::getline(in, line)) return false;
        return true;
    }

    RawLine raw;
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
        int c = platform::readInputBlocking();
        if (c < 0) {  // EOF on the tty
            put("\n");
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
            put("\n");
            line = buf;
            if (!buf.empty() && (history_.empty() || history_.back() != buf))
                history_.push_back(buf);
            return true;
        }

        // THERE IS NO ^C CASE HERE, and that is not an oversight. LineEdit mode leaves
        // ISIG ON (platform/terminal.h), so Ctrl-C at the monitor prompt is a SIGNAL and
        // never a byte: it cannot reach this loop, and a branch for it would be code that
        // can never run. It kills the process, exactly as it did before there was a CPU
        // (monitor.cpp says the same) -- and the handler in the platform layer gives the
        // terminal back on the way out, which is the part that used to be missing.
        //
        // Ctrl-C IS a byte to the GUEST, in Guest mode, where ISIG is off. Two modes,
        // opposite answers, and this is the one where ^C is a way out.
        if (c == 0x04) {  // Ctrl-D -- EOF, but only on an empty line
            if (buf.empty()) {
                put("\n");
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
            int a = platform::readInputBlocking(), b = platform::readInputBlocking();
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
                if (platform::readInputBlocking() == '~' && cur < buf.size()) {
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
}

} // namespace altair
