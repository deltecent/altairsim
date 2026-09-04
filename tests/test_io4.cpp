#include "test.h"

#include "boards/io4.h"
#include "boards/mits-88cpu.h"
#include "boards/s100-memory.h"
#include "core/board.h"
#include "core/clock.h"
#include "core/machine.h"
#include "host/stream.h"

using namespace altair;

namespace {

// An IO-4 on the bench with a scripted line on EACH of its two UART channels. A chip-backed
// card, so unlike the gsio rig it needs a Clock -- a UART with no crystal cannot time a
// character. Clock FIRST (a board holding a Clock deadline must outlive nothing that has
// already fired -- and the destruction-order sanitizer trap on Windows wants it built first).
// Each stream is bound through the REAL connect path, and the format straps live per unit
// ([board.unit.a]/[board.unit.b]), reached through unitProperties() exactly as a machine file
// would. setResolver() is installed once in tests/main.cpp.
struct Rig {
    Clock           clk;
    Io4Board        b;
    ScriptedStream* a  = nullptr;
    ScriptedStream* bb = nullptr;

    Rig() {
        b.attachClock(&clk);
        std::string err;
        CHECK(b.connect("a", "scripted", err), "the scripted line connects to channel a");
        CHECK(b.connect("b", "scripted", err), "the scripted line connects to channel b");
        a  = dynamic_cast<ScriptedStream*>(b.unitStream("a"));
        bb = dynamic_cast<ScriptedStream*>(b.unitStream("b"));
        CHECK(a != nullptr && bb != nullptr, "and both are ScriptedStreams we can drive");
    }

    uint8_t in(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return b.read(c);
    }
    void out(uint8_t port, uint8_t v) {
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = port;
        c.data = v;
        b.write(c);
    }
    bool decodes(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return b.decodes(c);
    }

    long long get(const std::string& unit, const std::string& key) {
        for (auto& pr : b.unitProperties(unit))
            if (pr.name == key) return pr.get().i();
        return -1;
    }
    std::string getStr(const std::string& unit, const std::string& key) {
        for (auto& pr : b.unitProperties(unit))
            if (pr.name == key) return pr.get().s();
        return "(none)";
    }
    void set(const std::string& unit, const std::string& key, const std::string& v) {
        std::string err;
        CHECK(setPropertyIn(b.unitProperties(unit), b.id + ":" + unit, key, v, err),
              ("SET " + unit + "." + key + "=" + v).c_str());
        b.configChanged();
    }
    void setBoard(const std::string& key, const std::string& v, bool ok = true) {
        std::string err;
        bool        r = setProperty(b, key, v, err);
        CHECK(r == ok, ("SET " + key + "=" + v).c_str());
    }
};

} // namespace

void test_io4() {
    SECTION("IO-4 -- identity and the two serial units 'a' and 'b'");
    {
        Rig g;
        CHECK(g.b.type() == "io4", "identifies as io4");
        CHECK(g.b.units().size() == 2, "two units -- a dual-serial card has two lines");
        CHECK(g.b.units()[0].name == "a", "first unit is 'a'");
        CHECK(g.b.units()[1].name == "b", "second unit is 'b'");
        CHECK(g.b.units()[0].kind == UnitKind::Serial, "a is serial");
        CHECK(g.b.units()[1].kind == UnitKind::Serial, "b is serial");

        std::string err;
        CHECK(!g.b.connect("tty", "null", err), "there is no unit but a/b");
        CHECK(err.find("tty") != std::string::npos, "and the error names the bad unit");
    }

    SECTION("IO-4 -- the default 4-port block: A at 0/1, B at 2/3");
    {
        // Switch S3 = all ON -> ports 0-3 (reference/SSM IO-4 2P+2S IO Board.md): Serial A
        // status/data at 0/1, Serial B at 2/3.
        Rig g;
        CHECK(g.decodes(0x00), "answers A's status port");
        CHECK(g.decodes(0x01), "answers A's data port");
        CHECK(g.decodes(0x02), "answers B's status port");
        CHECK(g.decodes(0x03), "answers B's data port");
        CHECK(!g.decodes(0x04), "and nothing past the block");
        CHECK(!g.decodes(0xFF), "nor a far-off port");

        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0x00;
        CHECK(!g.b.decodes(c), "decodes no MEMORY -- it is an I/O card");
    }

    SECTION("IO-4 -- switch S3 is a 4-port block: the base must be a multiple of 4");
    {
        Rig g;
        g.setBoard("port", "10");  // hex 0x10, a 4-boundary
        CHECK(g.decodes(0x10) && g.decodes(0x13), "the whole block relocated to 0x10-0x13");
        CHECK(!g.decodes(0x00), "and it left the old block");
        g.setBoard("port", "12", false);  // not a multiple of 4 -- refused
        CHECK(g.decodes(0x10), "the refused SET left the base where it was");
    }

    SECTION("IO-4 -- default profile is altair-rev1 (active low): DAV bit0, TBMT bit7");
    {
        // The board ships strapped for the SSM 8080 monitor console -- profile altair-rev1:
        // DAV -> D0, TBMT -> D7, status inverted (74368). An unconnected/quiet line: the
        // transmitter is ready (TBMT asserted -> bit7 reads 0, active low) and nothing is
        // waiting to receive (DAV clear -> bit0 reads 1).
        Rig g;
        CHECK(g.getStr("a", "profile") == "altair-rev1", "channel A defaults to altair-rev1");
        CHECK(g.getStr("b", "profile") == "altair-rev1", "channel B too");

        uint8_t s = g.in(0x00);
        CHECK((s & 0x80) == 0, "TBMT ready reads 0 in bit 7 (active low)");
        CHECK((s & 0x01) != 0, "DAV clear reads 1 in bit 0 (active low, nothing to receive)");

        g.a->feed("K");
        s = g.in(0x00);
        CHECK((s & 0x01) == 0, "a character on the line drives DAV -> bit 0 reads 0");
    }

    SECTION("IO-4 -- profile i8251: the full six-signal status map, positive sense");
    {
        // TBMT->D0, DAV->D1, TEOC->D2, RPE->D3, ROR->D4, RFE->D5, not inverted. On a quiet
        // line TBMT and TEOC are asserted (transmitter idle) and DAV is clear; the three error
        // signals are always inactive, so positive sense reads them 0.
        Rig g;
        g.set("a", "profile", "i8251");
        CHECK(g.getStr("a", "profile") == "i8251", "the profile took");
        CHECK(g.getStr("a", "stat_tbmt") == "0", "TBMT strapped to D0");
        CHECK(g.getStr("a", "stat_teoc") == "2", "TEOC strapped to D2");

        uint8_t s = g.in(0x00);
        CHECK((s & 0x01) != 0, "TBMT asserted reads 1 in D0 (positive sense)");
        CHECK((s & 0x02) == 0, "DAV clear reads 0 in D1");
        CHECK((s & 0x04) != 0, "TEOC asserted reads 1 in D2 -- the transmitter is idle");
        CHECK((s & 0x38) == 0, "the three always-inactive error bits (D3-D5) read 0");

        g.a->feed("Q");
        s = g.in(0x00);
        CHECK((s & 0x02) != 0, "a character drives DAV -> D1 reads 1");
    }

    SECTION("IO-4 -- invert_status flips the driven bits (74367 vs 74368)");
    {
        // altair-rev0 is DAV->D5, TBMT->D1, positive sense. Turn invert on and the same
        // signals read the other way -- and the always-inactive error lines, now inverted,
        // read 1 only where they are strapped (nowhere here), so the byte is just the two.
        Rig g;
        g.set("a", "profile", "altair-rev0");
        uint8_t pos = g.in(0x00);
        CHECK((pos & 0x02) != 0, "TBMT asserted reads 1 in D1 (positive)");
        CHECK((pos & 0x20) == 0, "DAV clear reads 0 in D5 (positive)");

        g.set("a", "invert_status", "true");
        CHECK(g.getStr("a", "profile") == "custom",
              "inverting a positive-sense profile no longer matches it -- now custom");
        uint8_t neg = g.in(0x00);
        CHECK((neg & 0x02) == 0, "TBMT asserted now reads 0 in D1 (inverted)");
        CHECK((neg & 0x20) != 0, "DAV clear now reads 1 in D5 (inverted)");
    }

    SECTION("IO-4 -- port_reversal swaps a channel's status and data addresses");
    {
        // Default (PR off): A status at 0, data at 1. Turn PR on and the two exchange
        // addresses -- data at 0, status at 1 (the IMSAI order). Transmit shows it without
        // tripping the receiver's line-rate pacing: an OUT to the data port reaches the wire.
        Rig g;
        g.out(0x01, 'X');
        CHECK(g.a->out() == "X", "PR off: A's data port is 1");

        g.set("a", "port_reversal", "true");
        g.out(0x00, 'Q');
        CHECK(g.a->out() == "XQ", "PR on: A's data port is now 0");

        g.out(0x01, 0xAA);  // port 1 is now the status/control port -- writes go nowhere
        CHECK(g.a->out() == "XQ", "and port 1 is now the status/control port -- write discarded");
        CHECK(g.decodes(0x00) && g.decodes(0x01), "the block still occupies the same two ports");
    }

    SECTION("IO-4 -- a custom status map: strap DAV and TBMT to arbitrary bits");
    {
        // trgeuy's roll-your-own: no profile, just individual straps. Put DAV on D3 and TBMT
        // on D4, positive sense, everything else unconnected.
        Rig g;
        g.set("a", "invert_status", "false");  // the default profile is inverted; go positive
        g.set("a", "stat_dav", "3");
        g.set("a", "stat_tbmt", "4");
        g.set("a", "stat_teoc", "none");
        CHECK(g.getStr("a", "profile") == "custom", "a hand-rolled map reports as custom");

        uint8_t s = g.in(0x00);
        CHECK((s & 0x10) != 0, "TBMT asserted reads 1 in D4");
        CHECK((s & 0x08) == 0, "DAV clear reads 0 in D3");
        CHECK((s & 0xE7) == 0, "and no other bit is driven");

        g.a->feed("M");
        s = g.in(0x00);
        CHECK((s & 0x08) != 0, "a character drives DAV -> D3 reads 1");
    }

    SECTION("IO-4 -- the data path both directions, and a discarded control write");
    {
        Rig g;
        g.a->feed("Q");
        CHECK((g.in(0x00) & 0x01) == 0, "DAV asserted -- a character is waiting");
        CHECK(g.in(0x01) == 'Q', "A's data port yields it");
        CHECK((g.in(0x00) & 0x01) != 0, "and reading it clears DAV (bit 0 back to 1)");
        CHECK(g.b.rxBytes() == 1, "the byte is counted");

        g.out(0x01, 'X');
        CHECK(g.a->out() == "X", "a write to A's data port goes out A's line");

        // OUT to the status/control port is accepted and discarded -- there is no control
        // register on the 1602 UART (word format is soldered pins).
        g.out(0x00, 0xAA);
        CHECK(g.a->out() == "X", "control writes go nowhere -- the line is unchanged");
    }

    SECTION("IO-4 -- the two channels are independent: no cross-talk");
    {
        Rig g;
        g.a->feed("A");
        g.bb->feed("B");
        CHECK(g.in(0x01) == 'A', "A delivers the byte fed to A");
        CHECK(g.in(0x03) == 'B', "B delivers the byte fed to B");
        CHECK(g.b.rxBytes() == 2, "rxBytes() sums both channels' deliveries");

        g.out(0x01, 'x');  // A's data port
        CHECK(g.a->out() == "x", "A's transmit reaches A's line");
        CHECK(g.bb->out().empty(), "and nothing bled onto B's line");
    }

    SECTION("IO-4 -- per-channel word format: data bits, parity, stop bits");
    {
        // These are switch S2 (Serial A) / S1 (Serial B) on the real card -- here the UART's
        // own format pins, per unit. They default to 8N1 and are independent between channels.
        Rig g;
        CHECK(g.get("a", "data_bits") == 8, "A defaults to 8 data bits");
        CHECK(g.getStr("a", "parity") == "none", "A defaults to no parity");
        CHECK(g.get("a", "stop_bits") == 1, "A defaults to 1 stop bit");

        g.set("a", "data_bits", "7");
        g.set("a", "parity", "even");
        g.set("a", "stop_bits", "2");
        CHECK(g.get("a", "data_bits") == 7, "A now 7 data bits");
        CHECK(g.getStr("a", "parity") == "even", "A now even parity");
        CHECK(g.get("a", "stop_bits") == 2, "A now 2 stop bits");

        CHECK(g.get("b", "data_bits") == 8, "B is untouched -- the two channels are strapped alone");
        CHECK(g.getStr("b", "parity") == "none", "B still 8N1");
    }

    SECTION("IO-4 -- baud is a SINGLE rate (RX=TX)");
    {
        // A real host serial port cannot be programmed to a split rate, so the board's W3
        // independent-RX/TX capability is documented, not exposed: one `baud` per channel.
        Rig g;
        CHECK(g.get("a", "baud") == 9600, "A defaults to 9600");
        bool split = false;
        for (auto& pr : g.b.unitProperties("a"))
            if (pr.name == "rx_baud" || pr.name == "tx_baud") split = true;
        CHECK(!split, "no rx_baud/tx_baud -- a single rate only");
        g.set("a", "baud", "1200");
        CHECK(g.get("a", "baud") == 1200, "and it is settable");
    }

    SECTION("IO-4 -- connectStream installs a pre-built line on a named channel");
    {
        Clock    clk;
        Io4Board b;
        b.attachClock(&clk);
        auto            s   = std::make_unique<ScriptedStream>();
        ScriptedStream* raw = s.get();
        std::string     err;
        CHECK(b.connectStream("b", std::move(s), err), "connectStream binds channel b");
        CHECK(b.unitStream("b") == raw, "and b's line is the stream we handed in");

        raw->feed("W");
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x03;  // B's data port
        CHECK(b.read(c) == 'W', "a byte on B's pre-built line reaches the guest");

        CHECK(!b.connectStream("z", std::make_unique<NullStream>(), err),
              "connectStream to a missing unit fails");
    }

    SECTION("IO-4 -- boots the SSM 8080 System Monitor on channel A");
    {
        // THE ACCEPTANCE. The SSM 8080 monitor's console is a MITS-SIO-Rev-0 port at 0/1 --
        // exactly the IO-4's default channel A -- so a real 8080 machine with this card boots
        // to the "MONITOR V1.0" banner. The memory map mirrors the real one: 60K of RAM and
        // the monitor as a ROM at F000 (a built-in, docs/roms.md). That the top of RAM is EFFF
        // matters -- the monitor SIZES memory at cold start and puts its stack at the top it
        // finds, so an all-RAM machine would let it climb into its own image. Channel A carries
        // a scripted line so we can read the banner back out of it.
        std::string err;

        Machine m;
        auto*   mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region  ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0;
        ram.size = 0xF000;  // 60K, 0000-EFFF -- the monitor's ROM sits above it
        mem->addRegion(ram, err);
        Region rom;
        rom.kind  = RegionKind::Rom;
        rom.at    = 0xF000;
        rom.mount = "builtin:ssm-8080mon";
        CHECK(mem->addRegion(rom, err), "the monitor ROM mounts at F000");
        setProperty(*mem, "fill", "zero", err);

        auto* io = dynamic_cast<Io4Board*>(m.add("io4", "io0", err));
        CHECK(io != nullptr, "the io4 board adds");
        auto            s   = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        std::string     cerr;
        CHECK(io->connectStream("a", std::move(s), cerr), "channel A gets the console line");

        m.add("8080", "cpu0", err);
        m.power();

        // Cold-start at the monitor's ORG (F000 is `JMP BEGIN`, the head of its jump table).
        m.cpu()->setPc(0xF000);

        // Run until the banner lands, giving up well before a real hang would matter. The
        // console is baud-paced (TBMT is a deadline), so the clock must advance with the CPU.
        bool banner = false;
        for (int i = 0; i < 2000000 && !banner; ++i) {
            StepResult st = m.master()->step(m.bus);
            m.clock.advance(st.tStates);
            io->pump();
            if (tty->out().find("MONITOR V1.0") != std::string::npos) banner = true;
        }
        CHECK(banner, "the IO-4 console prints MONITOR V1.0");
    }
}
