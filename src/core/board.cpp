#include "core/board.h"

#include <cctype>

namespace altair {

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Local spelling, so the twenty call sites below read the way they always did.
static std::string lower(std::string s) { return lowerAscii(std::move(s)); }

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
    // ...and this is what lets SHOW BUS IRQ find it. Every strap in the machine is
    // born here, so one flag here finds them all -- see Property::irqJumper.
    x.irqJumper = true;
    x.get     = [&j] {
        switch (j) {
        case IrqJumper::None: return Value::ofStr("none");
        case IrqJumper::Int:  return Value::ofStr("int");
        default:
            return Value::ofStr("vi" + std::to_string((int)j - (int)IrqJumper::Vi0));
        }
    };
    x.set = [&j](const Value& v, std::string&) {
        j = irqJumperFromText(v.s());
        return true;
    };
    return x;
}

// DOES THIS KEY NAME THIS PROPERTY? The one answer, for every path that takes a key from
// a human or a file -- SET, a unit's SET, the console's, and a [[board.<table>]] entry.
//
// Two rules, and both are already load-bearing elsewhere: a key is CASE-INSENSITIVE (the
// same rule board ids obey), and a key may be one of the property's other spellings
// (Property::aliases). Neither is allowed to be a local string compare in one caller,
// because the value of the reflection layer is that these four paths cannot disagree
// about what is legal -- see setOne() below and Board::loadSubUnit().
static bool named(const Property& p, const std::string& key) {
    std::string k = lower(key);
    if (lower(p.name) == k) return true;
    for (const auto& a : p.aliases)
        if (lower(a) == k) return true;
    return false;
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
    for (const auto& x : props)
        if (named(x, key)) p = &x;

    if (!p) {
        err = who + " has no property '" + key + "'. Known:";
        for (const auto& x : props) err += " " + x.name;
        return false;
    }

    // A PROPERTY WITH NO SETTER IS SOMETHING THE CARD KNOWS, NOT SOMETHING YOU CHOSE.
    // The 6850's `lines` reports what /DCD and /CTS are doing right now -- and you
    // cannot SET what the far end is doing, any more than you can set the temperature
    // by moving the thermometer. A memory card's `pages` is the same kind of fact,
    // arrived at from the other direction: it is DERIVED from the regions you declared.
    //
    // So the sentence is the PROPERTY'S OWN HELP, and not a fixed one. It used to read
    // "it reports a pin, not a jumper", which is exactly right for `lines` and simply
    // untrue of `pages` -- the hazard of writing a generic refusal while looking at one
    // example of the thing it refuses.
    //
    // Refusing here, in the ONE property path, is what makes read-only work everywhere
    // at once: SET says this, CONFIG SAVE already skips them (config/toml.cpp), MCP gets
    // it free, and the manual's generated reference marks them read-only off the same
    // signal. There is no second rule to keep in step -- which is why a card must say
    // "no setter" and NOT install a setter that always fails.
    if (!p->set) {
        err = who + ": '" + p->name + "' is read-only -- " + p->help;
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

// THE ONE DOOR INTO A SUB-UNIT TABLE (DESIGN.md 5). Same six checks as setOne() above,
// against the same Property vocabulary, from the same declaration -- which is the whole
// point: `readonly = maybe` in a machine file and `SET dsk0 ...` at the monitor now fail
// for the same reason, in the same words, out of the same list.
//
// WHAT IT DOES NOT DO IS SET ANYTHING. There is nothing to set: the drive does not exist
// yet. It validates, and hands the board the raw text to build from -- so the board still
// parses its own values (`at` is hex, `size` takes a K, a path is a path), and the radix
// rule lives where it always did, in the property's own declaration.
bool Board::loadSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    bool known = false;
    for (const auto& t : subUnitTables())
        if (t == table) known = true;
    if (!known) {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    auto schema = subUnitProperties(table);

    // WHAT THE BOARD WILL BE HANDED: the same pairs, under the schema's own spellings.
    // An alias is resolved HERE and nowhere else, so addSubUnit() below still compares
    // against one string per key and a board that gains an alias gains no code at all.
    KeyValues canon;
    // ...and how each of those keys was actually SPELLED in the file, for the one error
    // message below that has to quote the reader's own words back.
    std::vector<std::string> written;

    for (const auto& [k, text] : kv) {
        const Property* p = nullptr;
        for (const auto& x : schema)
            if (named(x, k)) p = &x;

        // A KEY THE BOARD NEVER DECLARED. Say what it does take -- the reason this bug
        // was worth fixing is that `readonly` existed and nobody could find it, so a
        // refusal that lists the alternatives is most of the cure.
        // ...and the message NAMES THE CARD, because the same table means different things
        // on different cards: `media = "minidisk"` is right on an 88-MDS and wrong on a
        // DCDD, and "must be one of: 8in fdc8mb" is only half an explanation without it.
        std::string where = type() + ": [[board." + table + "]] ";

        if (!p) {
            std::string legal;
            for (const auto& x : schema) {
                if (!legal.empty()) legal += ", ";
                legal += x.name;
            }
            err = where + "has no `" + k + "`";
            if (!legal.empty()) err += " -- it takes " + legal;
            return false;
        }

        Value v;
        if (!parseValue(text, p->kind, v, err, p->radix)) {
            err = where + p->name + ": " + err;
            return false;
        }
        // validate()'s own message already names the key and quotes what it got.
        if (!validate(*p, v, err)) {
            err = where + err;
            return false;
        }

        // ONE KEY, TWO SPELLINGS, IN THE SAME TABLE. TOML itself refuses a repeated key,
        // so `readonly` beside `writeprotect` is the only way to write the same drive
        // setting twice -- and whichever we then honoured, the file would be saying two
        // things and only one of them would happen. There is no reading of that file that
        // is safe to guess at, so say so instead.
        // NAME BOTH KEYS AS THE FILE WROTE THEM. `canon` has already had the first one
        // renamed, so reporting out of it would say "`readonly` as well as `readonly`" and
        // send the reader looking for a duplicate that is not there.
        for (size_t i = 0; i < canon.size(); i++)
            if (canon[i].first == p->name) {
                err = where + "says `" + k + "` as well as `" + written[i] +
                      "`, which are two spellings of one key -- use either, not both";
                return false;
            }

        canon.emplace_back(p->name, text);
        written.push_back(k);
    }

    return addSubUnit(table, canon, err);
}

} // namespace altair
