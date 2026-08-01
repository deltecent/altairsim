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

#include <functional>
#include <istream>
#include <string>
#include <vector>

#include "platform/terminal.h"

namespace altair {

// The unit test reaches the private transports and loop() through this friend (defined
// in tests/test_lineedit.cpp), driving the raw-mode editor with scripted keystrokes.
struct LineEditorTest;

// What Tab offers. The editor knows no grammar: the completer (the monitor) parses the
// line up to the cursor, decides where the word being completed begins, and hands back
// the candidates for it. The editor replaces the byte span [replaceFrom, cursor) and,
// when exactly one candidate remains, appends `suffix` -- a space after a finished word,
// or `=` after a property name so the value can be typed straight on.
struct Completions {
    size_t                   replaceFrom = 0;  // offset in lineToCursor where the word starts
    std::vector<std::string> matches;          // full candidate words
    std::string              suffix;           // appended after a UNIQUE match: " " or "="
};
using Completer = std::function<Completions(const std::string& lineToCursor)>;

class LineEditor {
public:
    LineEditor();

    // True if stdin is a terminal we can put in raw mode. When false, everything
    // below degrades to std::getline and no terminal state is ever touched.
    static bool interactive();

    // Read one line. Returns false at end of input (Ctrl-D on an empty line, or
    // EOF). `line` is set without its newline.
    bool read(const std::string& prompt, std::string& line, std::istream& in);

    // Run this while the editor is PARKED waiting for the operator to type (roughly
    // every 50 ms of idle). It is how the monitor keeps a stopped machine's video
    // window alive at the prompt -- the editor knows nothing of SDL, so the work goes
    // through here (the caller wires it to Display::pollEvents()). Only fires on the
    // raw-mode interactive path; a pipe/script has no window and never idles here.
    void setIdleHook(std::function<void()> fn) { onIdle_ = std::move(fn); }

    // Wire Tab completion. The editor calls this with the line up to the cursor and acts
    // on what comes back (see Completions). Without a completer, Tab does nothing.
    void setCompleter(Completer fn) { completer_ = std::move(fn); }

    const std::vector<std::string>& history() const { return history_; }

private:
    // The interactive editor, lifted out of read() so it can be driven without a real
    // terminal. It pulls bytes, waits, and writes THROUGH the three transports below,
    // which the constructor points at the platform layer. A test installs its own --
    // a scripted readByte_ (queued bytes, then -1), an always-Ready wait_, and a
    // capturing write_ -- and calls loop() directly, past interactive()/raw mode.
    bool loop(const std::string& prompt, std::string& line);

    std::vector<std::string> history_;
    std::function<void()>    onIdle_;
    Completer                completer_;

    std::function<int()>                    readByte_;  // one byte, blocking; -1 at EOF
    std::function<platform::InputWait(int)> wait_;      // first-byte wait, timeoutMs
    std::function<void(const std::string&)> write_;     // straight to the terminal

    friend struct LineEditorTest;
};

} // namespace altair
