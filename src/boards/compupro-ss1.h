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
//   * PHASE 3: the 8253 interval timer -- three counters + control at base+4..+7.
//   * PHASE 4 (added here): the dual 8259A interrupt controllers (+0..+3), in the
//     standard master/slave cascade. The master watches the S-100 VI lines and drives
//     pin 73; the slave takes the on-board sources (timer OUTs, the UART's RxRDY/TxRDY).
//   The math socket (+8/+9) is deferred to its own issue -- an empty socket is the real
//   board's default, so those ports float.
//
// Only the ports a landed phase actually implements are decoded; the rest of the
// block floats 0xFF, which is exactly right for an unpopulated math socket.
//
// ---------------------------------------------------------------------------
// THE INTERRUPT MATRIX (reference sec. 4.2, the stock J7/J8 shunt). The two 8259As
// cascade in the factory jumpering, which this board wires by construction:
//
//     master IR0-6  = S-100 VI0*-VI6*      (so the SS-1 IS this machine's VI encoder)
//     master IR7    = slave INT             (the cascade line)
//     slave  IR1-3  = 8253 Timer 0/1/2 OUT
//     slave  IR6    = 2651 TxRDY
//     slave  IR7    = 2651 RxRDY
//     slave  IR0/4/5 = unassigned / the empty math socket's END & SVRQ (tied inactive)
//
// The master's INT output is the card's pin-73 (pINT) driver, and the card claims the
// IntAck cycle to feed the 8259A's CALL vector -- see read()/the INTA sequence in the
// .cpp. The dip-header rewiring the manual allows (any source to any input) is NOT
// modeled: we implement the standard shunt, which is what all the CompuPro software
// assumes.
//
// ---------------------------------------------------------------------------
// NO PHANTOM* TRICK IS NEEDED HERE, AND THAT IS A FINDING, NOT AN OMISSION.
//
// On real hardware (reference sec. 4.4) an 8080/Z80 issues ONE INTA pulse and then
// fetches the injected CALL's two address bytes as ordinary memory reads at PC -- so
// the board must assert PHANTOM* to stop system memory answering them. This
// simulator's CPU cores do not behave that way: every core issues an INTA cycle for
// EACH injected byte (the opcode via readOp() and every operand via fetch() both route
// to Bus::intAck() while intFetch_ is set -- see cpu8080.cpp, cpu8085.cpp, cpuZ80.cpp).
// The CALL's address bytes therefore come from THIS board over further IntAck cycles,
// never from memory, so there is nothing for PHANTOM* to suppress. Modeling it would be
// giving the board a behavior with no observable effect (DESIGN.md 4.0). The card
// simply keeps claiming the IntAck cycle until it has driven all three CALL bytes.
//
// THE STANDARD BASE IS 50H. The manual claims no factory default beyond the CompuPro
// software convention that the block lives at 50H, so that is our default -- but the
// base is a strap like any other, and the `base` property moves the whole block.

#include "chips/intel8253.h"
#include "chips/intel8259a.h"
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

    // PIN 73. The master 8259A's INT output IS the card's pINT driver: it resolves the
    // machine's VI lines and the on-board sources and pulls pin 73 when one wins. The
    // legacy `interrupt` unit jumper (Phase 2) can still route the UART's RxRDY straight
    // to pINT/VI for a machine that does not program the on-board controllers.
    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    // THE MASTER WATCHES VI0*-VI6*, so it is a VI priority encoder like the 88-VI: the
    // bus tells it when a VI line moves, and it claims the IntAck cycle to drive the
    // CALL vector. Do not put an SS-1 (with its 8259As programmed) and an 88-VI in the
    // same machine -- both would claim IntAck and the contention detector says so.
    bool    watchesVi() const override { return true; }
    int     intWinner() const override;

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
    Sig2651&          uart() { return uart_; }
    const Sig2651&    uart() const { return uart_; }
    Intel8253&        timer() { return timer_; }
    const Intel8253&  timer() const { return timer_; }
    Intel8259a&       master() { return master_; }
    const Intel8259a& master() const { return master_; }
    Intel8259a&       slave() { return slave_; }
    const Intel8259a& slave() const { return slave_; }

    // The IR input levels the card wires to each controller, computed LIVE (the 8253
    // OUTs from the clock, the VI lines from the bus). Public for tests that drive the
    // chips directly; the interrupt logic in the .cpp reads these on every access.
    uint8_t slaveLive() const;
    uint8_t masterLive() const;

private:
    // The port offsets within the 16-port block (fixed regardless of base).
    uint8_t picMaster0Port() const { return (uint8_t)(base_ + 0); }
    uint8_t picMaster1Port() const { return (uint8_t)(base_ + 1); }
    uint8_t picSlave0Port() const { return (uint8_t)(base_ + 2); }
    uint8_t picSlave1Port() const { return (uint8_t)(base_ + 3); }
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

    // Drive the next byte of the master's interrupt-acknowledge CALL (see the .cpp).
    uint8_t  intAckByte();

    // The slave IR each on-board source is wired to (reference sec. 4.2, stock shunt).
    static constexpr int kSlaveTimer0 = 1, kSlaveTimer1 = 2, kSlaveTimer2 = 3;
    static constexpr int kSlaveTxRdy = 6, kSlaveRxRdy = 7;
    static constexpr int kMasterCascadeIr = 7;  // master IR7 = the slave's INT output

    uint8_t   base_ = 0x50;  // the 16-port block base -- CompuPro standard is 50H
    Msm5832   rtc_;
    Sig2651   uart_{"serial"};
    Intel8253 timer_{"timer"};
    Intel8259a master_{"pic-master"};
    Intel8259a slave_{"pic-slave"};

    // The interrupt-acknowledge sequence: which CALL byte comes next (0 = the CALL
    // opcode, 1 = address low, 2 = address high) and the address resolved on byte 0.
    // NOT serialized -- the whole 3-byte fetch runs inside one CPU instruction, so this
    // is always 0 at an instruction boundary, which is the only place a snapshot lands.
    uint8_t  intaByte_ = 0;
    uint16_t intaAddr_ = 0;

    Clock::Handle wake_ = Clock::kNone;
};

}  // namespace altair
