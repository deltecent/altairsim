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
#include "cli/monitor.h"
#include "core/machine.h"
#include "cpu/cpu.h"
#include "host/stream.h"
#include "test.h"

#include <memory>
#include <sstream>

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
    CHECK(R("REC") == "RECORD", "REC records");
    CHECK(R("REP") == "REPLAY", "REP replays");
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
    CHECK(R("STO") == "STOP", "STO stops");
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

    // Reserved commands still RESOLVE and still say what they wait on. SNAPSHOT and
    // RECORD are genuinely not here yet, and they hold their prefixes so that the day
    // they arrive they do not steal one from something else.
    const CommandDef* snap = resolveCommand("SN");
    CHECK(snap && !snap->built, "SNAPSHOT resolves but is not built yet");
    CHECK(snap && snap->waiting && std::string(snap->waiting) == "the debugger",
          "and says what it is waiting on");

    // Every command in the table is either BUILT (and then it needs no excuse) or
    // RESERVED (and then it must say what it is waiting for). A reserved command
    // with nothing to say is a dead entry nobody can act on -- and, until a moment
    // ago, a null pointer this loop walked straight into.
    for (const CommandDef& c : commands())
        CHECK(c.built || (c.waiting && *c.waiting),
              c.built ? "built" : "a reserved command says what it waits on");

    CHECK(resolveCommand("ZORK") == nullptr, "an unknown word is unknown");
    CHECK(resolveCommand("") == nullptr, "and so is nothing at all");

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
        // But the PROM burner still works, because it never touches the bus at all.
        std::ostringstream r;
        mb.exec("EX 0 RAW mem0", r);
        CHECK(r.str().compare(0, 4, "0000") == 0,
              "EXAMINE RAW needs no CPU -- it reaches behind the bus into the store");

        // AND EVERY OTHER MEMORY COMMAND STILL WORKS WITHOUT ONE (Patrick,
        // 2026-07-12): "we need to be able to debug the simulator without a CPU."
        // EXAMINE is the sole exception, and only because it IS the CPU.
        std::ostringstream w;
        mb.exec("DEPOSIT 0 41 42", w);
        mb.exec("FILL 2-3 5A", w);
        mb.exec("D 0-3", w);
        CHECK(w.str().find("41 42 5A 5A") != std::string::npos,
              "DEPOSIT, FILL and DUMP all work on a backplane with no processor");
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
    // STEP traces DDT-style: the machine as it stands, WITH the instruction it is
    // about to run. So the first line carries P=FF00 and the INR A that lives there.
    CHECK(st.str().find("P=FF00") != std::string::npos,
          "so STEP executes AT FF00, not wherever it was");
    CHECK(st.str().find("INR A") != std::string::npos,
          "and the trace names the instruction it is about to run");
    CHECK(c->pc() == 0xFF01, "and one instruction later the PC has moved");

    // EXAMINE NEXT drags the PC with it. The panel's counter IS the cursor -- it
    // has no other, which is why the switch is wired to it in the first place.
    mon2.exec("EX 0200", s2);
    mon2.exec("EX", s2);
    CHECK(c->pc() == 0x0201, "EXAMINE NEXT steps the PC too -- it is the same counter");

    // RAW is the PROM burner reaching behind the bus (DESIGN.md 10.2). No bus
    // cycle happens, so the CPU never sees an address and the PC does not move.
    uint16_t before = c->pc();
    mon2.exec("EX 0400 RAW mem0", s2);
    CHECK(c->pc() == before, "EXAMINE RAW does not touch the PC -- it is not a bus cycle");

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
}
