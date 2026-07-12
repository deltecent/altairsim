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
    case UnitKind::Cpu:    return "cpu";
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

// The one property-setting path, over a list of properties from wherever.
//
// `who` is only ever used to write a good error message -- "sio0:a has no
// property 'bawd'" rather than "unknown property". That is the entire reason it
// is a parameter: a board, a unit and the console all need the same six checks
// in the same order, and the only thing that differs between them is what you
// call the thing that refused.
static bool setOne(std::vector<Property> props, const std::string& who, const std::string& key,
                   const std::string& text, bool running, std::string& err) {
    const Property* p = nullptr;
    std::string k = lower(key);
    for (const auto& x : props)
        if (lower(x.name) == k) p = &x;

    if (!p) {
        err = who + " has no property '" + key + "'. Known:";
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

// A UNIT's property -- `SET sio0:a BAUD=9600` (DESIGN.md 7.2). Same six checks,
// same one parser, same radix rule. There is no second schema and no second
// validator, which is why a unit property cannot drift from a board property in
// what it accepts.
bool setUnitProperty(Board& b, const std::string& unit, const std::string& key,
                     const std::string& text, bool running, std::string& err) {
    return setOne(b.unitProperties(unit), b.id + ":" + unit, key, text, running, err);
}

// The console is not a Board -- it is the host's keyboard -- but it has
// properties and must obey exactly the same rules about them. Hence this: the
// same path, over a property list from anywhere at all.
bool setPropertyIn(std::vector<Property> props, const std::string& who, const std::string& key,
                   const std::string& text, bool running, std::string& err) {
    return setOne(std::move(props), who, key, text, running, err);
}

bool setProperty(Board& b, const std::string& key, const std::string& text, bool running,
                 std::string& err) {
    return setOne(b.properties(), b.id, key, text, running, err);
}

} // namespace altair
