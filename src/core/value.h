#pragma once
//
// Value + Property -- the reflection layer (DESIGN.md 5).
//
// This is the keystone of the whole program. The monitor's SET/SHOW, the TOML
// loader, CONFIG SAVE, the MCP tool schemas, and tab completion are ALL written
// once against Board::properties() and know nothing board-specific. A board
// added next year is configurable, scriptable, agent-drivable and completable
// the day it lands, with no change to any of those five consumers.
//
// The cost of that is this file: a property has to carry enough metadata to
// validate, render, and describe itself. That is the whole trick.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace altair {

enum class Kind { Int, Bool, Str, Enum };

// A property value. Deliberately small and non-generic -- Int/Bool/Str/Enum is
// every type a period S-100 board setting has ever needed.
class Value {
public:
    Value() = default;
    static Value ofInt(long long v);
    static Value ofBool(bool v);
    static Value ofStr(std::string v);

    Kind kind() const { return kind_; }
    long long i() const { return i_; }
    bool b() const { return i_ != 0; }
    const std::string& s() const { return s_; }

    // Render for SHOW / CONFIG SAVE. `radix` is the property's display radix.
    std::string text(int radix = 10) const;

private:
    Kind kind_ = Kind::Int;
    long long i_ = 0;
    std::string s_;
};

// Parse text into a Value of the given kind. Accepts 0x/0b/H suffix forms and
// underscores for Int. Returns false and sets `err` on bad input -- a bad SET
// must say why, not half-apply.
// `radix` is the property's own -- 16 for a port or an address, 10 for a baud
// rate. It is the DEFAULT base only; an explicit 0x/#/K marker still overrides.
bool parseValue(const std::string& text, Kind kind, Value& out, std::string& err, int radix = 10);

// THE ONE NUMBER PARSER. The monitor, the TOML loader, the property layer and
// every board go through this -- there is no second one, and no call site is
// allowed to pre-chew its input with a string hack to get the base it wanted.
//
//   ON THE WIRE -> HEX.  NEVER ON THE WIRE -> DECIMAL.  (Patrick, 2026-07-11)
//
// The base is a property of the OPERAND, so the CALLER supplies the default and
// the caller is the only one who knows: an address, a port or a data byte is hex
// (the 8080 sees it); a count, a size, a baud rate or a unit number is decimal
// (it never leaves the operator's head). Properties carry theirs as `radix`.
//
// Notations, all understood regardless of the default, in both directions:
//   0xFF  $FF  FFh  0FFH   force hex        #255   forces decimal
//   0b1010  binary          1_000  digit separators
//   48K  2M                 a SIZE SUFFIX IS ALWAYS DECIMAL and always wins,
//                           so `0x10K` is a contradiction and is REJECTED.
bool parseNumber(const std::string& text, long long& out, std::string& err, int base = 10);

struct Property {
    std::string name;                   // "baud", "phantom", "honors_phantom"
    std::string help;                   // one line, shown by SHOW
    Kind kind = Kind::Int;

    std::vector<std::string> choices;   // Kind::Enum -- also feeds tab completion
    long long min = 0, max = 0;         // Kind::Int; min==max means unbounded
    int radix = 10;                     // 16 for addresses, so SHOW reads right
    std::string unit;                   // "Hz", "bytes" -- display only

    // OTHER SPELLINGS THAT ARE ACCEPTED, AND NEVER WRITTEN.
    //
    // `name` stays the one true spelling: it is what SHOW prints, what the generated
    // reference tables, what CONFIG SAVE writes back, and what the board's own code
    // compares against. An alias is accepted AT THE DOOR and canonicalised there, so a
    // board cannot learn that its key has two names and no two consumers can disagree
    // about which one is real. That is the whole reason this is one field on the schema
    // rather than an extra string compare in each board.
    //
    // It exists because the operator's vocabulary and the file's drifted apart: a drive
    // is WRITE-PROTECTED everywhere a person is spoken to -- the CLI, SHOW MOUNTS, the
    // manual, the tab on a real diskette -- and `readonly = true` in the file. Neither
    // spelling is wrong, so the fix is to accept both rather than to break the one that
    // is in every machine file that already exists.
    //
    // Use it SPARINGLY. Two spellings for one key is a small tax on everybody reading
    // somebody else's file; it is worth paying to reconcile vocabulary we already ship,
    // and not worth paying to save a reader a trip to the reference.
    //
    // ACCEPTING is generic (Board::loadSubUnit, setOne); DISPLAYING is per-consumer, and
    // today the only aliased key is a sub-unit one, so the generated reference, the MCP
    // schema and SHOW's `[[board.<table>]]` listing print aliases and Monitor::showProps
    // does not. Put an alias on a SETTABLE property and that is the fourth place to teach.
    std::vector<std::string> aliases;

    // "I AM AN INTERRUPT STRAP." Set by irqJumperProperty() and by nothing else.
    //
    // The eight VI lines are the one part of the backplane you cannot see, and a
    // strap that lands nowhere fails in total silence. SHOW BUS IRQ has to find
    // every strap in the machine to say so -- and since every one of them is born
    // in irqJumperProperty(), marking that single function marks all of them, on
    // boards that do not exist yet, without a board-type list anywhere.
    bool irqJumper = false;

    std::function<Value()> get;
    // Return false + err to reject. The board validates what only it can know;
    // kind/choices/range are checked generically before this is ever called.
    std::function<bool(const Value&, std::string& err)> set;
};

// Generic validation, used identically by SET, the TOML loader, and MCP.
// This is why those three cannot disagree about what is legal.
bool validate(const Property& p, const Value& v, std::string& err);

} // namespace altair
