#pragma once
//
// 88-PIO -- MITS Parallel I/O Board. The S-100 card that interfaces any 8-bit
// parallel device to the Altair 8800. See docs/boards/mits-88pio.md and
// reference/MITS 88-PIO.md.
//
// A BIDIRECTIONAL LATCH PAIR, AND THAT IS THE WHOLE OF IT. Where the 88-C700 is a
// bare output latch (a printer sends nothing back), the 88-PIO carries BOTH an
// output latch and an input latch -- the two 8212s (IC G out, IC H in) -- so it
// drives a printer AND reads a keyboard. It is discrete TTL, not a PIA: the
// direction is fixed in hardware, there is no data-direction register to program
// (that is the 88-4PIO's job -- see mits-884pio.h).
//
// Two ports, an even/odd pair like the C700: Control/Status at an EVEN base, Data
// at the odd address above it. A0 picks the channel and is not part of the card
// decode, so the base is always even.
//
//   IN  <even>  status: DI0 = the output device is ready, DI1 = the input device
//                       has sent a byte the guest may now read
//   OUT <even>  control: DO0/DO1 = enable the output/input device interrupt
//   IN  <odd>   data: the byte the input device last latched
//   OUT <odd>   data: a byte to the output device, verbatim
//
// TWO CONNECTABLE LINES, like the Sol-PC's keyboard and printer (proctech-sol):
// `out` is the output device, `in` is the input device. CONNECT each to whatever
// the operator likes -- a file to capture a printout, the console for a keyboard,
// a socket or a loopback for general-purpose I/O. Where those bytes go is the
// operator's CONNECT, not the card's business (DESIGN.md 7.7).
//
// POLLED. The real card also has an interrupt structure (the DO0/DO1 enable bits,
// jumpered to the 88-VI or to pin 73). That path is NOT modeled here -- the enable
// bits are stored so a snapshot round-trips them and a future interrupt model can
// read them, but no request is raised and no wire is pulled. A polled driver
// (poll DI0/DI1, move a byte) is complete. Same deliberately-deferred stance as
// the C700 (issue #26).
//
// RAW, 8-BIT CLEAN. The bytes are the bytes on the line, control codes and all.
// No transform chain -- that is the console's alone (DESIGN.md 7.2).

#include "core/board.h"
#include "host/stream.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class PioBoard : public Board {
public:
    PioBoard();

    std::string type() const override { return "pio"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The software-visible state: the input latch
    // and its full flag, and the stored interrupt-enable bits. The port is a strap
    // and the two lines are host handles.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not allowed
    // to know what a socket is (DESIGN.md 7.7). Same resolver the serial cards use,
    // wired once in main.cpp.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // Non-owning; the card owns the streams. The MCP console reaches a line here.
    ByteStream* unitStream(const std::string& unit) override;

    // ---- The status pin, so a test can read it without going through the bus. ----
    uint8_t statusByte() const;

private:
    // CONNECT and a unit's `connect` property share this: resolve the endpoint,
    // remember the ORIGINAL spec (so a config-relative file path round-trips), and
    // swap the named line in.
    bool applyEndpoint(const std::string& unit, const std::string& endpoint,
                       std::string& err);
    const std::string& specOf(const std::string& unit) const;

    std::unique_ptr<ByteStream> out_;   // the output device -- never null (NullStream idle)
    std::unique_ptr<ByteStream> in_;    // the input device  -- never null
    std::string outSpec_ = "null";      // as written; what SHOW/SAVE echo
    std::string inSpec_  = "null";
    uint8_t     base_    = 0x04;         // even. Control/status at base_, data at base_+1
    uint8_t     inLatch_ = 0;            // the byte the input latch (8212 H) holds
    bool        inFull_  = false;        // DI1 -- a byte has arrived and not yet been read
    uint8_t     ctrl_    = 0;            // DO0/DO1 interrupt-enable -- stored, not acted on
};

} // namespace altair
