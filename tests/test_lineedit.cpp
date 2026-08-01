// The console line editor's raw-mode dispatch (cli/lineedit.cpp).
//
// This is the code that had no test at all: the byte loop, the escape decoder, and now
// Tab completion. It reads through injected transports, so we drive it with a scripted
// byte source and a capturing sink -- no terminal, no pty. LineEditorTest is the friend
// that reaches the private members and loop(); the editor is otherwise unchanged.

#include "cli/lineedit.h"
#include "test.h"

#include <sstream>
#include <string>
#include <vector>

namespace altair {

struct LineEditorTest {
    struct Result {
        std::string line;  // what the editor committed (Enter), empty on EOF
        std::string out;   // everything the editor painted
        bool        ret;   // loop()'s return: true on a committed line
    };

    // Run `script` through a fresh editor, optionally seeded with history and a completer.
    static Result drive(std::vector<int> script, std::vector<std::string> hist = {},
                        Completer comp = nullptr) {
        LineEditor ed;
        ed.history_ = std::move(hist);
        if (comp) ed.setCompleter(std::move(comp));

        ed.readByte_ = [script, i = size_t{0}]() mutable -> int {
            return i < script.size() ? script[i++] : -1;  // -1 == EOF once the script runs dry
        };
        ed.wait_ = [](int) { return platform::InputWait::Ready; };  // a byte is always waiting

        Result r;
        std::string out;
        ed.write_ = [&out](const std::string& s) { out += s; };
        r.ret = ed.loop("> ", r.line);
        r.out = std::move(out);
        return r;
    }
};

}  // namespace altair

using namespace altair;
using V = std::vector<int>;

namespace {

// A string's bytes as a script fragment.
V b(const std::string& s) {
    V v;
    for (unsigned char c : s) v.push_back(c);
    return v;
}
// Concatenate fragments.
V operator+(V a, const V& x) {
    a.insert(a.end(), x.begin(), x.end());
    return a;
}

// Named keys, as the bytes a terminal sends.
const V LEFT = {0x1B, '[', 'D'}, RIGHT = {0x1B, '[', 'C'};
const V UP = {0x1B, '[', 'A'}, DOWN = {0x1B, '[', 'B'};
const V HOME = {0x1B, '[', 'H'}, END = {0x1B, '[', 'F'};
const V HOME_T = {0x1B, '[', '1', '~'}, END_T = {0x1B, '[', '4', '~'};
const V DEL = {0x1B, '[', '3', '~'};
const V ALT_B = {0x1B, 'b'}, ALT_F = {0x1B, 'f'};
const V CTRL_LEFT = {0x1B, '[', '1', ';', '5', 'D'}, CTRL_RIGHT = {0x1B, '[', '1', ';', '5', 'C'};
const V ENTER = {'\r'};
const int CTRL_A = 0x01, CTRL_K = 0x0B, TAB = '\t', BS = 0x08, DEL7 = 0x7F, CTRL_D = 0x04;

std::string L(V script, std::vector<std::string> hist = {}, Completer comp = nullptr) {
    return LineEditorTest::drive(std::move(script), std::move(hist), std::move(comp)).line;
}

}  // namespace

void test_lineedit() {
    SECTION("backspace -- BOTH 0x08 and 0x7F erase");
    CHECK(L(b("abc") + V{BS} + ENTER) == "ab", "0x08 erases the char behind the cursor");
    CHECK(L(b("abc") + V{DEL7} + ENTER) == "ab", "0x7F does too -- the whole point of the editor");

    SECTION("cursor motion inserts at the cursor, not the end");
    CHECK(L(b("ab") + LEFT + b("X") + ENTER) == "aXb", "Left then X lands between a and b");
    CHECK(L(b("abc") + LEFT + LEFT + RIGHT + b("Y") + ENTER) == "abYc", "Left Left Right nets one step left");

    SECTION("Home and End -- CSI and keypad-tilde forms, parity with Ctrl-A/E");
    CHECK(L(b("abc") + HOME + b("X") + ENTER) == "Xabc", "ESC[H is Home");
    CHECK(L(b("abc") + HOME_T + b("X") + ENTER) == "Xabc", "ESC[1~ is Home too");
    CHECK(L(b("abc") + HOME + END + b("Z") + ENTER) == "abcZ", "ESC[F is End");
    CHECK(L(b("abc") + HOME + END_T + b("Z") + ENTER) == "abcZ", "ESC[4~ is End too");
    CHECK(L(b("abc") + V{CTRL_A} + b("X") + ENTER) == "Xabc", "and Ctrl-A still homes");

    SECTION("Ctrl-K kills to end of line");
    CHECK(L(b("abcdef") + LEFT + LEFT + LEFT + V{CTRL_K} + ENTER) == "abc",
          "three Lefts then Ctrl-K drops def");

    SECTION("Delete (ESC[3~) forward-deletes");
    CHECK(L(b("abc") + LEFT + LEFT + DEL + ENTER) == "ac", "Delete removes the char under the cursor");

    SECTION("word motion -- Alt-b/Alt-f and Ctrl-Left/Ctrl-Right");
    CHECK(L(b("one two") + ALT_B + b("X") + ENTER) == "one Xtwo", "Alt-b jumps to the start of the word");
    CHECK(L(b("one two") + CTRL_LEFT + b("X") + ENTER) == "one Xtwo", "Ctrl-Left does the same");
    CHECK(L(b("one two") + V{CTRL_A} + ALT_F + b("X") + ENTER) == "oneX two", "Alt-f jumps to the word end");
    CHECK(L(b("one two") + V{CTRL_A} + CTRL_RIGHT + b("X") + ENTER) == "oneX two", "Ctrl-Right does the same");

    SECTION("history -- Up recalls, Down restores the line in progress");
    std::vector<std::string> hist = {"run", "reset"};
    CHECK(L(UP + ENTER, hist) == "reset", "Up recalls the most recent entry");
    CHECK(L(UP + UP + ENTER, hist) == "run", "a second Up walks back one more");
    CHECK(L(b("ab") + UP + DOWN + ENTER, hist) == "ab", "Down past the newest restores what was typed");

    SECTION("Tab completion -- the editor edits, the completer decides");
    auto one = [](const std::string&) {
        Completions c;
        c.replaceFrom = 0;
        c.matches = {"SET"};
        c.suffix = " ";
        return c;
    };
    CHECK(L(b("SE") + V{TAB} + ENTER, {}, one) == "SET ", "a unique match completes the word and adds its space");

    auto twoLCP = [](const std::string&) {
        Completions c;
        c.replaceFrom = 0;
        c.matches = {"BAUD", "BANK"};  // common prefix BA
        return c;
    };
    CHECK(L(b("B") + V{TAB} + ENTER, {}, twoLCP) == "BA", "an ambiguous match fills in the common prefix");

    auto twoList = [](const std::string&) {
        Completions c;
        c.replaceFrom = 0;
        c.matches = {"AA", "AB"};  // common prefix is just A -- the fragment already
        return c;
    };
    auto r = LineEditorTest::drive(b("A") + V{TAB} + V{TAB} + ENTER, {}, twoList);
    CHECK(r.out.find("AA  AB") != std::string::npos, "a second Tab lists the candidates when the prefix cannot grow");

    SECTION("Ctrl-D on an empty line ends input");
    auto e = LineEditorTest::drive(V{CTRL_D});
    CHECK(!e.ret, "Ctrl-D on empty returns false (EOF)");
    CHECK(L(b("ab") + V{CTRL_D} + ENTER) == "ab", "but Ctrl-D on a non-empty line is ignored");

    // The persistence seam: loadHistory/saveHistory take streams, never files, so these
    // drive them with stringstreams -- the same disk-free shape the byte transports use.
    using VS = std::vector<std::string>;

    SECTION("history file -- round-trips through streams and recalls");
    {
        LineEditor ed;
        std::istringstream in("run\nreset\ndir\n");
        ed.loadHistory(in, 50);
        CHECK(ed.history() == VS({"run", "reset", "dir"}), "loadHistory seeds the vector oldest-first");
        std::ostringstream out;
        ed.saveHistory(out, 50);
        CHECK(out.str() == "run\nreset\ndir\n", "saveHistory writes each entry on its own line");
        CHECK(L(UP + ENTER, ed.history()) == "dir", "the newest saved line is what Up recalls first");
    }

    SECTION("history file -- load skips blanks and adjacent dupes, sheds a trailing CR");
    {
        LineEditor ed;
        std::istringstream in("run\r\nrun\r\n\r\nreset\r\n");  // CRLF file, an adjacent dupe, a blank
        ed.loadHistory(in, 50);
        CHECK(ed.history() == VS({"run", "reset"}),
              "adjacent duplicates collapse, blank lines drop, and the CR is shed");
    }

    SECTION("history file -- the cap keeps the newest and drops the oldest");
    {
        LineEditor ed;
        std::ostringstream feed;
        for (int i = 0; i < 60; ++i) feed << "cmd" << i << "\n";
        std::istringstream in(feed.str());
        ed.loadHistory(in, 50);
        CHECK(ed.history().size() == 50, "load trims to the cap");
        CHECK(ed.history().front() == "cmd10", "the oldest ten are the ones dropped");
        CHECK(ed.history().back() == "cmd59", "the newest survives");
        std::ostringstream out;
        ed.saveHistory(out, 3);  // a tighter cap on save
        CHECK(out.str() == "cmd57\ncmd58\ncmd59\n", "saveHistory honors a smaller cap, newest kept");
    }
}
