#pragma once
//
// The console line editor (DESIGN.md 10.4).
//
// WHY THIS EXISTS AT ALL, RATHER THAN std::getline:
//
// Because a terminal has exactly ONE erase character. The tty driver's VERASE is
// a single byte -- it is DEL (0x7F) on most Unixes, BS (0x08) on others, and what
// your terminal emulator actually SENDS when you hit the backspace key depends on
// the emulator, the OS, and $TERM. When they disagree, the erase byte is not
// erasing: it is just a control character, and the driver dutifully puts it in
// the line, where it prints as `^H` and you cannot get rid of it.
//
// You cannot fix that by picking the right VERASE, because there is no right one.
// The fix is to stop asking the driver to edit the line: take the terminal out of
// canonical mode, read bytes, and treat BOTH 0x08 AND 0x7F as backspace. Then it
// does not matter which one arrives, and it does not matter which OS you are on.
//
// A pipe or a script is NOT a terminal, so we fall back to getline there. Scripts,
// the test suite and --mcp all take that path and see no escape sequences at all.

#include <istream>
#include <string>
#include <vector>

namespace altair {

class LineEditor {
public:
    // True if stdin is a terminal we can put in raw mode. When false, everything
    // below degrades to std::getline and no terminal state is ever touched.
    static bool interactive();

    // Read one line. Returns false at end of input (Ctrl-D on an empty line, or
    // EOF). `line` is set without its newline.
    bool read(const std::string& prompt, std::string& line, std::istream& in);

    const std::vector<std::string>& history() const { return history_; }

private:
    std::vector<std::string> history_;
};

} // namespace altair
