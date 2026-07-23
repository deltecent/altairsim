#pragma once
//
// MITS 8800b Turnkey Module (reference/MITS Turn Key Board.md,
// docs/boards/mits-turnkey.md).
//
// THE CARD THAT MAKES AN 8800bt. One S-100 board with no front panel behind it,
// bundling four things onto one card (reference §1, the newer 200372-01 revision):
//
//   1. A boot PROM at FC00-FFFF that PHANTOMS OUT (reference §9). It shadows RAM for
//      reads from reset until the guest's first `IN` from port FE or FF, then vanishes
//      so the machine can use all 64K of RAM. The disable is a one-shot latch, re-armed
//      by any system reset. This is the signature behaviour, and it is why an unmodified
//      Altair BASIC drops into 64K after reading the sense switches once. We model the
//      post-Service-Bulletin-007 board: the trigger is an INPUT only (an `OUT FE/FF`
//      does not disable the PROM).
//
//   2. Sense switches on port FF (reference §4), exactly like the front panel's -- and
//      the SAME read that returns them is the phantom-disable trigger. Because this card
//      answers port FF, a machine with a Turnkey Module must NOT also carry an `fp`
//      board (they would contend for the port).
//
//   3. An integrated 6850 SIO terminal port at 0x10, 88-2SIO-Port-A compatible
//      (reference §5). The card owns a Sio2Port section (src/chips/sio2port.h); every
//      serial concern is delegated to it.
//
//   4. The Auto-Start circuit (reference §7). On reset the card JAMS a JMP instruction
//      onto the bus -- C3 00 <START-ADDR-hi> for the first three fetch cycles, while
//      holding PHANTOM* so no other memory board drives the bus -- so the machine boots
//      the PROM page the START ADDR switches select. The CPU resets its PC to 0; the
//      jam redirects that first fetch to the PROM.
//
// OUT OF SCOPE: the older REV 0 board's 1K onboard SRAM at F800-FBFF, and the
// 88-SYS-CLG/CLG2 field reworks (reference §1, §11). Main RAM is a separate `memory`
// board, strapped `honors_phantom = "read"` so writes fall through the PROM shadow.

#include "chips/sio2port.h"
#include "core/board.h"
#include "core/hex.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class TurnkeyBoard : public Board {
public:
    TurnkeyBoard();

    std::string type() const override { return "turnkey"; }

    // ---- bus ----
    bool    decodes(const BusCycle&) const override;
    bool    assertsPhantom(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // The autostart jam decodes address lines 0-1 within page 0, so page 0 is not
    // decode-uniform. See the note under Board::decodeIsPageUniform().
    bool decodeIsPageUniform() const override { return false; }

    // The card WATCHES the bus: the auto-start sequencer counts fetches, and the phantom
    // latch releases on an IN from port FE/FF.
    bool wantsSnoop() const override { return true; }
    void snoop(const BusCycle&) override;

    // ---- interrupts (the SIO's) ----
    bool    assertsInt() const override { return sio_.assertsInt(); }
    uint8_t assertsVi() const override { return sio_.assertsVi(); }

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void pump() override { sio_.pump(); }
    void clockAttached() override { sio_.attachClock(clock_); }
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<MapEntry> memMap() const override;

    // ---- serial units: the SIO's `tty`, delegated to the section ----
    std::vector<UnitDef>  units() const override { return sio_.units(); }
    std::vector<Property> unitProperties(const std::string& unit) override {
        return sio_.unitProperties(unit);
    }
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override {
        return sio_.connect(unit, endpoint, err);
    }
    bool disconnect(const std::string& unit, std::string& err) override {
        return sio_.disconnect(unit, err);
    }
    ByteStream* unitStream(const std::string& unit) override { return sio_.unitStream(unit); }
    uint64_t    rxBytes() const override { return sio_.rxBytes(); }
    std::vector<std::string> drainLog() override;

    // ---- PROM sockets: `[[board.socket]]` ----
    std::vector<std::string> subUnitTables() const override { return {"socket"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The endpoint resolver is the section's (shared with every SIO-bearing card).
    static void setResolver(EndpointResolver r) { Sio2Port::setResolver(std::move(r)); }

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    // The four 256-byte PROM sockets form one 1K window; normal base FC00 (reference §2).
    static constexpr int kPromSize = 0x400;

    bool inPromWindow(uint16_t addr) const {
        return addr >= promBase_ && (uint32_t)addr < (uint32_t)promBase_ + kPromSize;
    }

    // (Re)read the socket ROMs into prom_. Called on power() and when a socket is added,
    // exactly as a memory card re-reads its ROM regions (DESIGN.md 13).
    void loadProm();

    // ---- config / straps (rebuilt from TOML) ----
    uint16_t promBase_ = 0xFC00;  // PROM ADDR switches; the 1K window base
    uint16_t start_    = 0xFC00;  // START ADDR switches (a multiple of 256)
    uint8_t  sense_    = 0x00;    // SW6/SW7 sense switches, read at port FF

    struct Socket {
        uint16_t    at;      // where in the window this socket sits (FC00/FD00/FE00/FF00)
        std::string mount;   // "builtin:hdbl", "file..." -- what is in it
    };
    std::vector<Socket> sockets_;

    // ---- the PROM bytes (host-backed config: re-read on power, never serialized) ----
    uint8_t prom_[kPromSize];

    // ---- runtime latches (these DO travel in a snapshot) ----
    bool promArmed_      = true;   // the phantom PROM is visible until the first IN FE/FF
    bool autostartArmed_ = true;   // the JMP jam is live for the first 3 fetches post-reset
    int  autostartStep_  = 0;      // which of the three jammed bytes is next

    // ---- the integrated 6850 SIO section ----
    Sio2Port sio_;

    std::vector<std::string> log_;
};

} // namespace altair
