#include "test.h"

#include "boards/s100-memory.h"
#include "core/machine.h"

using namespace altair;

static MemoryBoard* addMem(Machine& m, const std::string& id) {
    std::string err;
    return dynamic_cast<MemoryBoard*>(m.add("memory", id, err));
}
static Region ram(uint16_t at, uint32_t size) {
    Region r;
    r.kind = RegionKind::Ram;
    r.at = at;
    r.size = size;
    return r;
}

void test_bus() {
    SECTION("the bus (DESIGN.md 4) -- it carries signals; it does not decide");

    {
        // The floating bus: ONE rule, and RST 7 falls out of it for free.
        Machine m;
        CHECK(m.bus.memRead(0x1234) == 0xFF, "unmapped memory reads FF -- nobody drives the bus");
        m.bus.memWrite(0x1234, 0x42);
        CHECK(m.bus.lastUnclaimed(), "an unclaimed write is simply GONE -- nobody latched it");
        CHECK(m.bus.ioRead(0x10) == 0xFF, "an unmapped IN reads FF");
        CHECK(m.bus.intAck() == 0xFF,
              "an unvectored IntAck floats to FF = RST 7. The bus does not know what a vector is; "
              "this is the SAME rule as unmapped memory.");
    }

    {
        // Contention is REPORTED, not resolved. A simulator that picks a winner
        // is lying to you about a fault a real backplane would have handed you.
        Machine m;
        std::string err;
        auto* a = addMem(m, "mem0");
        auto* b = addMem(m, "mem1");
        a->addRegion(ram(0x0000, 0x1000), err);
        b->addRegion(ram(0x0000, 0x1000), err);
        m.power();

        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0x0500;
        CHECK(m.bus.respondersTo(c).size() == 2, "two boards both decode 0500");

        m.bus.clearLog();
        m.bus.memRead(0x0500);
        CHECK(m.bus.drain().size() == 1, "and the bus SAYS SO");
        CHECK(m.bus.drain()[0].find("mem0") != std::string::npos &&
                  m.bus.drain()[0].find("mem1") != std::string::npos,
              "naming both boards");
    }

    {
        // The claim §4.6 makes: a PHANTOM* shadow is NOT contention. It falls out
        // of the model -- the shadowed board returns false from decodes() -- and
        // is not a special case anywhere.
        Machine m;
        std::string err;
        auto* r = addMem(m, "ram0");
        r->addRegion(ram(0x0000, 0x10000), err);

        auto* rom = addMem(m, "rom0");
        Region x;
        x.kind = RegionKind::Rom;
        x.at = 0xFF00;
        x.mount = "builtin:dbl";
        CHECK(rom->addRegion(x, err), ("builtin:dbl mounts: " + err).c_str());
        m.power();

        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0xFF00;
        auto who = m.bus.respondersTo(c);
        CHECK(who.size() == 1, "under PHANTOM*, exactly ONE board answers -- not two");
        CHECK(who.size() == 1 && who[0]->id == "rom0", "and it is the ROM");

        m.bus.clearLog();
        CHECK(m.bus.memRead(0xFF00) == 0x21, "FF00 reads 21 (LXI H) -- the ROM, through the bus");
        CHECK(m.bus.drain().empty(), "NO CONTENTION REPORTED. A shadow is not a fault.");

        // Elsewhere, the RAM is untouched and still answers.
        CHECK(m.bus.respondersTo({Cycle::MemRead, 0x0100, 0, false}).size() == 1,
              "the RAM still answers everywhere the ROM is not");
    }

    {
        // SET BUS UNCLAIMED (DESIGN.md 4.6.1): a guest reaching an I/O port no board
        // decodes reads 0xFF for ever and hangs -- this names the port and the PC.
        // I/O only, default Silent, de-duped once per port+direction per run.
        Machine m;                 // empty backplane: no board decodes anything
        m.bus.setInstrPc(0x0113);  // what the run loop publishes each instruction

        // Default is Silent -- the whole point is that no existing machine gains a line.
        m.bus.clearLog();
        m.bus.ioWrite(0xFE, 0x01);
        CHECK(m.bus.drain().empty(), "SILENT is the default -- an unclaimed OUT says nothing");
        CHECK(!m.bus.takeUnclaimedHalt(), "and it does not arm a halt");

        // WARN names the port, the PC, the byte and the direction, in the doc's words.
        m.bus.setUnclaimedPolicy(Unclaimed::Warn);
        m.bus.clearLog();
        m.bus.ioWrite(0xFE, 0x01);
        CHECK(m.bus.drain().size() == 1, "WARN emits exactly one line for an unclaimed OUT");
        CHECK(m.bus.drain()[0] ==
                  "warning: OUT FE <- 01 at PC=0113: no board decodes port 0xFE. "
                  "reads float to 0xFF.",
              "and it is the line DESIGN.md 4.6.1 documents, verbatim");
        CHECK(!m.bus.takeUnclaimedHalt(), "WARN runs on -- it does not arm a halt");

        // De-dup: the same port+direction again this run is silent -- a poll loop on an
        // absent UART must not bury the console.
        m.bus.clearLog();
        m.bus.ioWrite(0xFE, 0x02);
        CHECK(m.bus.drain().empty(), "the SAME port+direction warns once per run, not every time");

        // The other direction on the same port is a different fact, and IS reported.
        m.bus.clearLog();
        m.bus.ioRead(0xFE);
        CHECK(m.bus.drain().size() == 1, "IN from the same port is a separate warning");
        CHECK(m.bus.drain()[0].rfind("warning: IN FE -> FF", 0) == 0, "and it floated to FF");

        // resetUnclaimedWarnings re-arms, as the start of each RUN does.
        m.bus.resetUnclaimedWarnings();
        m.bus.clearLog();
        m.bus.ioWrite(0xFE, 0x03);
        CHECK(m.bus.drain().size() == 1, "resetUnclaimedWarnings re-arms the port for a new run");

        // Memory is NEVER warned -- guests scan memory constantly and it is normal.
        m.bus.resetUnclaimedWarnings();
        m.bus.clearLog();
        m.bus.memRead(0x1234);
        m.bus.memWrite(0x1234, 0x00);
        CHECK(m.bus.drain().empty(), "unclaimed MEMORY is silent under WARN -- I/O only (4.6.1)");
    }

    {
        // HALT logs AND arms the boundary stop the run loop honors (takeUnclaimedHalt),
        // once, naming the offending access.
        Machine m;
        m.bus.setInstrPc(0x0200);
        m.bus.setUnclaimedPolicy(Unclaimed::Halt);
        m.bus.clearLog();
        m.bus.ioWrite(0xEF, 0xAB);
        CHECK(m.bus.drain().size() == 1, "HALT still logs the warning line");
        CHECK(m.bus.haltPort() == 0xEF && m.bus.haltWasWrite(), "and records which access tripped it");
        CHECK(m.bus.takeUnclaimedHalt(), "HALT arms the boundary stop");
        CHECK(!m.bus.takeUnclaimedHalt(), "take is read-and-clear -- it fires once");
    }
}
