#include "core/board.h"

#include <cctype>

namespace altair {

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const char* unitKindName(UnitKind k) {
    switch (k) {
    case UnitKind::Disk:   return "disk";
    case UnitKind::Rom:    return "rom";
    case UnitKind::Serial: return "serial";
    case UnitKind::Tape:   return "tape";
    }
    return "?";
}

// A unit's name is the board's, and the board is the only one who knows them.
// Nothing here guesses, and nothing accepts an index.
bool Board::findUnit(const std::string& name, UnitDef& out) const {
    std::string n = lower(name);
    for (const auto& u : units())
        if (lower(u.name) == n) {
            out = u;
            return true;
        }
    return false;
}

bool setProperty(Board& b, const std::string& key, const std::string& text, bool running,
                 std::string& err) {
    auto props = b.properties();
    const Property* p = nullptr;
    std::string k = lower(key);
    for (const auto& x : props)
        if (lower(x.name) == k) p = &x;

    if (!p) {
        err = b.id + " has no property '" + key + "'. Known:";
        for (const auto& x : props) err += " " + x.name;
        return false;
    }

    // A config-time property set on a RUNNING machine is rejected outright, not
    // half-applied. Half-applying it is how you get a machine whose SHOW output
    // is a lie (DESIGN.md 10.1).
    if (running && !p->runtime) {
        err = p->name + " is config-time only; the machine is running. STOP first.";
        return false;
    }

    // The property's own `radix` IS the rule: 16 for the things the machine sees
    // (a port, an address), 10 for the things only the operator sees (a baud rate,
    // a count). It is handed to the one number parser as the default base -- this
    // used to prepend "0x" to the string and hope, which is the sort of thing that
    // works until somebody types `10K`.
    Value v;
    if (!parseValue(text, p->kind, v, err, p->radix)) return false;
    if (!validate(*p, v, err)) return false;
    return p->set(v, err);
}

} // namespace altair
