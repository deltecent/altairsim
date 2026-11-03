#pragma once
//
// The Vector Graphic ZCB, a Z-80 single board computer -- docs/boards/vector-zcb.md, from
// reference/ZCB CPU Manual.md.
//
// One board is the processor, the PROM sockets, 1K of RAM, an 8251 and an 8255, so it is a
// CpuZ80Board that also answers bus cycles. It is one [[board]] entry in a machine file, as
// it is one board in the backplane. The core, the crystal and the idle policy are the base
// class's.
//
//   - RESET (sheet 4). A flip-flop, armed_ here, is set by every reset. While it is set the
//     board pulls PHANTOM*, and every address decodes as the E000 block, so the Z-80 at 0000
//     reads the jump at E000. The first memory cycle in E000-FFFF clears it. Jumpers B and C
//     are `boot` and `phantom`. Nothing writes the PC.
//
//   - MEMORY. PROM in E000-EFFF and RAM at FC00. The board takes its own byte on a read and
//     ignores the data-in lines, so it needs no PHANTOM* in normal running (reference 5). A
//     write goes out on the bus too, so an off-board RAM at that address takes the byte, and
//     the on-board RAM takes it in snoop() and not in a decode.
//
//   - PORTS. One 8251 and one 8255, each at four ports of one 32-port group. A1 does not
//     reach the 8251, so its two registers repeat at the upper two ports. The board has no
//     interrupt source: the 8251's TxRDY and RxRDY pins go nowhere (reference 8).
//
//   - WAIT STATES. The automatic wait of jumper N, one T-state, through Board::holdReady().

#include "boards/mits-z80cpu.h"
#include "chips/intel8251.h"
#include "chips/intel8255.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class ZcbBoard : public CpuZ80Board {
public:
    ZcbBoard();
    ~ZcbBoard() override;

    std::string type() const override { return "vgzcb"; }

    // ---- the bus ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    // Every on-board read: U3 shuts off the data-in buffers for a read of the PROM, the RAM,
    // the 8251 or the 8255, and the board does not look at the bus (reference 5).
    bool    readsOnBoard(const BusCycle& c) const override { return decodes(c); }

    // A level, held from reset until the first memory cycle in the block.
    bool assertsPhantom(const BusCycle&) const override { return armed_ && phantom_; }

    // The clocked half: the release of the reset latch, the write to the on-board RAM, and
    // the automatic wait state, which also falls on cycles that an off-board memory answers.
    bool wantsSnoop() const override { return true; }
    void snoop(const BusCycle& c) override;

    void reset(Reset r) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    uint64_t rxBytes() const override { return u_.rxBytes(); }
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<MapEntry> memMap() const override;

    std::vector<std::string> subUnitTables() const override { return {"socket"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "serial" ? &u_.stream() : nullptr;
    }

    static void setResolver(EndpointResolver r);

    // ---- for tests, without going through the bus ----
    bool             armed() const { return armed_; }
    Intel8251&       usart() { return u_; }
    const Intel8251& usart() const { return u_; }
    Intel8255&       ppi() { return p_; }

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    void     refresh();
    uint64_t nextEdge() const;
    void     loadProm();

    uint8_t serialBase() const { return (uint8_t)(group_ + serial_); }
    uint8_t parallelBase() const { return (uint8_t)(group_ + parallel_); }
    bool    isSerial(uint8_t port) const { return (uint8_t)(port & 0xFC) == serialBase(); }
    bool    isParallel(uint8_t port) const { return (uint8_t)(port & 0xFC) == parallelBase(); }

    // WHICH ON-BOARD CHIP ANSWERS AT `a`? `t` is the address that U5 sees: `a` itself, or the
    // same address in the E000 block while the reset latch and jumper B override the decode.
    // The answer goes by 1K lines, so it is the same for a whole 256-byte page.
    enum class Onboard { None, Prom, Ram };
    Onboard onboard(uint16_t a, uint16_t& t) const {
        t = (armed_ && boot_) ? (uint16_t)(0xE000 | (a & 0x1FFF)) : a;
        if (t >= kRamBase) return Onboard::Ram;
        if (t >= kPromBase && t <= kPromLast && promLine_[(t - kPromBase) >> 10])
            return Onboard::Prom;
        return Onboard::None;
    }

    static constexpr uint16_t kBlock    = 0xE000;
    static constexpr uint16_t kPromBase = 0xE000;
    static constexpr uint16_t kPromLast = 0xEFFF;
    static constexpr uint16_t kRamBase  = 0xFC00;
    static constexpr int      kPromSize = 0x1000;
    static constexpr int      kRamSize  = 0x400;
    static constexpr int      kLines    = 4;     // E000, E400, E800, EC00
    static constexpr size_t   kSockets  = 3;     // U20, U21, U22

    // ---- the jumpers (config; not serialized) ----
    uint8_t group_    = 0x00;  // area G: the 32-port group of both chips
    uint8_t serial_   = 0x04;  // area E, pad 1: the 8251 inside the group
    uint8_t parallel_ = 0x08;  // area E, pad 2: the 8255 inside the group
    bool    boot_     = true;  // jumper B: the override of the decode after a reset
    bool    phantom_  = true;  // jumper C: PHANTOM* after a reset

    enum class Wait { M1, None, All };
    Wait wait_ = Wait::M1;     // area N

    struct Socket {
        uint16_t    at;
        std::string mount;
    };
    std::vector<Socket> sockets_;
    uint8_t prom_[kPromSize] = {};
    bool    promLine_[kLines] = {};

    // ---- runtime state (travels in a snapshot) ----
    bool      armed_ = true;
    uint8_t   ram_[kRamSize] = {};
    Intel8251 u_{"serial"};
    Intel8255 p_;

    std::vector<std::string> log_;
    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
