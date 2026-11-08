#pragma once
//
// CompuPro (Godbout) System Support 1 -- a multifunction S-100 board.
// docs/boards/compupro-ss1.md. Source: reference/CompuPro System Support 1.md
// (CompuPro/Godbout, Document #11620, 1981).
//
// One 16-port I/O block packs a serial channel (2651 UART), an interval timer
// (Intel 8253), two cascaded interrupt controllers (Intel 8259A), a battery-backed
// real-time clock/calendar (OKI MSM5832), and a socket for an AMD 9511A/9512 math
// coprocessor. This board was brought up in phases (issues #392, #393):
//
//   * PHASE 1: the MSM5832 real-time clock. Ports base+10/+11.
//   * PHASE 2: the 2651 UART -- a serial channel at base+12..+15.
//   * PHASE 3: the 8253 interval timer -- three counters + control at base+4..+7.
//   * PHASE 4: the dual 8259A interrupt controllers (+0..+3), in the standard
//     master/slave cascade. The master watches the S-100 VI lines and drives pin 73;
//     the slave takes the on-board sources (timer OUTs, the UART's RxRDY/TxRDY).
//   * THE MATH SOCKET (#393): +8 data, +9 command/status. The `math` property fits a
//     9511A or a 9512; the default `none` is the stock empty socket, and its two ports
//     float 0xFF.
//
// Ports +8/+9 are decoded only with a chip in the socket.
//
// ---------------------------------------------------------------------------
// THE MATH SOCKET (reference sec. 7.3, schematic p.98). The chip's CLK is pin 49 (the
// stock J5 strap), so its cycle counts are pin-49 ticks. Its RESET is the board's CLEAR
// (pRESET*/POC*/SLAVE CLR*), so every bus reset resets it. SVACK* and EACK* are both
// tied to +5 V: END clears only on a chip access or reset, SVREQ only when an SR = 0
// command ends or on reset. J6 is taken as set to match the chip (the 9511A's END is
// active low, the 9512's active high), so END reaches slave IR5 active high; SVRQ goes to
// slave IR4. The 9512's ERROR goes only to J7, not to either 8259A, so it is not wired.
//
// WAIT STATES: the board holds READY for 2 wait states on EVERY math access, and the
// chip's PAUSE holds it longer while a command runs (a data access or a command write,
// never a status read). The chip is handed the access at the tick PAUSE lets go.
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
//     slave  IR4    = math chip SVRQ        (inactive with the socket empty)
//     slave  IR5    = math chip END         (inactive with the socket empty)
//     slave  IR0    = S-100 VI7*          (through J8 pins 8-9)
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

#include "chips/am9511a.h"
#include "chips/am9512.h"
#include "chips/intel8253.h"
#include "chips/intel8259a.h"
#include "chips/msm5832.h"
#include "chips/sig2651.h"
#include "core/board.h"
#include "core/clock.h"

#include <cstdint>
#include <memory>
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

    // THE MASTER WATCHES VI0*-VI6* AND THE SLAVE VI7*, so the board is a VI priority
    // encoder like the 88-VI: the bus tells it when a VI line moves, and it claims the
    // IntAck cycle to drive the CALL vector. Do not put an SS-1 (with its 8259As programmed) and an 88-VI in the
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
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
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
    Am9511a&          math() { return apu_; }  // the chip `math = 9511a` fits
    const Am9511a&    math() const { return apu_; }
    Am9512&           fpu() { return fpu_; }   // the chip `math = 9512` fits
    const Am9512&     fpu() const { return fpu_; }

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
    uint8_t mathDataPort() const { return (uint8_t)(base_ + 8); }
    uint8_t mathCmdPort() const { return (uint8_t)(base_ + 9); }
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

    // The 8253's counter clock: pin-49 ticks of the bus CLOCK, or 0 with no clock or bus.
    uint64_t pin49() const;

    // Drive the next byte of the master's interrupt-acknowledge CALL (see the .cpp).
    uint8_t  intAckByte();

    // A data access or command write to the math chip: hold READY for the board's 2 wait
    // states, or until PAUSE lets go, and return the pin-49 tick the chip sees it at.
    uint64_t mathAccess(bool pauses);

    // The slave IR each on-board source is wired to (reference sec. 4.2, stock shunt).
    static constexpr int kSlaveVi7 = 0;
    static constexpr int kSlaveTimer0 = 1, kSlaveTimer1 = 2, kSlaveTimer2 = 3;
    static constexpr int kSlaveSvrq = 4, kSlaveEnd = 5;
    static constexpr int kSlaveTxRdy = 6, kSlaveRxRdy = 7;
    static constexpr int kMasterCascadeIr = 7;  // master IR7 = the slave's INT output

    // What sits in the math socket (U13). The stock board ships it empty.
    enum class Math { None, Am9511a, Am9512 };
    bool mathFitted() const { return math_ != Math::None; }
    Am95xx&       chip() { return math_ == Math::Am9512 ? (Am95xx&)fpu_ : apu_; }
    const Am95xx& chip() const { return math_ == Math::Am9512 ? (const Am95xx&)fpu_ : apu_; }

    uint8_t   base_ = 0x50;  // the 16-port block base -- CompuPro standard is 50H
    Math      math_ = Math::None;
    Am9511a   apu_{"math"};
    Am9512    fpu_{"math"};
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
