#include "test.h"

#include "boards/io4.h"
#include "boards/mits-88cpu.h"
#include "boards/mits-88virtc.h"
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
    void pump() { b.pump(); }
    // Bind a scripted line to a parallel port ("pa"/"pb") through the real connect path.
    ScriptedStream* connectParallel(const std::string& unit) {
        std::string err;
        CHECK(b.connect(unit, "scripted", err), ("parallel " + unit + " connects").c_str());
        return dynamic_cast<ScriptedStream*>(b.unitStream(unit));
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

// A whole machine -- 88-VI, io4, 60K RAM and an 8080 -- to prove an io4 interrupt strap
// reaches the BUS and vectors, not just that the pin moves. Mirrors the 88-VI suite's Rig.
// Verify mode re-derives pin 73 AND all eight VI lines on every bus cycle and aborts the
// instant the io4 disagrees with a wire. Serial A carries a scripted line, parallel A too.
struct IrqRig {
    Machine         m;
    Io4Board*       io = nullptr;
    VirtcBoard*     vi = nullptr;
    ScriptedStream* a  = nullptr;
    ScriptedStream* pa = nullptr;

    IrqRig() {
        std::string err;
        m.bus.setVerify(true);

        auto*  mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        m.add("8080", "cpu0", err);
        vi = dynamic_cast<VirtcBoard*>(m.add("virtc", "vi0", err));
        io = dynamic_cast<Io4Board*>(m.add("io4", "io0", err));
        CHECK(io != nullptr && vi != nullptr, "the machine has an io4 and an 88-VI");

        CHECK(io->connect("a", "scripted", err), "Serial A gets a scripted line");
        CHECK(io->connect("pa", "scripted", err), "Parallel A gets a scripted line");
        a  = dynamic_cast<ScriptedStream*>(io->unitStream("a"));
        pa = dynamic_cast<ScriptedStream*>(io->unitStream("pa"));

        m.power();
    }

    // OUT to the 88-VI control port, enabling the structure the way a guest does.
    void    ctl(uint8_t v) { m.bus.ioWrite(0xFE, v); }
    uint8_t intAck() { return m.bus.intAck(); }
    // Let a paced serial receiver clock its byte in (the card's own deadline fires here).
    void advance() {
        for (int i = 0; i < 200; ++i) m.clock.advance(1000);
    }
};

} // namespace

void test_io4() {
    SECTION("IO-4 -- identity and its four units: serial a/b, parallel pa/pb");
    {
        Rig g;
        CHECK(g.b.type() == "io4", "identifies as io4");
        auto u = g.b.units();
        CHECK(u.size() == 4, "four units -- two serial channels and two parallel ports");
        CHECK(u[0].name == "a" && u[1].name == "b", "the two serial channels come first");
        CHECK(u[2].name == "pa" && u[3].name == "pb", "then the two parallel ports");
        CHECK(u[0].kind == UnitKind::Serial && u[2].kind == UnitKind::Serial,
              "all four are CONNECTable byte-stream lines");
        CHECK(!u[2].consoleCapable && !u[3].consoleCapable,
              "the parallel ports are not eligible to be the console");

        std::string err;
        CHECK(!g.b.connect("tty", "null", err), "there is no unit but a/b/pa/pb");
        CHECK(err.find("tty") != std::string::npos, "and the error names the bad unit");
        CHECK(err.find("pa") != std::string::npos, "and lists the parallel ports too");
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
        CHECK(!g.decodes(0x06), "and nothing past the serial block and the parallel one (4-5)");
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

    SECTION("IO-4 -- the parallel section: a 2-port block, default at 4-5");
    {
        // Switch S4 default -> Parallel A at 4, Parallel B at 5, just past the serial block.
        Rig g;
        CHECK(g.decodes(0x04), "answers Parallel A");
        CHECK(g.decodes(0x05), "answers Parallel B");
        CHECK(!g.decodes(0x06), "and nothing past the parallel block");
        CHECK(g.get("", "par_port") == -1, "par_port is a BOARD property, not a unit one");

        std::string err;
        long long   base = -1;
        for (auto& pr : g.b.properties())
            if (pr.name == "par_port") base = pr.get().i();
        CHECK(base == 0x04, "and it reads 0x04 by default");
    }

    SECTION("IO-4 -- switch S4 is a 2-port block: the base must be a multiple of 2");
    {
        Rig g;
        g.setBoard("par_port", "20");  // hex 0x20, a 2-boundary
        CHECK(g.decodes(0x20) && g.decodes(0x21), "the parallel block relocated to 0x20-0x21");
        CHECK(!g.decodes(0x04), "and it left the old block");
        g.setBoard("par_port", "21", false);  // odd -- refused
        CHECK(g.decodes(0x20), "the refused SET left the base where it was");
        CHECK(g.decodes(0x00) && g.decodes(0x03), "the serial block is untouched");
    }

    SECTION("IO-4 -- parallel input: the strobe latches a byte and sets the service request");
    {
        // A byte the far end sends is the 8212's external strobe: pump() latches it and sets the
        // service-request flip-flop. Reading Parallel A (port 4) hands the byte over and
        // acknowledges -- the flip-flop clears, ready for the next strobe.
        Rig             g;
        ScriptedStream* pa = g.connectParallel("pa");
        CHECK(pa != nullptr, "pa is a ScriptedStream we can drive");

        pa->feed("K");
        g.pump();  // the strobe: latch the byte, set the service request
        CHECK(g.in(0x04) == 'K', "reading Parallel A yields the strobed-in byte");

        // A second byte only latches after the first is read (the latch is one deep).
        pa->feed("LM");
        g.pump();
        CHECK(g.in(0x04) == 'L', "the next strobe latched the next byte");
        g.pump();
        CHECK(g.in(0x04) == 'M', "and the one after that");
    }

    SECTION("IO-4 -- parallel output: a write latches out on the line");
    {
        Rig             g;
        ScriptedStream* pb = g.connectParallel("pb");  // Parallel B at port 5
        g.out(0x05, 'Z');
        CHECK(pb->out() == "Z", "a write to Parallel B goes out its line");
        g.out(0x05, '9');
        CHECK(pb->out() == "Z9", "and the next latches out after it");
    }

    SECTION("IO-4 -- the status/data console idiom: a DAV flag strapped across ports (§3.2.2)");
    {
        // The manual's 8080-console recipe: Parallel A = status port, Parallel B = data port,
        // with B's service request jumpered onto a bit of A's read, active-low ("D0 going low").
        // So the guest polls A for the flag and reads the byte from B.
        Rig             g;
        g.connectParallel("pa");
        ScriptedStream* pb = g.connectParallel("pb");
        g.set("pa", "dav_bit", "0");          // B's flag lands on D0 of A's read
        g.set("pa", "dav_source", "sibling");  // ...sourced from the DATA port (B)
        g.set("pa", "dav_active_low", "true"); // D0 low = a byte is waiting

        uint8_t s = g.in(0x04);
        CHECK((s & 0x01) != 0, "quiet: no byte waiting on B, so D0 reads 1 (active-low idle)");

        pb->feed("!");
        g.pump();  // strobe a byte into B; its service request sets
        s = g.in(0x04);  // poll the STATUS port -- this must NOT consume B's byte
        CHECK((s & 0x01) == 0, "a byte on B drives its flag -> A's D0 reads 0");

        CHECK(g.in(0x05) == '!', "the DATA port still holds the byte (polling A did not eat it)");
        s = g.in(0x04);
        CHECK((s & 0x01) != 0, "and reading B acknowledged it -> A's D0 back to 1");
    }

    SECTION("IO-4 -- the two sections are mutually exclusive where they overlap");
    {
        // Park the parallel block ON TOP of the serial block: Parallel A/B at 2-3, over Serial
        // B at 2-3. The reference: neither section responds in the contended ports (a deliberate
        // mutual-exclusion, not a bus fight). Serial A at 0-1 is untouched.
        Rig g;
        g.setBoard("par_port", "02");
        CHECK(g.decodes(0x00) && g.decodes(0x01), "Serial A (0-1) still answers");
        CHECK(!g.decodes(0x02), "port 2 is claimed by both -> dead on both");
        CHECK(!g.decodes(0x03), "port 3 too");

        // With those two ports dead, a write to what WAS Serial B's data port goes nowhere and a
        // strobe on the parallel side is not readable either -- proven by the write not landing.
        ScriptedStream* pa = g.connectParallel("pa");  // pa now at port 2
        g.out(0x02, 'X');
        CHECK(pa->out().empty(), "a write to the contended port reaches neither section");
        CHECK(g.in(0x02) == 0xFF, "and a read floats -- nobody drives the bus");
    }

    SECTION("IO-4 -- parallel connectStream + disconnect + reset clears the latches");
    {
        Clock    clk;
        Io4Board b;
        b.attachClock(&clk);
        auto            s   = std::make_unique<ScriptedStream>();
        ScriptedStream* raw = s.get();
        std::string     err;
        CHECK(b.connectStream("pa", std::move(s), err), "connectStream binds parallel pa");
        CHECK(b.unitStream("pa") == raw, "and pa's line is the stream we handed in");

        raw->feed("Q");
        b.pump();
        BusCycle rc;
        rc.type = Cycle::IoRead;
        rc.addr = 0x04;
        CHECK(b.read(rc) == 'Q', "a strobed byte on pa reaches the guest");

        // Feed another, latch it, then RESET before reading: the 8212 latch and the service
        // request clear (the line stays connected).
        raw->feed("R");
        b.pump();
        b.reset(Reset::PowerOn);
        CHECK(b.read(rc) == 0x00, "reset cleared the input latch");
        CHECK(b.unitStream("pa") == raw, "and the line is still connected");

        CHECK(b.disconnect("pa", err), "pa disconnects");
        CHECK(!b.connectStream("zz", std::make_unique<NullStream>(), err),
              "connectStream to a missing unit fails");
        CHECK(err.find("pa") != std::string::npos, "and the error names the parallel ports");
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

    // ---- Interrupts: header W4 (§3.3). No software enable -- the strap IS the enable. ----

    SECTION("IO-4 -- interrupts: W4 unstrapped (the default) raises nothing");
    {
        // A stock io4 ships with W4 bare, so a live source pulls no wire and the monitor boots
        // polled. Make the sources genuinely active first, so the negative is not vacuous.
        Rig             g;
        ScriptedStream* pa = g.connectParallel("pa");
        g.a->feed("K");
        pa->feed("Z");
        g.pump();
        CHECK((g.in(0x00) & 0x01) == 0, "a character IS waiting on Serial A (DAV low, altair-rev1)");
        CHECK(!g.b.assertsInt(), "...but with W4 unstrapped, pin 73 stays up");
        CHECK(g.b.assertsVi() == 0, "...and no VI line is pulled");
    }

    SECTION("IO-4 -- interrupts: Serial A receive to pin 73, then a VI line, and the ack");
    {
        Rig g;
        g.set("a", "rx_int", "int");
        CHECK(!g.b.assertsInt(), "a quiet line asks for nothing");
        g.a->feed("K");
        g.pump();
        CHECK(g.b.assertsInt(), "a character arrived (DAV): the int strap pulls pin 73");
        CHECK(g.b.assertsVi() == 0, "and pin 73 is not a VI line");
        g.in(0x01);  // read A's data port -- clears DAV
        CHECK(!g.b.assertsInt(), "reading the character drops the request");

        g.set("a", "rx_int", "vi1");  // the canonical Serial A receive line
        g.a->feed("M");
        g.pump();
        // A SECOND byte on the same line is baud-paced (the first landed at once): let the
        // receiver's own deadline clock it in before asserting on DAV.
        for (int i = 0; i < 100 && !g.b.assertsVi(); ++i) g.clk.advance(1000);
        CHECK(!g.b.assertsInt(), "a VI strap does not touch pin 73");
        CHECK(g.b.assertsVi() == viBit(IrqJumper::Vi1), "it pulls VI1 -- and only VI1");
        g.in(0x01);
        CHECK(g.b.assertsVi() == 0, "and reading the character drops VI1");
    }

    SECTION("IO-4 -- interrupts: the transmit interrupt is a LEVEL, held while TBMT is empty");
    {
        // Unlike the 88-SIO, this card has no software interrupt enable: a strapped TX source is
        // asserted whenever the transmit buffer is empty, which at idle is always. That is the
        // hardware, and it is on the operator not to strap a TX interrupt they will not service.
        Rig g;
        g.set("a", "tx_int", "vi2");
        CHECK(g.b.assertsVi() == viBit(IrqJumper::Vi2),
              "an idle transmitter holds TBMT -- the strap pulls VI2 with nothing to gate it");
        g.out(0x01, 'X');  // send a character: TBMT falls while it drains
        CHECK((g.b.assertsVi() & viBit(IrqJumper::Vi2)) == 0,
              "sending a character drops it -- the transmitter is busy");
        for (int i = 0; i < 100; ++i) g.clk.advance(1000);  // let the character leave
        CHECK((g.b.assertsVi() & viBit(IrqJumper::Vi2)) != 0,
              "and it rises again the moment the buffer empties");
    }

    SECTION("IO-4 -- interrupts: a parallel input raises even when unaddressed, and the read acks");
    {
        Rig             g;
        ScriptedStream* pa = g.connectParallel("pa");
        g.set("pa", "int", "vi6");  // the canonical Parallel A line
        pa->feed("Z");
        g.pump();  // the strobe sets the service request WITHOUT the port being addressed
        CHECK(g.b.assertsVi() == viBit(IrqJumper::Vi6), "the latched byte pulls VI6, unaddressed");
        CHECK(!g.b.assertsInt(), "and not pin 73");
        g.in(0x04);  // read Parallel A -- acknowledge
        CHECK(g.b.assertsVi() == 0, "reading the port acknowledges: VI6 drops");

        g.set("pa", "int", "int");  // and now to pin 73
        pa->feed("Y");
        g.pump();
        CHECK(g.b.assertsInt(), "a fresh strobe on the int strap pulls pin 73");
        g.in(0x04);
        CHECK(!g.b.assertsInt(), "and the read drops it");
    }

    SECTION("IO-4 -- interrupts: independent straps OR into the VI bitmask");
    {
        // The card can be pulling several lines at once -- which is why assertsVi() is a mask.
        Rig             g;
        ScriptedStream* pb = g.connectParallel("pb");
        g.set("a", "rx_int", "vi1");  // Serial A receive
        g.set("pb", "int", "vi5");    // Parallel B input
        g.a->feed("K");
        pb->feed("Z");
        g.pump();
        CHECK(g.b.assertsVi() == (uint8_t)(viBit(IrqJumper::Vi1) | viBit(IrqJumper::Vi5)),
              "both sources active at once -> the card pulls VI1 AND VI5");
    }

    SECTION("IO-4 -- interrupts: a card with no crystal drives nothing");
    {
        Io4Board    b;  // never attached to a clock -- the chips are not running
        std::string err;
        setUnitProperty(b, "a", "rx_int", "int", err);
        setUnitProperty(b, "pa", "int", "vi6", err);
        CHECK(!b.assertsInt(), "no clock: pin 73 stays up regardless of the straps");
        CHECK(b.assertsVi() == 0, "and no VI line is pulled");
    }

    SECTION("IO-4 -- interrupts END TO END: Serial A receive on VI2 vectors to RST 2");
    {
        // The definitive proof: the strap reaches the BUS. Serial A on VI2 -- a vector (0xD7)
        // that only a card claiming the IntAck cycle can produce; a floating bus reads 0xFF.
        IrqRig      g;
        std::string err;
        CHECK(setUnitProperty(*g.io, "a", "rx_int", "vi2", err), "Serial A receive -> VI2");
        g.ctl(0xC0);  // the 88-VI structure enabled
        CHECK(!g.m.bus.intPending(), "no character yet, so nothing is asking");

        g.a->feed("A");
        g.io->pump();
        g.advance();  // the receiver clocks the byte in on the card's own deadline
        CHECK(g.m.bus.intPending(), "DAV pulls VI2; the 88-VI prioritizes it and pulls pin 73");
        CHECK(g.vi->winner() == 2, "VI2 is the line being pulled");
        CHECK(g.intAck() == 0xD7, "-> RST 2 -> 0x0010, a vector only a driven bus can produce");

        g.m.bus.ioRead(0x01);  // read Serial A's data port -- clears DAV
        CHECK(!g.m.bus.intPending(), "the character was read: VI2 falls and pin 73 with it");
    }

    SECTION("IO-4 -- interrupts END TO END: a parallel strobe on VI6 vectors to RST 6");
    {
        IrqRig      g;
        std::string err;
        CHECK(setUnitProperty(*g.io, "pa", "int", "vi6", err), "Parallel A input -> VI6");
        g.ctl(0xC0);
        CHECK(!g.m.bus.intPending(), "nothing latched yet");

        g.pa->feed("Z");
        g.io->pump();  // the strobe sets the service request -- pa is never addressed here
        CHECK(g.m.bus.intPending(), "the service request pulls VI6; the 88-VI pulls pin 73");
        CHECK(g.vi->winner() == 6, "VI6 is the line being pulled");
        CHECK(g.intAck() == rstOpcode(6), "-> RST 6 -> 0x0030");

        g.m.bus.ioRead(0x04);  // read Parallel A -- acknowledge, clearing the service request
        CHECK(!g.m.bus.intPending(), "the byte was read: VI6 falls and pin 73 with it");
    }
}
