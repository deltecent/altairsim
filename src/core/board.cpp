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

// THE `interrupt` STRAP, FOR EVERY BOARD THAT HAS ONE.
//
// Ten choices, one spelling, one place. The 2SIO has two of these straps (one per
// 6850) and the 88-SIO has two (one for its input device, one for its output
// device, and the manual is explicit that they may sit at different VI
// priorities) -- so this was going to be copied four times before the first disk
// controller even arrived.
Property irqJumperProperty(std::string name, std::string help, IrqJumper& j) {
    Property x;
    x.name    = std::move(name);
    x.help    = std::move(help);
    x.kind    = Kind::Enum;
    x.choices = {"none", "int", "vi0", "vi1", "vi2", "vi3", "vi4", "vi5", "vi6", "vi7"};
    x.get     = [&j] {
        switch (j) {
        case IrqJumper::None: return Value::ofStr("none");
        case IrqJumper::Int:  return Value::ofStr("int");
        default:
            return Value::ofStr("vi" + std::to_string((int)j - (int)IrqJumper::Vi0));
        }
    };
    x.set = [&j](const Value& v, std::string&) {
        const std::string& s = v.s();
        if (s == "none") j = IrqJumper::None;
        else if (s == "int") j = IrqJumper::Int;
        else j = (IrqJumper)((int)IrqJumper::Vi0 + (s[2] - '0'));
        return true;
    };
    return x;
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
                   const std::string& text, std::string& err) {
    const Property* p = nullptr;
    std::string k = lower(key);
    for (const auto& x : props)
        if (lower(x.name) == k) p = &x;

    if (!p) {
        err = who + " has no property '" + key + "'. Known:";
        for (const auto& x : props) err += " " + x.name;
        return false;
    }

    // A PROPERTY WITH NO SETTER IS A PIN, NOT A JUMPER. The 6850's `lines` reports
    // what /DCD and /CTS are doing right now -- and you cannot SET what the far end
    // is doing, any more than you can set the temperature by moving the thermometer.
    //
    // Refusing here, in the ONE property path, is what makes read-only work
    // everywhere at once: SET says this sentence, CONFIG SAVE already skips them
    // (config/toml.cpp), and MCP gets it for free. There is no second rule to keep in
    // step.
    if (!p->set) {
        err = who + ": '" + p->name + "' is read-only -- it reports a pin, not a jumper";
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
                     const std::string& text, std::string& err) {
    bool ok = setOne(b.unitProperties(unit), b.id + ":" + unit, key, text, err);
    if (ok) b.configChanged();
    return ok;
}

// The console is not a Board -- it is the host's keyboard -- but it has
// properties and must obey exactly the same rules about them. Hence this: the
// same path, over a property list from anywhere at all.
bool setPropertyIn(std::vector<Property> props, const std::string& who, const std::string& key,
                   const std::string& text, std::string& err) {
    return setOne(std::move(props), who, key, text, err);
}

// EVERY successful set tells the board to re-settle, without asking what was set.
//
// `port`, `phantom`, `honors_phantom`, `bank_type` and `enabled` rewire the card's
// decode; `interrupt` moves the wire its IRQ is soldered to; `baud` and `connect`
// move a deadline it has already set. `upper` does none of the above. WE DO NOT
// TRY TO TELL THEM APART, and that is deliberate: the distinction is per-board
// knowledge, it would have to be kept in step by hand forever, and getting it
// wrong is SILENT -- a stale decode table that answers from the wrong board, or an
// interrupt that never arrives. A needless re-settle costs microseconds, and this
// path is an operator typing at a prompt. Buy the safety; it is not expensive.
bool setProperty(Board& b, const std::string& key, const std::string& text, std::string& err) {
    bool ok = setOne(b.properties(), b.id, key, text, err);
    if (ok) b.configChanged();
    return ok;
}

} // namespace altair
