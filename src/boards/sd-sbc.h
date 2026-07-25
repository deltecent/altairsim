#pragma once
//
// SD Systems SBC-100 / SBC-200 -- a Z80 single-board computer on the S-100 bus.
// See reference/SD Systems SBC-100 & SBC-200.md.
//
// PHASE 1 IS THE SERIAL CONSOLE ONLY. The real card is a whole computer -- CPU, an
// 8251 USART, a Z80-CTC baud generator, a parallel port, RAM and boot-PROM sockets,
// and it is the bus master. This board models the 8251 console at ports 7C (data) /
// 7D (status+command), and nothing else yet: the CTC (78-7B), the parallel port
// (7E/7F), the SBC-200 memory switch-out, the reset auto-start-jam to E000 and the
// vectored interrupts are later phases.
//
// It is the structural twin of the 88-SIO (boards/mits-88sio.h): one UART embedded
// DIRECTLY as a member, with the card owning refresh()/nextEdge()/wake_. It is NOT a
// Sio2Port card -- the 8251 puts data at the LOW port and status/command at the HIGH
// port, the reverse of the 6850 section, and the RxD->/DSR auto-baud strap is silicon
// behavior that belongs on this card.

#include "chips/intel8251.h"
#include "core/board.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class SbcBoard : public Board {
public:
    SbcBoard();
    ~SbcBoard() override;

    std::string type() const override { return "sbc"; }

    // ---- the bus: data at base_, status(read)/command(write) at base_+1 ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // Interrupts are a later phase; nothing is jumpered to a wire yet.
    bool    assertsInt() const override { return false; }
    uint8_t assertsVi() const override { return 0; }

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    uint64_t rxBytes() const override { return u_.rxBytes(); }
    std::vector<std::string> drainLog() override { return u_.drainLog(); }

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "tty" ? &u_.stream() : nullptr;
    }

    // The monitor resolves an endpoint string to a stream; the board is not allowed to
    // know what a socket is (DESIGN.md 7.7). Installed in main.cpp and tests/main.cpp.
    static void setResolver(EndpointResolver r);

    // ---- for tests, without going through the bus ----
    uint8_t statusByte() const;
    Intel8251&       usart() { return u_; }
    const Intel8251& usart() const { return u_; }

private:
    // Everything that could have moved a deadline has just happened: advance the
    // receiver and re-arm the one alarm for the next moment the chip changes on its
    // own (a frame completing, the transmitter draining).
    void     refresh();
    uint64_t nextEdge() const;

    // Which board this is strapped as. Inert in Phase 1 (the serial section is the same
    // 8251 either way); the 8251-vs-8251A / CTC-constant / mode-4F / memory-switch-out
    // differences land with the later phases.
    enum class Variant { Sbc100, Sbc200 };
    Variant variant_ = Variant::Sbc200;

    // The I/O region. The real card jumpers it (X-headers); the etch default is 7C.
    // Data at base_, status/command at base_+1.
    uint8_t base_ = 0x7C;

    // The console USART. Its RxD is strapped to /DSR (set in the constructor) -- the
    // whole point of this card, and what the monitor's auto-baud watches.
    Intel8251 u_{"tty"};

    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
