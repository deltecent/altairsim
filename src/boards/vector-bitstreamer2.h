#pragma once
//
// Vector Graphic Bitstreamer II -- three 8251 serial channels, two parallel channels and a
// 55 Hz clock, at eight ports from a base that always ends in 2. See reference/Bitstreamer II.md.
//
//   base+0, +1  Serial A, data and status (J3)
//   base+2, +3  Serial B, data and status (J2)
//   base+4, +5  Serial C, data and status (J1)
//   base+6      OUT  parallel A latch, which is also the interrupt mask
//               IN   the J4 lines
//   base+7      OUT  parallel B latch
//               IN   the J5 lines. The read also clears the clock latch.
//
// NOTHING ON THE BOARD DRIVES THE PARALLEL INPUTS. U12 and U24 only buffer the cable, so an IN
// reads what the cable brings, and an open TTL input reads 1. No cable can be attached here.
//
// J4 D0 IN IS THE ONE WIRE NO DOCUMENT SHOWS. OASIS reads the source of its interrupt from bit 0
// of `IN x8`, and the board has no such path. `j4_d0` is a jumper for that wire. `clock-latch`
// takes it from U4 pin 5, the Q of the clock latch, which the schematic leaves unconnected. Pad
// B-3 cannot be the source: a U10 gate that drives PINT with the right sense reads backward here.
// That is inferred from OASIS and is not in a Vector Graphic document (reference Q1, Q2).
//
// THE CLOCK LATCH IS NOT GATED BY THE MASK. U4 sets on every rising edge of the 55 Hz wave, and
// the mask only gates what the latch drives. So the ticker runs when anything is wired to the
// latch, whatever the mask holds, and unmasking a latch already set interrupts at once. With
// nothing wired there is no deadline, so the machine can stand down on a HLT.
//
// THE CLOCK IS THE BUS's 2 MHz DIVIDED DOWN, NOT THE PROCESSOR's CRYSTAL (issue #825). The chain
// is 13 for 9600 baud, 4 for 2400, 22 for "110", then 16 and 2. That is 54.6 Hz, not 55.
//
// PIN 73 FOLLOWS THE TEXT OF PAGE 1-7: the latch interrupts while it is set. The manual's gate
// wiring reads the other way, and the manual calls the difference harmless (reference Q2).

#include "chips/intel8251.h"
#include "core/board.h"
#include "core/busclock.h"
#include "core/hostticker.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Bitstreamer2Board : public Board {
public:
    Bitstreamer2Board();
    ~Bitstreamer2Board() override;

    std::string type() const override { return "vgbsii"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // PIN 73 and the VI lines: the area A jumpers. Pure and combinational. Each source passes its
    // mask bit first, and then goes where its jumper goes.
    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    void reset(Reset r) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    uint64_t rxBytes() const override;

    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;

    static void setResolver(EndpointResolver r);

    // ---- For tests. ----
    bool clockLatch() const { return latch_; }

protected:
    // A machine file is built on a scratch Clock, and its deadlines stay behind on that clock
    // when the board moves to the real one. So the ticker starts again here.
    void clockAttached() override { armClock(); }

private:
    // 2 MHz / 16 is the tick, so the T-state period stays whole at a 2.5 MHz processor. The
    // multiple is the rest of the chain: 13 x 4 x 22 x 2.
    static constexpr long long kClockTickHz = BusClock::kHz / 16;
    static constexpr uint64_t  kClockChain  = 13 * 4 * 22 * 2;

    static constexpr uint8_t kMaskClock = 0x40;

    enum class J4D0 { Open, ClockLatch };

    struct Channel {
        Channel(const char* n, uint8_t tx, uint8_t rx) : uart(n), txBit(tx), rxBit(rx) {}
        Intel8251 uart;
        uint8_t   txBit, rxBit;  // the mask bits of pads 3/6, 7/4 and 2/5
        IrqJumper txIrq = IrqJumper::None;
        IrqJumper rxIrq = IrqJumper::None;
    };

    Channel*       channel(const std::string& unit);
    bool           clockLine() const { return latch_ && (mask_ & kMaskClock); }
    bool           rxLine(const Channel& ch) const;
    bool           txLine(const Channel& ch) const;
    uint8_t        j4() const;
    std::vector<Property> channelProperties(Channel& ch);

    void     armClock();
    void     refresh();
    uint64_t nextEdge() const;

    uint16_t port_ = 0x02;  // area W

    Channel a_{"a", 0x01, 0x02};  // U31, J3
    Channel b_{"b", 0x04, 0x08};  // U30, J2
    Channel c_{"c", 0x10, 0x20};  // U29, J1

    uint8_t mask_  = 0;      // U5, U9
    uint8_t outB_  = 0;      // U16, U20
    bool    latch_ = false;  // half of U4

    IrqJumper  clockIrq_ = IrqJumper::None;
    J4D0       j4d0_     = J4D0::Open;
    HostTicker clockTick_;

    // The alarm for a serial source that moves on its own: a transmitter that drains, or a
    // character that finishes arriving.
    Clock::Handle wake_ = Clock::kNone;

    std::vector<std::string> log_;
};

} // namespace altair
