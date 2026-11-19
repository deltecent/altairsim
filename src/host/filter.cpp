#include "host/filter.h"

#include <cctype>

namespace altair {

// Inbound: the endpoint's bytes, on their way to the guest.
size_t FilterStream::read(uint8_t* buf, size_t n) {
    size_t got = inner_->read(buf, n);

    for (size_t i = 0; i < got; ++i) {
        uint8_t c = buf[i];

        if (s_->strip7in) c &= 0x7F;

        // Fold BEFORE the rubout mapping, so neither can hide the other.
        if (s_->upper) c = (uint8_t)std::toupper(c);

        switch (s_->bsmap) {
        case BsMap::Bs:  if (c == 0x7F) c = 0x08; break;
        case BsMap::Del: if (c == 0x08) c = 0x7F; break;
        case BsMap::Off: break;
        }

        buf[i] = c;
    }

    // Local echo, for half-duplex hardware -- the character goes back out the way
    // it came in. Note it echoes the TRANSFORMED byte: the operator should see
    // what the guest is about to see, not what the keyboard sent, or the screen
    // and the program will disagree about what was typed.
    if (s_->echo && got) inner_->write(buf, got);

    return got;
}

// Outbound: the guest's bytes, on their way to the endpoint.
size_t FilterStream::doWrite(const uint8_t* buf, size_t n) {
    // Nothing to do? Don't copy. The overwhelmingly common case is a guest
    // printing 7-bit ASCII with its own CRLFs, and it should cost nothing.
    if (!s_->strip7out && !s_->crlf && s_->bell) return inner_->write(buf, n);

    std::string out;
    out.reserve(n + 8);

    for (size_t i = 0; i < n; ++i) {
        uint8_t c = buf[i];

        if (s_->strip7out) c &= 0x7F;
        if (!s_->bell && c == 0x07) continue;

        out += (char)c;

        // CR -> CRLF. OFF BY DEFAULT and it must stay that way: period software
        // overwhelmingly sends its own LF (ALTMON's banner is literally
        // `db CR,LF,LF`), so turning this on by default would double-space
        // everything the machine ever printed and look like a bug in the guest.
        if (s_->crlf && c == 0x0D) out += '\n';
    }

    if (inner_->write((const uint8_t*)out.data(), out.size()) != out.size()) return 0;
    return n;  // we consumed all of the GUEST's bytes, whatever we made of them
}

std::vector<Property> FilterStream::properties() {
    std::vector<Property> p;

    auto flag = [&p](const char* name, const char* help, bool* slot) {
        Property x;
        x.name    = name;
        x.help    = help;
        x.kind    = Kind::Bool;
        x.get     = [slot] { return Value::ofBool(*slot); };
        x.set     = [slot](const Value& v, std::string&) {
            *slot = v.b();
            return true;
        };
        p.push_back(std::move(x));
    };

    flag("upper", "Fold keyboard input to uppercase (much period software insists)", &own_.upper);
    flag("strip7in", "Mask the high bit on input", &own_.strip7in);
    flag("strip7out", "Mask the high bit on output", &own_.strip7out);
    flag("crlf", "Add LF after every CR the guest prints. Usually WRONG -- it sends its own",
         &own_.crlf);
    flag("echo", "Local echo, for half-duplex hardware", &own_.echo);
    flag("bell", "Pass 0x07 through to the host bell", &own_.bell);

    {
        Property x;
        x.name    = "bsdel";
        x.help    = "Rubout key: off | bs (fold DEL->BS) | del (fold BS->DEL)";
        x.kind    = Kind::Enum;
        x.choices = {"off", "bs", "del"};
        x.get     = [this] {
            return Value::ofStr(own_.bsmap == BsMap::Bs    ? "bs"
                                : own_.bsmap == BsMap::Del ? "del"
                                                       : "off");
        };
        x.set = [this](const Value& v, std::string&) {
            own_.bsmap = v.s() == "bs" ? BsMap::Bs : v.s() == "del" ? BsMap::Del : BsMap::Off;
            return true;
        };
        p.push_back(std::move(x));
    }

    return p;
}

} // namespace altair
