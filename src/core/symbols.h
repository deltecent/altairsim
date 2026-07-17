#pragma once
//
// Symbol tables for the debugger (DESIGN.md 10.3.2) -- ONE implementation, three
// front ends: the SYMBOLS command, the TOML `startup` that re-loads a file at boot,
// and the MCP symbols tool. A file on disk is parsed by the same code whichever door
// it comes through, so the three cannot drift. Mirrors core/hex.h on purpose.
//
// A symbol is HOST-SIDE STATE, like a breakpoint -- it survives RESET and POWER and
// belongs to no board, because it is the operator's view OF the address space, not a
// fact IN it (DESIGN.md 10.3.2, and machine.h's "no machine-level board state" rule
// does not reach it for exactly that reason).
//
// TWO FORMATS, and the difference is load-bearing:
//
//   .PRN  an assembler LISTING (CP/M ASM, Microsoft M80, DR MAC -- one geometry).
//         It marks an EQU with '=' in column 7, so it can tell a program LABEL from a
//         constant, and only labels feed the reverse (address->name) map.
//   .SYM  the classic CP/M symbol file. A flat, alphabetical list of `HHHH NAME` pairs
//         with NO label/EQU distinction, so it feeds name->value only. DR MAC/RMAC write
//         it (every symbol); Microsoft L80 also writes one, but only with `/N/Y` and only
//         the GLOBALS -- `FOO/N/Y/E` -> FOO.COM + FOO.SYM. (L80's `/M` prints a map to the
//         console instead; that is not a file. Verified against the L80 manual 2026-07-17.)
//
// ABSOLUTE ADDRESSES ONLY. A relocatable M80 listing marks its addresses with a
// trailing apostrophe; loadPrn REFUSES such a file and names the line, because a
// module-relative address referenced as if it were absolute is a silent wrong answer
// (Patrick, 2026-07-17). A .SYM is written after linking and is absolute already.

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace altair {

struct SymbolTable {
    struct Sym {
        uint32_t value = 0;
        bool isAddr = false;    // a program label (feeds byAddr); false for an EQU
        std::string source;     // the file it came from -- SHOW SYMBOLS names it
    };

    // Forward: EVERY symbol, keyed by UPPERCASED name. This is what a reference resolves
    // through, so it is case-insensitive the way a register name is (expr.cpp upcases too).
    std::map<std::string, Sym> byName;

    // Reverse: address -> name, LABELS ONLY. A multimap because M80 truncates names to 6
    // significant characters, so two source labels can land on one address and both should
    // show. Used by display/annotation (deferred); SHOW SYMBOLS reads byName.
    std::multimap<uint32_t, std::string> byAddr;

    // The files loaded, in load order (deduped). CONFIG SAVE re-emits one SYMBOLS LOAD per
    // entry, so it round-trips the FILENAME, not the parsed table (the builtin: rule).
    std::vector<std::string> loadOrder;

    bool lookup(const std::string& name, uint32_t& out) const;  // case-insensitive
    void clear();
    bool empty() const { return byName.empty(); }
    size_t size() const { return byName.size(); }

    // Merge one symbol, maintaining byAddr and counting a redefinition. `isAddr` decides
    // whether it also enters the reverse map. Not usually called directly -- loadPrn/loadSym do.
    struct LoadStats;
    void put(const std::string& name, uint32_t value, bool isAddr,
             const std::string& source, LoadStats& st);

    // What a load did, for the one-line operator summary.
    struct LoadStats {
        int added = 0;
        int redefined = 0;
        std::vector<std::string> redefinedNames;   // the first few, for the message
    };
};

// True if this looks like a CP/M .SYM (`HHHH NAME` at column 1) rather than a .PRN
// listing (address at column 2, source at column 17). Backs autodetect when the
// extension does not decide; the SYMBOLS command tries the extension first.
bool looksLikeSym(std::span<const uint8_t> data);

// Merge an assembler LISTING into `t`. `source` is the name SHOW SYMBOLS prints.
// `replace` clears the table first. Returns false and fills `err` (naming the line)
// ONLY on a hard format error -- a relocatable address. A listing this parser does not
// recognise simply yields no symbols (st.added == 0); the caller says so out loud, it
// is not an error.
bool loadPrn(std::span<const uint8_t> text, const std::string& source,
             SymbolTable& t, bool replace, SymbolTable::LoadStats& st, std::string& err);

// Merge a CP/M .SYM into `t`. Same contract; a .SYM has no relocation marker and no
// EQU/label distinction, so every symbol enters byName only.
bool loadSym(std::span<const uint8_t> text, const std::string& source,
             SymbolTable& t, bool replace, SymbolTable::LoadStats& st, std::string& err);

} // namespace altair
