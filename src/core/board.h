#pragma once
//
// Board -- one physical card in the backplane (DESIGN.md 3, 4.1, 5).
//
// ONE BOARD IS ONE CARD. Not one chip, not one address range. A card carrying
// 48K of RAM and three PROM sockets is ONE board, because that is what you would
// pull out of the machine with your hand. Every decode question the bus can ask
// is answered here, by the card, using knowledge only the card has.

#include "core/bus.h"
#include "core/value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

// The key/value pairs of one TOML sub-unit table, in file order.
using KeyValues = std::vector<std::pair<std::string, std::string>>;

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
    // Whether PHANTOM* is pulled for READS ONLY or for reads and writes is
    // decided HERE, by the asserting board -- because that is where the gate
    // physically is. A card that gates PHANTOM* with the read strobe simply does
    // not pull it during a write cycle, so memory answers the write normally and
    // the byte lands in RAM under the shadow. The honoring board needs no strap
    // for this and must never grow one. See the Tarbell (docs/boards/tarbell.md).
    virtual bool assertsPhantom(const BusCycle&) const { return false; }

    // Do I drive the bus for this cycle? Everything board-specific lives behind
    // this one question: my address range, my ports, my bank, whether I honor
    // PHANTOM*, and whether the thing at this address is ROM (which never
    // answers a write).
    virtual bool decodes(const BusCycle&) const { return false; }

    virtual uint8_t read(const BusCycle&) { return 0xFF; }
    virtual void write(const BusCycle&) {}

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

    // ---- Lifecycle (DESIGN.md 6) ----

    // POC* or RESET*. Board-specific: each board decides what it means. Memory
    // clears its bank latch and touches not one byte of RAM.
    virtual void reset(Reset) {}

    // Power APPLIED. The only event that loses RAM and re-reads ROM images.
    virtual void power() {}

    // Runtime enable. A boot ROM that switches itself out after boot sets this
    // false; POWER restores it (DESIGN.md 4.2.1).
    virtual bool enabled() const { return enabled_; }
    virtual void setEnabled(bool e) { enabled_ = e; }

    // ---- Reflection (DESIGN.md 5) ----
    // The single source of truth for SET, SHOW, TOML, CONFIG SAVE, MCP schemas,
    // and tab completion. There is no second schema anywhere.
    virtual std::vector<Property> properties() = 0;

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

    virtual bool mount(int unit, const std::string& path, bool readOnly, std::string& err) {
        (void)unit; (void)path; (void)readOnly;
        err = type() + " has nothing to mount";
        return false;
    }
    virtual bool dismount(int unit, std::string& err) {
        (void)unit;
        err = type() + " has nothing to dismount";
        return false;
    }

protected:
    bool enabled_ = true;
};

// ---------------------------------------------------------------------------
// The ONE path by which any property is ever set.
//
// SET, the TOML loader, BOARD ADD's k=v arguments, and the MCP board_set tool
// all call this. That is why they cannot disagree about what is legal, what is
// runtime-settable, or what base a number is in -- there is only one answer,
// and it is computed from the board's own metadata.
//
// A property's `radix` decides how bare digits are read: PORT=10 is port 0x10,
// BAUD=9600 is nine thousand six hundred. 0x forces hex, # forces decimal.
// ---------------------------------------------------------------------------
bool setProperty(Board& b, const std::string& key, const std::string& text, bool running,
                 std::string& err);

} // namespace altair
