#include "core/expr.h"

#include <cctype>

namespace altair {

// ---------------------------------------------------------------------------
// The AST. Every node is one of: a literal, a register reference (resolved at
// eval time, so its VALUE is always live), or a binary operator over two nodes.
// ---------------------------------------------------------------------------
enum class Op { Num, Reg, Or, And, Eq, Ne, Lt, Gt, Le, Ge, BitAnd, BitOr };

struct Expr::Node {
    Op op;
    uint32_t num = 0;                 // Op::Num
    std::string reg;                  // Op::Reg
    std::shared_ptr<const Node> lhs, rhs;
};

namespace {

using Node = Expr::Node;
using NodeP = std::shared_ptr<const Node>;

NodeP mkNum(uint32_t v) {
    auto n = std::make_shared<Node>();
    n->op = Op::Num;
    n->num = v;
    return n;
}
NodeP mkReg(std::string name) {
    auto n = std::make_shared<Node>();
    n->op = Op::Reg;
    n->reg = std::move(name);
    return n;
}
NodeP mkBin(Op op, NodeP l, NodeP r) {
    auto n = std::make_shared<Node>();
    n->op = op;
    n->lhs = std::move(l);
    n->rhs = std::move(r);
    return n;
}

// A recursive-descent parser. It carries the register predicate so a bare word can
// be classified the instant it is seen, and it stops at the first thing it cannot
// make sense of -- setting `err` and leaving the caller with a null tree.
struct Parser {
    const std::string& s;
    const std::function<bool(const std::string&)>& known;
    size_t i = 0;
    std::string err;

    Parser(const std::string& src, const std::function<bool(const std::string&)>& k)
        : s(src), known(k) {}

    void skip() {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    }
    bool eof() {
        skip();
        return i >= s.size();
    }
    // Peek a fixed operator token without consuming it.
    bool at(const char* tok) {
        skip();
        size_t n = 0;
        while (tok[n]) ++n;
        return s.compare(i, n, tok) == 0;
    }
    bool eat(const char* tok) {
        if (!at(tok)) return false;
        while (*tok) {
            ++i;
            ++tok;
        }
        return true;
    }

    NodeP fail(const std::string& m) {
        if (err.empty()) err = m;
        return nullptr;
    }

    NodeP parseOr() {
        NodeP l = parseAnd();
        if (!l) return nullptr;
        while (eat("||")) {
            NodeP r = parseAnd();
            if (!r) return nullptr;
            l = mkBin(Op::Or, l, r);
        }
        return l;
    }
    NodeP parseAnd() {
        NodeP l = parseCmp();
        if (!l) return nullptr;
        while (eat("&&")) {
            NodeP r = parseCmp();
            if (!r) return nullptr;
            l = mkBin(Op::And, l, r);
        }
        return l;
    }
    NodeP parseCmp() {
        NodeP l = parseBitOr();
        if (!l) return nullptr;
        // At most one comparison per level -- `A==B==C` is a typo, not a chain.
        Op op;
        if (eat("=="))      op = Op::Eq;
        else if (eat("!=")) op = Op::Ne;
        else if (eat("<=")) op = Op::Le;
        else if (eat(">=")) op = Op::Ge;
        else if (eat("<"))  op = Op::Lt;
        else if (eat(">"))  op = Op::Gt;
        else return l;
        NodeP r = parseBitOr();
        if (!r) return nullptr;
        return mkBin(op, l, r);
    }
    NodeP parseBitOr() {
        NodeP l = parseBitAnd();
        if (!l) return nullptr;
        // A single '|', never confused with '||' -- parseOr ate those already, and
        // eat("|") checks the literal one character.
        while (at("|") && !at("||")) {
            eat("|");
            NodeP r = parseBitAnd();
            if (!r) return nullptr;
            l = mkBin(Op::BitOr, l, r);
        }
        return l;
    }
    NodeP parseBitAnd() {
        NodeP l = parsePrim();
        if (!l) return nullptr;
        while (at("&") && !at("&&")) {
            eat("&");
            NodeP r = parsePrim();
            if (!r) return nullptr;
            l = mkBin(Op::BitAnd, l, r);
        }
        return l;
    }
    NodeP parsePrim() {
        if (eof()) return fail("expected a value, found end of expression");
        if (eat("(")) {
            NodeP e = parseOr();
            if (!e) return nullptr;
            if (!eat(")")) return fail("missing )");
            return e;
        }
        // A word: [A-Za-z0-9]+. If it names a register it IS one; otherwise it must
        // be a hex number, and if it is neither it is a typo we name.
        skip();
        size_t start = i;
        while (i < s.size() && std::isalnum((unsigned char)s[i])) ++i;
        if (i == start) return fail(std::string("unexpected '") + s[i] + "'");
        std::string word = s.substr(start, i - start);
        std::string up;
        for (char c : word) up += (char)std::toupper((unsigned char)c);

        if (known(up)) return mkReg(up);

        // Not a register -- it must be hex, whole.
        uint32_t v = 0;
        for (char c : up) {
            int d;
            if (c >= '0' && c <= '9')      d = c - '0';
            else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
            else return fail("unknown register '" + word + "'");
            v = v * 16 + (uint32_t)d;
        }
        return mkNum(v);
    }
};

uint32_t evalNode(const Node& n, const Expr::Resolver& r) {
    switch (n.op) {
    case Op::Num: return n.num;
    case Op::Reg: {
        uint32_t v = 0;
        r(n.reg, v);   // false -> v stays 0; validated at parse time
        return v;
    }
    case Op::Or:  return (evalNode(*n.lhs, r) || evalNode(*n.rhs, r)) ? 1u : 0u;
    case Op::And: return (evalNode(*n.lhs, r) && evalNode(*n.rhs, r)) ? 1u : 0u;
    case Op::Eq:  return evalNode(*n.lhs, r) == evalNode(*n.rhs, r) ? 1u : 0u;
    case Op::Ne:  return evalNode(*n.lhs, r) != evalNode(*n.rhs, r) ? 1u : 0u;
    case Op::Lt:  return evalNode(*n.lhs, r) <  evalNode(*n.rhs, r) ? 1u : 0u;
    case Op::Gt:  return evalNode(*n.lhs, r) >  evalNode(*n.rhs, r) ? 1u : 0u;
    case Op::Le:  return evalNode(*n.lhs, r) <= evalNode(*n.rhs, r) ? 1u : 0u;
    case Op::Ge:  return evalNode(*n.lhs, r) >= evalNode(*n.rhs, r) ? 1u : 0u;
    case Op::BitAnd: return evalNode(*n.lhs, r) & evalNode(*n.rhs, r);
    case Op::BitOr:  return evalNode(*n.lhs, r) | evalNode(*n.rhs, r);
    }
    return 0;
}

} // namespace

std::shared_ptr<const Expr> Expr::parse(const std::string& src,
                                        const std::function<bool(const std::string&)>& known,
                                        std::string& err) {
    Parser p(src, known);
    NodeP root = p.parseOr();
    if (!root) {
        err = p.err.empty() ? "could not parse the condition" : p.err;
        return nullptr;
    }
    if (!p.eof()) {
        err = "trailing text after the condition: '" + src.substr(p.i) + "'";
        return nullptr;
    }
    // Trim the stored text so `describe()` reads back tidily whatever spacing the
    // operator typed.
    std::string t = src;
    size_t a = t.find_first_not_of(" \t");
    size_t b = t.find_last_not_of(" \t");
    if (a != std::string::npos) t = t.substr(a, b - a + 1);
    return std::make_shared<const Expr>(root, t);
}

uint32_t Expr::eval(const Resolver& r) const { return evalNode(*root_, r); }

} // namespace altair
