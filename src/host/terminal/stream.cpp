#include "host/terminal/stream.h"

#include "core/board.h"  // setPropertyIn
#include "host/display.h"
#include "host/terminal/emulator.h"
#include "host/terminal/font.h"

#include <cctype>
#include <utility>

namespace altair {

Display*                  TerminalStream::s_display   = nullptr;
const TerminalFont*       TerminalStream::s_font      = nullptr;
std::set<TerminalStream*>  TerminalStream::s_live;
TerminalStream::Settings  TerminalStream::s_settings;

TerminalStream::TerminalStream(std::string spec, int rows, int cols,
                               std::unique_ptr<TerminalEmulator> emu)
    : spec_(std::move(spec)), screen_(rows, cols), emu_(std::move(emu)) {
    renderer_.setFont(s_font);  // borrowed, session-lifetime; null just paints nothing
    s_live.insert(this);
}

TerminalStream::~TerminalStream() {
    // Only this terminal leaves: on a CONFIG LOAD the replacement line is constructed
    // before the old one is destroyed, and both are in s_live for that moment.
    s_live.erase(this);
    if (s_display) s_display->releaseKeyboard(this);
}

bool TerminalStream::hasWindow() { return s_display && s_display->isWindowed(); }

TerminalStream* TerminalStream::forOwner(const void* owner) {
    // Compared as an address only: a const_cast of a pointer we never follow unless it is
    // one of ours.
    auto it = s_live.find(static_cast<TerminalStream*>(const_cast<void*>(owner)));
    return it == s_live.end() ? nullptr : *it;
}

bool TerminalStream::routeKeys(const void* owner, const uint8_t* p, size_t n) {
    TerminalStream* t = forOwner(owner);
    if (!t) return false;
    for (size_t i = 0; i < n; ++i) t->keyAscii(p[i]);
    return true;
}

bool TerminalStream::routePointer(const void* owner, int kind, int x, int y) {
    TerminalStream* t = forOwner(owner);
    if (!t) return false;
    t->pointer(kind, x, y);
    return true;
}

bool TerminalStream::routeCopy(const void* owner) {
    TerminalStream* t = forOwner(owner);
    if (!t || !s_display) return false;
    s_display->setClipboardText(t->copyText());
    return true;
}

// A picture pixel -> the screen cell under it, clamped to the page, so a drag past the edge
// selects to the edge.
void TerminalStream::cellAt(int x, int y, int& row, int& col) const {
    const int cw = s_font ? s_font->cellCols() : 8, ch = s_font ? s_font->cellRows() : 16;
    row = y < 0 ? 0 : y / ch;
    col = x < 0 ? 0 : x / cw;
    if (row >= screen_.rows()) row = screen_.rows() - 1;
    if (col >= screen_.cols()) col = screen_.cols() - 1;
    if (row < 0) row = 0;
    if (col < 0) col = 0;
}

void TerminalStream::clearSelection() {
    if (!selOn_) return;
    selOn_ = false;
    renderer_.setSelection(false, 0, 0, 0, 0);
    screen_.markDirty();
}

void TerminalStream::pointer(int kind, int x, int y) {
    int r = 0, c = 0;
    if (kind == 0) {  // down: a new anchor; a click alone selects nothing
        clearSelection();
        cellAt(x, y, r, c);
        selAR_ = selHR_ = r;
        selAC_ = selHC_ = c;
        dragging_ = true;
        return;
    }
    if (!dragging_) return;  // a drag or up that began in another window
    cellAt(x, y, r, c);
    if (r != selHR_ || c != selHC_ || kind == 1) {
        selHR_ = r;
        selHC_ = c;
        selOn_ = selHR_ != selAR_ || selHC_ != selAC_;
        // Reading order: the selection runs from the earlier cell to the later one.
        const bool fwd = selAR_ < selHR_ || (selAR_ == selHR_ && selAC_ <= selHC_);
        if (fwd) renderer_.setSelection(selOn_, selAR_, selAC_, selHR_, selHC_);
        else     renderer_.setSelection(selOn_, selHR_, selHC_, selAR_, selAC_);
        screen_.markDirty();
    }
    if (kind == 2) dragging_ = false;
}

// The selected text in reading order, one line per screen row, trailing blanks cut (a
// terminal row is padded to its width; the clipboard should not be). With no selection, the
// whole screen, with the blank rows at the bottom cut too. A cell that is not printable
// ASCII (a line-drawing glyph) copies as a space.
std::string TerminalStream::copyText() const {
    int r0 = 0, c0 = 0, r1 = screen_.rows() - 1, c1 = screen_.cols() - 1;
    if (selOn_) {
        const bool fwd = selAR_ < selHR_ || (selAR_ == selHR_ && selAC_ <= selHC_);
        r0 = fwd ? selAR_ : selHR_;  c0 = fwd ? selAC_ : selHC_;
        r1 = fwd ? selHR_ : selAR_;  c1 = fwd ? selHC_ : selAC_;
    }
    std::string out;
    for (int r = r0; r <= r1; ++r) {
        const int from = r == r0 ? c0 : 0, to = r == r1 ? c1 : screen_.cols() - 1;
        std::string line;
        for (int c = from; c <= to; ++c) {
            const uint8_t g = screen_.charAt(r, c);
            line += (g >= 0x20 && g < 0x7F) ? (char)g : ' ';
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line;
        if (r != r1) out += '\n';
    }
    if (!selOn_)
        while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

bool TerminalStream::routeSpecial(const void* owner, int key) {
    TerminalStream* t = forOwner(owner);
    if (!t) return false;
    t->keySpecial(key);
    return true;
}

size_t TerminalStream::read(uint8_t* buf, size_t n) { return emu_->takeReply(buf, n); }

size_t TerminalStream::write(const uint8_t* buf, size_t n) {
    // Outbound (guest -> screen): strip7out first, so an even-parity 0x8D becomes the
    // 0x0D the emulator homes on; then drop a bell if muted, then feed. crlf adds
    // the LF a bare-CR guest omits, AFTER masking so it triggers on the real 0x0D.
    const Settings& s = s_settings;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = buf[i];
        if (s.strip7out) c &= 0x7F;
        if (!s.bell && c == 0x07) continue;
        emu_->feed(c, screen_);
        if (s.crlf && c == 0x0D) emu_->feed(0x0A, screen_);
    }
    return n;  // the emulated terminal consumes instantly -- it never back-pressures
}

bool TerminalStream::readable() const { return emu_->hasReply(); }
size_t TerminalStream::rxPending() const { return emu_->replyPending(); }

void TerminalStream::keyAscii(uint8_t b) {
    // Inbound (keyboard -> guest). Fold HERE, not in read(): read() drains the reply
    // FIFO, which also carries the emulator's cursor reports, and masking those would
    // corrupt them. bsdel folds after the case fold, matching FilterStream so neither
    // hides the other. echo shows the transformed key locally, for half-duplex guests.
    clearSelection();  // typing (or a paste) ends a selection, as in a terminal program
    const Settings& s = s_settings;
    if (s.strip7in) b &= 0x7F;
    if (s.upper) b = (uint8_t)std::toupper(b);
    switch (s.bsdel) {
    case BsMap::Bs:  if (b == 0x7F) b = 0x08; break;
    case BsMap::Del: if (b == 0x08) b = 0x7F; break;
    case BsMap::Off: break;
    }
    if (s.echo) emu_->feed(b, screen_);
    emu_->keyAscii(b);
}

std::vector<Property> TerminalStream::properties() {
    std::vector<Property> p;
    Settings* s = &s_settings;  // a pointer into the static -- stable for the lambdas

    auto flag = [&p](const char* name, const char* help, bool* slot) {
        Property x;
        x.name = name;
        x.help = help;
        x.kind = Kind::Bool;
        x.get  = [slot] { return Value::ofBool(*slot); };
        x.set  = [slot](const Value& v, std::string&) {
            *slot = v.b();
            return true;
        };
        p.push_back(std::move(x));
    };

    flag("upper", "Fold keyboard input to uppercase (much period software insists)", &s->upper);
    flag("strip7in", "Mask the high bit on keyboard input", &s->strip7in);
    flag("strip7out", "Mask the high bit on guest output (fixes an even-parity monitor)",
         &s->strip7out);
    flag("crlf", "Add LF after every CR the guest prints. Usually WRONG -- it sends its own",
         &s->crlf);
    flag("echo", "Local echo of keystrokes, for half-duplex hardware", &s->echo);
    flag("bell", "Pass 0x07 through to the terminal", &s->bell);

    {
        Property x;
        x.name    = "bsdel";
        x.help    = "Rubout key: off | bs (fold DEL->BS) | del (fold BS->DEL)";
        x.kind    = Kind::Enum;
        x.choices = {"off", "bs", "del"};
        x.get     = [s] {
            return Value::ofStr(s->bsdel == BsMap::Bs    ? "bs"
                                : s->bsdel == BsMap::Del ? "del"
                                                         : "off");
        };
        x.set = [s](const Value& v, std::string&) {
            s->bsdel = v.s() == "bs" ? BsMap::Bs : v.s() == "del" ? BsMap::Del : BsMap::Off;
            return true;
        };
        p.push_back(std::move(x));
    }

    return p;
}

// The key was `cr = cr | crlf` until 1.3.3; it is now the console's `crlf = on | off`. The
// value types differ, so an alias cannot carry it, and a file that still says `cr` stops with
// the line to write instead of loading with a setting it did not ask for.
bool TerminalStream::set(const std::string& key, const std::string& value, std::string& err) {
    std::string k;
    for (char c : key) k += (char)std::tolower((unsigned char)c);
    if (k == "cr") {
        err = "terminal: 'cr' is now 'crlf' -- write crlf=on for cr=crlf, crlf=off for cr=cr";
        return false;
    }
    return setPropertyIn(properties(), "terminal", key, value, err);
}

void TerminalStream::keySpecial(int key) {
    clearSelection();
    emu_->keySpecial((TerminalEmulator::Key)key);
}

void TerminalStream::setPhosphor(Phosphor p) {
    // Toned-down period palettes: a lit phosphor is not a saturated full-bright colour, and
    // the crt=on linear bloom lifts the apparent brightness further. Green matches the
    // renderer's built-in default; amber is the warm alternative.
    if (p == Phosphor::Amber) {
        renderer_.setPalette({0x05, 0x03, 0x00, 0xFF},   // near-black, warm cast
                             {0xCC, 0x88, 0x14, 0xFF},   // amber, toned down
                             {0x66, 0x44, 0x0A, 0xFF});  // half amber
    } else {
        renderer_.setPalette({0x03, 0x06, 0x03, 0xFF},   // near-black, faint green cast
                             {0x28, 0xC8, 0x52, 0xFF},   // green, ~78% bright
                             {0x14, 0x64, 0x2A, 0xFF});  // half green
    }
}

void TerminalStream::pump() {
    Display* d = s_display;
    if (!d) return;
    // The terminal's window is a keyboard whatever `[display] keyboard` says (issue #767).
    d->claimKeyboard(this);
    if (!renderer_.frameChanged(screen_, d)) return;
    if (!d->wantsFrame()) return;
    // `this` keys this terminal's own window (issue #234), distinct from any video board's;
    // its spec (e.g. "terminal:vt100") titles the window.
    renderer_.render(this, spec_, *d, screen_, videoWidth_);
}

} // namespace altair
