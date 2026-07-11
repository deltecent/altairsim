// The number base (DESIGN.md 10.1).
//
// PATRICK'S RULE, 2026-07-11:
//   "ON THE WIRE -> HEX.  NEVER ON THE WIRE -> DECIMAL."
//   "Suffixes like K and M are always behind decimal numbers."
//
// The base belongs to the OPERAND, not to the command line. An address, a port
// and a data byte are things the 8080 itself sees, and they are hex. A count, a
// width, a size, a baud rate and a unit number never leave the operator's head,
// and they are decimal.
//
// A single global base was never actually available: `baud=9600` cannot mean
// 38400, so the rule had to bend SOMEWHERE. Given that, it bends where it means
// something.
//
// The failure this catches is the quiet kind. Every one of these tokens parses
// fine under the wrong base -- `20` is a perfectly good number either way -- so
// nothing crashes and nothing complains. You just step 32 times when you asked
// for 20, or size a card at 16K when you wrote 10K, and you find out later.

#include "core/value.h"
#include "test.h"

using namespace altair;

// Parse and return, or -1 if it was rejected.
static long long P(const std::string& s, int base) {
    long long v = 0;
    std::string err;
    return parseNumber(s, v, err, base) ? v : -1;
}

void test_numbers() {
    SECTION("the number base -- on the wire is hex, never on the wire is decimal");

    // ---- the default base is the CALLER's, because only the caller knows ----
    CHECK(P("20", 16) == 0x20, "an address: bare 20 is 32");
    CHECK(P("20", 10) == 20, "a count: bare 20 is twenty");
    CHECK(P("FF", 16) == 255, "a byte needs no 0x to be believed");
    CHECK(P("9600", 10) == 9600, "a baud rate is a baud rate");

    // ---- and an explicit marker overrides it, in BOTH directions ----
    // A rule you cannot type your way out of is a trap, not a rule.
    CHECK(P("#32", 16) == 32, "# forces decimal even where hex is the default");
    CHECK(P("0x20", 10) == 32, "0x forces hex even where decimal is the default");
    CHECK(P("$20", 10) == 32, "$ forces hex");
    CHECK(P("20h", 10) == 32, "a trailing h forces hex");
    CHECK(P("0b1010", 10) == 10, "0b is binary");
    CHECK(P("1_000", 10) == 1000, "underscores are just spacing");

    // ---- a K/M suffix is ALWAYS decimal, and always wins ----
    // This is why nobody has ever had to ask what 10K means.
    CHECK(P("10K", 10) == 10240, "10K is ten K, not sixteen");
    CHECK(P("48K", 10) == 49152, "48K is a full Altair");
    CHECK(P("2M", 10) == 2097152, "M works too");
    CHECK(P("1k", 10) == 1024, "lower case counts");
    // The suffix beats a HEX default -- it brings its own base with it.
    CHECK(P("10K", 16) == 10240, "even asked for hex, 10K is still ten K");

    // `0x10K` demands hex and appends a suffix that is decimal by definition.
    // There is no right answer, so there is no guess: it is REJECTED. Silently
    // resolving this either way would be picking a number for the user and not
    // telling them which one.
    CHECK(P("0x10K", 10) == -1, "0x10K is a contradiction and is refused");
    CHECK(P("$10K", 10) == -1, "so is $10K");
    CHECK(P("10hK", 10) == -1, "and so is 10hK");

    // ---- garbage stays garbage ----
    CHECK(P("", 16) == -1, "nothing is not a number");
    CHECK(P("0x", 16) == -1, "a marker with no digits is not a number");
    CHECK(P("FF", 10) == -1, "FF is NOT a decimal number -- it is an error, not 0");
    CHECK(P("12ZZ", 10) == -1, "trailing garbage is refused, not ignored");

    // The last one deserves its name. strtoull() stops at the first bad character
    // and reports success on what it got, so "12ZZ" would quietly become 12. That
    // is how a typo in a config file turns into a machine that boots wrong.
}
