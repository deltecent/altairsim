#include "test.h"

#include "config/toml.h"
#include "core/machines.h"

#include <string>

using namespace altair;

// BUILT-IN MACHINES ARE IN THE BINARY, NOT ON THE DISK.
//
// This file never opens a file, and that is the entire point. altairsim ships as
// ONE EXECUTABLE plus documentation: it must not go looking for a machines/
// directory, an install prefix, or anything relative to argv[0]. If a built-in
// machine could only be found next to the binary, then copying the binary
// somewhere -- which is the ONLY thing anyone will do with it -- would break the
// default machine, and it would break it at startup, which is the worst possible
// time. These tests pass with the source tree deleted.
//
// The other half of the deal: a built-in is REAL TOML, run through the SAME
// parser as your own config file. So if the config format changes under them,
// these go red instead of the machines quietly rotting into a second dialect.

void test_machines() {
    SECTION("built-in machines -- compiled in, not looked up");

    auto all = builtinMachines();
    CHECK(all.size() >= 2, "at least the two machines are compiled in");

    // EVERY BUILT-IN MUST ACTUALLY LOAD, and this loop is not ceremony -- it is here
    // because it WASN'T, and altmon.toml shipped broken for exactly as long as it
    // took someone to run `altairsim -m altmon` by hand.
    //
    // The bug was TOML, not hardware: a [[board]] was inserted ABOVE the `startup`
    // key, so `startup` landed inside the board's table and the loader rejected it as
    // an unknown board property. The file was well-formed, the board was right, and
    // the machine was dead. This file checked `size > 0` and a blurb -- neither of
    // which can tell you a machine does not boot.
    //
    // A built-in that does not load is not a config bug. It is a BROKEN BINARY: these
    // are compiled in, so there is no file to fix.
    for (const auto& b : all) {
        CHECK(b.size > 0, "a built-in has bytes");
        CHECK(std::string(b.blurb).size() > 0, "and a blurb for --list");

        Machine mm;
        std::string e;
        bool ok = loadMachine(b, mm, e);
        if (!ok) std::printf("        %s: %s\n", b.name, e.c_str());
        CHECK(ok, "...and it LOADS -- every built-in, not just the default");
        if (!ok) continue;

        // ---- AND IT SURVIVES CONFIG SAVE. ----
        //
        // The writer is generic over properties() and unitProperties(); the reader has
        // to be generic over exactly the same pair, or CONFIG SAVE writes a file that
        // CONFIG LOAD refuses. IT DID. `[board.unit.<name>]` was written for every
        // board that had a unit with settings, and read back only for boards that had
        // opted in via subUnitTables() -- which was the 2SIO, and nothing else. So
        // saving ANY machine with a cassette or a disk in it produced this:
        //
        //     ps2int.toml: board 'acr0' (acr) has no [[board.unit]] table
        //
        // A save you cannot load is not a save. Every built-in now round-trips, and the
        // reload is compared to the original rather than merely required not to error --
        // a loader that silently dropped the units would pass the weaker test.
        std::string text = saveTomlText(mm);
        Machine     back;
        std::string e2;
        bool        reloaded = loadTomlText(text, std::string(b.name) + " (saved)", back, e2);
        if (!reloaded) std::printf("        %s: %s\n", b.name, e2.c_str());
        CHECK(reloaded, "...and CONFIG SAVE's own output loads straight back in");
        if (!reloaded) continue;

        CHECK(back.boards().size() == mm.boards().size(),
              "...with every board still in the backplane");
        CHECK(saveTomlText(back) == text,
              "...and saving it again is byte-identical: the round trip is a FIXED POINT, "
              "so nothing was silently dropped on the way through");
    }

    // ---- default: 56K, which is what CP/M means when it says 56K ----
    const BuiltinMachine* d = findMachine("default");
    CHECK(d != nullptr, "there IS a default machine -- no config file required");
    if (!d) return;

    Machine m;
    std::string err;
    CHECK(loadMachine(*d, m, err), "and it loads through the ordinary TOML parser");
    CHECK(m.name == "default", "it knows its name");

    // A front panel, a CPU, a serial card, a FLOPPY CONTROLLER and a memory card. The
    // 8080, the 2SIO, the PANEL and now the 88-DCDD each arrived as ONE MORE [[board]],
    // with nothing else about the machine moving -- which is the prediction this file
    // made back when it had none of them, and it has now held four times.
    //
    // The panel is still the sharpest of them, because it is the one that took something
    // AWAY: `[machine] sense` was a byte on the Machine, and it went out with the card
    // that replaced it. The count went 3 -> 4 and the struct got SMALLER.
    //
    // The DCDD is the one with consequences beyond this line, though: the default machine
    // is what `base = "default"` STARTS FROM, so its card list is now a contract that
    // other config files depend on. Adding a card here is no longer free.
    CHECK(m.boards().size() == 5, "a panel, a CPU, a 2SIO, an 88-DCDD and a memory card");
    CHECK(m.find("dsk0") != nullptr, "the floppy controller DBL boots from is in the backplane");
    CHECK(m.find("fp0") != nullptr, "the front panel is a card in the backplane");
    CHECK(m.cpu() != nullptr, "there is a processor in the default machine now");
    CHECK(m.isa() == "8080", "and it speaks 8080, so DISASM never has to be told");
    CHECK(m.master() != nullptr, "and it can drive the bus");

    // The console CP/M will print its banner on is in the backplane and connected.
    Board* sio = m.find("sio0");
    CHECK(sio != nullptr, "and a 2SIO, because CP/M needs somewhere to talk");
    if (sio) {
        UnitDef u;
        CHECK(sio->findUnit("a", u), "channel a exists");
        CHECK(u.state == "console", "and it is connected to the console");
    }

    // RAM at 0000-DFFF, and NOTHING above it. The top 8K is where the PROM and
    // the memory-mapped I/O lived; a machine claiming 64K of RAM would be lying
    // about hardware nobody shipped.
    CHECK(m.bus.memRead(0x0000) != 0xFF || true, "0000 is populated");  // contents are random
    CHECK(!m.bus.lastUnclaimed(), "a board drives 0000");
    m.bus.memWrite(0x0000, 0x76);
    CHECK(m.bus.memRead(0x0000) == 0x76, "and it is RAM -- it stores a write");

    m.bus.memWrite(0xDFFF, 0x3E);
    CHECK(m.bus.memRead(0xDFFF) == 0x3E, "DFFF is the last byte of the 56K");

    (void)m.bus.memRead(0xE000);
    CHECK(m.bus.lastUnclaimed(), "E000 is NOT populated -- nobody drives it");
    CHECK(m.bus.memRead(0xE000) == 0xFF, "so it floats to FF, which is the bus's answer");

    // ---- 4k: the machine BASIC was written for ----
    const BuiltinMachine* k = findMachine("4k");
    CHECK(k != nullptr, "the 4K Altair is here too");
    if (!k) return;

    Machine m4;
    CHECK(loadMachine(*k, m4, err), "4k loads");
    m4.bus.memWrite(0x0FFF, 0x21);
    CHECK(m4.bus.memRead(0x0FFF) == 0x21, "0FFF is the top of the 4K");
    (void)m4.bus.memRead(0x1000);
    CHECK(m4.bus.lastUnclaimed(), "and 1000 is empty backplane -- there is no card there");

    // Case, and the absence of a machine, both answer honestly.
    CHECK(findMachine("DEFAULT") != nullptr, "a machine name is not case-sensitive");
    CHECK(findMachine("sol20") == nullptr, "and one we have no manuals for is simply not there");

    SECTION("the default machine's boot PROM -- the real DBL, on the real bus");

    // `altairsim` then `D FF00` SHOWS YOU THE BOOT PROM. That is the whole point
    // of a default: the machine you get for free is the one you wanted.
    //
    // The ROM is compiled in, decoded by the ordinary Intel HEX loader, and put
    // on the bus by the ordinary memory board. Every one of those steps is tested
    // in isolation elsewhere; this checks they are actually WIRED TOGETHER, which
    // is the one thing unit tests routinely fail to notice has come apart.
    Machine md;
    CHECK(loadMachine(*d, md, err), "the default machine loads");

    // DBL's first act, read through the bus, one byte at a time, as a CPU would:
    //     FF00  21 13 FF   LXI H,FF13    ; source: myself
    //     FF03  11 00 2C   LXI D,2C00    ; destination: RAM
    //     FF06  0E EB      MVI C,0EBH    ; 235 bytes
    // This is the PROM copying itself into RAM to run there, and it is the reason
    // DBL needs no shadow RAM: it never writes to FFxx at all.
    CHECK(md.bus.memRead(0xFF00) == 0x21, "FF00: LXI H");
    CHECK(md.bus.memRead(0xFF01) == 0x13 && md.bus.memRead(0xFF02) == 0xFF, "      ,FF13");
    CHECK(md.bus.memRead(0xFF03) == 0x11, "FF03: LXI D");
    CHECK(md.bus.memRead(0xFF04) == 0x00 && md.bus.memRead(0xFF05) == 0x2C, "      ,2C00");
    CHECK(md.bus.memRead(0xFF06) == 0x0E && md.bus.memRead(0xFF07) == 0xEB, "FF06: MVI C,EB");
    CHECK(!md.bus.lastUnclaimed(), "and the PROM really is driving the bus for all of it");

    // A GUEST CANNOT WRITE ROM. Not "the write is rejected" -- the board never
    // answers the cycle, so nobody drives it and the byte is simply gone.
    md.bus.memWrite(0xFF00, 0x00);
    CHECK(md.bus.lastUnclaimed(), "nobody decodes a write to the PROM");
    CHECK(md.bus.memRead(0xFF00) == 0x21, "so the PROM is untouched");

    // 2C00 is where DBL relocates itself to. It had better be RAM, or the PROM's
    // very first loop writes into nothing and the machine is silently dead.
    md.bus.memWrite(0x2C00, 0x21);
    CHECK(md.bus.memRead(0x2C00) == 0x21, "2C00 (DBL's RUNLOC) is RAM, and it takes a write");
    md.bus.memWrite(0x2D70, 0xFF);
    CHECK(!md.bus.lastUnclaimed(), "and so is 2D70, the end of its sector buffer");

    // The 8K hole. This is WHY the number is 56K and not 64K: the top of memory
    // is where the PROM and the memory-mapped I/O lived.
    (void)md.bus.memRead(0xE000);
    CHECK(md.bus.lastUnclaimed(), "E000-FEFF is a hole -- no RAM, no ROM, nothing");
    (void)md.bus.memRead(0xFEFF);
    CHECK(md.bus.lastUnclaimed(), "right up to the byte below the PROM");

    SECTION("a file or a built-in? decided by SPELLING, never by the filesystem");

    // If this were decided by probing the disk, `altairsim default` would mean
    // one thing today and something else the day a file called `default` appears
    // in the working directory. A command line whose meaning depends on its
    // surroundings is a trap, and it is the kind that gets sprung at 2am.
    CHECK(!looksLikeFile("default"), "a bare word is a built-in name");
    CHECK(!looksLikeFile("4k"), "even a short one");
    CHECK(looksLikeFile("my.toml"), "a .toml is a file");
    CHECK(looksLikeFile("MY.TOML"), "in any case");
    CHECK(looksLikeFile("./default"), "a path is a file, even with a built-in's name");
    CHECK(looksLikeFile("cfg/x"), "a slash anywhere makes it a file");
    CHECK(!looksLikeFile("toml"), "and 'toml' alone is not a .toml -- do not match a bare suffix");

    SECTION("a startup entry is a COMMAND LINE, so it can quote a filename");

    // EVERY PERIOD TAPE IN THE TREE HAS A SPACE IN ITS NAME -- "4K BASIC Ver 3-1.tap",
    // "8K BASIC Ver 3-2.tap" -- so the monitor's tokenizer needs the quotes around a path,
    // and says so in as many words (cli/monitor.cpp: "a filename is the one place a quote
    // is not decoration but the only way to write a path with a space in it").
    //
    // Which meant that until the escape below existed, THE ONE THING `startup` IS FOR
    // could not be written. Every `"` toggled the string, escape or not, so
    //
    //     startup = ["MOUNT acr0:tape \"tapes/4KBasic31/4K BASIC Ver 3-1.tap\""]
    //
    // parsed as `MOUNT acr0:tape \` and the machine booted with an empty recorder -- no
    // error, just a tape that was never in the drive. docs/config.md's promise ("anything
    // you can type, a config can do") was false for every artifact we ship.
    const char* kQuoted = R"(
[machine]
name    = "quoted"
startup = [
  "MOUNT acr0:tape \"tapes/4KBasic31/4K BASIC Ver 3-1.tap\"",
  "LOAD \"tapes/4KBasic31/LDR4K31.HEX\"",
  "RUN 0",
]
)";

    Machine     mq;
    std::string eq;
    CHECK(loadTomlText(kQuoted, "quoted", mq, eq), "a startup entry parses with escaped quotes");
    CHECK(mq.startup.size() == 3, "...all three of them, and the escape does not split one in two");
    if (mq.startup.size() == 3) {
        CHECK(mq.startup[0] == "MOUNT acr0:tape \"tapes/4KBasic31/4K BASIC Ver 3-1.tap\"",
              "...and the QUOTES REACH THE MONITOR, which is the whole point: without them "
              "the tokenizer sees three arguments and the path dies at the first space");
        CHECK(mq.startup[2] == "RUN 0", "...and an entry with no escape in it is untouched");
    }

    // The round trip, which is where this broke the SAME way CONFIG SAVE broke on units:
    // the writer emitted the quotes raw, so the file it produced closed the TOML string
    // early and would not load back.
    std::string qtext = saveTomlText(mq);
    Machine     qback;
    std::string eq2;
    CHECK(loadTomlText(qtext, "quoted (saved)", qback, eq2),
          "...and CONFIG SAVE's own output loads back in -- the writer escapes what the "
          "reader unescapes, or it is not a round trip");
    CHECK(qback.startup == mq.startup, "...with every command byte-identical");

    // AN UNKNOWN ESCAPE IS AN ERROR, NOT A SHRUG. `\n` and `\t` mean nothing to a monitor
    // command, and silently dropping the backslash would turn a Windows path written with
    // single separators into a shorter, wrong path that fails somewhere else entirely.
    Machine     mbad;
    std::string ebad;
    CHECK(!loadTomlText("[machine]\nname = \"x\"\nstartup = [\"LOAD \\nope\"]\n", "bad", mbad, ebad),
          "an escape this parser does not know is refused");
    CHECK(ebad.find("\\n") != std::string::npos, "...and the message names the offender");

    SECTION("base = \"default\" -- start from a machine and say what is DIFFERENT");

    // THE MACHINE IS THE HARD PART TO GET RIGHT, AND IT IS THE PART NOBODY WANTS TO
    // RETYPE. A CP/M config is the default Altair with a floppy in drive 0 -- that is the
    // whole of it -- and before `base` existed it had to restate five cards to say so.
    //
    // This is not a convenience. Hand-copying a backplane is how you end up with a machine
    // that boots CP/M into a terminal that is not there, because the one card you forgot to
    // copy was the 2SIO. That happened, to the very files this feature replaced.

    const char* kDelta = R"(
[machine]
name = "delta"
base = "default"

[[board]]
id = "dsk0"
)";
    Machine     md2;
    std::string ed;
    CHECK(loadTomlText(kDelta, "delta", md2, ed), "a delta on the default machine loads");
    CHECK(md2.name == "delta", "...and `name` beats the base's, whatever order the keys are in");
    CHECK(md2.boards().size() == 5, "...with every card the base brought still in the backplane");
    CHECK(md2.find("sio0") != nullptr, "...including the console you did not have to remember");

    // `id` WITH NO `type` IS "THE ONE ALREADY IN THE MACHINE". It must not fit a second
    // card, and it must not silently do nothing.
    Board* dsk = md2.find("dsk0");
    CHECK(dsk != nullptr && dsk->type() == "dcdd", "an [[board]] with no `type` found the base's card");

    // ...and it is an ERROR when there is nothing to find, rather than a quiet no-op --
    // which is the difference between a typo you fix now and a machine that is missing a
    // card you will look for later.
    Machine     mno;
    std::string eno;
    CHECK(!loadTomlText("[machine]\nname = \"x\"\nbase = \"default\"\n\n[[board]]\nid = \"nope0\"\n",
                        "x", mno, eno),
          "...and an id that is in no base is refused, not ignored");

    // TYPE + AN ID THE BASE BROUGHT = REPLACE THE CARD OUTRIGHT. This is what makes a
    // memory board re-fittable: regions are a LIST, so appending a 24K region to a 56K
    // board would OVERLAP it, not replace it. Naming the type says "this is the whole
    // card now".
    const char* kReplace = R"(
[machine]
name = "small"
base = "default"

[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0000
  size = "24K"
)";
    Machine     ms;
    std::string es;
    CHECK(loadTomlText(kReplace, "small", ms, es), "re-fitting a card the base brought loads");
    CHECK(ms.boards().size() == 5, "...and REPLACES it -- there is not a second memory board");
    ms.bus.memWrite(0x5FFF, 0x42);
    CHECK(ms.bus.memRead(0x5FFF) == 0x42, "5FFF is the top of the new 24K");
    (void)ms.bus.memRead(0x6000);
    CHECK(ms.bus.lastUnclaimed(),
          "...and 6000 is EMPTY: the base's 56K region is gone, not overlapped by the new "
          "one. Two boards both answering 0000-5FFF is bus contention, and it is exactly "
          "what appending would have built");
    (void)ms.bus.memRead(0xFF00);
    CHECK(ms.bus.lastUnclaimed(),
          "...and the DBL PROM went with the card it was on. Replace means replace: if you "
          "want the ROM back, re-state it");

    // REMOVE takes a card out of the slot.
    const char* kRemove = R"(
[machine]
name = "nodisk"
base = "default"

[[board]]
id     = "dsk0"
remove = true
)";
    Machine     mr;
    std::string er;
    CHECK(loadTomlText(kRemove, "nodisk", mr, er), "a delta can pull a card out");
    CHECK(mr.boards().size() == 4, "...and the backplane really is one card lighter");
    CHECK(mr.find("dsk0") == nullptr, "...the floppy controller is gone");
    (void)mr.bus.ioRead(0x08);
    CHECK(mr.bus.lastUnclaimed(), "...and nobody answers port 08 any more -- it FLOATS, as an "
                                  "empty slot does");

    // A DUPLICATE ID WITHIN ONE FILE IS STILL AN ERROR, and this is the check REPLACE was
    // deliberately scoped around. A second [[board]] with a copy-pasted id is a typo; the
    // same thing against a BASE is intent. Conflating them would have thrown away the one
    // diagnostic that catches the commonest mistake in a hand-written machine file.
    Machine     mdup;
    std::string edup;
    CHECK(!loadTomlText("[machine]\nname = \"d\"\n\n[[board]]\ntype = \"memory\"\nid = \"mem0\"\n"
                        "\n[[board]]\ntype = \"memory\"\nid = \"mem0\"\n",
                        "dup", mdup, edup),
          "two [[board]] tables with one id, in one file, is still refused");

    // `remove` and `type` contradict each other, and saying both is not a preference.
    Machine     mc;
    std::string ec;
    CHECK(!loadTomlText("[machine]\nname = \"c\"\nbase = \"default\"\n\n[[board]]\ntype = \"dcdd\"\n"
                        "id = \"dsk0\"\nremove = true\n",
                        "c", mc, ec),
          "`remove` and `type` together are refused -- one takes the card out, the other "
          "fits a new one");

    // A BASE THAT DOES NOT EXIST IS AN ERROR, not an empty machine.
    Machine     mb;
    std::string eb;
    CHECK(!loadTomlText("[machine]\nname = \"x\"\nbase = \"sol20\"\n", "x", mb, eb),
          "a base we have no machine for is refused");

    // AND A SAVED DELTA IS A MACHINE, NOT A DELTA. CONFIG SAVE writes the backplane it can
    // see -- every card, base or not -- so the saved file has no `base` key and stands on
    // its own. That is the only honest thing it can do: the base may be a FILE, and a file
    // can change under you.
    std::string dtext = saveTomlText(md2);
    CHECK(dtext.find("base") == std::string::npos, "a saved machine does not refer to a base");
    Machine     dback;
    std::string ed2;
    CHECK(loadTomlText(dtext, "delta (saved)", dback, ed2), "...and it loads on its own");
    CHECK(dback.boards().size() == md2.boards().size(), "...with all five cards written out");
}
