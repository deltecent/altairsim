// The MITS 8800b Turnkey Module (docs/boards/mits-turnkey.md, reference/MITS Turn Key
// Board.md).
//
// The acceptance tests boot CP/M on a Turnkey machine (tests/acceptance, examples/turnkey).
// This file pins what a boot does NOT exercise on its own: the phantom PROM's one-shot
// disable (an IN from FE/FF, input-only per Service Bulletin 007), the Auto-Start JMP jam,
// the sense switches, and that the integrated 6850 answers at the right ports -- the things
// that fail silently, or on the cycle after the one you were watching.
//
// Built by hand rather than from a .toml, so a config bug cannot make these go red. The RAM
// is strapped honors_phantom=read, which is what lets the boot PROM shadow reads while
// writes fall through to the RAM underneath (the same strap the Tarbell uses,
// tests/test_phantom.cpp).

#include "boards/mits-turnkey.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/stream.h"
#include "test.h"

#include <string>

using namespace altair;

namespace {

// A Turnkey machine: the card (dbl in socket H1 at FF00, sense = 0x10) over 64K of RAM.
struct Rig {
    Machine       m;
    TurnkeyBoard* tk  = nullptr;
    MemoryBoard*  mem = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);  // re-derive PHANTOM*/decode/pin-73 every cycle, like Tarbell

        tk = dynamic_cast<TurnkeyBoard*>(m.add("turnkey", "tk0", err));
        CHECK(tk != nullptr, "the turnkey board type is registered");
        CHECK(tk->loadSubUnit("socket", {{"at", "FF00"}, {"mount", "builtin:dbl"}}, err),
              "socket H1 takes the dbl boot PROM");
        CHECK(setProperty(*tk, "sense", "10", err), "the sense switches are a hex strap");

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;  // full 64K -- the PROM shadows the top 1K until it phantoms out
        mem->addRegion(r, err);
        CHECK(setProperty(*mem, "fill", "zero", err), "RAM comes up zeroed");
        CHECK(setProperty(*mem, "honors_phantom", "read", err),
              "the RAM under the shadow: off for reads, answering writes");

        m.power();
    }
};

} // namespace

void test_turnkey() {
    SECTION("MITS Turnkey -- the phantom boot PROM");
    {
        Rig  rig;
        Bus& bus = rig.m.bus;

        // dbl's first byte at FF00 is 21 (LXI H,FF13) -- see machines/default.toml.
        CHECK(bus.memRead(0xFF00) == 0x21, "FF00 reads the boot PROM (dbl), not RAM");
        CHECK(bus.memRead(0xFC00) == 0xFF, "an unpopulated socket in the 1K window reads 0xFF");

        // A write into the window falls through to the RAM underneath.
        bus.memWrite(0xFF00, 0x99);
        CHECK(bus.memRead(0xFF00) == 0x21, "reads still come back from the PROM");
        CHECK(rig.mem->storeAt(0xFF00) == 0x99, "...but the byte landed in the RAM beneath");

        // OUT FF does NOT disable the PROM -- SB007 made the trigger input-only.
        bus.ioWrite(0xFF, 0x00);
        CHECK(bus.memRead(0xFF00) == 0x21, "an OUT FF leaves the boot PROM armed");

        // IN FF returns the sense switches AND is the disable trigger, in the one cycle.
        CHECK(bus.ioRead(0xFF) == 0x10, "IN FF returns the sense switches (SA8..SA15)");
        CHECK(bus.memRead(0xFF00) == 0x99, "...and the PROM is gone -- FF00 now reads RAM");
        CHECK(bus.memRead(0xFC00) == 0x00, "the whole window is RAM now (zero-filled)");
    }

    SECTION("MITS Turnkey -- IN FE also disables the PROM");
    {
        Rig  rig;
        Bus& bus = rig.m.bus;
        CHECK(bus.memRead(0xFF00) == 0x21, "armed at power-on");
        bus.ioRead(0xFE);
        CHECK(bus.memRead(0xFF00) == 0x00, "an IN FE disables it just as an IN FF does");
    }

    SECTION("MITS Turnkey -- every reset re-arms the PROM (reference §9)");
    {
        Rig  rig;
        Bus& bus = rig.m.bus;
        bus.ioRead(0xFF);
        CHECK(bus.memRead(0xFF00) == 0x00, "disabled by the sense-switch read");
        rig.m.reset(Reset::Bus);
        CHECK(bus.memRead(0xFF00) == 0x21, "RESET* (front-panel) re-enables the boot PROM");
        bus.ioRead(0xFF);
        rig.m.reset(Reset::PowerOn);
        CHECK(bus.memRead(0xFF00) == 0x21, "POC* re-arms it too");
    }

    SECTION("MITS Turnkey -- the Auto-Start JMP jam");
    {
        Rig  rig;
        Bus& bus = rig.m.bus;

        // Known bytes in low RAM, to prove the jam overrides them and then gets out of the
        // way. Writes fall straight through and do not disturb the latches.
        bus.memWrite(0, 0x76);
        bus.memWrite(1, 0x77);
        bus.memWrite(2, 0x78);
        rig.m.reset(Reset::Bus);  // PC is back at 0; the jam is armed at step 0

        // C3 00 <hi>: JMP to the START ADDR switches (default FC00) shifted into the high
        // byte. The low byte is always 0 -- SW8/SW9 are the high eight bits.
        CHECK(bus.memRead(0) == 0xC3, "fetch 0 is the JMP opcode");
        CHECK(bus.memRead(1) == 0x00, "fetch 1 is the low address byte (always 0)");
        CHECK(bus.memRead(2) == 0xFC, "fetch 2 is the START ADDR high byte (FC00)");

        // The jam is a one-shot: three fetches and it is done, low RAM reappears.
        CHECK(bus.memRead(0) == 0x76, "the jam is over -- fetch 0 now reads RAM");
        CHECK(bus.memRead(1) == 0x77, "...and so does 1");
    }

    SECTION("MITS Turnkey -- START ADDR is a strap");
    {
        Rig         rig;
        Bus&        bus = rig.m.bus;
        std::string err;
        CHECK(setProperty(*rig.tk, "start", "FF00", err), "START ADDR is a hex jumper");
        rig.m.reset(Reset::Bus);
        CHECK(bus.memRead(0) == 0xC3, "still a JMP");
        (void)bus.memRead(1);
        CHECK(bus.memRead(2) == 0xFF, "now it jumps to FF00");
        CHECK(!setProperty(*rig.tk, "start", "FF80", err),
              "a START ADDR that is not a multiple of 256 is refused");
    }

    SECTION("MITS Turnkey -- the integrated 6850 SIO at 0x10");
    {
        Rig         rig;
        Bus&        bus = rig.m.bus;
        std::string err;
        CHECK(rig.tk->connect("tty", "scripted", err), "connect a scripted terminal to tty");
        auto* tty = dynamic_cast<ScriptedStream*>(rig.tk->unitStream("tty"));
        CHECK(tty != nullptr, "the tty unit exposes its stream");

        // Program 8N1 /16: master reset, then the format. (mc6850.h: two OUTs, always.)
        bus.ioWrite(0x10, 0x03);  // master reset -- holds the chip until the next control write
        bus.ioWrite(0x10, 0x15);  // divide /16, 8 data + 1 stop, RTS low, interrupts off
        CHECK((bus.ioRead(0x10) & 0x02) != 0, "status at 0x10 (even): TDRE set, ready to send");

        // Receive: a byte fed to the line lands in RDRF and reads back at the data port.
        tty->feed("Z");
        CHECK((bus.ioRead(0x10) & 0x01) != 0, "RDRF set once a character arrives");
        CHECK(bus.ioRead(0x11) == 'Z', "the data port at 0x11 (odd) yields it");

        // Transmit: a byte written to the data port goes out the line.
        bus.ioWrite(0x11, 'A');
        rig.m.clock.advance(50000);  // comfortably past one 9600-baud character time
        CHECK(tty->out().find('A') != std::string::npos, "the byte went out the terminal");
    }
}
