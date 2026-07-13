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

    // ---- Patrick's ranking, 2026-07-11. These nine are listed first. ----
    CHECK(R("D") == "DUMP", "D dumps. A ROM monitor's D has always dumped.");
    CHECK(R("S") == "STEP", "S steps");
    CHECK(R("R") == "RESET", "R resets");
    CHECK(R("H") == "HISTORY", "H is history");
    CHECK(R("M") == "MOUNT", "M mounts");
    CHECK(R("B") == "BREAK", "B breaks");
    CHECK(R("E") == "EDIT", "E edits");
    CHECK(R("C") == "CONFIG", "C configures");

    // ---- RUN REPLACED GO (Patrick, 2026-07-12) ----
    // There was never a second thing for GO to be. A headless run is not a mode the
    // operator picks -- it is what happens when nothing holds the console, and the
    // machine already knows that. RESET owns R (it is in the nine), so RUN costs RU.
    CHECK(R("G") == "", "G is nothing now: GO is gone, and the panel's switch says RUN");
    bool hasGo = false;
    for (const CommandDef& c : commands())
        if (std::string(c.name) == "GO") hasGo = true;
    CHECK(!hasGo, "there is exactly ONE way to start the machine, and it is RUN");
    CHECK(R("RU") == "RUN", "RU runs (RESET keeps R)");
    CHECK(R("RUN") == "RUN", "and RUN");

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
    CHECK(R("REGI") == "REGION", "REGI is a memory region");
    CHECK(R("RES") == "RESET", "RES resets");
    CHECK(R("REST") == "RESTORE", "REST restores -- REST is not a prefix of RESET");
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

    // Reserved commands still RESOLVE and still say what they wait on. TRACE and
    // HISTORY are genuinely not here yet, and they hold their prefixes so that the
    // day they arrive they do not steal one from something else.
    const CommandDef* tr = resolveCommand("TR");
    CHECK(tr && !tr->built, "TRACE resolves but is not built yet");
    CHECK(tr && tr->waiting && std::string(tr->waiting) == "the debugger",
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
    CHECK(st.str().compare(0, 4, "FF00") == 0, "so STEP executes AT FF00, not wherever it was");

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
        // plugged in safely at all. RESET owns R, RE and RES. The cassette gets REW,
        // and only because nothing built-in claims those three letters.
        std::ostringstream re;
        mon4.exec("RE", re);
        CHECK(re.str().find("unknown") == std::string::npos && no.str() != re.str(),
              "RE is still RESET -- a card cannot move a built-in abbreviation");

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
}
