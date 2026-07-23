#include "test.h"

#include "core/symbols.h"

#include <cstring>
#include <string>

using namespace altair;

static std::span<const uint8_t> sv(const std::string& s) {
    return std::span<const uint8_t>((const uint8_t*)s.data(), s.size());
}

// A .PRN line with the verified geometry: col 1 space, cols 2-5 the 4-hex address,
// col 6 space, cols 7-16 the object field (an EQU puts '=' here), col 17+ the source.
static std::string prn(const char* addr4, const char* field, const char* src) {
    std::string f = field;
    f.resize(10, ' ');   // pad the object field to fill columns 7-16
    return std::string(" ") + addr4 + " " + f + src + "\r\n";
}

void test_symbols() {
    SECTION("symbol tables (DESIGN.md 10.3.2)");

    // ---- .PRN: the EQU rule is the whole game ----
    {
        // START is a real label (feeds the reverse map); BDOS and CONOUT are EQUs whose
        // values happen to look like an address and a function number -- name->value ONLY.
        std::string p = prn("0100", "314201", "START:\tLXI\tSP,STACK") +
                        prn("0005", "=", "BDOS\tEQU\t5") +
                        prn("0002", "=", "CONOUT\tEQU\t2") +
                        prn("0117", "C30000", "DONE:\tJMP\t0");
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.PRN", t, true, st, err), "a good listing loads");
        CHECK(st.added == 4, "four symbols");
        uint32_t v = 0;
        CHECK(t.lookup("START", v) && v == 0x0100, "START -> 0100");
        CHECK(t.lookup("BDOS", v) && v == 0x0005, "the EQU BDOS -> 0005");
        CHECK(t.lookup("done", v) && v == 0x0117, "lookup is case-insensitive");

        // The reverse map holds the LABELS and NOT the EQUs -- so 0005 never renders as BDOS.
        CHECK(t.byAddr.count(0x0100) == 1, "the label START is in the reverse map");
        CHECK(t.byAddr.count(0x0117) == 1, "the label DONE is in the reverse map");
        CHECK(t.byAddr.count(0x0005) == 0, "the EQU BDOS is NOT -- 0005 is not a label");
        CHECK(t.byAddr.count(0x0002) == 0, "nor is the function number CONOUT");

        // ---- annotation: labelsAt (headers) vs operandName (referenced values) ----
        // A leading `NAME:` line comes from labels only, so a disassembly heads START
        // and DONE but never prints a phantom `BDOS:` at 0005.
        CHECK(t.labelsAt(0x0100).size() == 1 && t.labelsAt(0x0100)[0] == "START",
              "labelsAt heads the line with a real label");
        CHECK(t.labelsAt(0x0005).empty(), "an EQU value is not a line header");

        // An operand names the value it points at. A real label wins; failing that, an
        // EQU that is really an address (BDOS) still names it -- which is what makes
        // `CALL 0005` read as `CALL BDOS`.
        CHECK(t.operandName(0x0100) == "START", "an operand at a label reads as the label");
        CHECK(t.operandName(0x0005) == "BDOS", "an operand at an EQU-address reads as the EQU");
        CHECK(t.operandName(0x9999).empty(), "an operand with no matching symbol stays a number");
    }

    // ---- operandName prefers a real label to an EQU that shares its value ----
    {
        // START and a stray EQU both equal 0100. The label must win so the address does
        // not read as the constant.
        std::string p = prn("0100", "314201", "START:\tLXI\tSP,STACK") +
                        prn("0100", "=", "PAGE1\tEQU\t100H");
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.PRN", t, true, st, err), "loads");
        CHECK(t.operandName(0x0100) == "START", "a label beats a same-valued EQU");
    }

    // ---- .PRN: both label styles, and the object field that abuts column 16 ----
    {
        // A bare tab-indented label (no colon), and the real case where five object bytes
        // fill columns 7-16 with no gap before the source: `011A 48454C4C4FMSG:`.
        std::string p = prn("F800", "3E03", "monit\tmvi\ta,3") +
                        prn("011A", "48454C4C4F", "MSG:\tDB\t'HELLO'");
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.PRN", t, true, st, err), "loads");
        uint32_t v = 0;
        CHECK(t.lookup("MONIT", v) && v == 0xF800, "the bare label parses");
        CHECK(t.lookup("MSG", v) && v == 0x011A, "so does the label abutting column 16");
    }

    // ---- .PRN: a line with an address but no label yields nothing ----
    {
        std::string p = prn("0122", "", "\tDS\t32") +          // continuation: tab at col 17
                        prn("0100", "", "\tORG\t0100H");        // ORG: no label
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.PRN", t, true, st, err), "loads");
        CHECK(st.added == 0, "no label in column 17 -> no symbol");
    }

    // ---- .PRN: a relocatable listing is REFUSED, and named ----
    {
        // An M80 relocatable value carries a trailing apostrophe in the object field.
        std::string p = std::string(" 0100'C30000    START:\tJMP\t0\r\n");
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(!loadPrn(sv(p), "reloc.PRN", t, true, st, err), "a relocatable address FAILS");
        CHECK(err.find("reloc.PRN") != std::string::npos, "and names the file");
        CHECK(err.find("line 1") != std::string::npos, "and the line");
        CHECK(err.find("relocatable") != std::string::npos, "and says why");
    }

    // ---- .PRN: a source apostrophe (column 17+) is NOT mistaken for relocation ----
    {
        std::string p = prn("011A", "48454C4C4F", "MSG:\tDB\t'HELLO'");
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.PRN", t, true, st, err),
              "an apostrophe in the SOURCE field is a string, not a relocation");
    }

    // ---- .PRN: a form-feed page break does not shift the columns ----
    {
        std::string p = std::string("\f") + prn("0100", "314201", "START:\tLXI\tSP,STACK");
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.PRN", t, true, st, err), "loads across a page break");
        uint32_t v = 0;
        CHECK(t.lookup("START", v) && v == 0x0100, "the ^L is stripped, columns hold");
    }

    // ---- .SYM: the exact bytes DR MAC writes (captured in-machine 2026-07-17) ----
    {
        std::string s =
            "0005 BDOS\t0002 CONOUT\t000D CR\t\t0117 DONE\t000A LF\r\n"
            "0106 LOOP\t011A MSG\t0142 STACK\t0100 START\r\n"
            "\x1a\x1a\x1a\x1a";
        CHECK(looksLikeSym(sv(s)), "a .SYM is recognised (hex digit in column 1)");
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadSym(sv(s), "prog.SYM", t, true, st, err), "the .SYM loads");
        CHECK(st.added == 9, "all nine pairs, tabs and double-tabs alike");
        uint32_t v = 0;
        CHECK(t.lookup("START", v) && v == 0x0100, "START -> 0100");
        CHECK(t.lookup("STACK", v) && v == 0x0142, "STACK -> 0142 (past the ^Z padding? no -- before it)");
        CHECK(t.lookup("CONOUT", v) && v == 0x0002, "CONOUT -> 0002");
        // A .SYM cannot tell a label from an EQU, so NOTHING enters the reverse map.
        CHECK(t.byAddr.empty(), "a .SYM feeds name->value only");
    }

    // ---- looksLikeSym tells a .SYM from a .PRN ----
    {
        std::string p = prn("0100", "314201", "START:\tLXI\tSP,STACK");
        CHECK(!looksLikeSym(sv(p)), "a .PRN (space in column 1) is not a .SYM");
        CHECK(!looksLikeSym(sv(std::string("\f 0100 ..."))), "nor is one that opens on a page break");
    }

    // ---- merge is the default; REPLACE and CLEAR ----
    {
        SymbolTable t;
        SymbolTable::LoadStats a;
        std::string err;
        loadPrn(sv(prn("F800", "3E03", "MONIT:\tmvi\ta,3")), "rom.PRN", t, true, a, err);
        CHECK(t.size() == 1 && t.loadOrder.size() == 1, "first file: one symbol, one source");

        // Merge a second file. A collision is COUNTED and the newest wins.
        SymbolTable::LoadStats b;
        std::string p2 = prn("0100", "314201", "START:\tx") + prn("F800", "00", "MONIT:\ty");
        loadPrn(sv(p2), "prog.PRN", t, /*replace=*/false, b, err);
        uint32_t v = 0;
        CHECK(t.size() == 2, "merge added START, kept MONIT");
        CHECK(b.redefined == 1 && b.redefinedNames[0] == "MONIT", "the redefinition is reported");
        CHECK(t.lookup("MONIT", v) && v == 0xF800, "and the newest MONIT wins");
        CHECK(t.loadOrder.size() == 2, "both sources are remembered for CONFIG SAVE");

        // REPLACE clears first.
        SymbolTable::LoadStats c;
        loadPrn(sv(prn("0200", "00", "ONLY:\tz")), "only.PRN", t, /*replace=*/true, c, err);
        CHECK(t.size() == 1 && t.loadOrder.size() == 1, "REPLACE cleared the table and the sources");
        CHECK(!t.lookup("START", v), "the merged symbols are gone");
    }
}
