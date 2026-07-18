#pragma once
//
// Sol I/O -- the Processor Technology Sol-PC's onboard I/O, modeled as ONE composite
// S-100 card. See reference/Sol-20.md and docs/boards/proctech-sol.md.
//
// WHY ONE CARD AND NOT FOUR. Everything else in altairsim is a bag of single-purpose
// cards. The Sol-20 is an integrated machine: the Sol-PC motherboard carries the
// serial port, the keyboard, the parallel/printer port and the CUTS cassette all at
// once -- and, decisively, it multiplexes the READINESS of the keyboard, the parallel
// port and the tape into ONE physical status register at 0xFA. Our bus is
// single-driver (one board answers a read, or it floats 0xFF), so three cards could
// not each OR a bit into `IN 0FAH`. The hardware is one register, so the model is one
// board -- and the four functions are named UNITS you CONNECT independently:
//
//     CONNECT sol0:serial   socket:2323
//     CONNECT sol0:printer  file:out.txt
//     CONNECT sol0:tape     file:tape.cuts
//     CONNECT sol0:keyboard console
//
// PORTS (fixed on the Sol-PC; see reference/Sol-20.md for the bit tables):
//
//     F8  in   serial status   (active HIGH: D6 RX-ready, D7 TX-empty)
//     F9  i/o  serial data
//     FA  in   general status  (MIXED polarity: D0 kbd / D1,2 parallel active LOW,
//                               D3,4,6,7 tape active HIGH)
//     FA  out  tape motor (D7/D6) + cassette 300-baud select (D5)
//     FB  i/o  tape (CUTS) data
//     FC  in   keyboard data   (ready = FA D0, active low)
//     FD  i/o  parallel (printer) data
//     FE  out  VDM display parameter (scroll) -- forwarded to the vdm1 board
//
//     FF (sense switches) is the `fp` board; the VDM screen RAM is the `vdm1` board.
//
// SCOPE. Serial and keyboard are fully modeled; the parallel/printer port works if
// you connect it; the CUTS tape is DEFERRED (the ports decode and read idle so SOLOS
// never hangs, but there is no Kansas-City framing yet). SOLOS is polled, so this
// card raises no interrupts.

#include "chips/uart1602.h"  // the serial UART -- A CHIP IS NOT A CARD (DESIGN.md 7.8)
#include "core/board.h"
#include "host/stream.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class VdmBoard;  // the scroll target for OUT 0xFE, located on the bus at run time

class SolBoard : public Board {
public:
    SolBoard();

    std::string type() const override { return "sol"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void power() override;
    void pump() override;

    // A keystroke arriving is live traffic too, so the idle policy stands the machine
    // back up for it -- not just serial bytes (Board::rxBytes, [[idle policy]]).
    uint64_t rxBytes() const override { return serial_.rxBytes() + kbRx_; }

    std::vector<std::string> drainLog() override { return serial_.drainLog(); }

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // The monitor resolves an endpoint string to a stream; the BOARD may not know what
    // a socket is (DESIGN.md 7.7). Wired once in main.cpp / tests/main.cpp.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The connector behind a unit, for an operator that owns the endpoint (the MCP
    // console). Non-owning. See Board::unitStream.
    ByteStream* unitStream(const std::string& unit) override;

    // ---- The pins, so a test can look at them without going through the bus. ----
    uint8_t serialStatus() const;   // what IN 0xF8 returns
    uint8_t generalStatus() const;  // what IN 0xFA returns
    bool    keyboardReady() const { return kbHave_; }

private:
    // Take one character off the keyboard line into the holding register, if one is
    // waiting and the register is free. Called from pump() ONLY -- reading the host
    // from inside a bus cycle is the thing the chip/card split exists to prevent.
    void latchKeyboard();

    bool     serialTxEmpty() const;
    bool     printerWritable() const { return printer_ && printer_->writable(); }
    std::string endpointOf(const std::string& unit) const;

    // The vdm1 board on the same bus, for OUT 0xFE (scroll). Located each time rather
    // than cached, so a machine rebuilt under us (CONFIG LOAD) is never stale; OUT
    // 0xFE is rare (only on scroll), so the walk is free. Null if there is no VDM.
    VdmBoard* vdm() const;

    static Clock& deadCard();  // a stopped clock, for when no crystal is attached

    uint8_t base_ = 0xF8;  // F8..FE on the Sol-PC; a property for tests, fixed in fact

    // ---- The serial port: a real UART on F8/F9 (src/chips/uart1602.h). ----
    Uart1602 serial_{"serial"};

    // ---- The other three lines: connectable byte streams. Never null (NullStream
    //      stands in) so the read/write path has no pointer to check. ----
    std::unique_ptr<ByteStream> kb_;       // keyboard  (FC data, FA D0)
    std::unique_ptr<ByteStream> printer_;  // parallel  (FD data, FA D1/D2)
    std::unique_ptr<ByteStream> tape_;     // CUTS tape (FB data, FA D3/4/6/7) -- deferred

    // The keyboard holding register + its strobe (FA D0, active low = a key waiting).
    uint8_t  kbData_ = 0;
    bool     kbHave_ = false;
    uint64_t kbRx_   = 0;  // keystrokes handed to the guest -- the idle-traffic signal

    uint8_t  tapeCtl_ = 0;  // last OUT 0xFA (motor + baud select) -- held, deferred
};

} // namespace altair
