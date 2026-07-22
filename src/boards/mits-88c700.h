#pragma once
//
// 88-C700 -- MITS Centronics Printer Controller. The S-100 card that drives an
// Altair C700 line printer. See docs/boards/mits-88c700.md and
// reference/88-C700 Centronics Printer Controller.md.
//
// AN OUTPUT CARD, AND THAT IS THE WHOLE OF IT. Where the 88-SIO wraps a UART with
// a receiver, a transmit deadline and a word frame, the C700 is a bare latch: the
// guest writes a byte to the data port and it lands on the line; the guest reads
// the status port to see whether the printer will take the next one. There is no
// receive path -- a printer sends nothing back -- so there is no chip here, just a
// single ByteStream and the port decode around it.
//
// Two ports, an even/odd pair like the 88-SIO: Control/Status at an EVEN base, Data
// at the odd address above it. The MITS default is 002 (so status/control at 02,
// data at 03), and MITS software requires it.
//
// POLLED. The real card also has a single-level interrupt (per-character or
// per-CR/LF, SW2 #4). That path is NOT modeled here -- the status byte carries the
// INTERRUPT ENABLE bit software wrote, but no request is raised and no wire is
// pulled. A polled driver (write a byte, poll ACKNOWLEDGE/BUSY) is complete; the
// interrupt structure is a deliberately separate, deferred addition (issue #26).
//
// RAW, 8-BIT CLEAN. The bytes the guest sends are the bytes on the line, control
// codes and all (CR/LF/SO/DC1/DC3/DEL). No transform chain -- that is the console's
// alone (DESIGN.md 7.2, host/filter.h). Where those bytes go -- a file, the
// console, a socket, a real serial printer -- is the operator's CONNECT, not the
// card's business (DESIGN.md 7.7).

#include "core/board.h"
#include "host/stream.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class C700Board : public Board {
public:
    C700Board();

    std::string type() const override { return "c700"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The one bit of software state -- the stored
    // interrupt-enable. The port is a strap and the output line is a host handle.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not allowed
    // to know what a socket is (DESIGN.md 7.7). Shared alias -- same resolver the
    // serial cards use, wired once in main.cpp.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // Non-owning; the card owns the stream. The MCP console reaches the line here.
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "prn" ? stream_.get() : nullptr;
    }

    // ---- The status pin, so a test can read it without going through the bus. ----
    uint8_t statusByte() const;

private:
    // CONNECT and the `connect` property share this: resolve the endpoint, remember
    // the ORIGINAL spec (so a config-relative file path round-trips), and swap the
    // line in.
    bool applyEndpoint(const std::string& endpoint, std::string& err);

    std::unique_ptr<ByteStream> stream_;             // never null -- NullStream when idle
    std::string                 connectSpec_ = "null";  // as written; what SHOW/SAVE echo
    uint8_t                     base_ = 0x02;         // even. Control at base_, data at base_+1
    bool                        intEnabled_ = false;  // control D1 -- stored, not yet acted on
};

} // namespace altair
