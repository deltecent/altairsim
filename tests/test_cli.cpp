// Command prefix resolution (docs/cli-commands.md, cli/commands.cpp).
//
// The abbreviations are a CONTRACT. Once `D` dumps, it must dump forever -- so
// these tests pin the priority-1 list, and they pin the two properties that make
// the whole scheme safe: an exact spelling always wins, and a command is never
// silently reinterpreted because a newer command was added above it.

#include "boards/mits-2sio.h"
#include "boards/mits-88virtc.h"
#include "boards/registry.h"
#include "boards/s100-memory.h"
#include "cli/commands.h"
#include "cli/lineedit.h"
#include "cli/monitor.h"
#include "config/toml.h"
#include "core/machine.h"
#include "cpu/cpu.h"
#include "host/console.h"
#include "host/display_null.h"
#include "host/endpoint.h"
#include "host/media.h"
#include "host/stream.h"
#include "test.h"

#include <memory>
#include <sstream>
#include <filesystem>
#include <fstream>

using namespace altair;

namespace {

// Resolve `word` and return the command it names, or "" for unknown/ambiguous.
std::string R(const char* word) {
    const CommandDef* c = resolveCommand(word);
    return c ? c->name : "";
}

} // namespace

void test_cli() {
    SECTION("command abbreviation -- table order IS the ranking; first match wins");

    // ---- Patrick's ranking, 2026-07-11. These eight are listed first. ----
    CHECK(R("D") == "DUMP", "D dumps. A ROM monitor's D has always dumped.");
    CHECK(R("S") == "STEP", "S steps");
    CHECK(R("R") == "RUN", "R RUNS -- a bare R must never reset the machine");
    CHECK(R("H") == "HISTORY", "H is history");
    CHECK(R("M") == "MOUNT", "M mounts");
    CHECK(R("B") == "BREAK", "B breaks");
    CHECK(R("E") == "EDIT", "E edits");
    CHECK(R("C") == "CONFIG", "C configures");

    // ---- RUN REPLACED GO (Patrick, 2026-07-12) ----
    // There was never a second thing for GO to be. A headless run is not a mode the
    // operator picks -- it is what happens when nothing holds the console, and the
    // machine already knows that.
    CHECK(R("G") == "", "G is nothing now: GO is gone, and the panel's switch says RUN");
    bool hasGo = false;
    for (const CommandDef& c : commands())
        if (std::string(c.name) == "GO") hasGo = true;
    CHECK(!hasGo, "there is exactly ONE way to start the machine, and it is RUN");
    CHECK(R("RU") == "RUN", "RU runs");
    CHECK(R("RUN") == "RUN", "and RUN");

    // ---- THE R-CLUSTER (Patrick, 2026-07-13) ----
    // The same rule as D: the shortest key goes to the command that cannot destroy
    // anything, and the one that throws the machine away pays letters for it. RUN is
    // typed every session and costs nothing if you did not mean it; a bare R that
    // RESET the machine would be a machine you have to set up again. So RUN takes R,
    // REGS takes RE, and RESET pays RES. Nobody assigned these -- the order did.
    CHECK(R("RE") == "REGS", "RE is the registers -- REGS is the first RE- word in the table");
    // RECORD and REPLAY were DROPPED, not deferred (commands.cpp's R-cluster note), so
    // REC/REP resolve to nothing now -- their prefixes are free for the next claimant.
    CHECK(R("REC") == "", "REC is nothing now -- RECORD was dropped");
    CHECK(R("REP") == "", "REP is nothing now -- REPLAY was dropped");
    CHECK(R("RES") == "RESET", "RES resets -- and a reset costs you three letters, on purpose");
    CHECK(R("REST") == "RESTORE", "REST restores -- REST is not a prefix of RESET");
    CHECK(R("REGI") == "REGION", "REGI is a memory region");
    CHECK(R("RESET") == "RESET", "the invariant: RESET's own name still reaches RESET");

    // ---- and the losers, at the cost of exactly one more letter ----
    CHECK(R("DE") == "DEPOSIT", "DE deposits -- the front panel keeps its word");
    CHECK(R("DU") == "DUMP", "DU still dumps");
    CHECK(R("DUM") == "DUMP", "DUM still dumps");
    CHECK(R("SE") == "SET", "SE sets (SET outranks SEARCH)");
    CHECK(R("SEA") == "SEARCH", "SEA searches");
    CHECK(R("SH") == "SHOW", "SH shows");
    CHECK(R("SA") == "SAVE", "SA saves");
    CHECK(R("ST") == "STEP", "ST steps");
    CHECK(R("STO") == "", "STO is nothing now -- STOP was dropped");
    CHECK(R("SN") == "SNAPSHOT", "SN snapshots");
    // ---- THE N-CLUSTER (Patrick, 2026-07-15) ----
    // NEXT took `N` from NOBREAK, by the same rule that gave RUN `R` and STEP `S`: the
    // command your fingers reach for between two steps wins the single letter, and the
    // rare one pays. NEXT is a step-over you type constantly while walking code; NOBREAK
    // clears a breakpoint now and then. So `N` is NEXT and NOBREAK pays `NO`. Neither is
    // a strict prefix of the other, so both full names stay typeable.
    CHECK(R("N") == "NEXT", "N is NEXT -- the step-over you type constantly");
    CHECK(R("NE") == "NEXT", "NE is NEXT");
    CHECK(R("NEXT") == "NEXT", "and NEXT");
    CHECK(R("NO") == "NOBREAK", "NO is NOBREAK -- it pays one letter for NEXT taking N");
    CHECK(R("NOBREAK") == "NOBREAK", "and NOBREAK's own name still reaches it");
    CHECK(R("HE") == "HELP", "HE helps");
    // The command is PLURAL, and that is what makes both spellings work: BOARD is a
    // prefix of BOARDS, and prefixes are the whole resolver. No alias, no second
    // table entry, nothing to keep in sync.
    CHECK(R("BO") == "BOARDS", "BO is the board command");
    CHECK(R("BOARD") == "BOARDS", "BOARD resolves -- it is a prefix of BOARDS");
    CHECK(R("BOARDS") == "BOARDS", "and so does BOARDS");
    // EXAMINE and DEPOSIT are the front panel's two switches, so they get the short
    // keys: DE and EX. E stays with EDIT, which outranks both.
    CHECK(R("EX") == "EXAMINE", "EX examines -- the front-panel switch, not the door");
    CHECK(R("EXA") == "EXAMINE", "and EXA");
    // `EXI` is not a prefix of EXAMINE (E-X-A), so with EXIT gone it names nothing.
    // It is an honest "unknown command", not a silent hit on something else.
    CHECK(R("EXI") == "", "EXI is now nothing at all -- EXIT is gone");

    // THERE IS NO EXIT. One word for leaving, not two: a second spelling is a second
    // thing to learn and buys nothing, and EXIT was the only reason EXAMINE could not
    // just be `EX`.
    CHECK(R("Q") == "QUIT", "Q quits");
    bool hasExit = false;
    for (const CommandDef& c : commands())
        if (std::string(c.name) == "EXIT") hasExit = true;
    CHECK(!hasExit, "EXIT does not exist -- QUIT is the only way out");
    CHECK(R("MO") == "MOUNT", "MO mounts");
    CHECK(R("MOV") == "MOVE", "MOV moves");
    CHECK(R("REG") == "REGS", "REG is registers (REGS outranks REGION)");
    CHECK(R("CON") == "CONFIG", "CON configures");
    CHECK(R("CONS") == "CONSOLE", "CONS is the console");
    CHECK(R("CONN") == "CONNECT", "CONN connects");
    CHECK(R("COM") == "COMPARE", "COM compares");
    // UNMOUNT, not DISMOUNT: the plain word, and it takes `U`, which nothing else
    // wanted. It also gets out of DISASM's way -- with the D-cluster one shorter,
    // DISASM drops from DISA to DI, and nobody had to decide that either.
    CHECK(R("U") == "UNMOUNT", "U unmounts");
    CHECK(R("DI") == "DISASM", "DI disassembles -- UNMOUNT left the D-cluster");
    CHECK(R("DISC") == "DISCONNECT", "and DISC still disconnects");
    bool hasDismount = false;
    for (const CommandDef& c : commands())
        if (std::string(c.name) == "DISMOUNT") hasDismount = true;
    CHECK(!hasDismount, "DISMOUNT does not exist");

    // ---- an exact spelling ALWAYS wins, whatever it is ranked ----
    for (const CommandDef& c : commands())
        CHECK(R(c.name) == c.name, (std::string("exact spelling wins: ") + c.name).c_str());
    CHECK(R("step") == "STEP", "case does not matter");

    // NO COMMAND MAY BE A STRICT PREFIX OF ANOTHER. If one ever is, its full,
    // correctly-spelled name becomes un-typeable -- it would resolve by priority
    // to the other command, and there would be no way to say what you meant.
    // Adding SHOWALL, or renaming REGS to REG, would trip exactly this.
    std::string offender;
    for (const CommandDef& x : commands())
        for (const CommandDef& y : commands()) {
            std::string a = x.name, b = y.name;
            if (a != b && a.size() < b.size() && b.compare(0, a.size(), a) == 0)
                offender = a + " is a strict prefix of " + b;
        }
    CHECK(offender.empty(), offender.empty() ? "no command is a strict prefix of another"
                                             : offender.c_str());

    // ---- AND THE SAME INVARIANT, FOR THE VERBS THE CARDS BRING (core/board.h) ----
    //
    // THE STATIC MENU ALWAYS WINS: the monitor resolves the built-in table first and
    // asks the boards only when nothing there matched. That is what makes a board
    // safe to plug in -- it cannot move `D` or `RE` under your fingers.
    //
    // The price is that a card CAN declare a verb that NOBODY CAN EVER TYPE: one whose
    // every prefix, up to and including its full name, a built-in claims first. A
    // board named its verb SHOW and it would simply never run.
    //
    // A USER CANNOT CREATE THAT. Only a board AUTHOR can -- so it is caught HERE, as a
    // merge gate, and not at runtime where the message would be about a word somebody
    // typed instead of about the card that is wrong. boardAbbreviation() returns the
    // bare name with no [brackets] exactly when the verb is unreachable.
    for (const BoardType& t : boardTypes()) {
        auto b = makeBoard(t.name);
        if (!b) continue;
        for (const CommandDef& v : b->commands()) {
            std::string full = v.name;
            std::string ab   = boardAbbreviation(v);
            CHECK(ab != full || full.size() == 1,
                  (t.name + " declares an UNREACHABLE verb: a built-in prefix-matches " +
                   full + " first. Rename it.")
                      .c_str());
            // And it must actually resolve to nothing, which is the same claim from
            // the other side: that is *how* the monitor reaches the card at all.
            std::string typed = ab.substr(0, ab.find('['));
            CHECK(!resolveCommand(typed),
                  (t.name + ": `" + typed + "` reaches the card, because no built-in claims it")
                      .c_str());
        }
    }

    // THE ABBREVIATION CONTRACT HELD, AND HERE IS THE PROOF.
    //
    // `S` meant STEP when STEP did not exist -- that is why it was in the table
    // reserved, rather than being left out until the CPU landed. The CPU has now
    // landed, STEP is built, and `S` means exactly what it always meant. Nobody's
    // fingers had to relearn anything, which was the entire point of paying for the
    // reservation up front.
    const CommandDef* s = resolveCommand("S");
    CHECK(s && std::string(s->name) == "STEP", "S is STEP -- as it was before STEP existed");
    CHECK(s && s->built, "and now it is built");

    // TRACE and HISTORY have now landed, and they meant what they always meant: `TR`
    // is TRACE, `H` is HISTORY, exactly as reserved.
    const CommandDef* tr = resolveCommand("TR");
    CHECK(tr && std::string(tr->name) == "TRACE", "TR is TRACE -- as it was reserved");
    CHECK(tr && tr->built, "and now it is built");
    const CommandDef* h = resolveCommand("H");
    CHECK(h && std::string(h->name) == "HISTORY" && h->built, "H is HISTORY, and built");

    // TYPE landed after TRACE, so `T` still means TRACE and TYPE pays `TY`. Adding a
    // command must not silently change what a shorter prefix already resolves to.
    const CommandDef* ty = resolveCommand("TY");
    CHECK(ty && std::string(ty->name) == "TYPE" && ty->built, "TY is TYPE, and built");
    CHECK(std::string(resolveCommand("T")->name) == "TRACE", "and T is still TRACE, above it");

    // SNAPSHOT and RESTORE are built now (SN/REST); their prefixes are theirs.
    const CommandDef* snap = resolveCommand("SN");
    CHECK(snap && std::string(snap->name) == "SNAPSHOT" && snap->built,
          "SN is SNAPSHOT, and built");

    // RECORD, REPLAY and STOP were DROPPED, not deferred (commands.cpp). Nothing is
    // reserved today: REC/REP/STO resolve to nothing, so their prefixes are free for
    // whatever claims them next. The built=false mechanism stays for that day.
    CHECK(resolveCommand("REC") == nullptr, "REC no longer resolves -- RECORD is gone");
    CHECK(resolveCommand("REP") == nullptr, "REP no longer resolves -- REPLAY is gone");
    CHECK(resolveCommand("STO") == nullptr, "STO no longer resolves -- STOP is gone");

    // Every command in the table is either BUILT (and then it needs no excuse) or
    // RESERVED (and then it must say what it is waiting for). A reserved command
    // with nothing to say is a dead entry nobody can act on -- and, until a moment
    // ago, a null pointer this loop walked straight into. Nothing is reserved today,
    // so this holds vacuously -- but it is the guard the next reserved command needs.
    for (const CommandDef& c : commands())
        CHECK(c.built || (c.waiting && *c.waiting),
              c.built ? "built" : "a reserved command says what it waits on");

    CHECK(resolveCommand("ZORK") == nullptr, "an unknown word is unknown");
    CHECK(resolveCommand("") == nullptr, "and so is nothing at all");

    // ---- CONNECT's gloss may not fall behind endpointHelp() ----
    //
    // CONNECT's help GLOSSES each endpoint -- `null` and `scripted` tell you nothing
    // by themselves -- and a gloss is a hand-copy of somebody else's vocabulary, which
    // is the exact thing that rotted here once before: the list sat there promising
    // that socket: and serial: "are coming" long after resolveEndpoint() had shipped
    // both. The enumeration is {endpoints}'s job and cannot rot. THIS is what keeps the
    // prose beside it honest: every name endpointHelp() offers must be a word CONNECT
    // says. Add an endpoint and stay silent about it, and this fails.
    const CommandDef* conn = resolveCommand("CONNECT");
    CHECK(conn && conn->detail, "CONNECT has a detail to check");
    if (conn && conn->detail) {
        const std::string help = conn->detail;
        const std::string grammar = endpointHelp();

        // Split "a | b | socket:PORT" into names. A prefixed endpoint is glossed by its
        // PREFIX (`socket:`), because the help explains one scheme, not each of its two
        // spellings -- so cut at the colon and keep it.
        size_t at = 0;
        while (at < grammar.size()) {
            size_t bar = grammar.find('|', at);
            std::string tok = grammar.substr(at, bar == std::string::npos ? bar : bar - at);
            at = (bar == std::string::npos) ? grammar.size() : bar + 1;

            size_t b = tok.find_first_not_of(" \t\n");
            if (b == std::string::npos) continue;
            tok = tok.substr(b, tok.find_last_not_of(" \t\n") - b + 1);

            size_t colon = tok.find(':');
            if (colon != std::string::npos) tok = tok.substr(0, colon + 1);

            // A bracketed grammar (`terminal[?emulation=...]`) is glossed by its NAME, not
            // its whole option string -- keep what is before the '['.
            size_t brk = tok.find('[');
            if (brk != std::string::npos) tok = tok.substr(0, brk);

            CHECK(help.find(tok) != std::string::npos,
                  (std::string("CONNECT's help says what '") + tok +
                   "' is -- endpointHelp() offers it, so the prose must gloss it")
                      .c_str());
        }
    }

    // ---- IN and OUT (Patrick, 2026-07-11) ----
    // The two commands every ROM monitor has had since there were ROM monitors.
    // Nothing else in the table starts with I or O, so they get their initial for
    // free -- no reshuffling, no cost to anyone else's abbreviation.
    CHECK(R("I") == "IN", "I is IN");
    CHECK(R("O") == "OUT", "O is OUT");
    CHECK(R("OU") == "OUT", "and OU, and OUT");

    const CommandDef* in = resolveCommand("IN");
    CHECK(in && in->built, "IN is built -- it runs a real ioRead through the real decode");

    // ---------------------------------------------------------------------
    // DUMP: a bare address is a PAGE, and lines align to the width
    // (Patrick, 2026-07-11)
    // ---------------------------------------------------------------------
    SECTION("DUMP -- a bare address means a page, and the columns line up");

    Machine m;
    Monitor mon(m);
    std::ostringstream sink;
    // The CPU is here because EXAMINE needs one below -- the panel's EXAMINE is a
    // bus cycle the PROCESSOR drives (see the next section). DUMP does not need it.
    mon.exec("BOARDS ADD 8080 cpu0", sink);
    mon.exec("BOARDS ADD memory mem0", sink);
    mon.exec("SET mem0 fill=zero", sink);
    mon.exec("REGION ADD mem0 type=ram at=0 size=1K", sink);
    mon.exec("DEPOSIT 0 41 42 43", sink);

    auto run = [&](const char* line) {
        std::ostringstream o;
        mon.exec(line, o);
        return o.str();
    };
    auto lines = [](const std::string& s) {
        std::vector<std::string> v;
        std::istringstream is(s);
        for (std::string l; std::getline(is, l);) v.push_back(l);
        return v;
    };

    // A single address dumps a PAGE, not one byte. `D 100` is not a request to see
    // one byte -- nobody has ever wanted that -- it is "what is at 0100".
    auto page = lines(run("D 0"));
    CHECK(page.size() == 16, "a bare address dumps a full page: 16 lines of 16");
    CHECK(page.front().compare(0, 4, "0000") == 0, "starting at 0000");
    CHECK(page.back().compare(0, 4, "00F0") == 0, "and ending on the 00F0 line");

    // LINES ALIGN TO THE WIDTH, NOT TO THE START. `D 0001` opens on the 0000 line
    // with the 0000 column BLANK, so 0001 sits under the "01" heading. Reading a
    // dump means reading the column position; if the columns shift with the start
    // address, every byte is off by one and you will not notice until it matters.
    auto off = lines(run("D 0001"));
    CHECK(off.front().compare(0, 4, "0000") == 0, "D 0001 still opens on the 0000 line");
    CHECK(off.front().compare(6, 3, "   ") == 0, "with the 0000 column held BLANK");
    CHECK(off.front().compare(9, 2, "42") == 0, "and 42 -- the byte at 0001 -- in the 01 column");

    // IT STOPS ON THE PAGE BOUNDARY (Patrick, 2026-07-11). `D 0001` is 0001-00FF --
    // it does NOT count out 256 bytes from wherever you started and dangle a
    // one-byte line at 0100. So the last line is always a full one, and dumps stay
    // page-aligned no matter where you first landed.
    CHECK(off.back().compare(0, 4, "00F0") == 0, "D 0001 stops at 00FF -- the end of the page");
    CHECK(off.size() == 16, "and it is still 16 lines, the last one full");

    // Which means the NEXT bare DUMP opens cleanly on the next page.
    CHECK(lines(run("D")).front().compare(0, 4, "0100") == 0, "so a bare D continues at 0100");

    // An explicit range means EXACTLY what it says. Only the bare form expands --
    // otherwise `FILL 100 5A` would have to expand too, and that is a footgun.
    auto one = lines(run("D 0-0"));
    CHECK(one.size() == 1 && one[0].compare(9, 2, "  ") == 0,
          "an explicit range is exact: D 0-0 shows one byte and pads the rest");

    // ---- EXAMINE: one byte, and bare EXAMINE is the panel's EXAMINE NEXT ----
    std::string e0 = run("EX 0");
    CHECK(e0.compare(0, 8, "0000  41") == 0, "EX 0 shows the byte at 0000");
    CHECK(e0.find("01000001") != std::string::npos, "with the bits, as the panel's LEDs show");
    CHECK(e0.find('A') != std::string::npos, "and the character");

    // The switch steps. EX, EX, EX walks memory a byte at a time.
    CHECK(run("EX").compare(0, 8, "0001  42") == 0, "bare EXAMINE steps to 0001 -- EXAMINE NEXT");
    CHECK(run("EX").compare(0, 8, "0002  43") == 0, "and again to 0002");

    // EXAMINE's cursor is its OWN. A DUMP walks a page; if they shared a cursor, a
    // dump would silently throw your examine position 256 bytes down the road.
    run("D 8000");
    CHECK(run("EX").compare(0, 8, "0003  00") == 0, "a DUMP does not move the EXAMINE latch");

    // Looking at ONE byte is exactly when you must know it IS one. Nothing is
    // populated at 8000, so the bus floats it -- and EXAMINE says so.
    CHECK(run("EX 8000").find("nobody drives this") != std::string::npos,
          "EXAMINE distinguishes a real FF from an empty slot");

    // ---------------------------------------------------------------------
    // `.` REPEATS THE LAST COMMAND
    // ---------------------------------------------------------------------
    // One keystroke, and no echo: it re-runs the exact last line, which is what
    // makes the continuing verbs (bare DISASM/DUMP, STEP) walk forward under it.
    SECTION(". repeats the last command, and never itself");

    // An explicit-address command has no cursor to move, so `.` reproduces it
    // byte-for-byte. That IS the test that `.` re-ran the exact line.
    std::string d0 = run("D 0");
    CHECK(run(".") == d0, ". repeats the last command exactly");

    // `.` is never recorded as the last command, so a second `.` still repeats the
    // ORIGINAL line -- if it recorded itself, exec would recurse on `.` forever.
    std::string r1 = run(".");
    std::string r2 = run(".");
    CHECK(r1 == d0 && r2 == d0, ". repeats the original, not the previous . (and cannot loop)");

    // The point of it: a bare DISASM continues from its own cursor, so `.` walks
    // forward. Two `.` after a bare DI land on different addresses each time.
    run("DI 0");                 // seat the disasm cursor
    std::string di1 = run("DI"); // bare DI continues; this line becomes the one `.` repeats
    std::string di2 = run(".");  // repeats "DI" -- continues further, so it differs
    CHECK(di1 != di2, ". re-runs a continuing verb, walking DISASM forward");

    // A `.` before anything has been typed has nothing to repeat, and says so
    // rather than doing something -- the way a bare `!` reminds you of its form.
    {
        Machine  fresh;
        Monitor  mf(fresh);
        std::ostringstream o;
        mf.exec(".", o);
        CHECK(o.str().find("nothing to repeat") != std::string::npos,
              "a . with no prior command reports there is nothing to repeat");
    }

    // ---------------------------------------------------------------------
    // EXAMINE *IS* THE CPU (Patrick, 2026-07-12)
    // ---------------------------------------------------------------------
    // On the panel this is not a side effect, it is what the switch is FOR: it
    // jams the address switches into the PROGRAM COUNTER, and the CPU drives the
    // address lines and MEMR. Two things follow, and both are tested here.
    //   1. `EX FF00` is a JMP you can see the destination of -- STEP executes THERE.
    //   2. With no CPU card, nothing drives the bus. There is no examine to do.
    SECTION("EXAMINE loads the PC, exactly as the front-panel switch does");

    // Take the CPU out and the switch stops working -- as it must. Nothing is
    // putting an address on the bus.
    {
        Machine bare;
        Monitor mb(bare);
        std::ostringstream o;
        mb.exec("BOARDS ADD memory mem0", o);
        mb.exec("REGION ADD mem0 type=ram at=0 size=1K", o);
        std::ostringstream e;
        mb.exec("EX 0", e);
        CHECK(e.str().find("no CPU") != std::string::npos,
              "EXAMINE with no CPU is an error: nothing drives the address lines");

        // AND EVERY OTHER MEMORY COMMAND STILL WORKS WITHOUT ONE (Patrick,
        // 2026-07-12): "we need to be able to debug the simulator without a CPU."
        // EXAMINE is the sole exception, and only because it IS the CPU. DUMP is how
        // you look at a machine with no processor in it -- it runs no cycle, so it
        // needs nobody to drive one. (`EX 0 RAW mem0` was the old way round this, and
        // it went with RAW: reading behind the bus bought nothing a ROM does not
        // already give you through it.)
        std::ostringstream w;
        mb.exec("DEPOSIT 0 41 42", w);
        mb.exec("FILL 2-3 5A", w);
        mb.exec("D 0-3", w);
        CHECK(w.str().find("41 42 5A 5A") != std::string::npos,
              "DEPOSIT, FILL and DUMP all work on a backplane with no processor");
    }

    SECTION("SET CONSOLE base=octal -- the wire class reads and writes SPLIT octal");
    {
        // MITS documentation and the front panel speak octal. base=octal moves the
        // WIRE class -- addresses, ports, data bytes -- and nothing else, on input
        // (the default parse base) and output (split octal: each byte 000..377, a
        // 16-bit value its two bytes hi-then-lo). The decimal class does not move.
        Machine mo;
        Monitor mono(mo);
        std::ostringstream so;
        mono.exec("BOARDS ADD 8080 cpu0", so);
        mono.exec("BOARDS ADD memory mem0", so);
        mono.exec("SET mem0 fill=zero", so);
        mono.exec("REGION ADD mem0 type=ram at=0 size=1K", so);
        mono.exec("SET CONSOLE base=octal", so);

        auto run = [&](const char* line) {
            std::ostringstream o;
            mono.exec(line, o);
            return o.str();
        };

        // A bare number is now octal, so `100` is the 65th byte (0x40), and it reads
        // back as split octal. `377q` and `0o` still force octal; `0x` still forces hex.
        run("DEPOSIT 100 0o76 377q");
        std::string e = run("EX 100");
        CHECK(e.compare(0, 9, "000 100  ") == 0, "the address 0x0040 reads as split octal 000 100");
        CHECK(e.compare(9, 3, "076") == 0, "and the byte 0x3E as octal 076");

        // A byte typed with an explicit hex marker still lands, in octal mode.
        run("DEPOSIT 0x40 0xFF");
        CHECK(run("EX 100").compare(9, 3, "377") == 0, "0xFF deposited via a hex marker reads back 377");

        // DUMP: the address column and every byte are octal; the ASCII column is not.
        auto dl = run("D 100-100");
        CHECK(dl.compare(0, 7, "000 100") == 0, "DUMP's address column is split octal");
        CHECK(dl.find("377") != std::string::npos, "and its byte column is octal");

        // DISASM: opcode bytes AND the 16-bit operand render in the chosen base.
        run("DEPOSIT 0 0xC3 0x34 0x12");  // JMP 1234
        std::string d = run("DISASM 0 1");
        CHECK(d.find("JMP 022 064") != std::string::npos,
              "a JMP target renders as split octal (0x1234 -> 022 064)");

        // base=hex comes straight back -- and a bare number is hex again, so 40 (not
        // 100) is the byte we filled, proving the DEFAULT parse base flipped too.
        run("SET CONSOLE base=hex");
        CHECK(run("EX 40").compare(0, 6, "0040  ") == 0, "base=hex restores four-hex-digit addresses");
    }

    Machine m2;
    Monitor mon2(m2);
    std::ostringstream s2;
    mon2.exec("BOARDS ADD 8080 cpu0", s2);
    mon2.exec("BOARDS ADD memory mem0", s2);
    mon2.exec("SET mem0 fill=zero", s2);
    mon2.exec("REGION ADD mem0 type=ram at=0 size=64K", s2);
    mon2.exec("POWER ON", s2);
    // 3C = INR A. One at FF00, one at 0100, so we can tell WHICH one ran.
    mon2.exec("DEPOSIT FF00 3C", s2);
    mon2.exec("DEPOSIT 0100 00", s2);

    CpuCore* c = m2.cpu();
    CHECK(c && c->pc() == 0x0000, "power-on leaves the PC at 0000");

    mon2.exec("EX FF00", s2);
    CHECK(c->pc() == 0xFF00, "EX FF00 loads the PC -- the switch latches the address");

    std::ostringstream st;
    mon2.exec("STEP", st);
    // STEP prints one line, AFTER the instruction runs: the machine as it now stands.
    // The INR A at FF00 executed, so A is 01 and the PC has moved on to FF01 -- which
    // together prove it ran AT FF00, not at some other 3C. (It used to also print a
    // line BEFORE the instruction, so a single step showed two lines and looked like
    // two ran.)
    CHECK(st.str().find("A=01") != std::string::npos,
          "the INR A at FF00 actually ran -- A went 00 -> 01");
    CHECK(st.str().find("P=FF01") != std::string::npos,
          "so STEP executed AT FF00, not wherever it was, and the PC moved one on");
    CHECK(c->pc() == 0xFF01, "and one instruction later the PC has moved");

    // A COUNT prints one line per instruction, not one plus a trailing snapshot. Three
    // NOPs at 0100.. give `S 3` three register lines -- the old code printed four.
    mon2.exec("DEPOSIT 0100 00 00 00", s2);
    mon2.exec("EX 0100", s2);
    {
        std::ostringstream ss;
        mon2.exec("S 3", ss);
        size_t lines = 0;
        for (size_t p = ss.str().find("P="); p != std::string::npos;
             p = ss.str().find("P=", p + 1))
            ++lines;
        CHECK(lines == 3, "S 3 prints three instruction lines, one per step -- not four");
        CHECK(c->pc() == 0x0103, "and three NOPs later the PC has advanced by three");
    }

    // EXAMINE NEXT drags the PC with it. The panel's counter IS the cursor -- it
    // has no other, which is why the switch is wired to it in the first place.
    mon2.exec("EX 0200", s2);
    mon2.exec("EX", s2);
    CHECK(c->pc() == 0x0201, "EXAMINE NEXT steps the PC too -- it is the same counter");

    // There is no longer an EXAMINE that leaves the PC alone. `EX <addr> RAW mem0` was
    // one -- it ran no bus cycle, so the CPU never saw an address -- and it went with
    // RAW (DESIGN.md 10.2). Every EXAMINE is the panel switch now, and the panel has
    // exactly one counter.

    // -----------------------------------------------------------------------
    // NEXT -- STEP that runs OVER a CALL/RST instead of into it. It is a temporary
    // breakpoint at the return address plus a RUN, so a subroutine reads as one step.
    // -----------------------------------------------------------------------
    SECTION("NEXT -- a CALL/RST runs to its return; anything else is a single step");

    Machine mn;
    Monitor monN(mn);
    std::ostringstream sn;
    monN.exec("BOARDS ADD 8080 cpu0", sn);
    monN.exec("BOARDS ADD memory mem0", sn);
    monN.exec("SET mem0 fill=zero", sn);
    monN.exec("REGION ADD mem0 type=ram at=0 size=64K", sn);
    monN.exec("POWER ON", sn);
    CpuCore* cn = mn.cpu();

    // 0200: LXI SP,0400   give the CALL a real stack (top of RAM is not present)
    // 0203: CALL 0209     the instruction under test
    // 0206: HLT           where a completed step-over must land (the return address)
    // 0209: INR A / RET   the callee: proves it ran AND returned
    monN.exec("DEPOSIT 0200 31 00 04 CD 09 02 76 00 00 3C C9", sn);
    monN.exec("EX 0200", sn);
    monN.exec("STEP", sn);  // execute the LXI SP, leaving the PC on the CALL
    CHECK(cn->pc() == 0x0203, "set up: the PC is sitting on the CALL");

    {
        std::ostringstream out;
        monN.exec("NEXT", out);
        CHECK(cn->pc() == 0x0206,
              "NEXT over a CALL stops at the RETURN address -- the callee ran and came back");
        // A completion is SILENT: no breakpoint line, no instruction tally. Just the
        // register line NEXT prints itself, exactly as STEP would.
        CHECK(out.str().find("breakpoint") == std::string::npos,
              "a clean step-over says nothing about breakpoints");
        CHECK(out.str().find("instructions,") == std::string::npos,
              "and prints no run tally -- it is one logical step");
    }

    // RST is a one-byte call to a fixed vector -- step over it the same way. RST 1
    // (CF) calls 0008; put a RET there, and NEXT must land on the byte after the RST.
    monN.exec("DEPOSIT 0008 C9", sn);           // RET at the RST 1 vector
    monN.exec("DEPOSIT 0300 31 00 04 CF 76", sn);  // LXI SP,0400 ; RST 1 ; HLT
    monN.exec("EX 0300", sn);
    monN.exec("STEP", sn);                      // execute the LXI SP
    CHECK(cn->pc() == 0x0303, "set up: the PC is sitting on the RST");
    monN.exec("NEXT", sn);
    CHECK(cn->pc() == 0x0304, "NEXT over an RST stops one byte on -- RST is a 1-byte call");

    // Not a call: NEXT is just a single step. 3C = INR A at 0500.
    monN.exec("DEPOSIT 0500 3C 76", sn);
    monN.exec("EX 0500", sn);
    monN.exec("NEXT", sn);
    CHECK(cn->pc() == 0x0501, "NEXT on a non-call advances exactly one instruction, like STEP");

    // A REAL breakpoint inside the callee still wins -- NEXT's temp target does not
    // hide it, and the stop is reported as the breakpoint it is.
    monN.exec("EX 0200", sn);
    monN.exec("STEP", sn);                      // back onto the CALL at 0203
    CHECK(cn->pc() == 0x0203, "set up: on the CALL again");
    monN.exec("BREAK 0209", sn);                // a breakpoint in the callee
    {
        std::ostringstream out;
        monN.exec("NEXT", out);
        CHECK(cn->pc() == 0x0209,
              "a breakpoint inside the callee stops NEXT there, not at the return");
        CHECK(out.str().find("breakpoint") != std::string::npos,
              "and it is reported as the breakpoint it is, not swallowed");
    }
    monN.exec("NOBREAK", sn);

    // -----------------------------------------------------------------------
    // TRACEPOINTS -- BREAK <addr> [IF <expr>] TRACE ON|OFF.
    //
    // THE PARSE IS THE RISK. IF takes the WHOLE REST OF THE LINE as its
    // expression, so a trailing TRACE ON must be stripped before the expression
    // parser ever sees it -- otherwise it is handed `HL==8000 TRACE ON` and the
    // combination the feature was asked for is the one that cannot be typed.
    // -----------------------------------------------------------------------
    SECTION("BREAK ... TRACE ON|OFF -- a tracepoint, and IF still composes");

    monN.exec("NOBREAK", sn);
    {
        std::ostringstream out;
        monN.exec("BREAK 0400 TRACE ON", out);
        CHECK(out.str().find("trace on") != std::string::npos, "a plain tracepoint sets");
        CHECK(mn.debug.breakpoints().back().action == BreakAction::TraceOn, "with the action on it");
    }
    {
        std::ostringstream out;
        monN.exec("BREAK MEM W 2000 TRACE OFF", out);
        CHECK(mn.debug.breakpoints().back().action == BreakAction::TraceOff,
              "TRACE OFF on a CYCLE breakpoint is allowed -- a plain tracepoint reads no registers");
        CHECK(mn.debug.breakpoints().back().kind == BreakKind::MemWrite, "and the kind survives it");
    }
    {
        // The one that would break if TRACE were stripped after IF instead of before.
        std::ostringstream out;
        monN.exec("BREAK 0500 IF HL==8000 TRACE ON", out);
        const Breakpoint& b = mn.debug.breakpoints().back();
        CHECK(b.action == BreakAction::TraceOn, "IF and TRACE compose: the action is read");
        CHECK(b.cond != nullptr, "and the condition parsed");
        CHECK(b.cond->text().find("TRACE") == std::string::npos,
              "the expression does NOT swallow the TRACE ON tokens");
        CHECK(out.str().find("bad condition") == std::string::npos, "so it is not a parse error");
    }
    {
        // A bare BREAK is still a stop, and says nothing about tracing.
        std::ostringstream out;
        monN.exec("BREAK 0600", out);
        CHECK(mn.debug.breakpoints().back().action == BreakAction::Stop, "the default is still Stop");
        CHECK(out.str().find("trace") == std::string::npos, "and it does not mention tracing");
    }
    {
        // TRACE ON without an address is not a tracepoint on nothing -- it is a usage
        // error. `end` collapsing onto argi is what catches it.
        std::ostringstream out;
        monN.exec("BREAK TRACE ON", out);
        CHECK(out.str().find("usage") != std::string::npos, "BREAK TRACE ON alone is a usage error");
    }
    monN.exec("NOBREAK", sn);

    SECTION("BREAK MEM/IO ... IF|LOADS -- conditions on a cycle breakpoint");

    // IF is no longer refused on a cycle kind: it is judged at the instruction boundary
    // (core/debug.h CondWhen), so a MEM/IO breakpoint may carry one. It records a Before
    // condition -- the pre-instruction state, the inputs.
    {
        std::ostringstream out;
        monN.exec("BREAK IO R 10 IF B==5", out);
        const Breakpoint& b = mn.debug.breakpoints().back();
        CHECK(b.kind == BreakKind::IoRead && b.cond != nullptr, "IF is accepted on BREAK IO");
        CHECK(b.condWhen == CondWhen::Before, "and it is a Before condition -- the instruction's inputs");
        CHECK(out.str().find("io r   10 if B==5") != std::string::npos, "described as an IF");
    }
    // LOADS is the After condition, and only on an IO READ -- where an IN loads a register.
    {
        std::ostringstream out;
        monN.exec("BREAK IO R 10 LOADS A>7F", out);
        const Breakpoint& b = mn.debug.breakpoints().back();
        CHECK(b.condWhen == CondWhen::After, "LOADS records an After condition -- judged post-read");
        CHECK(out.str().find("io r   10 loads A>7F") != std::string::npos, "described as LOADS, not IF");
    }
    // LOADS on anything but an IO read has no value to test, and is refused with a reason.
    {
        std::ostringstream out;
        size_t before = mn.debug.breakpoints().size();
        monN.exec("BREAK IO W 10 LOADS A==0", out);
        CHECK(out.str().find("LOADS applies to BREAK IO R") != std::string::npos,
              "LOADS on an IO WRITE is refused -- a write loads nothing");
        CHECK(mn.debug.breakpoints().size() == before, "and no breakpoint was added");
    }
    monN.exec("NOBREAK", sn);

    // -----------------------------------------------------------------------
    // Breakpoint IDs restart at 1 once the set goes empty -- via NOBREAK
    // (clear all) OR removing the last one -- so the numbers do not march off
    // to the hundreds across a session of add/clear cycles.
    // -----------------------------------------------------------------------
    SECTION("NOBREAK resets the id counter when the set becomes empty");

    monN.exec("NOBREAK", sn);
    {
        monN.exec("BREAK 0100", sn);
        monN.exec("BREAK 0200", sn);
        CHECK(mn.debug.breakpoints().back().id == 2, "the second breakpoint is id 2");
        monN.exec("NOBREAK", sn);              // clears all -> counter rewinds
        std::ostringstream out;
        monN.exec("BREAK 0300", out);
        CHECK(mn.debug.breakpoints().back().id == 1,
              "after NOBREAK cleared everything, the next breakpoint is id 1 again");
    }
    {
        // Removing breakpoints one at a time: the reset fires only on the last one.
        monN.exec("NOBREAK", sn);
        monN.exec("BREAK 0100", sn);
        monN.exec("BREAK 0200", sn);
        int id1 = mn.debug.breakpoints().front().id;
        int id2 = mn.debug.breakpoints().back().id;
        monN.exec("NOBREAK " + std::to_string(id2), sn);   // set not yet empty
        monN.exec("BREAK 0300", sn);
        CHECK(mn.debug.breakpoints().back().id > id1,
              "with one breakpoint still live, the counter does NOT rewind");
        monN.exec("NOBREAK", sn);                          // now empty -> rewind
        std::ostringstream out;
        monN.exec("BREAK 0400", out);
        CHECK(mn.debug.breakpoints().back().id == 1, "emptied by NOBREAK, ids restart at 1");
    }
    monN.exec("NOBREAK", sn);

    // -----------------------------------------------------------------------
    // BOARDS names the RAM and the ROM apart, and says WHICH ROM.
    //
    // The old listing printed `mem:0000-DFFF,FF00-FFFF` -- two ranges squashed
    // into one comma list, with no way to tell which was the ROM or what was in
    // it. Both facts were in the MapEntry all along and were being dropped.
    // -----------------------------------------------------------------------
    SECTION("BOARDS -- RAM and ROM are named apart, and the ROM says which chip");

    Machine m3;
    Monitor mon3(m3);
    std::ostringstream s3;
    mon3.exec("BOARDS ADD memory mem0", s3);
    mon3.exec("REGION ADD mem0 type=ram at=0 size=56K", s3);
    mon3.exec("REGION ADD mem0 type=rom at=FF00 mount=builtin:dbl", s3);
    mon3.exec("REGION ADD mem0 type=rom at=F800", s3);  // a socket with no chip in it

    std::ostringstream bl;
    mon3.exec("BOARDS", bl);
    std::string L = bl.str();
    CHECK(L.find("UNITS") != std::string::npos && L.find("MEMORY") != std::string::npos,
          "BOARDS has a header");
    CHECK(L.find("0000-DFFF  ram  56K") != std::string::npos, "the RAM says it is RAM, and how big");
    CHECK(L.find("FF00-FFFF  rom  dbl") != std::string::npos,
          "the ROM says it is ROM, and WHICH ROM is in it");
    CHECK(L.find("2 rom: rom0, rom1(empty)") != std::string::npos,
          "the units are counted and named, and the empty socket says so");

    // AN EMPTY SOCKET DECODES NOTHING, so it is not in the memory column at all --
    // those pages float, exactly as they do on the bench.
    CHECK(L.find("F800") == std::string::npos, "an empty socket is not in the memory map");

    // Both spellings run, because BOARD is a prefix of BOARDS.
    std::ostringstream bs;
    mon3.exec("BOARD", bs);
    CHECK(bs.str() == L, "BOARD and BOARDS are the same command");

    // UNMOUNT PULLS THE CHIP; IT DOES NOT UNSOLDER THE SOCKET. Erasing the region
    // would renumber the sockets behind it -- pull rom0 and the chip sitting in
    // rom1 silently BECOMES rom0, so MOUNTing rom0 back would put it in the wrong
    // socket. The socket stays, empty, and keeps its name.
    std::ostringstream um;
    mon3.exec("UNMOUNT mem0:rom0", um);
    std::ostringstream b2;
    mon3.exec("BOARDS", b2);
    CHECK(b2.str().find("rom0(empty)") != std::string::npos, "the socket survives its chip");
    CHECK(b2.str().find("FF00") == std::string::npos,
          "and stops decoding: the pages it held now float");

    std::ostringstream rm;
    mon3.exec("MOUNT mem0:rom0 builtin:dbl", rm);
    std::ostringstream b3;
    mon3.exec("BOARDS", b3);
    CHECK(b3.str().find("FF00-FFFF  rom  dbl") != std::string::npos,
          "and the chip goes back into the SAME socket it came out of");

    // -----------------------------------------------------------------------
    // SHOW BOARD <type> lists a property's LEGAL VALUES under its help, the same
    // facts the generated ref/boards.md prints -- so an operator can read the choices
    // (e.g. a USIO built-in profile) straight off the type card, not just its prose.
    // -----------------------------------------------------------------------
    SECTION("cli: SHOW BOARD lists a property's legal values under its help");
    {
        Machine mp;
        Monitor monP(mp);
        std::ostringstream o;
        monP.exec("SHOW BOARD usio", o);
        const std::string s = o.str();
        // The enum's choices, including every built-in profile name.
        // The enum header plus each built-in name -- the list can word-wrap across lines,
        // so assert the members individually rather than one contiguous string.
        CHECK(s.find("values: custom | tuart | imsai-sio2") != std::string::npos,
              "the profile enum lists custom plus each built-in as its legal values");
        CHECK(s.find("compupro-if2") != std::string::npos, "compupro-if2 is a listed profile");
        CHECK(s.find("compupro-ss1") != std::string::npos, "compupro-ss1 is a listed profile");
        // A bounded int prints its range in the property's own radix...
        CHECK(s.find("values: 0x0 .. 0xFF") != std::string::npos,
              "a port's range is shown in hex, the radix it is written in");
        // ...a bit field in decimal, and a bool as on|off.
        CHECK(s.find("values: 0 .. 7") != std::string::npos, "a bit field's range is 0 .. 7");
        CHECK(s.find("values: on | off") != std::string::npos, "a bool is on | off");
        // A free-form endpoint string constrains nothing, so it gets NO values line:
        // `connect` is the last property, so nothing after it may advertise values.
        const size_t cAt = s.find("\n  connect ");
        CHECK(cAt != std::string::npos, "the connect property is listed");
        CHECK(s.find("values:", cAt) == std::string::npos,
              "a free-form string property advertises no values line of its own");
    }

    // -----------------------------------------------------------------------
    // SHOW BOARD <type> (the full view) shows the board's own properties, then -- if it
    // has units -- a footer NAMING them and pointing at the UNITS view for their settings.
    // The unit tables themselves live in `SHOW BOARD <type> UNITS`, not stacked here.
    // -----------------------------------------------------------------------
    SECTION("cli: SHOW BOARD names a board's units and points at the UNITS view");
    {
        Machine mu;
        Monitor monU(mu);
        std::ostringstream o;
        monU.exec("SHOW BOARD acr", o);
        const std::string s = o.str();
        // The footer names the unit and directs the reader to the UNITS view...
        CHECK(s.find("This board has units: tape") != std::string::npos,
              "the footer names the tape unit");
        CHECK(s.find("SHOW BOARD acr UNITS for their properties") != std::string::npos,
              "the footer points at the UNITS view");
        // ...but the unit's own property table is NOT stacked under the board here.
        CHECK(s.find("Unit 'tape'  (tape, MOUNT)") == std::string::npos,
              "the unit table is deferred to the UNITS view");
        CHECK(s.find("values: full | real") == std::string::npos,
              "the unit's rate choices are not in the full view");

        // A controller's [[board.drive]] schema is named in the footer too -- the disk you
        // may declare in a machine file, before any drive exists to SHOW.
        std::ostringstream od;
        Machine            md;
        Monitor            monD(md);
        monD.exec("SHOW BOARD tarbell", od);
        const std::string sd = od.str();
        CHECK(sd.find("This board has units: drive") != std::string::npos,
              "the footer names the drive sub-unit table");
        CHECK(sd.find("[[board.drive]]  (in a machine file)") == std::string::npos,
              "the drive schema itself is deferred to the UNITS view");
    }

    // -----------------------------------------------------------------------
    // SHOW BOARD <type> UNITS narrows to just the units: no board description, no
    // board-level property table, only the unit and sub-unit schemas -- with a full-width
    // rule setting each heading off so it does not get lost between two tables.
    // -----------------------------------------------------------------------
    SECTION("cli: SHOW BOARD <type> UNITS shows only the units");
    {
        Machine mu;
        Monitor monU(mu);
        std::ostringstream o;
        monU.exec("SHOW BOARD acr units", o);
        const std::string s = o.str();
        // The unit is present, with its motivating property...
        CHECK(s.find("Unit 'tape'  (tape, MOUNT)") != std::string::npos, "the tape unit is shown");
        CHECK(s.find("values: full | real") != std::string::npos, "rate's choices are shown");
        // ...but the board's OWN properties are not: `port` heads the board table only.
        CHECK(s.find("\n  port ") == std::string::npos,
              "the board's own properties are omitted in units view");
        // The unit heading sits on its own line.
        CHECK(s.find("\n  Unit 'tape'") != std::string::npos, "the unit heading is on its own line");

        // With two stacked entries (a serial unit AND a [[board.drive]] schema), two blank
        // lines set the second heading off from the table above it.
        std::ostringstream of;
        Machine            mf;
        Monitor            monF(mf);
        monF.exec("SHOW BOARD 16fdc units", of);
        CHECK(of.str().find("\n\n\n  [[board.drive]]") != std::string::npos,
              "two blank lines set a stacked heading off from the table above");

        // The PROPERTY/HELP header prints once, over the first unit, not per-unit -- so a
        // board with several units reads as one list. sol has five.
        std::ostringstream os;
        Machine            ms;
        Monitor            monS(ms);
        monS.exec("SHOW BOARD sol units", os);
        const std::string ss = os.str();
        CHECK(ss.find("PROPERTY") != std::string::npos, "the header is present");
        CHECK(ss.find("PROPERTY", ss.find("PROPERTY") + 1) == std::string::npos,
              "the header is printed only once, not per unit");
        // A serial unit's heading names its verb: CONNECT, not MOUNT.
        CHECK(ss.find(", CONNECT)") != std::string::npos,
              "a serial unit's heading names its verb");

        // A prefix of UNITS works -- `u` is the shortest -- first match wins.
        std::ostringstream ou;
        Monitor            monU2(mu);
        monU2.exec("SHOW BOARD acr u", ou);
        CHECK(ou.str().find("Unit 'tape'  (tape, MOUNT)") != std::string::npos,
              "a prefix of UNITS is accepted");

        // A board with no unit properties says so, plainly, rather than printing nothing.
        std::ostringstream oe;
        Machine            me;
        Monitor            monE(me);
        monE.exec("SHOW BOARD fp units", oe);
        CHECK(oe.str().find("has no unit properties") != std::string::npos,
              "an empty units view is explained");

        // An unknown trailing word is a usage error, not a silent full listing.
        std::ostringstream ob;
        Machine            mb;
        Monitor            monB(mb);
        monB.exec("SHOW BOARD acr wat", ob);
        CHECK(ob.str().find("SHOW BOARD <type> [units]") != std::string::npos,
              "a bogus option prints usage");
        CHECK(ob.str().find("unit 'tape'") == std::string::npos,
              "a bogus option does not fall through to a listing");
    }

    // -----------------------------------------------------------------------
    // HELP <cmd> <sub>: extra words narrow the help to that sub-level. The text is
    // FILTERED from the command's own detail block (commands.cpp), never a second list,
    // so `HELP SHOW BOARD` shows the BOARD line(s) and drops the unrelated siblings. A
    // sub-topic that matches nothing falls back to the whole block, today's behaviour.
    // -----------------------------------------------------------------------
    SECTION("cli: HELP narrows to the sub-level you asked for");
    {
        Machine mh;
        Monitor monH(mh);

        // Baseline: bare HELP SHOW is the whole menu -- many siblings at once.
        std::ostringstream all;
        monH.exec("HELP SHOW", all);
        CHECK(all.str().find("SHOW BOARD") != std::string::npos, "HELP SHOW lists BOARD");
        CHECK(all.str().find("SHOW MOUNTS") != std::string::npos, "HELP SHOW lists MOUNTS");
        CHECK(all.str().find("SHOW PATHS") != std::string::npos, "HELP SHOW lists PATHS");

        // HELP SHOW BOARD keeps only the board lines and drops the siblings.
        std::ostringstream bd;
        monH.exec("HELP SHOW BOARD", bd);
        CHECK(bd.str().find("SHOW BOARD") != std::string::npos,
              "HELP SHOW BOARD keeps the BOARD line");
        CHECK(bd.str().find("SHOW MOUNTS") == std::string::npos,
              "HELP SHOW BOARD drops the MOUNTS sibling");
        CHECK(bd.str().find("SHOW PATHS") == std::string::npos,
              "HELP SHOW BOARD drops the PATHS sibling");

        // A third word narrows further: HELP SHOW BUS IRQ is the IRQ line, not MAP.
        std::ostringstream irq;
        monH.exec("HELP SHOW BUS IRQ", irq);
        CHECK(irq.str().find("VI0") != std::string::npos, "HELP SHOW BUS IRQ keeps the IRQ line");
        CHECK(irq.str().find("who decodes what") == std::string::npos,
              "HELP SHOW BUS IRQ drops the MAP sibling");

        // A sub-topic that matches nothing falls back to the full block, not an error.
        std::ostringstream no;
        monH.exec("HELP SHOW nonsense", no);
        CHECK(no.str().find("SHOW MOUNTS") != std::string::npos,
              "an unmatched sub-topic falls back to the whole of SHOW");
    }

    // -----------------------------------------------------------------------
    // SHOW PATHS: "built in to the binary" is a claim about ORIGIN -- whether a file
    // was loaded -- NOT about whether the machine file's dirname is empty. A file NAMED
    // IN THE CWD (`altairsim foo.toml` from foo.toml's own folder, which is how every
    // example README starts) has an empty dirname and is still a file; reading empty-dir
    // as built-in made SHOW PATHS lie precisely there. `fromFile` carries the fact.
    // -----------------------------------------------------------------------
    SECTION("cli: SHOW PATHS tells a cwd machine file from a built-in machine");
    {
        std::string err;
        const char* kText = "[machine]\nname = \"t\"\n";

        // Built-in: the source is a scheme, not a file. SHOW PATHS says so.
        Machine mbi;
        CHECK(loadTomlText(kText, "builtin:default", mbi, err), "a built-in source loads");
        CHECK(!mbi.fromFile, "a builtin: source is not a file");
        Monitor            monbi(mbi);
        std::ostringstream obi;
        monbi.exec("SHOW PATHS", obi);
        CHECK(obi.str().find("built in to the binary") != std::string::npos,
              "a built-in machine reports it is built in");

        // A file NAMED IN THE CWD -- a bare filename, empty dirname. It is NOT built in.
        Machine mcwd;
        CHECK(loadTomlText(kText, "trek80.toml", mcwd, err), "a cwd file source loads");
        CHECK(mcwd.fromFile, "a bare .toml source is a file");
        CHECK(mcwd.dir.empty(), "...with an empty dirname -- which is the whole trap");
        Monitor            moncwd(mcwd);
        std::ostringstream ocwd;
        moncwd.exec("SHOW PATHS", ocwd);
        CHECK(ocwd.str().find("built in to the binary") == std::string::npos,
              "a machine file in the cwd is NOT reported built in (the bug this fixes)");
        CHECK(ocwd.str().find("machine file") != std::string::npos,
              "...it prints a machine file row instead");

        // A file named THROUGH a directory keeps behaving as before: its dir is shown.
        Machine msub;
        CHECK(loadTomlText(kText, "examples/sol/trek80.toml", msub, err), "a subdir file loads");
        CHECK(msub.fromFile, "a path'd .toml is a file");
        CHECK(msub.dir == "examples/sol", "its dirname is carried through");
        Monitor            monsub(msub);
        std::ostringstream osub;
        monsub.exec("SHOW PATHS", osub);
        CHECK(osub.str().find("built in to the binary") == std::string::npos,
              "a machine file in a subdir is not built in either");
        // SHOW PATHS renders the resolved absolute path with NATIVE separators, so on Windows
        // the row reads `...\examples\sol`. Normalise to forward slashes before matching -- the
        // claim is that the directory is shown, not which slash the host spells it with.
        std::string osubNorm = osub.str();
        for (char& ch : osubNorm)
            if (ch == '\\') ch = '/';
        CHECK(osubNorm.find("examples/sol") != std::string::npos,
              "...and its directory is shown, resolved absolute");
    }

    // -----------------------------------------------------------------------
    // A REFUSED `MOUNT ... CREATE` MUST NOT LEAVE ITS ZERO-BYTE FILE BEHIND.
    //
    // CREATE pre-creates the file so the board can open it; a controller with a
    // fixed geometry (hdsk) then refuses the 0-byte image. The file we made has to
    // go -- and ONLY the file we made: a file that was already there is the
    // operator's, and survives a failed mount. (monitor.cpp, the MOUNT handler.)
    // -----------------------------------------------------------------------
    SECTION("cli: a refused MOUNT ... CREATE leaves no zero-byte file behind");
    {
        // This test touches the real filesystem (CREATE writes a file, we check it is
        // gone), so it needs the real resolver -- not whatever MemoryMedia stub a board
        // test ran before us in the same process may have left behind. tests/main.cpp
        // installs openHostFile by default; make it explicit so ordering cannot break us.
        setMediaResolver(openHostFile);

        std::filesystem::path tmp =
            std::filesystem::temp_directory_path() / "altairsim-create-turd.dsk";
        std::error_code ec;
        std::filesystem::remove(tmp, ec);

        Machine            m;
        Monitor            mon(m);
        std::ostringstream sink;
        mon.exec("BOARDS ADD hdsk h0", sink);

        // hdsk has a fixed multi-megabyte geometry, so a 0-byte CREATE is refused -- and the
        // refusal names the MISTAKE (you cannot make a blank platter here), not the symptom
        // "0 bytes", which is just the size of the file CREATE made and we are deleting.
        std::ostringstream o;
        mon.exec("MOUNT h0:drive0 \"" + tmp.string() + "\" CREATE", o);
        CHECK(o.str().find("cannot make a blank platter") != std::string::npos,
              "hdsk refuses the zero-byte image CREATE made, naming the mistake not the size");
        CHECK(!std::filesystem::exists(tmp, ec),
              "...and the zero-byte file it created is removed, not left as a turd");

        // BUT ONLY THE FILE WE MADE. Pre-place a file: CREATE sees it exists and
        // leaves it, the mount still fails on size, and the operator's file must
        // survive -- we unlink our own litter, never theirs.
        { std::ofstream(tmp) << "not a disk"; }
        std::ostringstream o2;
        mon.exec("MOUNT h0:drive0 \"" + tmp.string() + "\" CREATE", o2);
        CHECK(std::filesystem::exists(tmp, ec),
              "a pre-existing file that fails to mount is the operator's -- NOT removed");
        std::filesystem::remove(tmp, ec);
    }

    // -----------------------------------------------------------------------
    // A VERB EXISTS ONLY WHILE THE CARD THAT BRINGS IT IS IN A SLOT (core/board.h).
    //
    // This is the whole claim of board-injected commands, and it is why REWIND is not
    // in the static table: a machine with no cassette in it cannot rewind anything,
    // and should say so rather than offer a verb that always fails.
    // -----------------------------------------------------------------------
    SECTION("cli: the cards bring their own verbs, and take them away again");
    {
        Machine m4;
        Monitor mon4(m4);

        // No 88-ACR in the machine: there is no such command. Not "no tape" -- no
        // COMMAND. Nothing in this machine has ever heard of rewinding.
        std::ostringstream no;
        mon4.exec("REW", no);
        CHECK(no.str().find("unknown command") != std::string::npos,
              "with no cassette card in the machine, REW is not a command at all");

        std::string err;
        m4.add("acr", "acr0", err);

        // ...and now it is. Nothing was recompiled and no table was edited.
        std::ostringstream yes;
        mon4.exec("REW acr0:tape", yes);
        CHECK(yes.str().find("unknown command") == std::string::npos,
              "plug the card in and the verb is there");
        CHECK(yes.str().find("no cassette") != std::string::npos,
              "...and it runs, and complains about the TAPE -- not about the word");

        // THE STATIC MENU STILL WINS, and this is the guarantee that lets a card be
        // plugged in safely at all. The built-ins own R (RUN), RE (REGS) and RES
        // (RESET). The cassette gets REW, and only because nothing built-in claims
        // those three letters.
        std::ostringstream re;
        mon4.exec("RE", re);
        CHECK(re.str().find("unknown") == std::string::npos && no.str() != re.str(),
              "RE is still REGS -- a card cannot move a built-in abbreviation");

        // HELP finds it, or a verb you can type is a verb you cannot look up.
        std::ostringstream h;
        mon4.exec("HELP REW", h);
        CHECK(h.str().find("REW[IND]") != std::string::npos,
              "HELP REW shows the abbreviation the resolver actually honours");

        // And when the card comes out, so does the verb.
        m4.remove("acr0", err);
        std::ostringstream gone;
        mon4.exec("REW acr0:tape", gone);
        CHECK(gone.str().find("unknown command") != std::string::npos,
              "pull the card and REWIND goes with it");
    }

    // -----------------------------------------------------------------------
    // NAMING A CARD: CASE-BLIND, AND THE INDEX IS OPTIONAL (cli/monitor.cpp).
    //
    // Three claims, and the third is the one with teeth:
    //
    //   - `ACR0` and `acr0` are ONE BOARD. That is an identity (core/machine.cpp),
    //     not a kindness, which is why adding both is refused.
    //   - `acr` finds acr0, because the `0` was only ever there to tell two cassettes
    //     apart, and there are not two. It is NOT prefix matching: `ac` finds nothing.
    //   - and the moment there ARE two, the short name STOPS WORKING and says why.
    //     A shorthand that silently picked one would be worse than no shorthand.
    // -----------------------------------------------------------------------
    SECTION("cli: a board is named case-blind, and its index is optional");
    {
        Machine m5;
        Monitor mon5(m5);
        std::string err;
        m5.add("acr", "acr0", err);
        m5.add("dcdd", "dsk0", err);
        m5.add("2sio", "sio0", err);

        // The bug this all started from: MOUNT in the case the user actually typed.
        // The tape file does not exist, and that is fine -- the ERROR IS ABOUT THE
        // FILE, which is proof the name resolved all the way to the card.
        for (const char* spec : {"ACR0:TAPE", "acr0:TAPE", "ACR:tape", "ACR"}) {
            std::ostringstream o;
            mon5.exec(std::string("MOUNT ") + spec + " nosuch.tap", o);
            CHECK(o.str().find("no board") == std::string::npos &&
                      o.str().find("has no unit") == std::string::npos &&
                      o.str().find("expected") == std::string::npos,
                  "every spelling of the card and its tape reaches acr0:tape");
            CHECK(o.str().find("acr0") == 0, "...and the card answers under its own name");
        }

        // A LONE UNIT NEEDS NO NAMING -- but four drives do, and two serial ports do.
        std::ostringstream d;
        mon5.exec("MOUNT DSK0 cpm.dsk", d);
        CHECK(d.str().find("drive0 drive1 drive2 drive3") != std::string::npos,
              "a card with four drives will not guess -- and it names all four");

        std::ostringstream c;
        mon5.exec("CONNECT SIO console", c);
        CHECK(c.str().find("a b") != std::string::npos,
              "...nor will a 2SIO, whose two ports are both serial");

        // The kind filter is what makes the inference safe: a 2SIO has units, and
        // NONE of them is something you could put a tape in.
        std::ostringstream ms;
        mon5.exec("MOUNT SIO x.dsk", ms);
        CHECK(ms.str().find("nothing you can mount into") != std::string::npos,
              "a card with no mountable unit says so, rather than picking a serial port");

        // THIS IS NOT PREFIX MATCHING. Only the trailing INDEX may be dropped.
        std::ostringstream ac;
        mon5.exec("MOUNT AC:TAPE x.tap", ac);
        CHECK(ac.str().find("no board 'AC'") != std::string::npos,
              "`ac` is not `acr` -- an index may be dropped, letters may not");

        // Every command that names a card goes through the one resolver.
        std::ostringstream sh;
        mon5.exec("SHOW ACR", sh);
        CHECK(sh.str().find("acr0") != std::string::npos, "SHOW resolves it");
        std::ostringstream rw;
        mon5.exec("REW ACR", rw);
        CHECK(rw.str().find("no cassette") != std::string::npos,
              "a board's own verb resolves it, and complains about the TAPE");

        // ONE BOARD. Adding acr0 twice under two spellings is not two cards.
        std::string dup;
        CHECK(m5.add("acr", "ACR0", dup) == nullptr,
              "ACR0 and acr0 are the same board, so the second one is refused");

        // ...and now a SECOND cassette, which is what the index was for all along.
        m5.add("acr", "acr1", err);
        std::ostringstream amb;
        mon5.exec("MOUNT ACR:TAPE x.tap", amb);
        CHECK(amb.str().find("ambiguous") != std::string::npos &&
                  amb.str().find("acr0") != std::string::npos &&
                  amb.str().find("acr1") != std::string::npos,
              "with two cassettes the short name stops working, and names both");

        std::ostringstream ok;
        mon5.exec("MOUNT ACR1:TAPE nosuch.tap", ok);
        CHECK(ok.str().find("acr1") == 0, "the long name still says exactly which");

        // BOARDS REMOVE resolves like everything else, and removes the CARD it found
        // -- not the string that was typed.
        std::ostringstream rm5;
        mon5.exec("BOARDS REMOVE ACR1", rm5);
        CHECK(rm5.str().find("acr1: removed") != std::string::npos,
              "BOARDS REMOVE resolves the name, and reports the card's own");
        std::ostringstream back;
        mon5.exec("REW ACR", back);
        CHECK(back.str().find("no cassette") != std::string::npos,
              "...and with the second card gone, the short name works again");
    }

    // -----------------------------------------------------------------------
    // SHOW BUS IRQ (DESIGN.md 10, cli/monitor.cpp).
    //
    // The interrupt wiring is the one part of the backplane with no other window
    // onto it: eight wires and a pin, none of them addressable, and the two ways
    // of mis-wiring it BOTH FAIL IN SILENCE. So the warnings are tested as hard as
    // the table is -- a view that renders a dead wire beautifully and says nothing
    // about it being dead would be worse than no view.
    // -----------------------------------------------------------------------
    SECTION("cli: SHOW BUS IRQ -- the eight wires, and the two silent ways to get them wrong");
    {
        // A machine wired like machines/ps2int.toml: 88-VI at FE, console 6850 on VI7.
        struct Rig {
            Machine         m;
            VirtcBoard*     vi  = nullptr;
            Sio2Board*      sio = nullptr;
            ScriptedStream* tty = nullptr;

            explicit Rig(bool withVi) {
                std::string err;
                m.bus.setVerify(true);  // eight wires: re-derive them all, every step
                auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
                Region r;
                r.kind = RegionKind::Ram;
                r.at   = 0;
                r.size = 0x10000;
                mem->addRegion(r, err);
                m.add("8080", "cpu0", err);
                if (withVi) vi = dynamic_cast<VirtcBoard*>(m.add("virtc", "vi0", err));
                sio = dynamic_cast<Sio2Board*>(m.add("2sio", "sio0", err));
                setProperty(*sio, "port", "10", err);
                auto s = std::make_unique<ScriptedStream>();
                tty    = s.get();
                sio->channel("a")->connect(std::move(s));
                m.power();
            }
            std::string irq() {
                Monitor           mon(m);
                std::ostringstream o;
                mon.exec("SHOW BUS IRQ", o);
                return o.str();
            }
            // Through the MONITOR, not through setUnitProperty -- so the CLI path that
            // an operator actually uses is the one under test.
            void strap(const char* where) {
                Monitor            mon(m);
                std::ostringstream o;
                mon.exec(std::string("SET sio0:a interrupt=") + where, o);
            }
            void ctl(uint8_t v) { m.bus.ioWrite(0xFE, v); }
        };

        // ---- the vector table. A row a floating bus could not have produced. ----
        {
            Rig         g(true);
            std::string s = g.irq();
            CHECK(s.find("VI0   RST 0  C7 -> 0000") != std::string::npos,
                  "VI0 is RST 0 at 0000 -- and it is the HIGHEST priority, not the lowest");
            CHECK(s.find("VI2   RST 2  D7 -> 0010") != std::string::npos,
                  "VI2 is RST 2 (D7) at 0010");
            CHECK(s.find("VI7   RST 7  FF -> 0038") != std::string::npos,
                  "VI7 is RST 7 at 0038 -- which is what the PS2 ReadMe means by 'vector 7'");
        }

        // ---- the strap is found generically, through Property::irqJumper ----
        {
            Rig g(true);
            g.strap("vi7");
            std::string s = g.irq();
            CHECK(s.find("VI7   RST 7  FF -> 0038  sio0:a") != std::string::npos,
                  "the 6850 on channel a is named on the line it is soldered to");
            CHECK(s.find("88-VI   vi0") != std::string::npos, "and the card that watches it");
            CHECK(s.find("WARNINGS") == std::string::npos,
                  "a correctly wired machine is not nagged at");
        }

        // ---- THE DEAD WIRE. This tree shipped it for months. ----
        {
            Rig g(false);  // no 88-VI in the machine
            g.strap("vi7");
            std::string s = g.irq();
            CHECK(s.find("WARNINGS") != std::string::npos,
                  "a vi* strap with nothing watching the VI lines is a BUG, and is reported");
            CHECK(s.find("sio0:a is strapped to VI7") != std::string::npos &&
                      s.find("goes nowhere") != std::string::npos,
                  "...by name, and by line, and it says exactly what is wrong with it");
        }

        // ---- THE FORBIDDEN MIX. The manual is blunt about it. ----
        {
            Rig g(true);
            g.strap("int");
            std::string s = g.irq();
            CHECK(s.find("strapped to pINT while an 88-VI (vi0) is present") != std::string::npos,
                  "an `int` strap in a VI machine is forbidden, and SHOW BUS IRQ quotes the "
                  "manual saying so");
        }

        // ---- LIVE: a character arrives, and the whole chain lights up ----
        {
            Rig g(true);
            g.strap("vi7");
            g.ctl(0x80);  // bit 7: enable the VI structure. Compare off (bit 3 clear).

            g.m.bus.ioWrite(0x10, 0x03);  // 6850 master reset
            g.m.bus.ioWrite(0x10, 0x95);  // 8N2, and the RECEIVE INTERRUPT enabled
            g.tty->feed("A");
            g.sio->pump();
            for (int i = 0; i < 200; ++i) g.m.clock.advance(1000);  // it clocks in on its own

            std::string s = g.irq();
            CHECK(g.vi->intWinner() == 7, "the encoder picks VI7");
            CHECK(s.find("pINT    ASSERTED") != std::string::npos, "pin 73 is pulled...");
            CHECK(s.find("pulled by vi0") != std::string::npos, "...by the 88-VI, not by the 6850");
            CHECK(s.find("VI7   RST 7  FF -> 0038  sio0:a               WINS") != std::string::npos,
                  "and VI7 is the line that wins");
            CHECK(s.find("vi0 will jam FF") != std::string::npos,
                  "and the view says which byte the acknowledge will produce");

            // ---- MASKED: the same wire, still pulled, and now refused. ----
            //
            // This is what the PS2 monitor's own ISR does on entry (level 7, bit 3
            // set) so that another level 7 cannot interrupt it -- and it is invisible
            // in every other view: the wire is still pulled, the CPU just never sees
            // it. Getting this wrong looks exactly like a lost interrupt.
            g.ctl(0x88);  // level 7, and bit 3 makes the compare live
            std::string t = g.irq();
            CHECK(g.vi->intWinner() == -1, "nothing at or below the current level may interrupt");
            CHECK(t.find("MASKED") != std::string::npos,
                  "the line is still PULLED -- and the view says it is masked, not idle");
            CHECK(t.find("pINT    idle") != std::string::npos, "and pin 73 has dropped");
        }

        // ---- a backplane with no CPU is legal, and still has wiring worth seeing ----
        {
            Machine            m5;
            Monitor            mon5(m5);
            std::ostringstream o;
            mon5.exec("SHOW BUS IRQ", o);
            CHECK(o.str().find("CPU     (none)") != std::string::npos,
                  "no processor: say so, and print the wires anyway");
            CHECK(o.str().find("88-VI   (none)") != std::string::npos,
                  "no 88-VI: an acknowledged interrupt would float FF = RST 7");
        }
    }

    // -----------------------------------------------------------------------
    // THE CPU CARD CARRIES BOTH SLEEPING POLICIES (Patrick, 2026-07-13)
    // -----------------------------------------------------------------------
    // `clock_hz` is the crystal: does the run loop keep time? `idle` is the nap: does
    // it stand down when the guest has nothing to do but poll an empty keyboard? Two
    // questions, two properties, one card -- and the card publishes BOTH to the Clock,
    // on power and on every SET, which is what makes `SET cpu0 idle=off` bite mid-run.
    SECTION("cli: the CPU card publishes the crystal AND the idle nap, and they are separate");
    {
        Machine            m6;
        Monitor            mon6(m6);
        std::ostringstream o;
        std::string        err;
        m6.add("8080", "cpu0", err);
        m6.power();

        CHECK(m6.clock.free(), "flat out is the default (clock_hz = 0)");
        CHECK(m6.clock.idle(), "...and a machine at a prompt STANDS DOWN by default");

        // The knob. Before this, CP/M at `A0>` spun a host core at 100% for ever, and
        // there was no way to say otherwise -- because there was nothing to say it to.
        mon6.exec("SET cpu0 idle=off", o);
        CHECK(!m6.clock.idle(), "SET cpu0 idle=off reaches the run loop's policy");

        // AND THE CRYSTAL MUST NOT PUT IT BACK. Same card, same publish, two policies:
        // an operator who asked for the spin has to keep it when they ask for 2 MHz.
        mon6.exec("SET cpu0 clock_hz=2000000", o);
        CHECK(!m6.clock.free(), "the crystal is real now");
        CHECK(!m6.clock.idle(), "and it did not quietly turn the nap back on");

        mon6.exec("SET cpu0 idle=on", o);
        CHECK(m6.clock.idle() && !m6.clock.free(),
              "a 2 MHz machine idles too -- the two are orthogonal");
    }

    // TYPE puts keystrokes in the console's input buffer, as though a key were pressed --
    // which is how a machine file's `startup` reaches a program the monitor cannot, like
    // SOLOS `XE` (examples/sol). Here the whole point is the DECODING: the escapes turn
    // into control bytes, so `XE TRK80\r` ends in a real carriage return and SOLOS runs it.
    SECTION("cli: TYPE injects keystrokes at the guest, escapes decoded");
    {
        Console& con = Console::instance();
        uint8_t  drop;
        while (con.read(&drop, 1)) {}     // start from an empty buffer

        Machine            mt;
        Monitor            mont(mt);
        std::ostringstream ts;

        mont.exec("TYPE \"XE TRK80\\r\"", ts);
        std::string got;
        uint8_t     b;
        while (con.read(&b, 1)) got += (char)b;
        CHECK(got == "XE TRK80\r", "the text verbatim, with \\r decoded to a carriage return");

        // \t decodes; an unknown escape (\z) keeps its backslash rather than vanishing.
        mont.exec("TYPE \"a\\tb\\zc\"", ts);
        got.clear();
        while (con.read(&b, 1)) got += (char)b;
        CHECK(got == "a\tb\\zc", "a tab, and an unknown escape left as written");
    }
}

// ---------------------------------------------------------------------------
// THE IDLE JUDGEMENT (cli/monitor.h, guestIsWaiting).
//
// This is the run loop's decision to stand down and stop pinning a host core while the
// guest spins on a UART status bit waiting for a human. It used to be an inline expression
// in runMachine() that nothing could reach, and it was WRONG in a way no test could have
// been written to catch -- so the first move was to make it a pure function of one slice's
// four deltas. It is a policy, so it gets pinned like one.
//
// THE ONE THAT MATTERS IS `received`. A guest taking XMODEM down a wire prints nothing for
// a whole 128-byte block and polls its line exactly as a CP/M prompt polls the keyboard: by
// every other signal here it IS a prompt. The only thing that tells them apart is that one
// of them IS GETTING BYTES -- and if the run loop naps through a transfer it drags it to a
// crawl (measured: 7.7 kB/s -> 250 B/s in an early draft).
// ---------------------------------------------------------------------------
void test_idle_judgement() {
    SECTION("guestIsWaiting: a prompt naps, a transfer never does");

    // A CP/M prompt: 2,000 instructions, said nothing, got nothing, and hit an empty line
    // ~600 times (a CONIN spin is three instructions).
    CHECK(guestIsWaiting({2000, 0, 0, 600}), "a CONIN spin IS waiting -- this is the nap's whole job");

    // THE TRANSFER. Identical in every respect but one: bytes are arriving.
    CHECK(!guestIsWaiting({2000, 0, 1, 600}),
          "ONE byte arriving means it is NOT a prompt, however quiet and however hungry it looks");
    CHECK(!guestIsWaiting({2000, 0, 128, 600}), "...and a whole XMODEM block certainly is not");

    // A guest with something to say is working, not waiting -- excluded before we even count.
    CHECK(!guestIsWaiting({2000, 1, 0, 600}), "a guest that PRINTED something is not waiting");

    // A stopped machine is not an idle one. A slice that retired nothing hit a breakpoint or
    // a HLT, and napping on it would be napping on a machine that is not running at all.
    CHECK(!guestIsWaiting({0, 0, 0, 0}), "a slice that retired NO instructions is stopped, not waiting");

    // THE RATIO IS THE DISCRIMINATION. A program that computes and checks for an abort key
    // every few hundred instructions must never be taken for a prompt.
    CHECK(!guestIsWaiting({2000, 0, 0, 4}),
          "a program that computes and peeks at the keyboard now and then is WORKING");
    // The bar is exactly `hungry * 32 >= steps` -- 2,000/32 = 62.5, so 62 is working and 63
    // is a spin. Pinned on both sides, because an off-by-one here is a machine that either
    // naps through real work or never naps at all.
    CHECK(!guestIsWaiting({2000, 0, 0, 62}), "...62 empty polls in 2,000 instructions is still working");
    CHECK(guestIsWaiting({2000, 0, 0, 63}),
          "...but 63 is one poll every 32 instructions -- a spin, and that is the bar");
}

// ---------------------------------------------------------------------------
// THE THROTTLE DECISION (cli/monitor.h, shouldPace).
//
// Whether the run loop sleeps to hold the machine to the CPU card's crystal. Extracted
// pure for the same reason as guestIsWaiting: it was an inline `anyConsole && tty` that
// nothing could test, and it was WRONG -- a machine whose only line was a socket or a real
// serial port has no console by that test, so it paced against nothing and ran flat out no
// matter what clock_hz you set (bug #6, the 13,086%-of-asked measurement).
// ---------------------------------------------------------------------------
void test_should_pace() {
    SECTION("shouldPace: pace for anything real-time, but never for a script or a free crystal");

    // free() wins over everything: flat out is the default and no line changes that.
    CHECK(!shouldPace(true,  true,  true,  /*free=*/true), "free-running never throttles, console or not");
    CHECK(!shouldPace(false, false, false, /*free=*/true), "...and a bare machine certainly does not");

    // The interactive console: a human at the host keyboard. Needs BOTH the console line
    // and a host tty.
    CHECK(shouldPace(true, true, false, /*free=*/false), "interactive console + crystal -> pace");

    // A PIPED console -- console line, no tty -- is a script. It has no wall clock to keep
    // step with, and pacing it would only make `-c` runs and CPU tests slow.
    CHECK(!shouldPace(true, false, false, /*free=*/false),
          "a piped console is a script, not a clock to match -- stays flat out");

    // THE BUG. A socket someone dialed into, or a real serial port: real-time, but NO
    // console and NO host tty. This is the case that used to pace against nothing.
    CHECK(shouldPace(false, false, true, /*free=*/false),
          "a socket/serial line IS real-time -> pace, even with no console and no tty");
    CHECK(shouldPace(false, true, true, /*free=*/false), "...tty or not, a remote line paces");

    // No line of any kind, crystal asked for: a headless CPU-ish run. Nothing to pace for.
    CHECK(!shouldPace(false, true, false, /*free=*/false),
          "a tty but no line at all -- nothing real-time to keep step with");
}

// The achieved crystal, as the reflection layer sees it: read-only, and the run loop's
// measurement reaches it through the CpuCard seam -- not the wall-clock timing itself
// (that needs a run loop and a real clock), but the plumbing SHOW depends on.
static std::string cliProp(Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().text(p.radix);
    return "(no such property)";
}

void test_achieved_hz() {
    SECTION("achieved_hz: the crystal you got, read-only, reached through CpuCard");

    std::string err;
    Machine     m;
    Board*      cpu = m.add("8080", "cpu0", err);
    CHECK(cpu != nullptr, "a CPU card goes in");

    // The run loop finds the CARD, not just the running core, to hand back what it
    // measured. cpuCard() must be that same board.
    CpuCard* card = m.cpuCard();
    CHECK(card != nullptr, "cpuCard() finds the CPU card");
    CHECK(dynamic_cast<Board*>(card) == cpu, "...and it is the very board we added");

    // Before it has run, the honest answer is 0 -- "not measured", not a missing value.
    CHECK(cliProp(*cpu, "achieved_hz") == "0", "achieved_hz reads 0 until the machine runs");
    CHECK(card->achievedHz() == 0, "...and the card agrees");

    // The run loop's report is what SHOW then reads back -- the same number, through the
    // reflection layer that SHOW and the MCP server use, so the test cannot see a value
    // the operator cannot.
    card->reportAchievedHz(1500000);
    CHECK(cliProp(*cpu, "achieved_hz") == "1500000", "a reported rate shows through the property");
    CHECK(card->achievedHz() == 1500000, "...and reportAchievedHz round-trips");

    // READ-ONLY IS THE ABSENCE OF A SETTER, and the ONE property path enforces it: you
    // cannot SET a measurement, and CONFIG SAVE will not write one back (config/toml.cpp).
    CHECK(!setProperty(*cpu, "achieved_hz", "42", err),
          "achieved_hz is read-only -- SET is refused");
    CHECK(card->achievedHz() == 1500000, "...and the refused SET did not perturb it");

    // A backplane with no processor has no crystal to have achieved anything.
    Machine bare;
    CHECK(bare.cpuCard() == nullptr, "no CPU card -> cpuCard() is null, like cpu()");

    // -----------------------------------------------------------------------
    // LOAD and SAVE: FORMAT=, and ROM (DESIGN.md 10.2)
    //
    // FORMAT= was ADVERTISED IN THE HELP AND PARSED NOWHERE -- you could type
    // FORMAT=HEX and it was dropped on the floor without a word. ROM replaced
    // `RAW <id>`: same burner, no board id, no board-local offsets.
    // -----------------------------------------------------------------------
    SECTION("LOAD/SAVE -- FORMAT= overrides, and ROM programs a ROM");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "altairsim-loadtest";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string hex = (dir / "img.hex").generic_string();
        const std::string dat = (dir / "out.dat").generic_string();

        {
            std::ofstream f(hex);
            f << ":02010000AABB98\n:00000001FF\n";  // AA BB at 0100
        }

        Machine mm;
        Monitor mon(mm);
        std::ostringstream o;
        mon.exec("BOARDS ADD 8080 cpu0", o);
        mon.exec("BOARDS ADD memory mem0", o);
        mon.exec("REGION ADD mem0 type=ram at=0 size=32K", o);
        // dbl is a HEX file and places ITSELF at FF00, so the region must say FF00 --
        // ask for anywhere else and REGION ADD refuses. Check the socket really exists:
        // every ROM assertion below would pass just as happily against an unmapped hole,
        // which is exactly the false pass this test shipped with for ten minutes.
        std::ostringstream rr;
        mon.exec("REGION ADD mem0 type=rom at=FF00 mount=builtin:dbl", rr);
        CHECK(rr.str().find("rom") != std::string::npos && rr.str().find("FF00") != std::string::npos,
              "the ROM socket is real and populated -- not an unmapped hole");

        // AT relocates a HEX file to where you said, through the monitor.
        std::ostringstream a1;
        mon.exec("LOAD " + hex + " AT 200", a1);
        CHECK(mm.bus.memRead(0x0200) == 0xAA && mm.bus.memRead(0x0201) == 0xBB,
              "LOAD ... AT relocates a HEX file to the address you asked for");

        // FORMAT=BIN forces the same file to load as the literal text it is.
        std::ostringstream a2;
        mon.exec("LOAD " + hex + " AT 300 FORMAT=BIN", a2);
        CHECK(mm.bus.memRead(0x0300) == ':',
              "FORMAT=BIN overrides the sniff -- the file loads as the ASCII it literally is");

        std::ostringstream a3;
        mon.exec("LOAD " + hex + " FORMAT=SREC", a3);
        CHECK(a3.str().find("BIN or HEX") != std::string::npos,
              "and a format that is not BIN or HEX is refused, not ignored");

        // THROUGH THE BUS, A ROM DOES NOT TAKE THE WRITE. It never answers the cycle.
        uint8_t romWas = mm.bus.memRead(0xFF00);
        std::ostringstream b1;
        mon.exec("LOAD " + hex + " AT FF00", b1);
        CHECK(mm.bus.memRead(0xFF00) == romWas, "a plain LOAD does not reach a ROM...");
        CHECK(b1.str().find("landed nowhere") != std::string::npos,
              "...and it SAYS SO rather than half-loading in silence");
        CHECK(b1.str().find("LOAD") != std::string::npos && b1.str().find("ROM") != std::string::npos,
              "...and it names the thing that would have worked");

        // ...and the burner does.
        std::ostringstream b2;
        mon.exec("LOAD " + hex + " AT FF00 ROM", b2);
        CHECK(mm.bus.memRead(0xFF00) == 0xAA && mm.bus.memRead(0xFF01) == 0xBB,
              "LOAD ... ROM programs it: the operator can write ROM, the guest cannot");

        // Nobody home is a different bug from a ROM, and it says which.
        std::ostringstream b3;
        mon.exec("LOAD " + hex + " AT 9000 ROM", b3);
        CHECK(b3.str().find("no board answers here") != std::string::npos,
              "burning where no chip exists says so, and does not invent one");

        // DEPOSIT is the panel switch and keeps its own honesty; ROM is the override.
        std::ostringstream c1;
        mon.exec("DEPOSIT FF10 42", c1);
        CHECK(mm.bus.memRead(0xFF10) != 0x42, "DEPOSIT is a bus write, so a ROM ignores it");
        std::ostringstream c2;
        mon.exec("DEPOSIT FF10 42 ROM", c2);
        CHECK(mm.bus.memRead(0xFF10) == 0x42, "DEPOSIT ... ROM programs it");

        // SAVE: the NAME decides, FORMAT= overrides. It cannot sniff -- there is no
        // file yet -- so this is the other half of LOAD's rule, not the same half.
        std::ostringstream d1;
        mon.exec("SAVE " + dat + " 200-201", d1);
        CHECK(d1.str().find("(bin)") != std::string::npos, "a name that is not .hex saves binary");
        std::ostringstream d2;
        mon.exec("SAVE " + dat + " 200-201 FORMAT=HEX", d2);
        CHECK(d2.str().find("(hex)") != std::string::npos, "...and FORMAT=HEX overrides the name");
        {
            std::ifstream f(dat);
            std::string first;
            std::getline(f, first);
            CHECK(!first.empty() && first[0] == ':',
                  "...and what landed on disk really is Intel HEX, not just a label");
        }

        // OCTAL is the third, WRITE-ONLY format: a .OCT name picks it, FORMAT=OCTAL
        // overrides, and the bytes are split-octal addresses + octal 000-377, eight to
        // a line -- always octal whatever base the console prints in. LOAD never reads
        // it back; this test only asserts what SAVE writes.
        mm.bus.memWrite(0x0200, 0x3E);  // 076
        mm.bus.memWrite(0x0201, 0x41);  // 101
        const std::string oct = (dir / "img.oct").generic_string();
        std::ostringstream d3;
        mon.exec("SAVE " + oct + " 200-201", d3);
        CHECK(d3.str().find("(octal)") != std::string::npos, "a .OCT name saves octal");
        {
            std::ifstream f(oct);
            std::string hdr, row;
            std::getline(f, hdr);
            std::getline(f, row);
            CHECK(hdr.rfind("; altairsim octal image", 0) == 0,
                  "...the file opens with the octal header comment");
            CHECK(row == "002 000  076 101",
                  "...split-octal address, two spaces, octal bytes -- and it really is octal");
        }
        std::ostringstream d4;
        mon.exec("SAVE " + dat + " 200-201 FORMAT=OCTAL", d4);
        CHECK(d4.str().find("(octal)") != std::string::npos,
              "...and FORMAT=OCTAL overrides a name that is not .oct");
        std::ostringstream d5;
        mon.exec("SAVE " + dat + " 200-201 FORMAT=DECIMAL", d5);
        CHECK(d5.str().find("BIN, HEX, OCTAL or PRN") != std::string::npos,
              "...and an unknown FORMAT names all four, PRN included");

        // PRN is the fourth, WRITE-ONLY format (issue #176): a .PRN or .LST name picks
        // it, FORMAT=PRN overrides, and the file is the DISASM listing -- address,
        // object bytes, mnemonic -- written for reading, not loading. The bytes at 0200
        // are 3E 41, which is MVI A,41; the listing must decode them as such.
        const std::string prnf = (dir / "img.prn").generic_string();
        std::ostringstream d6;
        mon.exec("SAVE " + prnf + " 200-201", d6);
        CHECK(d6.str().find("(listing)") != std::string::npos, "a .PRN name saves a listing");
        {
            std::ifstream f(prnf);
            std::string hdr, row;
            std::getline(f, hdr);
            std::getline(f, row);
            CHECK(hdr.rfind("; altairsim disassembly", 0) == 0,
                  "...the file opens with the disassembly header comment");
            CHECK(row.rfind("0200", 0) == 0 && row.find("3E 41") != std::string::npos &&
                      row.find("MVI A,41") != std::string::npos,
                  "...and the listing decodes 3E 41 at 0200 as MVI A,41");
        }
        std::ostringstream d7;
        mon.exec("SAVE " + dat + " 200-201 FORMAT=PRN", d7);
        CHECK(d7.str().find("(listing)") != std::string::npos,
              "...and FORMAT=PRN overrides a name that is not .prn/.lst");

        // A listing needs a decoder, so a machine with no CPU refuses it BEFORE it
        // truncates the file -- and it names the formats that would have worked.
        Machine noCpu;
        Monitor mc(noCpu);
        std::ostringstream ne;
        mc.exec("BOARDS ADD memory mem0", ne);
        mc.exec("REGION ADD mem0 type=ram at=0 size=1K", ne);
        std::ostringstream d8;
        mc.exec("SAVE " + dat + " 0-1 FORMAT=PRN", d8);
        CHECK(d8.str().find("no CPU") != std::string::npos,
              "a .PRN with no CPU to disassemble is refused, not written empty");

        fs::remove_all(dir, ec);
    }

    SECTION("SYMBOLS -- load, merge, REPLACE, reference by name, and refuse a relocatable");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "altairsim-symtest";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string prn   = (dir / "prog.PRN").generic_string();
        const std::string sym   = (dir / "prog.SYM").generic_string();
        const std::string reloc = (dir / "reloc.PRN").generic_string();

        // A .PRN with the real column geometry: an EQU (BDOS, '=' in column 7) and two
        // labels (START, LOOP). CRLF, because these are CP/M files.
        {
            std::ofstream f(prn, std::ios::binary);
            f << " 0005 =         BDOS\tEQU\t5\r\n"
                 " 0100 314201    START:\tLXI\tSP,STACK\r\n"
                 " 0106 7E        LOOP:\tMOV\tA,M\r\n";
        }
        // The exact bytes DR MAC writes: HHHH NAME, tab-separated, ^Z-padded.
        {
            std::ofstream f(sym, std::ios::binary);
            f << "0005 BDOS\t0200 OTHER\t0100 START\r\n\x1a\x1a";
        }
        // A relocatable value carries a trailing apostrophe in the object field.
        {
            std::ofstream f(reloc, std::ios::binary);
            f << " 0100'314201   START:\tLXI\tSP,STACK\r\n";
        }

        Machine mm;
        Monitor mon(mm);
        std::ostringstream o;
        mon.exec("BOARDS ADD 8080 cpu0", o);
        mon.exec("BOARDS ADD memory mem0", o);
        mon.exec("REGION ADD mem0 type=ram at=0 size=32K", o);

        std::ostringstream s1;
        mon.exec("SYMBOLS LOAD " + prn, s1);
        CHECK(s1.str().find("3 symbol") != std::string::npos, "the .PRN loads its three symbols");

        // DISASM reads symbolic once they are loaded. Lay down CALL 0005 / JMP 0106 /
        // MVI A,05 at the START label and disassemble it: BDOS (an EQU-address) names
        // the CALL, LOOP (a label) names the JMP, START and LOOP each head their own
        // line -- and the *byte* operand 05 stays a number even though BDOS equals 5,
        // because only a 16-bit operand is an address.
        mon.exec("DEPOSIT 0100 CD 05 00 C3 06 01 3E 05", o);
        std::ostringstream dis;
        mon.exec("DISASM 0100 3", dis);
        const std::string dtext = dis.str();
        CHECK(dtext.find("\nSTART:\n") != std::string::npos || dtext.compare(0, 7, "START:\n") == 0,
              "the START label heads its own line");
        CHECK(dtext.find("CALL BDOS") != std::string::npos, "CALL 0005 reads as CALL BDOS");
        CHECK(dtext.find("JMP LOOP") != std::string::npos, "JMP 0106 reads as JMP LOOP");
        CHECK(dtext.find("LOOP:\n") != std::string::npos, "the LOOP label heads its line where it lands");
        CHECK(dtext.find("MVI A,05") != std::string::npos && dtext.find("MVI A,BDOS") == std::string::npos,
              "a byte operand stays a number -- 05 is not the EQU BDOS");

        // A symbol resolves anywhere a true address is typed. BREAK names the address it set.
        std::ostringstream b1;
        mon.exec("BREAK START", b1);
        CHECK(b1.str().find("0100") != std::string::npos, "BREAK START breaks at 0100");

        // And an EQU resolves too -- EXAMINE reads the byte at the EQU's value.
        std::ostringstream e1;
        mon.exec("EXAMINE BDOS", e1);
        CHECK(e1.str().find("0005") != std::string::npos, "EXAMINE BDOS looks at 0005");

        // SHOW SYMBOLS lists them; the EQU is flagged, the label is not.
        std::ostringstream sh;
        mon.exec("SHOW SYMBOLS", sh);
        CHECK(sh.str().find("START") != std::string::npos && sh.str().find("0100") != std::string::npos,
              "SHOW SYMBOLS lists the label");
        std::ostringstream shp;
        mon.exec("SHOW SYMBOLS LO*", shp);
        CHECK(shp.str().find("LOOP") != std::string::npos && shp.str().find("START") == std::string::npos,
              "SHOW SYMBOLS <glob> filters");

        // Merge: the .SYM shares BDOS and START (redefined) and adds OTHER.
        std::ostringstream s2;
        mon.exec("SYMBOLS LOAD " + sym, s2);
        CHECK(s2.str().find("redefined") != std::string::npos, "a merge reports the redefinitions");
        std::ostringstream e2;
        mon.exec("EXAMINE OTHER", e2);
        CHECK(e2.str().find("0200") != std::string::npos, "the merged-in OTHER resolves to 0200");

        // REPLACE clears first: after it, LOOP (from the .PRN) is gone.
        std::ostringstream s3;
        mon.exec("SYMBOLS LOAD " + sym + " REPLACE", s3);
        std::ostringstream sh2;
        mon.exec("SHOW SYMBOLS LOOP", sh2);
        CHECK(sh2.str().find("no symbol matches") != std::string::npos,
              "REPLACE cleared the .PRN symbols");

        // A relocatable listing is REFUSED, by the line, and nothing is loaded from it.
        std::ostringstream s4;
        mon.exec("SYMBOLS LOAD " + reloc, s4);
        CHECK(s4.str().find("relocatable") != std::string::npos, "a relocatable .PRN is refused");
        CHECK(s4.str().find("line 1") != std::string::npos, "and the line is named");

        // CLEAR empties it.
        mon.exec("SYMBOLS CLEAR", o);
        std::ostringstream sh3;
        mon.exec("SHOW SYMBOLS", sh3);
        CHECK(sh3.str().find("no symbols loaded") != std::string::npos, "SYMBOLS CLEAR empties the table");

        // A symbol no longer defined is not silently zero -- the reference fails to parse.
        std::ostringstream b2;
        mon.exec("BREAK START", b2);
        CHECK(b2.str().find("0100") == std::string::npos, "and a cleared name no longer resolves");

        fs::remove_all(dir, ec);
    }

    SECTION("closing the video window stops the guest and hands back the monitor");
    {
        // A window nobody has closed answers no forever; one that has been closed
        // says so ONCE. That is the whole contract SdlDisplay implements against a
        // real close box, minus SDL -- so this runs headless, on every platform.
        struct ClosableDisplay : NullDisplay {
            bool closed = false;
            bool takeQuitRequest() override {
                bool q = closed;
                closed = false;
                return q;
            }
        };
        ClosableDisplay disp;
        Monitor::setDisplay(&disp);

        Machine mw;
        Monitor monW(mw);
        std::ostringstream sw;
        monW.exec("BOARDS ADD 8080 cpu0", sw);
        monW.exec("BOARDS ADD memory mem0", sw);
        monW.exec("SET mem0 fill=zero", sw);
        monW.exec("REGION ADD mem0 type=ram at=0 size=64K", sw);
        monW.exec("POWER ON", sw);

        // 0200: JMP 0200 -- a guest that will NEVER stop on its own. If the close box
        // is not read, this test hangs rather than fails, which is the honest shape:
        // the bug is that nothing stops the run.
        monW.exec("DEPOSIT 0200 C3 00 02", sw);
        // 0300: HLT -- the same machine, stopping for a reason of its own.
        monW.exec("DEPOSIT 0300 76", sw);

        {
            disp.closed = true;
            std::ostringstream out;
            monW.exec("EX 0200", sw);
            monW.exec("RUN", out);
            CHECK(out.str().find("window closed") != std::string::npos,
                  "closing the window stops the run, and the monitor says so");
            CHECK(mw.cpu()->pc() == 0x0200,
                  "and the machine is exactly where it was -- RUN resumes it");
            // Same family as ATTN: the operator stopped it, so there is no work to
            // tally. A tally here would read as though the guest had finished.
            CHECK(out.str().find("instructions,") == std::string::npos,
                  "an operator stop prints no instruction tally");
        }

        {
            // CONSUMING. One click stops one run: the next RUN must stop for its own
            // reason, not inherit a stale close. This is the bug takeAttn() already
            // learned to avoid.
            std::ostringstream out;
            monW.exec("EX 0300", sw);
            monW.exec("RUN", out);
            CHECK(out.str().find("window closed") == std::string::npos,
                  "the close is consumed -- it cannot stop the NEXT run too");
            CHECK(out.str().find("HLT") != std::string::npos,
                  "which leaves the guest free to stop for its own reason");
        }

        // setDisplay is a process-global. Put it back, or every test that runs after
        // this one inherits a pointer to a destroyed stack object.
        Monitor::setDisplay(nullptr);
    }

    SECTION("the window is named after the MACHINE, and renaming the machine renames it");
    {
        // The board that draws cannot answer this: the same VDM-1 is the screen of a
        // bare `vdm1` and of a Sol-20, so the run loop publishes the machine's name
        // instead (host/display.h). What SdlDisplay does with the string is SDL's
        // business; that it is TOLD, and told again when the machine changes, is not.
        struct TitledDisplay : NullDisplay {
            std::string title;
            int         calls = 0;
            void setTitle(const std::string& t) override { title = t; ++calls; }
        };
        TitledDisplay disp;
        Monitor::setDisplay(&disp);

        Machine mt;
        mt.name = "sol20";
        Monitor monT(mt);
        std::ostringstream st;
        monT.exec("BOARDS ADD 8080 cpu0", st);
        monT.exec("BOARDS ADD memory mem0", st);
        monT.exec("SET mem0 fill=zero", st);
        monT.exec("REGION ADD mem0 type=ram at=0 size=1K", st);
        monT.exec("POWER ON", st);
        monT.exec("DEPOSIT 0100 76", st);  // HLT -- stops on its own

        CHECK(disp.calls == 0, "nothing is published before the guest has ever been run");

        monT.exec("EX 0100", st);
        monT.exec("RUN", st);
        CHECK(disp.title == "sol20", "starting the guest names the window after the machine");

        // THE POINT OF PUBLISHING RATHER THAN WIRING. CONFIG LOAD replaces the machine
        // wholesale, and it can do it with the window still open -- so a name captured
        // once would leave the window claiming to be a machine that no longer exists.
        mt.name = "cuter";
        monT.exec("EX 0100", st);
        monT.exec("RUN", st);
        CHECK(disp.title == "cuter", "and the next run re-publishes it, so a swap is caught");

        Monitor::setDisplay(nullptr);
    }

    SECTION("stopping the guest hands the keyboard back to the terminal");
    {
        // The video window is an input device, so clicking it takes the keyboard --
        // and then the guest stops and the monitor prompts into a terminal that cannot
        // be typed into (host/display.h). What a windowed host DOES about that is the
        // platform layer's business and is macOS-only; that the run loop ASKS, on every
        // stop, is this layer's and is testable everywhere.
        struct FocusDisplay : NullDisplay {
            int yields = 0;
            void yieldFocus() override { ++yields; }
        };
        FocusDisplay disp;
        Monitor::setDisplay(&disp);

        Machine mf;
        Monitor monF(mf);
        std::ostringstream sf;
        monF.exec("BOARDS ADD 8080 cpu0", sf);
        monF.exec("BOARDS ADD memory mem0", sf);
        monF.exec("SET mem0 fill=zero", sf);
        monF.exec("REGION ADD mem0 type=ram at=0 size=1K", sf);
        monF.exec("POWER ON", sf);
        monF.exec("DEPOSIT 0100 76", sf);  // HLT -- stops on its own

        CHECK(disp.yields == 0, "nothing is asked of the window before a run");

        monF.exec("EX 0100", sf);
        monF.exec("RUN", sf);
        CHECK(disp.yields == 1, "a guest that stops gives the keyboard back");

        // EVERY stop of a RUN, not just the close box: a breakpoint and a HLT leave you
        // at the same prompt with the same window in front of it, and this one was a HLT.
        monF.exec("EX 0100", sf);
        monF.exec("RUN", sf);
        CHECK(disp.yields == 2, "and again on the next stop -- it is not a once-per-session thing");

        // STEP is NOT one of these, and that is not an oversight. It never enters the
        // run loop -- it drives the debugger an instruction at a time and never takes
        // the terminal, pumps a board or polls the window -- so there is no moment at
        // which the window could have taken the keyboard for it to be given back.
        monF.exec("STEP", sf);
        CHECK(disp.yields == 2, "a STEP does not run the guest, so it has nothing to give back");

        // ...UNLESS THE WINDOW IS MEANT TO HAVE THE KEYBOARD. The run loop still asks
        // on every stop; what changes is the answer the display gives, which is where
        // the policy belongs -- the run loop has no business knowing about windows.
        Display::setFocusPolicy(true);
        monF.exec("EX 0100", sf);
        monF.exec("RUN", sf);
        CHECK(disp.yields == 3, "the run loop asks regardless -- the display decides");
        Display::setFocusPolicy(false);

        Monitor::setDisplay(nullptr);
    }

    SECTION("the video window's focus is a display setting, not a board's");
    {
        // A 1975 video card has no opinion about window managers, and a machine with
        // two of them still has one operator with one keyboard -- so this is the
        // display's, alongside the console's own transforms, and it answers even in a
        // build with no video at all (host/display.h).
        CHECK(!Display::focusPolicy(), "the terminal keeps the keyboard by default");

        Machine md;
        Monitor monD(md);
        std::ostringstream sd;

        monD.exec("SET DISPLAY focus=on", sd);
        CHECK(Display::focusPolicy(), "SET DISPLAY reaches it");

        sd.str("");
        monD.exec("SHOW DISPLAY", sd);
        CHECK(sd.str().find("focus") != std::string::npos, "and SHOW DISPLAY reports it");

        // A bad value is refused by the same Property layer as everything else, which
        // is the point of it being a Property and not a flag parsed here.
        sd.str("");
        monD.exec("SET DISPLAY focus=maybe", sd);
        CHECK(Display::focusPolicy(), "a value that does not parse leaves it alone");

        sd.str("");
        monD.exec("SET DISPLAY nosuchkey=1", sd);
        CHECK(sd.str().find("nosuchkey") != std::string::npos, "and an unknown key is named");

        // THE REASON IT IS ANSWERABLE BEFORE A WINDOW EXISTS: a machine file says what
        // it wants at load time, and the window does not open until the first frame.
        Display::setFocusPolicy(false);
        Machine mc;
        std::string err;
        CHECK(loadTomlText("[display]\nfocus = true\n", "test", mc, err),
              "a machine file can ask for it");
        CHECK(Display::focusPolicy(), "and it takes effect with no window in sight");

        Display::setFocusPolicy(false);  // a process-wide setting: put it back
    }

    SECTION("a video board's window width is its own, in pixels (per-board, not [display])");
    {
        // Patrick went looking for this on the board, and a real Altair has one video-out
        // cable per board -> its own monitor. So `width` lives on the video board, parsed
        // through the same Property layer as everything else. The window math is untestable
        // without a display (host/display_sdl.cpp), but the property round-trip is not.
        Machine     mv;
        std::string err;
        Board*      vdm = mv.add("vdm1", "vdm0", err);
        CHECK(vdm != nullptr, "a VDM-1 goes in");

        Monitor            monV(mv);
        std::ostringstream sv;

        monV.exec("SET vdm0 width=1024", sv);
        sv.str("");
        monV.exec("SHOW vdm0", sv);
        CHECK(sv.str().find("width") != std::string::npos, "SHOW names the width knob");
        CHECK(sv.str().find("1024") != std::string::npos, "and reports the pixels set");

        sv.str("");
        monV.exec("SET vdm0 width=auto", sv);
        sv.str("");
        monV.exec("SHOW vdm0", sv);
        CHECK(sv.str().find("auto") != std::string::npos, "auto restores the default");

        // Out of range and non-numeric are refused by the same layer, and the message names
        // the range in pixels -- so a width the window could never honor never reaches it.
        sv.str("");
        monV.exec("SET vdm0 width=1", sv);
        CHECK(sv.str().find("128 to 8192") != std::string::npos,
              "a too-small width is refused, with the pixel range");
        sv.str("");
        monV.exec("SET vdm0 width=nope", sv);
        CHECK(sv.str().find("128 to 8192") != std::string::npos, "and so is a non-number");
    }

    SECTION("SET BUS UNCLAIMED -- the floating-bus diagnostic, from the CLI (DESIGN.md 4.6.1)");
    {
        // A guest that OUTs to a port no board decodes, then HLTs: OUT FE ; HLT. Each
        // policy gets a FRESH machine, because a CPU that reached HLT stays halted and a
        // second RUN would not re-execute the OUT (that is 8080 HLT, not this feature).
        auto fresh = [](Monitor& mon) {
            std::ostringstream s;
            mon.exec("BOARDS ADD 8080 cpu0", s);
            mon.exec("BOARDS ADD memory mem0", s);
            mon.exec("SET mem0 fill=zero", s);
            mon.exec("REGION ADD mem0 type=ram at=0 size=64K", s);
            mon.exec("POWER ON", s);
            mon.exec("DEPOSIT 0100 D3 FE 76", s);  // OUT FE ; HLT
        };
        auto run = [](Monitor& mon, const char* line) {
            std::ostringstream o;
            mon.exec(line, o);
            return o.str();
        };

        // The command parses and echoes the policy it set -- it is NOT the old error.
        {
            Machine mp;
            Monitor mon(mp);
            fresh(mon);
            CHECK(run(mon, "SET BUS UNCLAIMED=WARN").find("bus: unclaimed=WARN") != std::string::npos,
                  "SET BUS UNCLAIMED=WARN is accepted and echoed");
            CHECK(run(mon, "SET BUS UNCLAIMED=HALT").find("bus: unclaimed=HALT") != std::string::npos,
                  "and HALT");
            CHECK(run(mon, "SET BUS UNCLAIMED=SILENT").find("bus: unclaimed=SILENT") !=
                      std::string::npos,
                  "and SILENT");
            CHECK(run(mon, "SET BUS NONSENSE=1").find("UNCLAIMED=WARN|HALT|SILENT") !=
                      std::string::npos,
                  "a bad SET BUS subcommand names UNCLAIMED alongside CONTENTION");
        }

        // WARN: the machine runs to the HLT and the warning names the port and PC.
        {
            Machine mw;
            Monitor mon(mw);
            fresh(mon);
            run(mon, "SET BUS UNCLAIMED=WARN");
            std::string warn = run(mon, "RUN 0100");
            CHECK(warn.find("warning: OUT FE <- 00 at PC=0100: no board decodes port 0xFE") !=
                      std::string::npos,
                  "under WARN the run finishes and the port+PC are named");
            CHECK(warn.find("HLT at 0103") != std::string::npos, "the guest ran on to its HLT");
        }

        // HALT: the machine stops at the offending cycle, before the HLT.
        {
            Machine mh;
            Monitor mon(mh);
            fresh(mon);
            run(mon, "SET BUS UNCLAIMED=HALT");
            std::string halt = run(mon, "RUN 0100");
            CHECK(halt.find("stopped: OUT to port 0xFE") != std::string::npos,
                  "under HALT the guest is stopped at the unclaimed access");
        }

        // SILENT is the default -- an unclaimed port says nothing, so no existing
        // machine or acceptance transcript gains a line.
        {
            Machine md;
            Monitor mon(md);
            fresh(mon);
            CHECK(run(mon, "RUN 0100").find("no board decodes") == std::string::npos,
                  "the default is SILENT");
        }
    }

    SECTION("WHO IO reports BOTH sides of a port -- IN and OUT decode independently");
    {
        // A port is two doors. The 2SIO answers both, so WHO IO must show both -- the
        // old command probed only OUT and hid the IN side, which read as if a serial
        // board could be written to but never read from.
        Machine mio;
        Monitor mon(mio);
        std::ostringstream s;
        mon.exec("BOARDS ADD 8080 cpu0", s);
        mon.exec("BOARDS ADD memory mem0", s);
        mon.exec("BOARDS ADD 2sio sio0", s);
        mon.exec("BOARDS ADD fp fp0", s);
        auto run = [&](const char* line) {
            std::ostringstream o;
            mon.exec(line, o);
            return o.str();
        };

        std::string w = run("WHO IO 10");
        CHECK(w.find("port 10 IN:") != std::string::npos, "WHO IO prints the IN side");
        CHECK(w.find("port 10 OUT:") != std::string::npos, "and the OUT side");
        CHECK(w.find("IN:  sio0") != std::string::npos, "the 2SIO answers an IN at 10");
        CHECK(w.find("OUT: sio0") != std::string::npos, "and an OUT at 10");

        // The whole point of showing both: they can differ. The front panel drives the
        // sense switches onto IN FF but decodes no OUT there, so one line names it and
        // the other says nobody. The old OUT-only WHO could never have shown this.
        std::string ff = run("WHO IO FF");
        CHECK(ff.find("IN:  fp0") != std::string::npos,
              "the front panel answers IN FF (sense switches)");
        CHECK(ff.find("OUT: nobody") != std::string::npos, "but nothing decodes OUT FF");

        // A port nobody touches: both sides say nobody, each in its own words.
        std::string dead = run("WHO IO 80");
        CHECK(dead.find("IN:  nobody (an IN here reads FF)") != std::string::npos,
              "an unclaimed IN reads the floating FF");
        CHECK(dead.find("OUT: nobody (an OUT here goes nowhere)") != std::string::npos,
              "and an unclaimed OUT is simply gone");
    }

    SECTION("SHOW BUS IO -- separate IN and OUT columns, and the port range");
    {
        Machine mio;
        Monitor mon(mio);
        std::ostringstream s;
        mon.exec("BOARDS ADD 8080 cpu0", s);
        mon.exec("BOARDS ADD memory mem0", s);
        mon.exec("BOARDS ADD 2sio sio0", s);
        mon.exec("BOARDS ADD fp fp0", s);
        mon.exec("BOARDS ADD c700 lp0", s);
        std::ostringstream o;
        mon.exec("SHOW BUS IO", o);
        std::string io = o.str();

        // The 2SIO's first channel spans two ports and answers both directions: the row
        // shows the RANGE, not just the low port, and both columns are lit.
        CHECK(io.find("10-11") != std::string::npos, "a two-port entry shows its range");
        CHECK(io.find("10-11     sio0     IN  OUT    6850") != std::string::npos,
              "the 2SIO answers both IN and OUT across 10-11");

        // The front panel is read-only: IN is named, OUT is a dash. This is the whole
        // reason the columns are split -- a merged 'read/write' column could not say it.
        CHECK(io.find("FF        fp0      IN  --") != std::string::npos,
              "the front panel reads FF (sense switches) but decodes no OUT there");

        // The C700 data port is the mirror case: write-only, so OUT is named and IN is
        // a dash. Its decode() actually distinguishes the two, unlike the coarse boards.
        CHECK(io.find("lp0      --  OUT    C700 -- data") != std::string::npos,
              "the C700 data port is OUT-only");

        // The notes carry FUNCTION, not direction -- the columns carry direction now.
        // No 'out:' / 'in:' / '(read)' / '(write)' survives in the I/O rows.
        CHECK(io.find("out:") == std::string::npos && io.find("in:") == std::string::npos,
              "direction words are stripped from the notes -- the columns say it");
        CHECK(io.find("(read)") == std::string::npos && io.find("(write)") == std::string::npos,
              "and so are the parenthesised (read)/(write) markers");
    }

    // -----------------------------------------------------------------------
    // The `!` shell escape. The monitor's OWN behaviour is what a unit test can
    // reach: the parse (a leading `!`, after any whitespace, is recognised and
    // never mistaken for a command word) and the bare-`!` help. The actual
    // shell-out -- whose child writes to the real fd, not to this ostream -- is
    // proved on a real pty in tests/acceptance/cli.exp.
    // -----------------------------------------------------------------------
    SECTION("! -- a leading bang is the shell escape, not a command word");
    {
        Machine mb;
        Monitor mon(mb);

        std::ostringstream bare;
        mon.exec("!", bare);
        CHECK(bare.str().find("host shell") != std::string::npos,
              "a bare ! reminds you of the form");

        // Whitespace before the bang still counts -- it is the first non-blank
        // character that decides, so a spaced-out empty escape is the same help.
        std::ostringstream spaced;
        mon.exec("   !  ", spaced);
        CHECK(spaced.str().find("host shell") != std::string::npos,
              "leading whitespace before ! is skipped, and an empty escape is the help");

        // `!echo ...` is DISPATCHED as a shell line, not resolved as a command.
        // `echo` is a builtin of both /bin/sh and cmd.exe, so this is portable
        // and harmless; we assert only that the monitor did not reject it. (Its
        // output goes to the process stdout, not to `out`, so there is nothing
        // to match there.)
        std::ostringstream shell;
        mon.exec("!echo altairsim-shell-test", shell);
        CHECK(shell.str().find("unknown command") == std::string::npos,
              "!echo is handed to the shell, never resolved as a command");
    }

    // ---------------------------------------------------------------------
    // EDIT -- interactive DEPOSIT. It reads its follow-up bytes from the
    // monitor's own input, so it is driven through repl(), not a bare exec()
    // (which is exactly the case that has no keyboard -- see the last check).
    // ---------------------------------------------------------------------
    SECTION("EDIT -- interactive DEPOSIT: type a byte and drop to the next, '.' stops");
    {
        Machine me;
        Monitor mon(me);
        std::ostringstream setup;
        mon.exec("BOARDS ADD memory mem0", setup);
        mon.exec("SET mem0 fill=zero", setup);
        mon.exec("REGION ADD mem0 type=ram at=0 size=64K", setup);
        mon.exec("DEPOSIT 0100 00 00 00", setup);

        // A whole session: a byte written, a bare Enter that LEAVES the byte, a bad
        // token that re-prompts without advancing, a byte written, then '.' to stop.
        // The follow-up lines are EDIT's -- if it did not consume them, repl would
        // see them as commands and answer "unknown command".
        std::istringstream in(
            "EDIT 0100\n"
            "3E\n"       // 0100 <- 3E, advance
            "\n"         // 0101 left at 00, advance
            "ZZ\n"       // not a byte -> re-prompt, stay on 0102
            "C3\n"       // 0102 <- C3, advance
            ".\n"        // stop
            "DUMP 0100-0102\n"
            "QUIT\n");
        std::ostringstream out;
        mon.repl(in, out, /*interactive=*/false);
        std::string s = out.str();

        CHECK(s.find("3E 00 C3") != std::string::npos,
              "0100<-3E, 0101 left at 00 by a bare Enter, 0102<-C3");
        CHECK(s.find("a byte is 00-FF") != std::string::npos,
              "a token that is not a byte re-prompts instead of writing garbage");
        CHECK(s.find("unknown command") == std::string::npos,
              "EDIT consumed its own follow-up lines -- none reached the command loop");

        // No REPL, no keyboard: a bare exec() has nothing to read the bytes from, so
        // EDIT says so rather than spinning. This is the MCP/startup path.
        std::ostringstream noinput;
        mon.exec("EDIT 0100", noinput);
        CHECK(noinput.str().find("interactive or piped session") != std::string::npos,
              "EDIT with no input stream points you at DEPOSIT");
    }

    // ---------------------------------------------------------------------
    // EDIT assembles a mnemonic in place, when the machine has a CPU whose ISA we
    // assemble for. The encoding falls out, so the prompt drops by the instruction
    // length -- `IN 10` at 0100 lands the next prompt at 0102. A byte still deposits
    // a byte first (byte-first rule), and a bad mnemonic re-prompts without moving.
    // ---------------------------------------------------------------------
    SECTION("EDIT -- type a mnemonic and it assembles in place, dropping by its length");
    {
        Machine me;
        std::string err;
        me.add("8080", "cpu0", err);  // an ISA we assemble for -> EDIT accepts mnemonics
        Monitor mon(me);
        std::ostringstream setup;
        mon.exec("BOARDS ADD memory mem0", setup);
        mon.exec("SET mem0 fill=zero", setup);
        mon.exec("REGION ADD mem0 type=ram at=0 size=64K", setup);

        std::istringstream in(
            "EDIT 0100\n"
            "IN 10\n"          // 0100 <- DB 10, drop to 0102
            "LXI H,FF13\n"     // 0102 <- 21 13 FF, drop to 0105
            "C3\n"             // 0105 <- C3 as a BYTE (byte-first), drop to 0106
            "FOO 1\n"          // not an instruction -> re-prompt, stay on 0106
            "MVI C,EB\n"       // 0106 <- 0E EB, drop to 0108
            ".\n"
            "DISASM 0100 3\n"
            "DUMP 0100-0107\n"
            "QUIT\n");
        std::ostringstream out;
        mon.repl(in, out, /*interactive=*/false);
        std::string s = out.str();

        // The DUMP is the proof of the whole address progression at once: a
        // CONTIGUOUS byte layout only appears if each line dropped by exactly its
        // encoding length. IN 10 (DB 10) at 0100, LXI H,FF13 (21 13 FF) at 0102 --
        // the +2 drop -- C3 as a plain byte at 0105 -- byte-first, +1 -- and after
        // FOO 1 re-prompted in place, MVI C,EB (0E EB) at 0106.
        CHECK(s.find("DB 10 21 13 FF C3 0E EB") != std::string::npos,
              "each line dropped by its own length -- the bytes land contiguous 0100..0107");
        CHECK(s.find("IN 10") != std::string::npos && s.find("LXI H,FF13") != std::string::npos,
              "and DISASM reads the assembled bytes straight back");
        CHECK(s.find("unknown command") == std::string::npos,
              "EDIT consumed every mnemonic line -- none reached the command loop");
    }

    // -----------------------------------------------------------------
    // Tab completion (cli/monitor.cpp Monitor::complete). A pure read over the same
    // reflection SET uses -- no editor, no pty.
    // -----------------------------------------------------------------
    SECTION("Tab completion walks command -> board -> property -> value");
    {
        Machine cm;
        Monitor cmon(cm);
        std::ostringstream csink;
        cmon.exec("BOARDS ADD 8080 cpu0", csink);
        cmon.exec("BOARDS ADD memory mem0", csink);
        cmon.exec("BOARDS ADD 2sio sio0", csink);   // two serial units 'a' and 'b'
        cmon.exec("BOARDS ADD dcdd disk0", csink);  // four mountable drives 'drive0..3'
        cmon.exec("BOARDS ADD acr acr0", csink);    // brings the REWIND/WIND/EXTRACT verbs

        auto has = [](const Completions& c, const std::string& want) {
            for (const std::string& m : c.matches)
                if (m == want) return true;
            return false;
        };

        // Word 0: command names. The empty line offers everything; a fragment narrows it,
        // and the span to replace starts at column 0 with a trailing space on completion.
        Completions c0 = cmon.complete("");
        CHECK(has(c0, "SET") && has(c0, "DUMP"), "an empty line offers the command names");
        Completions cS = cmon.complete("SE");
        CHECK(has(cS, "SET"), "'SE' narrows to SET");
        CHECK(cS.replaceFrom == 0 && cS.suffix == " ", "word 0 replaces from the start, suffix is a space");

        // SET word 1: board ids plus the pseudo-targets.
        Completions ct = cmon.complete("SET ");
        CHECK(has(ct, "mem0") && has(ct, "cpu0"), "SET offers the board ids in the machine");
        CHECK(has(ct, "CONSOLE") && has(ct, "DISPLAY"), "and CONSOLE / DISPLAY");
        // Debug channels are SET targets too: a library channel (6850, socket) is no
        // board, so it appears only because the completer walks the channel registry.
        CHECK(has(ct, "6850") && has(ct, "socket"), "SET offers the library debug channels");
        CHECK(cmon.complete("SET me").replaceFrom == 4, "the target fragment replaces from just after 'SET '");

        // SET word 2, no '=': property NAMES, suffix '=' so the value types straight on.
        Completions ck = cmon.complete("SET mem0 ");
        CHECK(has(ck, "fill"), "a board's property names come from its own reflection");
        CHECK(ck.suffix == "=", "a property name completes with an '=' ready for the value");

        // SET word 2, past '=': the property's legal enum values, replacing only after '='.
        Completions cv = cmon.complete("SET mem0 fill=");
        CHECK(has(cv, "zero") && has(cv, "random"), "fill's choices are its Kind::Enum values");
        CHECK(cv.suffix == " ", "a chosen value completes with a trailing space");
        Completions cvz = cmon.complete("SET mem0 fill=z");
        CHECK(has(cvz, "zero") && !has(cvz, "random"), "and the value fragment narrows them");
        CHECK(cvz.replaceFrom == std::string("SET mem0 fill=").size(),
              "only the run after '=' is replaced");

        // CONSOLE resolves to the host console's own schema, not a board's.
        CHECK(has(cmon.complete("SET CONSOLE base="), "octal"),
              "SET CONSOLE base= offers the console's enum values");

        // SET word 1, a unit target: past the ':' the board's unit NAMES, replacing
        // only the run after the colon (so the id typed already stays put).
        Completions cu = cmon.complete("SET sio0:");
        CHECK(has(cu, "a") && has(cu, "b"), "SET sio0: offers the 2SIO's two serial units");
        CHECK(cu.replaceFrom == std::string("SET sio0:").size(),
              "a unit fragment replaces from just after the ':'");
        CHECK(has(cmon.complete("SET sio0:a"), "a") && !has(cmon.complete("SET sio0:a"), "b"),
              "and the unit fragment narrows them");

        // SET word 2 on a unit target: the UNIT's own properties (not the board's),
        // resolved the way the SET executor resolves a unit.
        Completions cuk = cmon.complete("SET sio0:a ");
        CHECK(has(cuk, "baud") && has(cuk, "dcd"), "a unit target's word-2 keys are its unitProperties");
        CHECK(cuk.suffix == "=", "a unit property name completes with an '=' too");

        // SET word 2 value on a unit's enum property.
        Completions cuv = cmon.complete("SET sio0:a dcd=");
        CHECK(has(cuv, "ground") && has(cuv, "wired"), "dcd's Kind::Enum choices complete after '='");
        CHECK(cuv.replaceFrom == std::string("SET sio0:a dcd=").size(),
              "only the run after '=' is replaced on a unit value");

        // MOUNT: word 1 is a target filtered to MOUNTABLE units. A disk board offers its
        // drives; a serial-only board offers none past the colon.
        Completions cm1 = cmon.complete("MOUNT ");
        CHECK(has(cm1, "disk0") && has(cm1, "sio0"), "MOUNT offers the board ids");
        CHECK(has(cmon.complete("MOUNT disk0:"), "drive0"), "MOUNT disk0: offers the mountable drives");
        CHECK(cmon.complete("MOUNT sio0:").matches.empty(),
              "a serial-only board has no mountable units, so MOUNT sio0: is inert");

        // CONNECT: word 1 is a target filtered to SERIAL units -- the mirror image.
        CHECK(has(cmon.complete("CONNECT sio0:"), "a"), "CONNECT sio0: offers the serial units");
        CHECK(cmon.complete("CONNECT disk0:").matches.empty(),
              "a disk board has no serial units, so CONNECT disk0: is inert");

        // A verb a board brought (the 88-ACR declares REWIND): word 1 completes as an
        // Any-kind target, so the ACR's id is offered.
        CHECK(has(cmon.complete("REWIND "), "acr0"), "a board verb completes its word-1 target ids");

        // BOARDS REMOVE is a built-in word-2 id. BOARDS word 1 offers its keywords; REMOVE then ids.
        Completions cb = cmon.complete("BOARDS ");
        CHECK(has(cb, "LIST") && has(cb, "REMOVE"), "BOARDS offers its subcommands");
        CHECK(has(cmon.complete("BOARDS REMOVE "), "disk0"), "BOARDS REMOVE offers the board ids");
        CHECK(has(cmon.complete("BOARDS ADD "), "2sio") && has(cmon.complete("BOARDS ADD 2"), "2sio"),
              "BOARDS ADD offers the registry's board type names");

        // SHOW: word 1 is a board id OR a sub-command keyword -- both are offered.
        Completions csh = cmon.complete("SHOW ");
        CHECK(has(csh, "sio0") && has(csh, "disk0"), "SHOW offers the board ids");
        CHECK(has(csh, "BUS") && has(csh, "MOUNTS"), "SHOW offers its sub-command keywords");
        CHECK(has(cmon.complete("SHOW d"), "disk0") && has(cmon.complete("SHOW d"), "DISPLAY"),
              "SHOW prefix-matches ids and keywords together");

        // SHOW keyword set includes DEBUG.
        CHECK(has(cmon.complete("SHOW "), "DEBUG"), "SHOW offers the DEBUG keyword");

        // -------- DEBUG on a channel: the facility's keys ride with the board's --------
        // disk0 is a dcdd, a channel carrying `sector`/`seek`. Its word-2 keys are its
        // own properties PLUS DEBUG/NODEBUG, which are the facility's, not schema rows.
        Completions cdk = cmon.complete("SET disk0 ");
        CHECK(has(cdk, "DEBUG") && has(cdk, "NODEBUG"),
              "a channel board offers DEBUG/NODEBUG alongside its own property keys");

        // DEBUG= value: the channel's flags, plus the all/none wildcards.
        Completions cdv = cmon.complete("SET disk0 DEBUG=");
        CHECK(has(cdv, "sector") && has(cdv, "seek") && has(cdv, "all") && has(cdv, "none"),
              "DEBUG= offers the channel's flags and the all/none wildcards");
        CHECK(has(cmon.complete("SET disk0 DEBUG=se"), "sector") &&
                  has(cmon.complete("SET disk0 DEBUG=se"), "seek"),
              "the fragment 'se' narrows to both matching flags");
        CHECK(has(cmon.complete("SET disk0 NODEBUG="), "seek"), "NODEBUG= offers the flags too");

        // A comma-separated list: only the run past the last comma is completed/replaced.
        Completions cdc = cmon.complete("SET disk0 DEBUG=sector,se");
        CHECK(has(cdc, "seek") && has(cdc, "sector"),
              "after a comma the run 'se' still matches both flags");
        CHECK(cdc.replaceFrom == std::string("SET disk0 DEBUG=sector,").size(),
              "only the run after the last comma is replaced, not the whole value");

        // The console's DEBUG is the SINK, not a channel flag: its key and its values.
        CHECK(has(cmon.complete("SET CONSOLE "), "DEBUG"),
              "SET CONSOLE offers DEBUG -- the one global sink");
        Completions ccs = cmon.complete("SET CONSOLE DEBUG=");
        CHECK(has(ccs, "stderr") && has(ccs, "stdout"),
              "SET CONSOLE DEBUG= offers the reserved sink words");

        // A command with no completion grammar wired is simply inert -- no matches, no throw.
        CHECK(cmon.complete("DUMP ").matches.empty(), "DUMP takes no completion, so Tab is inert there");
    }

    // -----------------------------------------------------------------
    // SET / SHOW DEBUG -- the runtime diagnostic facility (core/debuglog.h) driven
    // through the monitor. The channels are process-global, so this section leaves the
    // sink and every flag it touched back at their defaults for the suites that follow.
    // -----------------------------------------------------------------
    SECTION("SET / SHOW DEBUG drive the diagnostic facility");
    {
        Machine dm;
        Monitor dmon(dm);
        std::ostringstream ds;
        dmon.exec("BOARDS ADD dcdd disk0", ds);  // a channel carrying sector/seek
        auto run = [&](const std::string& l) {
            std::ostringstream o;
            dmon.exec(l, o);
            return o.str();
        };
        const auto npos = std::string::npos;

        // Enabling flags on a channel, and SHOW DEBUG reflecting it: an ON flag prints
        // in UPPER CASE, so the state reads and greps at a glance.
        CHECK(run("SET disk0 DEBUG=sector,seek").find("disk0: debug=sector,seek") != npos,
              "SET <channel> DEBUG= enables its flags and echoes them");
        CHECK(run("SHOW DEBUG").find("SECTOR SEEK") != npos,
              "SHOW DEBUG upper-cases the enabled flags");

        // NODEBUG is subtractive -- it removes just the named flag.
        CHECK(run("SET disk0 NODEBUG=seek").find("disk0: nodebug=seek") != npos,
              "SET <channel> NODEBUG= disables a flag");
        CHECK(run("SHOW DEBUG").find("SECTOR seek") != npos,
              "and SHOW DEBUG shows sector still on, seek back off");

        // An unknown flag is reported and changes nothing (Channel::enable is atomic).
        CHECK(run("SET disk0 DEBUG=nope").find("nope") != npos,
              "an unknown flag is named in the error");
        CHECK(run("SHOW DEBUG").find("SECTOR seek") != npos,
              "and the mask is unchanged after the rejected flag");

        // A library channel is settable by NAME with no board at all.
        CHECK(run("SET 6850 DEBUG=serial").find("6850: debug=serial") != npos,
              "a library channel (the 6850) sets by its name");

        // The one global SINK: SET CONSOLE DEBUG=<sink>.
        CHECK(run("SET CONSOLE DEBUG=stdout").find("sink=stdout") != npos,
              "SET CONSOLE DEBUG= points the sink");
        CHECK(run("SHOW DEBUG").find("sink  stdout") != npos, "SHOW DEBUG names the sink");
        // A bad path fails and KEEPS the prior sink (dbg::setSink is all-or-nothing).
        run("SET CONSOLE DEBUG=/no_such_dir_altairsim/x.log");
        CHECK(run("SHOW DEBUG").find("sink  stdout") != npos,
              "a bad sink path leaves the prior sink in place");

        // Leave the process-global facility exactly as the other suites expect it.
        run("SET disk0 NODEBUG=all");
        run("SET 6850 NODEBUG=all");
        run("SET CONSOLE DEBUG=stderr");
    }

    // The RUN banner names WHERE the console is (issue #244 follow-up). A machine whose
    // console lives on a `terminal:`/`socket:` line still HAS a console -- it just is not
    // the stdio one, so the old banner libelled it as "(no console connected)". We drive a
    // HLT at the reset vector so RUN prints the banner and returns at once, with the line on
    // a non-blocking loopback (a socket would block on accept, a terminal needs a window).
    SECTION("RUN banner -- a live non-stdio line reads as a console, not 'none'");
    {
        Machine bm;
        Monitor bmon(bm);
        std::ostringstream setup;
        bmon.exec("BOARDS ADD 8080 cpu0", setup);
        bmon.exec("BOARDS ADD memory mem0", setup);
        bmon.exec("SET mem0 fill=zero", setup);
        bmon.exec("REGION ADD mem0 type=ram at=0 size=64K", setup);
        bmon.exec("BOARDS ADD 2sio sio0", setup);
        bmon.exec("POWER ON", setup);
        bmon.exec("DEPOSIT 0 76", setup);  // HLT at the reset vector: RUN halts immediately

        // A live wire that is not the stdio console: the banner should NAME its scheme.
        bmon.exec("CONNECT sio0:a loopback", setup);
        {
            std::ostringstream out;
            bmon.exec("EX 0", out);   // park the PC on the HLT
            bmon.exec("RUN", out);
            CHECK(out.str().find("(console on loopback)") != std::string::npos,
                  "a loopback console is named, not called 'no console connected'");
            CHECK(out.str().find("no console connected") == std::string::npos,
                  "and the misleading phrase is gone for a live line");
        }

        // A truly bare backplane -- no live serial line at all -- keeps the honest old
        // message: this is the ROM-talking-to-a-disk / CPU-test-talking-to-nobody case.
        bmon.exec("CONNECT sio0:a null", setup);
        {
            std::ostringstream out;
            bmon.exec("EX 0", out);
            bmon.exec("RUN", out);
            CHECK(out.str().find("(no console connected)") != std::string::npos,
                  "with every serial line dark the banner still says so");
            CHECK(out.str().find("console on") == std::string::npos,
                  "and it does not invent a console that is not there");
        }
    }

    // The front panel's graphical bridge is a CONNECTed serial unit too (endpoint
    // plumbing), but the guest never does I/O over it -- so the RUN banner must never
    // name it as "the console" (#295). It used to: a live socket on fp0:gui, scanned
    // before the console card, was reported as "(console on socket)" for a machine
    // whose real console was on a terminal window. We stand a live loopback on the
    // panel and check it is not mistaken for a console.
    SECTION("RUN banner -- the front-panel bridge is never named as the console (#295)");
    {
        Machine bm;
        Monitor bmon(bm);
        std::ostringstream setup;
        bmon.exec("BOARDS ADD 8080 cpu0", setup);
        bmon.exec("BOARDS ADD memory mem0", setup);
        bmon.exec("SET mem0 fill=zero", setup);
        bmon.exec("REGION ADD mem0 type=ram at=0 size=64K", setup);
        // The front panel goes in BEFORE the console card, mirroring the default
        // machine's order -- so its gui line is the FIRST live wire the scan meets.
        bmon.exec("BOARDS ADD fp fp0", setup);
        bmon.exec("BOARDS ADD 2sio sio0", setup);
        bmon.exec("POWER ON", setup);
        bmon.exec("DEPOSIT 0 76", setup);  // HLT at the reset vector

        // A live wire on the panel bridge; the console card left dark.
        bmon.exec("CONNECT fp0:gui loopback", setup);
        bmon.exec("CONNECT sio0:a null", setup);
        {
            std::ostringstream out;
            bmon.exec("EX 0", out);
            bmon.exec("RUN", out);
            CHECK(out.str().find("(no console connected)") != std::string::npos,
                  "a live front-panel bridge is not a console -- the backplane is bare");
            CHECK(out.str().find("console on") == std::string::npos,
                  "and the panel's socket is never named as one");
        }

        // Now give the guest a real console. The banner names IT, not the panel line
        // that the scan still meets first.
        bmon.exec("CONNECT sio0:a loopback", setup);
        {
            std::ostringstream out;
            bmon.exec("EX 0", out);
            bmon.exec("RUN", out);
            CHECK(out.str().find("(console on loopback)") != std::string::npos,
                  "the real console is named past the earlier front-panel line");
        }
    }
}
