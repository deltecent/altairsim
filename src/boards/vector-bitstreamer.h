#pragma once
//
// Vector Graphic Bit Streamer -- one 8251 and two parallel ports at four ports. See
// docs/boards/vector-bitstreamer.md, from reference/Bitstreamer I.md.
//
//   base+0  OUT parallel latch A, IN the J2 A lines
//   base+1  OUT parallel latch B, IN the J2 B lines
//   base+2  8251 data
//   base+3  8251 status / mode and command
//
// NOTHING ON THE BOARD DRIVES THE PARALLEL INPUTS. U4 to U6 only buffer the cable, so an IN reads
// what the cable brings, and an open TTL input reads 1. No cable can be attached here. That also
// leaves out the U18 keyboard latch, which reaches port A only through a wire on J2.
//
// THE OUTPUT LATCHES HAVE NO CLEAR. They are 74LS75s, so a reset leaves them alone.
//
// The board has no clock and no interrupt logic: the 8251's TxRDY and RxRDY pins go nowhere.

#include "chips/intel8251.h"
#include "core/board.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class BitstreamerBoard : public Board {
public:
    BitstreamerBoard();
    ~BitstreamerBoard() override;

    std::string type() const override { return "vgbs"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset r) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    uint64_t rxBytes() const override { return u_.rxBytes(); }
    std::vector<std::string> drainLog() override { return u_.drainLog(); }

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "serial" ? &u_.stream() : nullptr;
    }

    static void setResolver(EndpointResolver r);

    // ---- For tests. ----
    uint8_t          latchA() const { return outA_; }
    uint8_t          latchB() const { return outB_; }
    const Intel8251& usart() const { return u_; }

private:
    void     refresh();
    uint64_t nextEdge() const;

    uint8_t port_ = 0x00;  // the pads beside U7

    Intel8251 u_{"serial"};
    uint8_t   outA_ = 0;  // U16, U17
    uint8_t   outB_ = 0;  // U14, U15

    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
