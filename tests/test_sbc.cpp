#include "test.h"

#include "boards/s100-memory.h"
#include "boards/sd-sbc.h"
#include "boards/sd-vdb8024.h"
#include "chips/intel8251.h"
#include "core/clock.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/display_null.h"
#include "host/stream.h"

#include <filesystem>
#include <fstream>
#include <memory>

using namespace altair;

namespace {

// ---- STATUS byte bits, by the datasheet's names (reference/Intel 8251 USART.md) ----
constexpr uint8_t kTxRDY   = 0x01;
constexpr uint8_t kRxRDY   = 0x02;
constexpr uint8_t kTxEMPTY = 0x04;
constexpr uint8_t kDSR     = 0x80;

// A bare 8251 on a Clock, driving a scripted terminal. No S-100 around it: this is
// the CHIP, exercised directly, the way the auto-baud line model has to be pinned.
struct ChipRig {
    Clock          clk;
    Intel8251      u{"tty"};
    ScriptedStream* tty = nullptr;

    ChipRig() {
        clk.setHz(4000000);  // the SBC-200's 4 MHz, so bit = 4e6/9600 = ~416 T-states
        auto s = std::make_unique<ScriptedStream>();
        tty    = s.get();
        u.connect(std::move(s));
        u.baud   = 9600;
        u.dsrSrc = DsrSource::FollowRxD;
        u.powerOn(clk);
    }

    // Program the chip the way MSMONR21 does out of reset.
    void program() {
        u.writeControl(0x4E, clk);  // MODE: async 8N1, x16
        u.writeControl(0x37, clk);  // COMMAND: TxEN|DTR|RxE|ErrReset|RTS
    }

    uint8_t status() { return u.readStatus(clk); }
    void     advance(uint64_t dt) { clk.advance(dt); }
};

// bit period and whole-character time at 9600/4MHz, for readable assertions.
constexpr uint64_t kBit  = 4000000 / 9600;  // ~416
constexpr uint64_t kChar = kBit * 10;       // 8N1 = 10 bits

// An SBC over a plain 64K RAM board that stands down for PHANTOM* on reads -- the layout
// the onboard memory is made for. Set jumpers and fill sockets, then power().
struct MemRig {
    Machine      m;
    std::string  err;
    SbcBoard*    sbc = nullptr;
    MemoryBoard* mem = nullptr;

    MemRig() {
        m.bus.setVerify(true);
        sbc = dynamic_cast<SbcBoard*>(m.add("sbc", "sbc0", err));
        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);
        setProperty(*mem, "honors_phantom", "read", err);
    }
    bool set(const char* name, const char* v) { return setProperty(*sbc, name, v, err); }
    bool socket(const char* at, const std::string& mount) {
        return sbc->loadSubUnit("socket", {{"at", at}, {"mount", mount}}, err);
    }
    bool logSays(const std::string& what) {
        for (const auto& l : sbc->drainLog())
            if (l.find(what) != std::string::npos) return true;
        return false;
    }
};

} // namespace

void test_sbc() {
    SECTION("SBC 8251 -- the write-target state machine (mode, then commands)");
    {
        ChipRig g;
        // Out of powerOn the chip expects a MODE word. 0x4E is 8N1.
        g.u.writeControl(0x4E, g.clk);
        CHECK(g.u.params().dataBits == 8, "the first control write is the MODE word (8 data bits)");
        CHECK(g.u.params().stopBits == 1, "one stop bit");
        CHECK(g.u.params().parity == LineParity::None, "no parity");

        // 0x37 is a COMMAND, not another mode: if it were taken as a mode it would set
        // 6 data bits (0x37 D3:D2 = 01). It does not.
        g.u.writeControl(0x37, g.clk);
        CHECK(g.u.params().dataBits == 8, "0x37 is a COMMAND -- the frame is unchanged (still 8)");

        // Internal-reset (command D6) rewinds to expecting a MODE. The next control
        // write must be taken as a mode again: 0x4A programs 7 data bits.
        g.u.writeControl(0x40, g.clk);  // COMMAND: internal reset
        g.u.writeControl(0x4A, g.clk);  // -> MODE, 7 data bits
        CHECK(g.u.params().dataBits == 7,
              "after internal-reset the next control write is a MODE again (7 data bits)");
    }

    SECTION("SBC 8251 -- status polarity: TxRDY=D0, RxRDY=D1");
    {
        ChipRig g;
        g.program();
        uint8_t s = g.status();
        CHECK((s & kTxRDY) != 0, "an idle transmitter is ready: D0 set");
        CHECK((s & kRxRDY) == 0, "nobody typed: D1 clear");

        g.tty->feed("A");
        g.u.pump();
        g.status();               // the frame begins on this poll (RxD leaves the stream)
        g.advance(kChar + kBit);  // ...and one character time later it has finished arriving
        s = g.status();
        CHECK((s & kRxRDY) != 0, "a character finished arriving: D1 set");
        CHECK(g.u.readData(g.clk) == 'A', "and the data port yields it");
        CHECK((g.status() & kRxRDY) == 0, "reading the data clears RxRDY");
    }

    SECTION("SBC 8251 -- TxRDY is a DEADLINE, not a flag");
    {
        ChipRig g;
        g.program();
        CHECK((g.status() & kTxRDY) != 0, "idle: ready");

        // DOUBLE-buffered (#899): a byte to an idle transmitter goes straight to the shift
        // register, so TxRDY stays set and only TxEMPTY drops. A second byte fills the holding
        // register and drops TxRDY until the first has left.
        g.u.writeData('X', g.clk);
        CHECK((g.status() & kTxRDY) != 0, "one byte: straight to the shift register -- still ready");
        CHECK((g.status() & kTxEMPTY) == 0, "but TxEMPTY (D2) drops: the shift register is busy");
        g.u.writeData('Y', g.clk);
        CHECK((g.status() & kTxRDY) == 0, "a second byte while the first shifts: TxRDY clears -- BUSY");
        CHECK(g.tty->out() == "XY", "and both characters really are on the line");

        g.advance(kChar - kBit);
        CHECK((g.status() & kTxRDY) == 0, "a bit time before X has left: still busy");
        g.advance(2 * kBit);
        CHECK((g.status() & kTxRDY) != 0, "X has left: Y moves to the shift register, TxRDY set");
        CHECK((g.status() & kTxEMPTY) == 0, "and TxEMPTY still clear while Y shifts");

        g.advance(kChar);
        CHECK((g.status() & kTxEMPTY) != 0, "Y has left too: TxEMPTY set, nothing left to send");
    }

    SECTION("SBC 8251 -- DSR follows the RxD line: the auto-baud model");
    {
        // THE HEART OF PHASE 1. The board straps RxD to /DSR, so status bit 7 mirrors
        // the incoming serial line bit by bit. A CR (0x0D, bit0 = 1) has a one-bit-wide
        // start pulse; the monitor times exactly that to detect baud.
        ChipRig g;
        g.program();

        g.tty->feed("\r");   // a carriage return: 0x0D
        g.u.pump();

        // The frame starts on the first poll that sees the byte. At elapsed 0 the line
        // is at the START bit (space) -> DSR asserted -> D7 = 1.
        CHECK((g.status() & kDSR) != 0, "at the start bit, DSR is asserted (D7 = 1)");

        // Still inside the start bit a moment later.
        g.advance(kBit / 2);
        CHECK((g.status() & kDSR) != 0, "mid start bit, still asserted");

        // Past one bit period we are on data bit 0, which for CR is 1 (mark) -> D7 = 0.
        // So the DSR=1 run the monitor measures is exactly ONE bit period.
        g.advance(kBit);
        CHECK((g.status() & kDSR) == 0, "one bit later (CR data bit 0 = 1, mark), DSR drops");

        // The rest of the frame arrives; RxRDY rises and the CR is readable.
        g.advance(kChar);
        CHECK((g.status() & kRxRDY) != 0, "the frame completed: RxRDY set");
        CHECK(g.u.readData(g.clk) == 0x0D, "and the byte is the CR");
    }

    SECTION("SBC 8251 -- DSR run widens with a longer low span (LSB-first proof)");
    {
        // 0x02 = 00000010: bit0 = 0, bit1 = 1. So the low run is start + bit0 = TWO bit
        // periods, then bit1 (mark) drops it. This proves the line is read LSB-first and
        // that D7 = NOT(data bit), not a fixed one-bit pulse.
        ChipRig g;
        g.program();
        g.tty->feed("\x02");
        g.u.pump();

        CHECK((g.status() & kDSR) != 0, "start bit: DSR asserted");
        g.advance(kBit + kBit / 2);   // into bit0 (= 0, space)
        CHECK((g.status() & kDSR) != 0, "data bit 0 is 0 (space): still asserted -- TWO periods");
        g.advance(kBit);              // into bit1 (= 1, mark)
        CHECK((g.status() & kDSR) == 0, "data bit 1 is 1 (mark): DSR drops");
    }

    SECTION("SBC 8251 -- a generic 8251 (dsr=inactive) does NOT auto-baud");
    {
        // The DSR strap is the CARD's, not the chip's. With it inactive the D7 bit is a
        // constant 0 no matter what arrives, so a plain 8251 board never auto-bauds.
        ChipRig g;
        g.u.dsrSrc = DsrSource::Inactive;
        g.program();
        g.tty->feed("\r");
        g.u.pump();
        CHECK((g.status() & kDSR) == 0, "start bit: D7 stays 0");
        g.advance(kBit / 2);
        CHECK((g.status() & kDSR) == 0, "...and it never asserts");
    }

    SECTION("SBC 8251 -- snapshot mid-frame keeps the line model");
    {
        ChipRig g;
        g.program();
        g.tty->feed("\r");
        g.u.pump();
        (void)g.status();          // start the frame
        g.advance(kBit / 4);       // partway into the start bit

        StateWriter w;
        g.u.serialize(w);

        // A fresh chip on its OWN clock, wound to the same T-state, restores the frame.
        Clock clk2;
        clk2.setHz(4000000);
        clk2.advance(g.clk.now());
        Intel8251 u2{"tty"};
        u2.baud   = 9600;
        u2.dsrSrc = DsrSource::FollowRxD;
        u2.connect(std::make_unique<NullStream>());
        StateReader r(w.data());
        u2.deserialize(r);

        CHECK((u2.statusByte(clk2) & kDSR) != 0, "the restored chip is still in the start bit");
        clk2.advance(kChar);
        u2.poll(clk2);
        CHECK(u2.rxReady(), "and the frame completes after restore");
        CHECK(u2.readData(clk2) == 0x0D, "with the right byte");
    }

    // ---- the board, on a real bus ----

    SECTION("SBC board -- data at 7C, status/command at 7D (the orientation)");
    {
        Machine m;
        std::string err;
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);

        auto* sbc = dynamic_cast<SbcBoard*>(m.add("sbc", "ser0", err));
        CHECK(sbc != nullptr, "the 'sbc' board type is registered");
        auto s = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        sbc->usart().connect(std::move(s));

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();

        // The card is one 8-port block, 78-7F on the etch: CTC 78-7B, 8251 7C/7D,
        // parallel 7E/7F. It decodes all eight and nothing outside them.
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x7C;
        CHECK(sbc->decodes(c), "decodes the data port (7C)");
        c.addr = 0x7D;
        CHECK(sbc->decodes(c), "decodes the status/command port (7D)");
        c.addr = 0x78;
        CHECK(sbc->decodes(c), "decodes the CTC block (78)");
        c.addr = 0x7E;
        CHECK(sbc->decodes(c), "decodes the parallel data port (7E)");
        c.addr = 0x7F;
        CHECK(sbc->decodes(c), "decodes the parallel handshake / mem-switch port (7F)");
        c.addr = 0x77;
        CHECK(!sbc->decodes(c), "does NOT decode below the block (77)");
        c.addr = 0x80;
        CHECK(!sbc->decodes(c), "does NOT decode above the block (80)");

        // The RxD->/DSR jumper is a BOARD property (default on), and it drives the chip's
        // strap -- the auto-baud is a fact about this card, not the generic 8251.
        CHECK(sbc->usart().dsrSrc == DsrSource::FollowRxD, "the jumper defaults ON (etch default)");
        CHECK(setProperty(*sbc, "rxd2dsr", "off", err), "and it is a board parameter");
        CHECK(sbc->usart().dsrSrc == DsrSource::Inactive, "turning it off unstrapped the chip");
        CHECK(setProperty(*sbc, "rxd2dsr", "on", err) &&
                  sbc->usart().dsrSrc == DsrSource::FollowRxD, "and back on again");

        // Writing 7D is the CONTROL port: a MODE word lands there. 0x4A = 7 data bits.
        m.bus.ioWrite(0x7D, 0x4A);
        CHECK(sbc->usart().params().dataBits == 7, "OUT 7D wrote the MODE word (control port)");

        // ...and reading 7C is the DATA port, not the status port. Program+receive.
        m.bus.ioWrite(0x7D, 0x4E);  // back to 8N1 mode
        m.bus.ioWrite(0x7D, 0x37);  // command: RxE on
        tty->feed("Q");
        sbc->pump();
        m.clock.advance(kChar + kBit);
        CHECK(m.bus.ioRead(0x7C) == 'Q', "IN 7C reads the DATA port");
    }

    SECTION("SBC board -- a Z80 auto-baud loop reaches its sentinel (end to end)");
    {
        // ACCEPTANCE. This is the monitor's auto-baud idiom in miniature, on a real Z80
        // over the real bus: program the 8251, spin on DSR (bit 7) until it asserts
        // (the start bit), then spin until it drops, then write a sentinel and HALT. If
        // the DSR line model did not pulse, WAIT0 would loop forever and the sentinel
        // would never be written.
        Machine m;
        std::string err;
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);

        auto* sbc = dynamic_cast<SbcBoard*>(m.add("sbc", "ser0", err));
        auto s = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        sbc->usart().connect(std::move(s));

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();

        uint16_t a = 0;
        for (uint8_t b : {
            0x3E, 0x4E,        // 0000  LD A,4E
            0xD3, 0x7D,        // 0002  OUT (7D),A   ; mode
            0x3E, 0x37,        // 0004  LD A,37
            0xD3, 0x7D,        // 0006  OUT (7D),A   ; command (RxE on)
            0xDB, 0x7D,        // 0008  WAIT0: IN A,(7D)
            0xE6, 0x80,        // 000A  AND 80h
            0xCA, 0x08, 0x00,  // 000C  JP Z,0008    ; spin while DSR=0
            0xDB, 0x7D,        // 000F  WAIT1: IN A,(7D)
            0xE6, 0x80,        // 0011  AND 80h
            0xC2, 0x0F, 0x00,  // 0013  JP NZ,000F   ; spin while DSR=1
            0x3E, 0xAA,        // 0016  LD A,AA
            0x32, 0x50, 0x00,  // 0018  LD (0050),A  ; the sentinel
            0x76,              // 001B  HALT
        }) m.bus.memWrite(a++, b);
        m.cpu()->setPc(0x0000);

        // The user presses Enter. The byte is on the line before the loop reads DSR.
        tty->feed("\r");
        sbc->pump();

        for (int i = 0; i < 20000; ++i) {
            StepResult sr = m.master()->step(m.bus);
            m.clock.advance(sr.tStates);
            if (m.bus.memRead(0x0050) == 0xAA) break;
        }
        CHECK(m.bus.memRead(0x0050) == 0xAA,
              "the auto-baud loop saw DSR pulse 0->1->0 and reached its sentinel");
    }

    SECTION("SBC board -- the CTC keyboard interrupt: RxRDY raises a mode-2 vector 0x82");
    {
        // SD CP/M's CONIO arms CTC channel 1 for a mode-2 vectored interrupt whose
        // trigger is the 8251's RxRDY, then services it by reading the data port. This
        // pins the whole path without booting CP/M: arm the CTC the way CONIO does, put
        // a byte on the line, and watch /INT rise, the acknowledge hand back 0x82, and
        // the data read clear it again.
        Machine     m;
        std::string err;
        m.bus.setVerify(true);  // re-derive pin 73 every cycle -- catch a missing intChanged()

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);

        auto* sbc = dynamic_cast<SbcBoard*>(m.add("sbc", "ser0", err));
        auto  s   = std::make_unique<ScriptedStream>();
        ScriptedStream* tty = s.get();
        sbc->usart().connect(std::move(s));

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();

        // Program the 8251 (mode 8N1, command RxE) exactly as the monitor does.
        m.bus.ioWrite(0x7D, 0x4E);
        m.bus.ioWrite(0x7D, 0x37);

        // A byte arrives BEFORE the CTC is armed: /INT must stay low (nothing armed yet).
        tty->feed("A");
        sbc->pump();
        m.clock.advance(kChar + kBit);
        CHECK(sbc->usart().rxReady(), "the byte arrived (RxRDY is up)");
        CHECK(!m.bus.intPending(), "but with ch1 unarmed, no interrupt is asserted");

        // CONIO's arming sequence: vector base 0x80 to ch0 (D0=0), then ch1 control word
        // 0xD7 (interrupt-enable) + time constant 1.
        m.bus.ioWrite(0x78, 0x80);  // CTC ch0: interrupt vector base
        m.bus.ioWrite(0x79, 0xD7);  // CTC ch1: counter, interrupt-enable (D7), TC follows
        m.bus.ioWrite(0x79, 0x01);  // CTC ch1: time constant

        CHECK(m.bus.intPending(), "now ch1 is armed and RxRDY is up: /INT is asserted");
        CHECK(m.bus.intAck() == 0x82, "the IntAck hands back vector 0x82 (base 0x80 | ch1<<1)");
        CHECK(m.bus.ioRead(0x7C) == 'A', "the ISR reads the data port...");
        CHECK(!m.bus.intPending(), "...which clears RxRDY, so /INT drops -- no EOI needed");

        // A second byte re-raises it (the level tracks RxRDY, arming persists).
        tty->feed("B");
        sbc->pump();
        m.clock.advance(kChar + kBit);
        CHECK(m.bus.intPending(), "the next character raises /INT again");
        CHECK(m.bus.intAck() == 0x82, "...with the same vector");
    }

    SECTION("SBC board -- the onboard PROM shadow and the OUT 7F memory switch-out");
    {
        // The authentic single-board layout: the SBC owns its boot PROM (here MSMONR21 at
        // E000) over a plain 64K RAM card. The PROM shadows RAM for reads; a write falls
        // through; and OUT 7F bit 1 -- CP/M's cold-boot PROM switch-out -- drops it out of
        // the map so the RAM shows through. A reset puts it back.
        Machine     m;
        std::string err;
        m.bus.setVerify(true);

        auto* sbc = dynamic_cast<SbcBoard*>(m.add("sbc", "ser0", err));
        CHECK(sbc->loadSubUnit("socket", {{"at", "E000"}, {"mount", "builtin:msmonr21"}}, err),
              "a socket takes the MSMONR21 monitor PROM at E000");

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);
        CHECK(setProperty(*mem, "honors_phantom", "read", err),
              "the RAM under the shadow answers writes but stands down on reads");

        m.power();

        CHECK(m.bus.memRead(0xE000) == 0xC3, "E000 reads the PROM (JP E003), not RAM");
        CHECK(m.bus.memRead(0xE800) == 0x00, "the gap above the 2K ROM falls through to RAM");
        // The interrupt-table page is the onboard 1K RAM (slot 7, F800-FFFF), not PROM.
        m.bus.memWrite(0xFF80, 0x5A);
        CHECK(m.bus.memRead(0xFF80) == 0x5A, "the interrupt-table page is RAM, not PROM");

        // A write into the PROM window lands in the RAM beneath the shadow.
        m.bus.memWrite(0xE000, 0x99);
        CHECK(m.bus.memRead(0xE000) == 0xC3, "reads still come from the PROM...");
        CHECK(mem->storeAt(0xE000) == 0x99, "...but the write reached the RAM under it");

        // OUT 7F bit 1 = switch the onboard memory out -> RAM shows through.
        m.bus.ioWrite(0x7F, 0x03);
        CHECK(m.bus.memRead(0xE000) == 0x99, "OUT 7F,3 switched the PROM out: E000 is RAM now");

        // OUT 7F with bit 1 clear switches it back in.
        m.bus.ioWrite(0x7F, 0x00);
        CHECK(m.bus.memRead(0xE000) == 0xC3, "OUT 7F,0 switched the PROM back in");

        // A reset re-arms the onboard memory regardless of the latch's state.
        m.bus.ioWrite(0x7F, 0x03);
        CHECK(m.bus.memRead(0xE000) == 0x99, "switched out again");
        m.reset(Reset::Bus);
        CHECK(m.bus.memRead(0xE000) == 0xC3, "a reset switched the onboard PROM back in");
    }

    SECTION("SBC board -- the memory decode jumpers (X1, X3): where a socket may sit");
    {
        // The etch: 2K ROMs in bank 3, so the slots are C000, C800, ... F000 and the RAM
        // is the last one, F800.
        MemRig g;
        CHECK(g.socket("E000", "builtin:msmonr21"), "E000 is a socket (ROM 0, high)");
        CHECK(g.socket("F000", "builtin:msmonr21"), "F000 is a socket (ROM 2, high)");
        CHECK(!g.socket("F800", "builtin:msmonr21"), "F800 is the RAM's place: refused");
        CHECK(!g.socket("E400", "builtin:msmonr21"), "E400 is not a slot address: refused");
        CHECK(!g.socket("C000", "builtin:msmonr21"),
              "C000 is ROM 0 again (its low place), and ROM 0 is at E000: refused");

        // 4K ROMs in bank 0: the slots are 0000, 1000, ... 6000.
        MemRig lo;
        CHECK(lo.set("rom_size", "4K") && lo.set("bank", "0"), "4K ROMs, bank 0");
        CHECK(lo.socket("4000", "builtin:msmonr21"), "4000 is a socket there (ROM 0, high)");
        CHECK(!lo.socket("E000", "builtin:msmonr21"), "and E000 is not");

        // 4K ROMs have two banks. Bank 3 is not one of them.
        MemRig bad;
        bad.set("rom_size", "4K");
        bad.set("bank", "3");
        CHECK(!bad.socket("8000", "builtin:msmonr21"), "4K ROMs have no bank 3: refused");

        // A 2K ROM in a 1K socket: the socket holds the first 1K, and the board says so.
        MemRig cut;
        CHECK(cut.set("rom_size", "1K") && cut.set("bank", "7"), "1K ROMs, bank 7");
        CHECK(cut.socket("E000", "builtin:msmonr21"), "E000 is a socket there too");
        cut.m.power();
        CHECK(cut.m.bus.memRead(0xE000) == 0xC3, "the first 1K of the ROM is in the socket");
        uint8_t v = 0;
        CHECK(!cut.sbc->peek(0xE400, v), "the second 1K is not: E400 is another socket");
        CHECK(cut.m.bus.memRead(0xE400) == 0x00, "...so the RAM board answers there");
        CHECK(cut.logSays("outside the 1K socket at E000"), "and the log says bytes were cut");
    }

    SECTION("SBC board -- the onboard 1K static RAM (X3-15 to X3-16)");
    {
        MemRig g;  // no sockets: the RAM is jumpered in by default
        g.m.power();

        // A write goes to the onboard RAM and to the RAM board at that address.
        g.m.bus.memWrite(0xF800, 0x5A);
        CHECK(!g.m.bus.lastContended(), "one board takes the write on the bus");
        CHECK(g.m.bus.memRead(0xF800) == 0x5A, "F800 reads the byte back");
        CHECK(g.mem->storeAt(0xF800) == 0x5A, "the RAM board took the same write");
        CHECK(g.m.bus.memRead(0xFC00) == 0x5A, "the 1K repeats in its 2K slot: FC00 is F800");

        // Switched out, the RAM board answers; a write now does not reach the onboard RAM.
        g.m.bus.ioWrite(0x7F, 0x02);
        g.m.bus.memWrite(0xF800, 0x11);
        CHECK(g.m.bus.memRead(0xF800) == 0x11, "OUT 7F,2: F800 is the RAM board");
        g.m.bus.ioWrite(0x7F, 0x00);
        CHECK(g.m.bus.memRead(0xF800) == 0x5A, "OUT 7F,0: the onboard RAM is back, unchanged");

        g.m.bus.ioWrite(0x7F, 0x02);
        g.m.reset(Reset::Bus);
        CHECK(g.m.bus.memRead(0xF800) == 0x5A, "a reset switches it in and keeps the contents");
        CHECK(g.m.bus.drain().empty(), "no contention was reported");

        // The snapshot carries the 1024 bytes.
        StateWriter w;
        g.sbc->serialize(w);
        MemRig h;
        h.m.power();
        StateReader r(w.data());
        h.sbc->deserialize(r);
        CHECK(h.m.bus.memRead(0xF800) == 0x5A, "a restored board has the RAM contents");

        // ram = off: the jumper is out, and the slot belongs to the RAM board.
        MemRig off;
        CHECK(off.set("ram", "off"), "the RAM jumper comes out");
        off.m.power();
        uint8_t v = 0;
        CHECK(!off.sbc->peek(0xF800, v), "the board does not answer F800");
        off.m.bus.memWrite(0xF800, 0x77);
        CHECK(off.m.bus.memRead(0xF800) == 0x77, "the RAM board does");
    }

    SECTION("SBC board -- auto-start (X16, X17, X18): a reset reads the PROM at the start page");
    {
        MemRig g;
        g.socket("E000", "builtin:msmonr21");
        CHECK(!g.set("start", "E800"), "start is a 4K boundary: E800 is refused");
        CHECK(g.set("start", "E000"), "start = E000");
        g.m.power();

        // MSMONR21 opens with JP E00F, and E00F is its IN A,(7F).
        CHECK(g.m.bus.memRead(0x0000) == 0xC3 && g.m.bus.memRead(0x0001) == 0x0F &&
                  g.m.bus.memRead(0x0002) == 0xE0,
              "0000 reads the PROM's JP E00F");
        CHECK(g.m.bus.memRead(0x1234) == g.m.bus.memRead(0xE234),
              "every 4K page reads the start page: 1234 is E234");

        g.m.bus.memWrite(0x0000, 0x42);
        CHECK(g.mem->storeAt(0x0000) == 0x42, "a write is not moved: it reaches RAM at 0000");
        CHECK(g.m.bus.memRead(0x0000) == 0xC3, "and the read is still the PROM");

        // The snapshot carries the latch. Take it while armed.
        StateWriter w;
        g.sbc->serialize(w);

        (void)g.m.bus.ioRead(0x7F);
        CHECK(g.m.bus.memRead(0x0000) == 0x42, "IN 7F releases it: 0000 is RAM");
        CHECK(g.m.bus.memRead(0xE000) == 0xC3, "and the PROM is still at E000");

        StateReader r(w.data());
        g.sbc->deserialize(r);
        CHECK(g.m.bus.memRead(0x0000) == 0xC3, "a restored board is armed again");
        (void)g.m.bus.ioRead(0x7F);

        g.m.reset(Reset::Bus);
        CHECK(g.m.bus.memRead(0x0000) == 0xC3, "a RESET arms it again");

        // Switching the onboard memory out takes the PROM away, armed or not.
        g.m.bus.ioWrite(0x7F, 0x02);
        CHECK(g.m.bus.memRead(0x0000) == 0x42, "OUT 7F,2 while armed: 0000 is RAM");
        // The memory map is how the board is built: the switch does not change it.
        CHECK(g.sbc->memMap().size() == 2 &&
                  g.sbc->memMap()[1].note.find("repeated in its 2K slot") != std::string::npos,
              "the memory map still lists the PROM socket and the RAM in its 2K slot");

        (void)g.m.bus.ioRead(0x7F);
        g.m.power();
        CHECK(g.m.bus.memRead(0x0000) == 0xC3, "a POWER arms it again");
        CHECK(g.m.bus.drain().empty(), "no contention was reported");

        // start = 0000 (the default): no override.
        MemRig none;
        none.socket("E000", "builtin:msmonr21");
        none.m.power();
        CHECK(none.m.bus.memRead(0x0000) == 0x00, "start = 0000: 0000 is RAM after a reset");

        // start names a place with no PROM: nothing to read, and the board says so.
        MemRig empty;
        empty.socket("E000", "builtin:msmonr21");
        empty.set("start", "D000");
        empty.m.power();
        CHECK(empty.m.bus.memRead(0x0000) == 0x00, "no PROM at D000: 0000 is RAM");
        CHECK(empty.logSays("no PROM socket is there"), "and the log says so");
    }

    SECTION("SBC board -- auto-start to a PROM in a low bank (4K ROMs, bank 0, start = 4000)");
    {
        namespace fs = std::filesystem;
        const fs::path rom = fs::temp_directory_path() / "altairsim-sbc-autostart.bin";
        {
            std::ofstream f(rom, std::ios::binary);
            f.write("\xC3\x03\x40\xDB\x7F\x76", 6);  // JP 4003 / IN A,(7F) / HALT
        }
        MemRig g;
        CHECK(g.set("rom_size", "4K") && g.set("bank", "0"), "4K ROMs, bank 0");
        CHECK(g.socket("4000", rom.string()), "a PROM at 4000");
        CHECK(g.set("start", "4000"), "start = 4000");
        g.m.power();
        CHECK(g.m.bus.memRead(0x0000) == 0xC3 && g.m.bus.memRead(0x0002) == 0x40,
              "0000 reads the PROM's JP 4003");
        (void)g.m.bus.ioRead(0x7F);
        CHECK(g.m.bus.memRead(0x0000) == 0x00, "released: 0000 is RAM");
        CHECK(g.m.bus.memRead(0x4000) == 0xC3, "and the PROM is at 4000");
        std::error_code ec;
        fs::remove(rom, ec);
    }

    SECTION("SBC board -- auto-start with a Z80: POWER, and the monitor PROM runs from 0000");
    {
        MemRig g;
        g.socket("E000", "builtin:msmonr21");
        g.set("start", "E000");
        g.m.add("z80", "cpu0", g.err);
        g.m.power();
        CHECK(g.m.cpu()->pc() == 0x0000, "the Z80 starts at 0000, as it always does");

        StepResult sr = g.m.master()->step(g.m.bus);  // JP E00F, read at 0000
        g.m.clock.advance(sr.tStates);
        CHECK(g.m.cpu()->pc() == 0xE00F, "the PROM's JP put PC in the PROM");
        CHECK(g.m.bus.memRead(0x0000) == 0xC3, "the circuit is still armed");

        sr = g.m.master()->step(g.m.bus);             // IN A,(7F)
        g.m.clock.advance(sr.tStates);
        CHECK(g.m.cpu()->pc() == 0xE011, "IN A,(7F) ran");
        CHECK(g.m.bus.memRead(0x0000) == 0x00, "and released the circuit: 0000 is RAM");

        // RESET on the running machine goes back to the PROM, not to RAM at 0000.
        g.m.reset(Reset::Bus);
        sr = g.m.master()->step(g.m.bus);
        g.m.clock.advance(sr.tStates);
        CHECK(g.m.cpu()->pc() == 0xE00F, "a RESET runs the PROM again");
    }

    SECTION("SBC + VDB-8024 -- the video keyboard interrupt: a VDB key raises CTC vector 0x02");
    {
        // THE VIDEO CONSOLE'S KEYBOARD IS INTERRUPT-DRIVEN. The SD video build of SDOS (and
        // SD CP/M's video console) runs console input under Z80 mode-2 interrupts: the
        // VDB-8024's keyboard strobe is strapped to S-100 VI2, and the SBC-200's CTC turns
        // that into the mode-2 vector 0x02 the CBIOS ISR is hung on. Unlike the serial
        // console (the 0x82 case above), the trigger crosses TWO boards -- the VDB pulls the
        // VI line, the SBC's CTC vectors it. This pins that whole path without booting: strap
        // the VDB, latch a key, arm the CTC the way the video CONIO does, and watch /INT rise,
        // the acknowledge hand back 0x02, and the ISR's IN 01H clear it.
        Machine     m;
        NullDisplay disp;
        Vdb8024Board::setDisplay(&disp);
        std::string err;
        m.bus.setVerify(true);  // re-derive pin 73 every cycle -- catch a missing intChanged()

        auto* sbc = dynamic_cast<SbcBoard*>(m.add("sbc", "sbc0", err));
        CHECK(sbc != nullptr, "the SBC-200 (its CTC) is in the machine");

        auto* vdb = dynamic_cast<Vdb8024Board*>(m.add("vdb8024", "vdb0", err));
        CHECK(setProperty(*vdb, "interrupt", "vi2", err), "the VDB keyboard straps to VI2");
        CHECK(vdb->connect("keyboard", "scripted", err), "its keyboard binds to a scripted line");
        auto* kbd = dynamic_cast<ScriptedStream*>(vdb->unitStream("keyboard"));

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region rr;
        rr.kind = RegionKind::Ram;
        rr.at   = 0;
        rr.size = 0x10000;
        mem->addRegion(rr, err);
        setProperty(*mem, "fill", "zero", err);

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();

        // A key waits at the VDB (it pulls VI2), but the CTC is not armed yet: no interrupt.
        kbd->feed("D");
        vdb->pump();  // the keyboard is latched in pump(), never in a bus cycle
        CHECK((vdb->statusByte() & 0x02) != 0, "the VDB latched the key (status D1)");
        CHECK(!m.bus.intPending(), "but with CTC ch1 unarmed, VI2 alone raises no interrupt");

        // The video CONIO arming: vector base 0x00 to CTC ch0 (D0=0), then ch1 control word
        // 0xC7 (counter, interrupt-enable D7, time-constant follows) + the time constant.
        m.bus.ioWrite(0x78, 0x00);  // CTC ch0: interrupt vector base 0x00
        m.bus.ioWrite(0x79, 0xC7);  // CTC ch1: counter, interrupt-enable (D7), TC follows
        m.bus.ioWrite(0x79, 0x01);  // CTC ch1: time constant
        CHECK(m.bus.intPending(), "ch1 armed and a key waiting on VI2: /INT is asserted");
        CHECK(m.bus.intAck() == 0x02, "the IntAck hands back vector 0x02 (base 0x00 | ch1<<1)");
        CHECK(m.bus.ioRead(0x01) == 'D', "the ISR reads the VDB keyboard port (IN 01H)...");
        CHECK(!m.bus.intPending(), "...which drops the strobe, so VI2 falls and /INT with it");

        // A second key raises it again -- the level re-arms on its own, no EOI needed.
        kbd->feed("E");
        vdb->pump();
        CHECK(m.bus.intPending(), "the next keystroke raises /INT again");
        CHECK(m.bus.intAck() == 0x02, "...with the same vector");
        CHECK(m.bus.ioRead(0x01) == 'E', "...and the ISR reads the second byte");

        // Unstrapped (interrupt = none, the board default), the keyboard is POLLED: a key
        // still shows in status D1, but it pulls no VI line, so it never interrupts.
        CHECK(setProperty(*vdb, "interrupt", "none", err), "unstrap the keyboard interrupt");
        kbd->feed("F");
        vdb->pump();
        CHECK((vdb->statusByte() & 0x02) != 0, "the key is still there to be polled");
        CHECK(!m.bus.intPending(), "but polled, the VDB raises no interrupt (SDMONV21's world)");
    }
}
