#pragma once
//
// A tiny expression, for BREAK <addr> IF <expr> (DESIGN.md 10, the DEBUG block).
//
// IT IS CPU-AGNOSTIC, ON PURPOSE. It resolves a register's VALUE by name through a
// callback and never learns what an 8080 is -- the same bet registers()-as-
// reflection already makes (DESIGN.md 3.0.3), so a Z80 gets conditional
// breakpoints on the day it lands with nothing written here.
//
// Grammar (|| binds loosest, a primary tightest):
//
//     or   := and  ('||' and)*
//     and  := cmp  ('&&' cmp)*
//     cmp  := bor  (('=='|'!='|'<='|'>='|'<'|'>') bor)?
//     bor  := band ('|' band)*
//     band := prim ('&' prim)*
//     prim := NUMBER | REGISTER | '(' or ')'
//
// A NUMBER is HEX -- it is a value the guest could hold, and those are hex on the
// wire (DESIGN.md 10.0.1). A bare word that NAMES a register IS that register, so
// `A` is the accumulator and the literal ten is written `0A` -- exactly as
// `(A&0F)==0` writes fifteen. The leading zero is how you say "I meant the number".

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace altair {

class Expr {
public:
    // Read a register's current value by name. false if there is no such register.
    using Resolver = std::function<bool(const std::string& name, uint32_t& out)>;

    // Read a loaded symbol's value by name. Optional -- when set, a bare word that is not
    // a register but IS a symbol becomes that CONSTANT at parse time (folded to a number,
    // never a live read), so a `.PRN`/`.SYM` name works in `BREAK 200 IF HL==STACK`. A
    // register still wins over a symbol, and a symbol still wins over a bare hex literal
    // (the leading-zero escape forces the number either way).
    using Symbols = std::function<bool(const std::string& name, uint32_t& out)>;

    // Parse `src`. `known(name)` decides whether a bare word is a register -- so a
    // typo is caught HERE, at entry, and so `A` the register is told apart from
    // `0A` the number. Returns null and fills `err` on any failure. `symbols` is
    // optional (see above).
    static std::shared_ptr<const Expr> parse(
        const std::string& src, const std::function<bool(const std::string&)>& known,
        std::string& err, const Symbols& symbols = {});

    // A comparison yields 1 or 0; && and || are logical over nonzero; & and | are
    // bitwise. A name that no longer resolves reads as 0 (it was checked at parse
    // time, so this only bites if the CPU itself changed underneath the breakpoint).
    uint32_t eval(const Resolver&) const;

    const std::string& text() const { return text_; }

    struct Node;   // opaque; the AST lives in the .cpp
    Expr(std::shared_ptr<const Node> root, std::string text)
        : root_(std::move(root)), text_(std::move(text)) {}

private:
    std::shared_ptr<const Node> root_;
    std::string text_;
};

} // namespace altair
