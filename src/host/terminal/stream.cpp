#include "host/terminal/stream.h"

#include "host/display.h"
#include "host/terminal/emulator.h"
#include "host/terminal/font.h"

#include <cctype>
#include <utility>

namespace altair {

Display*                  TerminalStream::s_display   = nullptr;
const TerminalFont*       TerminalStream::s_font      = nullptr;
TerminalStream*           TerminalStream::s_keyTarget = nullptr;
TerminalStream::Settings  TerminalStream::s_settings;

TerminalStream::TerminalStream(std::string spec, int rows, int cols,
                               std::unique_ptr<TerminalEmulator> emu)
    : spec_(std::move(spec)), screen_(rows, cols), emu_(std::move(emu)) {
    renderer_.setFont(s_font);  // borrowed, session-lifetime; null just paints nothing
    s_keyTarget = this;         // the newest terminal claims the one host keyboard
}

TerminalStream::~TerminalStream() {
    // Only relinquish the keyboard if it is still ours: on a CONFIG LOAD the replacement
    // line is constructed (claiming the target) before the old one is destroyed, so a
    // blind clear here would strand the new terminal (see [[altairsim-config-load-replaces]]).
    if (s_keyTarget == this) s_keyTarget = nullptr;
}

bool TerminalStream::hasWindow() { return s_display && s_display->isWindowed(); }

size_t TerminalStream::read(uint8_t* buf, size_t n) { return emu_->takeReply(buf, n); }

size_t TerminalStream::write(const uint8_t* buf, size_t n) {
    // Outbound (guest -> screen): strip7out first, so an even-parity 0x8D becomes the
    // 0x0D the emulator homes on; then drop a bell if muted, then feed. cr=crlf adds
    // the LF a bare-CR guest omits, AFTER masking so it triggers on the real 0x0D.
    const Settings& s = s_settings;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = buf[i];
        if (s.strip7out) c &= 0x7F;
        if (!s.bell && c == 0x07) continue;
        emu_->feed(c, screen_);
        if (s.cr == Settings::Cr::CrLf && c == 0x0D) emu_->feed(0x0A, screen_);
    }
    return n;  // the emulated terminal consumes instantly -- it never back-pressures
}

bool TerminalStream::readable() const { return emu_->hasReply(); }

void TerminalStream::keyAscii(uint8_t b) {
    // Inbound (keyboard -> guest). Fold HERE, not in read(): read() drains the reply
    // FIFO, which also carries the emulator's cursor reports, and masking those would
    // corrupt them. bsdel folds after the case fold, matching FilterStream so neither
    // hides the other. echo shows the transformed key locally, for half-duplex guests.
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

    {
        Property x;
        x.name    = "cr";
        x.help    = "Guest CR handling: cr (pass through) | crlf (add LF after every CR)";
        x.kind    = Kind::Enum;
        x.choices = {"cr", "crlf"};
        x.get     = [s] { return Value::ofStr(s->cr == Settings::Cr::CrLf ? "crlf" : "cr"); };
        x.set     = [s](const Value& v, std::string&) {
            s->cr = v.s() == "crlf" ? Settings::Cr::CrLf : Settings::Cr::Cr;
            return true;
        };
        p.push_back(std::move(x));
    }

    return p;
}

void TerminalStream::keySpecial(int key) {
    emu_->keySpecial((TerminalEmulator::Key)key);
}

void TerminalStream::pump() {
    Display* d = s_display;
    if (!d) return;
    if (!renderer_.frameChanged(screen_, d)) return;
    if (!d->wantsFrame()) return;
    // `this` keys this terminal's own window (issue #234), distinct from any video board's;
    // its spec (e.g. "terminal:vt100") titles the window.
    renderer_.render(this, spec_, *d, screen_, videoWidth_);
}

} // namespace altair
