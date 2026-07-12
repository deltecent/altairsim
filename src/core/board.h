#pragma once
//
// Board -- one physical card in the backplane (DESIGN.md 3, 4.1, 5).
//
// ONE BOARD IS ONE CARD. Not one chip, not one address range. A card carrying
// 48K of RAM and three PROM sockets is ONE board, because that is what you would
// pull out of the machine with your hand. Every decode question the bus can ask
// is answered here, by the card, using knowledge only the card has.

#include "core/bus.h"
#include "core/clock.h"
#include "core/value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

// The key/value pairs of one TOML sub-unit table, in file order.
using KeyValues = std::vector<std::pair<std::string, std::string>>;

// ---------------------------------------------------------------------------
// UNITS ARE NAMED AND TYPED (Patrick, 2026-07-11).
//
// ONE CARD IS NOT ONE KIND OF THING. A card may carry drives AND ROM sockets AND
// a serial port -- the Tarbell already carries a boot PROM and a disk controller
// -- and nothing in the bus model ever said otherwise.
//
// So a unit is a NAME, not an index. `MOUNT dj:drive0` and `CONNECT dj:tty`, not
// `MOUNT dj:0` and `MOUNT dj:4` with a numbering convention buried in the card's
// documentation. The kind is checked: mounting a disk image onto a serial port is
// an ERROR with a sentence explaining it, not undefined behaviour that half-works.
//
// The index scheme was not merely inconvenient -- it could not be made safe. With
// a flat integer namespace, `MOUNT dj:4` on a serial unit can only fail; it can
// never explain, because the board has nothing to distinguish 4-the-drive from
// 4-the-port.
// ---------------------------------------------------------------------------
enum class UnitKind {
    Disk,   // MOUNT / UNMOUNT a host image
    Rom,    // MOUNT / UNMOUNT an image into a socket -- a region on a memory card
    Serial, // CONNECT / DISCONNECT an endpoint
    Tape,   // MOUNT / UNMOUNT
    Cpu,    // a PROCESSOR on the card. Neither mounted nor connected: it is
            // SOLDERED ON. A card with an 8080 and an 8085 on it, switching
            // between them when the guest does an OUT, is a real product -- so
            // cores are units, exactly one active (DESIGN.md 3.0.1). This needs no
            // new bus concept at all: the card decodes the OUT, sets its own
            // latch, and reports a different active core, which is structurally
            // identical to a memory card switching banks. The bus arbitrates
            // nothing, here as everywhere.
};

const char* unitKindName(UnitKind k);

struct UnitDef {
    std::string name;  // "drive0", "rom0", "tty" -- the board's own word
    UnitKind kind = UnitKind::Disk;
    std::string state; // what is in it now: a path, or "(empty)"
};

// Can this kind of unit be MOUNTed (as opposed to CONNECTed)?
inline bool isMountable(UnitKind k) {
    return k == UnitKind::Disk || k == UnitKind::Rom || k == UnitKind::Tape;
}

// ---------------------------------------------------------------------------
// WHERE A BOARD'S INTERRUPT REQUEST IS SOLDERED (DESIGN.md 4.4).
//
// This describes a BUS STRAP, not a chip -- which is why it lives here and not
// beside some particular UART. Pin 73 and VI0-VI7 are the backplane's wires, and
// every interrupting card in the machine is strapped to one of them with the same
// vocabulary: `interrupt = none | int | vi0 .. vi7`.
//
// It lived in mits-2sio.h until the 88-SIO needed it too, and an 88-SIO that had
// to `#include "boards/mits-2sio.h"` to learn what pin 73 is called would be
// saying something false about the hardware.
// ---------------------------------------------------------------------------
enum class IrqJumper {
    None,  // the wire is not installed. The chip's own IRQ pin still does whatever
           // it does -- it has no idea what you did or did not solder to it, and a
           // status register that reports it must still report it.
    Int,   // straight to pINT (pin 73). No VI board present -> nobody claims the
           // IntAck cycle -> the bus floats 0xFF -> the 8080 executes RST 7.
           // That is not a fallback. That IS the Altair.
    Vi0, Vi1, Vi2, Vi3, Vi4, Vi5, Vi6, Vi7,
};

// The `interrupt` property, written once. Every interrupting board strap gets the
// same ten choices, the same spelling, and the same tab completion -- and a card
// with TWO straps (the 88-SIO has one for its input device and one for its output
// device, at independent VI priorities) gets them both for free.
Property irqJumperProperty(std::string name, std::string help, IrqJumper& j);

class Board {
public:
    virtual ~Board() = default;

    virtual std::string type() const = 0;
    std::string id;

    // ---- The bus interface. Four questions and two answers. ----

    // Do I pull PHANTOM* (pin 67) for this cycle? A board that shadows another
    // says yes. It is a SIGNAL, not a request to the bus to arbitrate.
    //
    // COMBINATIONAL AND PURE. The bus calls this several times per cycle (once to
    // resolve the signal, once more inside each board's decodes()), so it must
    // not latch anything. The clocked half is snoop(), below.
    //
    // PHANTOM* IS A LEVEL. A card that shadows another asserts it and HOLDS it --
    // like an interrupt -- until something releases it. It is not gated with the
    // read strobe, and the asserting card has no opinion about what a write should
    // do: the READ/WRITE DISTINCTION LIVES ON THE HONORING BOARD, which is why the
    // memory card's jumper is `honors_phantom = none | read | all` and not a bool.
    // `read` = off for reads, still answering writes, so the byte lands in the RAM
    // under the shadow. See the Tarbell (docs/boards/tarbell-sd.md).
    //
    // (This comment used to say the exact opposite, in bold, and it was wrong:
    // reasoned instead of sourced. Patrick read the schematic, 2026-07-12.)
    virtual bool assertsPhantom(const BusCycle&) const { return false; }

    // Do I drive the bus for this cycle? Everything board-specific lives behind
    // this one question: my address range, my ports, my bank, whether I honor
    // PHANTOM*, and whether the thing at this address is ROM (which never
    // answers a write).
    //
    // TWO CONTRACTS, BOTH LOAD-BEARING, because the bus CACHES this answer:
    //
    //   1. PURE. Same board state, same cycle -> same answer, no side effects.
    //      (assertsPhantom() too. The clocked half is snoop().)
    //
    //   2. PURE, again, and that is the only contract. Decode at ANY granularity
    //      you like -- see decodeIsPageUniform() below, which is how a card that
    //      decodes a low address line says so.
    //
    // AND IF YOUR DECODE CHANGES, SAY SO -- call decodeChanged(). A bank strap, a
    // PHANTOM* jumper, a chip pulled from a socket, going enabled/disabled. Forget
    // it and the bus's cached tables go stale, which is a lie told quietly. Run
    // with Bus::setVerifyDecode(true) and it stops being quiet.
    virtual bool decodes(const BusCycle&) const { return false; }

    // IS MY MEMORY DECODE THE SAME FOR EVERY ADDRESS IN A 256-BYTE PAGE?
    //
    // Nearly always yes: S-100 memory decoding comes off the HIGH address lines,
    // and a card selected at 1K or 4K granularity answers a whole page or none of
    // it. The bus caches the decode one entry per page on the strength of that.
    //
    // The Tarbell is the card that says NO, and it is not an edge case -- it is in
    // the boot path. Its PROM and its PHANTOM* are gated by **A5**, one address
    // line, so it decodes 0x0000-0x001F and NOT 0x0020-0x003F: two different
    // answers inside page 0. (0x0040 does not release it either -- bit 5 is clear.
    // It is a WIRE, not a threshold.)
    //
    // Say false and the bus PROBES every address of every page you might be in,
    // and any page where the answer is not uniform is served by the exact,
    // uncached two-pass path -- combinational PHANTOM* settle and all. You lose
    // nothing but the cache, and only on the pages you actually touch.
    virtual bool decodeIsPageUniform() const { return true; }

    // Do I WATCH cycles I do not answer? Almost no card does, so this is false and
    // the bus skips you entirely -- snoop() on every card on every cycle was N
    // virtual calls to reach a do-nothing default. Say true and you get every
    // cycle, exactly as before. The Tarbell says true: it releases PHANTOM* the
    // first time it sees a memory read with A5 high.
    virtual bool wantsSnoop() const { return false; }

    virtual uint8_t read(const BusCycle&) { return 0xFF; }
    virtual void write(const BusCycle&) {}

    // LOOK WITHOUT TOUCHING. Not a bus cycle: no strobe, no side effect, no snoop.
    //
    // A read() can CONSUME -- an IN from a UART's data port takes the byte and the
    // guest never sees it -- so DISASM and TRACE must never be built on one. They
    // would work perfectly on RAM and then quietly eat the console's input the
    // first time someone disassembled a page with a UART mapped into it.
    //
    // A board that cannot answer without side effects returns FALSE, and that is
    // an honest answer, not a failure: the byte on a real bus is only defined
    // DURING a cycle. The caller shows FF, which is what it would have floated to.
    virtual bool peek(uint16_t addr, uint8_t& out) const {
        (void)addr; (void)out;
        return false;
    }

    // EVERY BOARD SEES EVERY CYCLE. That is what a backplane IS: the address bus
    // is not addressed TO anyone, it is simply present, and any card may watch it
    // whether or not it answers.
    //
    // This is the CLOCKED half of the interface -- called exactly once per cycle,
    // after it completes -- and it is the ONLY place a board may latch what it
    // saw. assertsPhantom() and decodes() are combinational and must stay pure.
    //
    // The Tarbell single-density disk controller releases PHANTOM* here,
    // permanently, the first time it sees a memory read with A5 high. That is not
    // a bus feature and the bus does not know it happened. It is one flip-flop on
    // one card, which is the whole point.
    virtual void snoop(const BusCycle&) {}

    // ---- pINT (pin 73). A LEVEL ON A WIRE, AND THE BOARD DRIVES IT. ----

    // Am I pulling pin 73 right now? A UART with a character waiting and its
    // interrupt jumper installed says yes, and keeps saying yes until the guest
    // reads the character -- an interrupt is a LEVEL, not an event that gets
    // queued and delivered. Model it as a level and a board cannot "lose" an
    // interrupt, because there was never a queue to lose it from.
    //
    // COMBINATIONAL AND PURE, exactly like decodes(). It reports the settled state
    // of a pin, computed from the state of the chip and nothing else. It does not
    // advance a receiver, take a byte off a line, or do any work the guest has not
    // paid for.
    //
    // IT USED TO DO ALL THREE, and there was a whole section of DESIGN.md (4.4.1)
    // defending that. A 6850 has to notice a character has finished arriving --
    // which happens on the chip's own clock, with no help from the CPU -- and the
    // only thing that ever woke the card up was being asked this question, so the
    // card advanced its receiver here. It worked. It was the wrong shape, and
    // Patrick named it (2026-07-12):
    //
    //     "In a real system, the bus doesn't poll a board for interrupt status.
    //      The board sets high/low signals on the bus that the CPU reads from
    //      the bus. The board then clears the int signal based on its design."
    //
    // So the card does its free-running work on its OWN schedule now -- a Clock
    // deadline it sets itself, and pump(), which is where the host's keystrokes get
    // in -- and this is only ever asked what the pin SAYS. Which is what a wire is.
    //
    // The board does NOT supply a vector here. If it wants to drive one, it claims
    // the IntAck cycle like any other cycle (DESIGN.md 4.4).
    virtual bool assertsInt() const { return false; }

    // "MY INTERRUPT PIN MAY HAVE MOVED." The exact analogue of decodeChanged(),
    // and for the same reason: the bus CACHES the wire -- it keeps a running
    // wire-OR count, so intPending() is one integer test per instruction instead of
    // a virtual call to every board in the backplane -- and a cache nobody
    // invalidates is a lie told quietly.
    //
    // Call it after ANYTHING that could change assertsInt(): a register written, a
    // character taken off the line, a deadline coming due, a jumper moved. A
    // spurious call costs a virtual call. A MISSING one hangs the guest forever,
    // waiting for an interrupt that already happened -- so this is not left to
    // trust either: Bus::setVerify(true) re-derives the whole wire the slow way on
    // every instruction and aborts the moment a board disagrees with it.
    void intChanged() {
        bool now = enabled_ && assertsInt();
        if (now == intWire_) return;
        intWire_ = now;
        if (bus_) bus_->intWireChanged(now);
    }

    // What this card is ACTUALLY driving onto pin 73 -- the latched wire, not a
    // fresh computation. Bus::attach()/detach() read it to keep the wire-OR honest
    // across a card going into or coming out of the backplane.
    bool intWire() const { return intWire_; }

    // ---- Lifecycle (DESIGN.md 6) ----

    // POC* or RESET*. Board-specific: each board decides what it means. Memory
    // clears its bank latch and touches not one byte of RAM.
    virtual void reset(Reset) {}

    // Power APPLIED. The only event that loses RAM and re-reads ROM images.
    virtual void power() {}

    // Runtime enable. A boot ROM that switches itself out after boot sets this
    // false; POWER restores it (DESIGN.md 4.2.1).
    //
    // A disabled card drives nothing, so this changes the decode -- hence the
    // announcement. It is not virtual any more: an override that forgot to tell
    // the bus would be a stale-table bug, and there was no reason for one.
    bool enabled() const { return enabled_; }
    void setEnabled(bool e) {
        if (enabled_ == e) return;
        enabled_ = e;
        decodeChanged();
        intChanged();  // a card that is out of the machine is not pulling pin 73
    }

    // ---- Reflection (DESIGN.md 5) ----
    // The single source of truth for SET, SHOW, TOML, CONFIG SAVE, MCP schemas,
    // and tab completion. There is no second schema anywhere.
    virtual std::vector<Property> properties() = 0;

    // The properties of ONE UNIT -- `SET sio2a:a BAUD=9600` (DESIGN.md 7.2, and
    // the 88-2SIO's own doc). A unit is a real thing with real settings: the two
    // halves of a 2SIO have independent baud rates and independent transforms,
    // because they are two independent 6850s with their own crystals-worth of
    // jumpers. Folding them into the board's properties() as `a_baud`/`b_baud`
    // would work for a 2SIO and fall apart on the first card with eight ports.
    //
    // Empty for a board whose units have no settings, which is most of them.
    virtual std::vector<Property> unitProperties(const std::string& unit) {
        (void)unit;
        return {};
    }

    // ---- Host services ----

    // Give the host a turn: accept a socket connection, drain a keyboard. Called
    // once per time slice by the run loop, NEVER from inside a bus cycle.
    //
    // This is the seam that keeps a board pure. A board's read()/write() are
    // pure computation over state; anything that has to TALK TO THE OUTSIDE WORLD
    // happens here, at a known point in emulated time -- which is what makes a
    // recorded session replay identically instead of depending on when the host
    // scheduler happened to deliver a packet.
    virtual void pump() {}

    // The machine's clock, set when the card goes into the backplane (DESIGN.md
    // 7.5). A card with nothing time-dependent on it never looks at this, and
    // most don't. A UART absolutely does: TDRE is a deadline, not a flag.
    void attachClock(Clock* c) { clock_ = c; }

    // WHAT THE CARD WANTS SAID OUT LOUD. A bank select it could not decode, a ROM
    // that failed to load, a sector whose checksum did not match. Drained by the
    // monitor after every command and after every run, and cleared by the draining.
    //
    // VIRTUAL, AND ON Board, because it used to be a `dynamic_cast<MemoryBoard*>` in
    // Machine::drainBoardLog() -- which meant a disk controller with something to say
    // about a bad sector had NO WAY to say it, and would have had to grow a second
    // channel or teach the machine about a second board type. Both are the same bug:
    // a general facility with one card's name compiled into it.
    virtual std::vector<std::string> drainLog() { return {}; }

    // ---- RAW: behind the bus (DESIGN.md 10.2) ----
    //
    // Straight into the card's backing store, bypassing decode entirely.
    // Offsets are BOARD-LOCAL and the store may be far larger than 64K (a banked
    // card's bank 3 simply IS offset 0x30000).
    //
    // THIS IS THE PROM BURNER, AND THAT IS NOT A METAPHOR. It is how the
    // operator writes a ROM the guest cannot -- because burning a PROM is not a
    // bus operation on real hardware either. You pull the chip.
    virtual size_t rawSize() const { return 0; }
    virtual uint8_t rawRead(size_t) const { return 0xFF; }
    virtual bool rawWrite(size_t, uint8_t) { return false; }

    // ---- Introspection ----
    virtual std::vector<MapEntry> memMap() const { return {}; }
    virtual std::vector<MapEntry> ioMap() const { return {}; }

    // ---- Sub-units: `id:unit` ----
    //
    // Regions on a memory card, drives on a controller, ports on a 2SIO. Scalar
    // settings are properties(); a LIST of things is a sub-unit, and gets its
    // own TOML table -- [[board.region]], [[board.drive]], [board.unit.a].
    //
    // The board names the tables it accepts and builds the sub-unit itself, so
    // the TOML loader stays as ignorant of what a "region" is as the bus is.
    virtual std::vector<std::string> subUnitTables() const { return {}; }
    virtual bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
        (void)kv;
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    // Every unit this card has, in the board's own order and by the board's own
    // names. THIS IS THE ONLY LIST -- SHOW, MOUNT, CONNECT, the MCP schemas and tab
    // completion all read it, so they cannot disagree about what units exist.
    virtual std::vector<UnitDef> units() const { return {}; }

    // Find a unit by name, case-insensitively. False if this card has no such unit.
    bool findUnit(const std::string& name, UnitDef& out) const;

    virtual bool mount(const std::string& unit, const std::string& path, bool readOnly,
                       std::string& err) {
        (void)unit; (void)path; (void)readOnly;
        err = type() + " has nothing to mount";
        return false;
    }
    virtual bool unmount(const std::string& unit, std::string& err) {
        (void)unit;
        err = type() + " has nothing to unmount";
        return false;
    }

    // The character-device half. No board implements these yet -- the serial cards
    // are not written -- but the UNIT MODEL has to know that Serial is a kind, or
    // `MOUNT dj:tty` could not be rejected with a reason.
    virtual bool connect(const std::string& unit, const std::string& endpoint, std::string& err) {
        (void)unit; (void)endpoint;
        err = type() + " has nothing to connect";
        return false;
    }
    virtual bool disconnect(const std::string& unit, std::string& err) {
        (void)unit;
        err = type() + " has nothing to disconnect";
        return false;
    }

    // The backplane this card is plugged into, or null on the bench. Set by
    // Bus::attach().
    void attachBus(Bus* b) { bus_ = b; }

    // "MY DECODE JUST CHANGED." Tell the backplane so it can re-derive the wiring.
    // Cheap: it sets a flag, and the tables are rebuilt lazily on the next cycle.
    // Call it liberally -- a spurious rebuild costs microseconds, a missed one
    // costs you a day.
    //
    // Public because setProperty() calls it FOR every board, on every successful
    // set: `port`, `phantom`, `honors_phantom`, `bank_type`, `enabled` all change
    // the decode, and rather than trust each board to remember that, the one path
    // by which any property is EVER set announces it centrally. A board still calls
    // it directly for changes that do not come through a property -- a guest OUT
    // that moves a bank strap, a chip pulled from a socket.
    void decodeChanged() {
        if (bus_) bus_->invalidateDecode();
    }

    // "SOMEONE MOVED A JUMPER ON ME." Called by the ONE property path (below) after
    // every successful SET, on every board, without trying to work out which
    // properties actually matter -- because the moment it tries, it is wrong about
    // some board it has never heard of.
    //
    // The default covers what the bus caches: the decode and the interrupt wire. A
    // board with its own cached state or its own timers overrides this and re-arms
    // them -- the 2SIO does, because `baud` changes a character time, `interrupt`
    // changes which wire the chip's IRQ is soldered to, and `connect` changes what
    // is on the end of the line. All three move a deadline the card has already set.
    virtual void configChanged() {
        decodeChanged();
        intChanged();
    }

protected:
    bool   enabled_ = true;
    Clock* clock_   = nullptr;
    Bus*   bus_     = nullptr;

private:
    // What we are driving onto pin 73. Latched, not computed: this is a WIRE, and
    // the bus reads it rather than asking us about it every instruction.
    bool intWire_ = false;
};

// ---------------------------------------------------------------------------
// The ONE path by which any property is ever set.
//
// SET, the TOML loader, BOARDS ADD's k=v arguments, and the MCP board_set tool
// all call this. That is why they cannot disagree about what is legal or what
// base a number is in -- there is only one answer, and it is computed from the
// board's own metadata.
//
// A property's `radix` decides how bare digits are read: PORT=10 is port 0x10,
// BAUD=9600 is nine thousand six hundred. 0x forces hex, # forces decimal.
//
// THERE IS NO "CONFIG-TIME ONLY" PROPERTY (Patrick, 2026-07-12). Every property
// can be set, always. Two reasons, and the second is the real one:
//
//   - You can only type at the prompt when the machine is STOPPED -- by ATTN, by a
//     breakpoint, by a HLT. That is the front panel's STOP switch, and there is no
//     moment at which a SET would be racing a running CPU.
//
//   - And even on real hardware the gate would be a fiction. A card being worked on
//     sits on an EXTENDER, out where you can reach it, and its jumpers get moved
//     with the power on. That is how it was actually done.
//
// The old `runtime` flag was rejected-if-running, and it never once fired: nothing
// ever set the flag it was gated on. Deleting it removes a rule the simulator was
// only pretending to enforce.
// ---------------------------------------------------------------------------
bool setProperty(Board& b, const std::string& key, const std::string& text, std::string& err);

// The same path for a UNIT's properties -- `SET sio0:a BAUD=9600`.
bool setUnitProperty(Board& b, const std::string& unit, const std::string& key,
                     const std::string& text, std::string& err);

// ...and for anything else with properties that is not a board at all. The host
// console has properties (ATTN) and must obey the same rules about them; making
// it a fake Board to get that would have been the wrong way round.
bool setPropertyIn(std::vector<Property> props, const std::string& who, const std::string& key,
                   const std::string& text, std::string& err);

} // namespace altair
