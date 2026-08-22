#pragma once
//
// CompuPro (Godbout) System Support 1 -- a multifunction S-100 board.
// docs/boards/compupro-ss1.md. Source: reference/CompuPro System Support 1.md
// (CompuPro/Godbout, Document #11620, 1981).
//
// One 16-port I/O block packs a serial channel (2651 UART), an interval timer
// (Intel 8253), two cascaded interrupt controllers (Intel 8259A), a battery-backed
// real-time clock/calendar (OKI MSM5832), and a socket for an AMD 9511A/9512 math
// coprocessor. This board is being brought up in phases (issue #392):
//
//   * PHASE 1: the MSM5832 real-time clock. Ports base+10/+11.
//   * PHASE 2: the 2651 UART -- a serial channel at base+12..+15.
//   * PHASE 3 (added here): the 8253 interval timer -- three counters + control at
//     base+4..+7. Its OUT lines are readable; wiring them to interrupts is Phase 4.
//   * later: the dual 8259A (+0..+3). The math socket (+8/+9) is deferred to its own
//     issue -- an empty socket is the real board's default, so those ports float.
//
// Only the ports a landed phase actually implements are decoded; the rest of the
// block floats 0xFF, which is exactly right for an unpopulated math socket.
//
// THE STANDARD BASE IS 50H. The manual claims no factory default beyond the CompuPro
// software convention that the block lives at 50H, so that is our default -- but the
// base is a strap like any other, and the `base` property moves the whole block.

#include "chips/intel8253.h"
#include "chips/msm5832.h"
#include "chips/sig2651.h"
#include "core/board.h"
#include "core/clock.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Ss1Board : public Board {
public:
    Ss1Board();
    ~Ss1Board() override;  // cancels the UART's clock deadline (a fired stale alarm is a UAF)

    std::string type() const override { return "ss1"; }

    bool decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void write(const BusCycle& c) override;

    // THE 2651's RECEIVE INTERRUPT. On the standard board the UART's RxRDY/TxRDY feed the
    // on-board 8259A (Phase 4); until then the `interrupt` unit jumper can route RxRDY
    // straight to pINT or an S-100 VI line for a machine that wants a receive interrupt.
    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The straps are config; the RTC (its host-time
    // offset and edit latches) and the UART (its live registers and deadlines) carry the
    // runtime state.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    uint64_t rxBytes() const override { return uart_.rxBytes(); }
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "serial" ? &uart_.stream() : nullptr;
    }

    // The monitor resolves an endpoint string to a stream; the board is not allowed to
    // know what a socket is (DESIGN.md 7.7). Installed in main.cpp and tests/main.cpp.
    static void setResolver(EndpointResolver r);

    // ---- for tests, without going through the bus ----
    Sig2651&         uart() { return uart_; }
    const Sig2651&   uart() const { return uart_; }
    Intel8253&       timer() { return timer_; }
    const Intel8253& timer() const { return timer_; }

private:
    // The port offsets within the 16-port block (fixed regardless of base).
    uint8_t timer0Port() const { return (uint8_t)(base_ + 4); }
    uint8_t timer1Port() const { return (uint8_t)(base_ + 5); }
    uint8_t timer2Port() const { return (uint8_t)(base_ + 6); }
    uint8_t timerCtlPort() const { return (uint8_t)(base_ + 7); }
    uint8_t clockCmdPort() const { return (uint8_t)(base_ + 10); }
    uint8_t clockDataPort() const { return (uint8_t)(base_ + 11); }
    uint8_t uartDataPort() const { return (uint8_t)(base_ + 12); }
    uint8_t uartStatusPort() const { return (uint8_t)(base_ + 13); }
    uint8_t uartModePort() const { return (uint8_t)(base_ + 14); }
    uint8_t uartCmdPort() const { return (uint8_t)(base_ + 15); }

    // The UART's card-owned clock (DESIGN.md 7.5): advance the receiver, re-drive the
    // interrupt wire, and arm one alarm for the next moment the chip changes on its own.
    void     refresh();
    uint64_t nextEdge() const;

    uint8_t   base_ = 0x50;  // the 16-port block base -- CompuPro standard is 50H
    Msm5832   rtc_;
    Sig2651   uart_{"serial"};
    Intel8253 timer_{"timer"};
    Clock::Handle wake_ = Clock::kNone;
};

}  // namespace altair
