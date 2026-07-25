#pragma once
//
// 88-LPC -- MITS Line Printer Controller. The S-100 card that drives the Altair
// 88-LP line printer (an Okidata mechanism). See docs/boards/mits-88lpc.md and
// reference/88-LPC Printer Interface.md.
//
// A SIBLING OF THE 88-C700, BUT NOT A BYTE PIPE. Like the C700 it is two ports, an
// even/odd pair: Control at an EVEN base, Data at the odd address above it, with the
// MITS default 002 (control/status at 02, data at 03) that MITS software requires.
// But where the C700 hands the guest's byte straight to the line, the 88-LPC speaks
// a LINE-ORIENTED COMMAND protocol:
//
//   - The DATA channel (odd) loads a 6-BIT character CODE into the printer's
//     80-character line buffer, one at a time. The code is not finished ASCII: the
//     64-char set (0x20..0x5F) is packed into six bits with bit 6 = complement of
//     bit 5, so 0x20->space, 0x00->'@', 0x01->'A'. We decode it to the glyph the
//     printer would strike. (That packing is inferred -- the Okidata glyph chart is
//     not in the LPC manual -- but it matches the manual's own test program, whose
//     space is 100000 octal = 0x20. See the reference doc and docs/sources.md.)
//
//   - The CONTROL channel (even) is ACTIVE-HIGH COMMAND STROBES, not a static word:
//     D0 PRINT, D1 LINE FEED, D2 CLEAR, D3 interrupt-enable. A line commits on PRINT
//     or when the buffer fills to 80 characters (auto-print); printing advances the
//     paper, so a printed line becomes a text line + '\n'. LINE FEED is a bare '\n';
//     CLEAR discards the pending line.
//
// SO THIS CARD IS A PRINT MECHANISM, DELIBERATELY -- and that is the one real
// departure from the C700's "the line is raw, transforms are the console's" rule
// (DESIGN.md 7.2). The C700 can be raw because its wire carries finished bytes whose
// line breaks are data. The 88-LPC's wire carries 6-bit codes whose line breaks are
// COMMANDS; there is no byte-transparent reading of it, so decoding codes to glyphs
// and commands to lines is the ONLY faithful model, not an invented transform. Where
// the resulting text goes -- a file, the console, a socket, a printer: queue -- is
// still the operator's CONNECT, not the card's business (DESIGN.md 7.7).
//
// POLLED. The real card has full hardware interrupt capability (after each line, via
// the 88-VI or single-level PINT). That path is NOT modeled -- the interrupt-enable
// bit software wrote is stored, but no request is raised and no wire is pulled. A
// polled driver (load the buffer, poll BUFFER EMPTY, PRINT) is complete; the
// interrupt structure is the same deliberate deferral as the C700 (issue #26).

#include "core/board.h"
#include "host/stream.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class LpcBoard : public Board {
public:
    LpcBoard();

    std::string type() const override { return "lpc"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The software-visible state is the stored
    // interrupt-enable AND the printer's pending line buffer. The port is a strap and
    // the output line is a host handle.
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
    // serial and C700 cards use, wired once in main.cpp.
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

    // Commit the pending line: the decoded characters, then the paper advance ('\n').
    void printLine();

    std::unique_ptr<ByteStream> stream_;                // never null -- NullStream when idle
    std::string                 connectSpec_ = "null";  // as written; what SHOW/SAVE echo
    std::string                 lineBuf_;                // the printer's 80-char buffer, decoded
    uint8_t                     base_       = 0x02;      // even. Control at base_, data at base_+1
    bool                        intEnabled_ = false;     // control D3 -- stored, not yet acted on

    static constexpr size_t kLineWidth = 80;            // auto-print when the buffer fills
};

} // namespace altair
