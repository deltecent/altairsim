#include "test.h"

#include "core/expr.h"

#include <map>

using namespace altair;

namespace {

// A stand-in for a CPU's reflected registers -- the evaluator never learns it is
// not a real one, which is the whole point (DESIGN.md 3.0.3).
struct Regs {
    std::map<std::string, uint32_t> v{
        {"A", 0x00}, {"B", 0x00}, {"F", 0x00}, {"HL", 0x8000}, {"Z", 1}, {"PC", 0x0100},
    };
    std::function<bool(const std::string&)> known() const {
        return [this](const std::string& n) { return v.count(n) != 0; };
    }
    Expr::Resolver resolver() const {
        return [this](const std::string& n, uint32_t& out) {
            auto it = v.find(n);
            if (it == v.end()) return false;
            out = it->second;
            return true;
        };
    }
};

// Parse and evaluate in one breath; asserts the parse succeeded.
uint32_t val(const Regs& r, const std::string& src) {
    std::string err;
    auto e = Expr::parse(src, r.known(), err);
    if (!e) {
        std::printf("  (parse failed: %s -- for '%s')\n", err.c_str(), src.c_str());
        return 0xDEADBEEF;
    }
    return e->eval(r.resolver());
}

bool parses(const Regs& r, const std::string& src) {
    std::string err;
    return Expr::parse(src, r.known(), err) != nullptr;
}

} // namespace

void test_expr() {
    SECTION("the expression evaluator -- registers by name, hex by value, CPU-agnostic");

    Regs r;

    // A bare word that names a register IS that register; a number is hex, and the
    // literal ten is `0A` because `A` is taken.
    CHECK(val(r, "A") == 0x00, "A reads the register");
    CHECK(val(r, "HL") == 0x8000, "HL reads the pair");
    CHECK(val(r, "0A") == 0x0A, "0A is the number ten, not the accumulator");
    CHECK(val(r, "0F") == 0x0F, "0F is fifteen -- the leading zero is how you say 'a number'");
    CHECK(val(r, "8000") == 0x8000, "a bare 8000 is hex");

    SECTION("comparisons yield 1 or 0");

    CHECK(val(r, "A==0") == 1, "A is zero");
    CHECK(val(r, "A==1") == 0, "and not one");
    CHECK(val(r, "HL==8000") == 1, "HL equals 8000");
    CHECK(val(r, "HL!=0") == 1, "and is not zero");
    CHECK(val(r, "HL>=8000") == 1, ">=");
    CHECK(val(r, "HL>8000") == 0, ">");
    CHECK(val(r, "HL<=8000") == 1, "<=");
    CHECK(val(r, "A<HL") == 1, "a register on both sides");

    SECTION("&& and || combine; & and | mask");

    CHECK(val(r, "A==0 && Z==1") == 1, "both hold");
    CHECK(val(r, "A==0 && Z==0") == 0, "one does not");
    CHECK(val(r, "A==1 || Z==1") == 1, "either holds");
    CHECK(val(r, "A==1 || Z==0") == 0, "neither");
    CHECK(val(r, "HL & 00FF") == 0x00, "bitwise and, low byte of 8000 is zero");
    CHECK(val(r, "HL & F000") == 0x8000, "bitwise and, high nibble");
    CHECK(val(r, "A | F0") == 0xF0, "bitwise or");

    SECTION("precedence and parentheses");

    // Bitwise binds tighter than comparison, so this holds WITHOUT the parens -- but
    // the parenthesized form is how DESIGN.md writes it, and it must mean the same.
    CHECK(val(r, "(A&0F)==0") == 1, "(A&0F)==0 with A zero");
    CHECK(val(r, "A&0F==0") == 1, "and the same without parens, since & binds tighter");
    CHECK(val(r, "HL==8000 && A==0 || Z==0") == 1, "&& binds tighter than ||");
    CHECK(val(r, "A==0 || A==1 && Z==0") == 1, "A==0 wins the || before the && drags it down");

    SECTION("errors are caught at parse time, and named");

    CHECK(!parses(r, "A=="), "a dangling operator is rejected");
    CHECK(!parses(r, "FOO==1"), "an unknown register is rejected -- not silently a number");
    CHECK(!parses(r, "(A==0"), "an unclosed paren is rejected");
    CHECK(!parses(r, "A==0 garbage"), "trailing text is rejected");
    CHECK(!parses(r, ""), "and so is nothing at all");

    // The stored text is normalized whitespace, for describe().
    std::string err;
    auto e = Expr::parse("  A==0  ", r.known(), err);
    CHECK(e && e->text() == "A==0", "the round-tripped text is trimmed");

    // Case-insensitive, exactly as SET REG and the status line are.
    CHECK(val(r, "hl==8000") == 1, "a lowercase register name still resolves");
}
