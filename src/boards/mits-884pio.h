#pragma once
//
// 88-4PIO -- MITS 4-Port Parallel Input/Output Board. See docs/boards/mits-884pio.md
// and reference/MITS 88-4PIO.md.
//
// A 6820 PIA REGISTER FILE, where the 88-PIO is a bare latch pair. Each board can
// be populated with up to four Motorola 6820s (ICs J, K, L, M); each 6820 has two
// independent SECTIONS, A and B; and each section is a control/status register, a
// data-direction register (DDR) and a data register. Unlike the fixed-direction
// 88-PIO, the direction of every one of the eight data lines is SOFTWARE-set, which
// is what the DDR is for.
//
// 16 ADDRESSES PER BOARD (4 per port), on a 16-address boundary:
//
//   addr = base + port*4 + section*2 + reg
//     port    (A3,A2) -> 0=J 1=K 2=L 3=M
//     section (A1)    -> 0=A 1=B
//     reg     (A0)    -> 0=control/status, 1=data-or-DDR
//
// The data address reaches the DATA register or the DDR depending on control-
// register bit 2 (0 = DDR, 1 = data). Writing 0 to a DDR bit makes that line an
// input; writing 1 makes it an output. Power-on clears every register, so all lines
// come up as inputs.
//
// CONNECTABLE LINES, like the 88-PIO's out/in but one per section: `ja`,`jb` for
// port J, `ka`,`kb`, `la`,`lb`, `ma`,`mb` -- only for the ports that are populated
// (`ports`). CONNECT each to a file, the console, a socket, whatever the operator
// likes (DESIGN.md 7.7).
//
// PRAGMATIC 6820. Modeled: the control/status register, the DDR, the data register,
// and status bit 7 (the C1/IRQ1 flag) driving the standard poll -- set when a byte
// arrives, cleared when the guest reads the data register. STORED but not simulated:
// the C1/C2 control bits (a guest may write them freely) and the CA2/CB2 output-
// strobe timing -- invisible through a byte-stream endpoint. POLLED, like the C700:
// the enable bits are kept but no interrupt wire is pulled (issue #26). DDR is
// stored and reported; a byte endpoint carries all 8 bits, so per-bit direction
// masking is not applied.

#include "core/board.h"
#include "host/stream.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Pio4Board : public Board {
public:
    Pio4Board();

    std::string type() const override { return "4pio"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void pump() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    ByteStream* unitStream(const std::string& unit) override;

    static constexpr int kMaxPorts = 4;
    static constexpr int kSections = kMaxPorts * 2;  // two per 6820

private:
    // One 6820 section: control/status + DDR + data, plus the input latch and the
    // line it is CONNECTed to.
    struct Section {
        uint8_t ctrl    = 0;      // control/status: bits 5..0 stored; 7,6 = live IRQ flags
        uint8_t ddr     = 0;      // data-direction: 0 bit = input line, 1 = output
        uint8_t outReg  = 0;      // the byte last written to the data lines
        uint8_t inLatch = 0;      // the byte the input side last received
        bool    inFull  = false;  // status bit 7 -- a byte has arrived, unread
        std::unique_ptr<ByteStream> stream;  // never null (NullStream when idle)
        std::string spec = "null";
    };

    // Unit name ("ja".."mb") -> section index, or -1 if not a populated section.
    int sectionIndex(const std::string& unit) const;
    static std::string unitName(int idx);
    bool applyEndpoint(const std::string& unit, const std::string& endpoint,
                       std::string& err);

    std::array<Section, kSections> sec_;
    uint8_t base_  = 0x20;  // 16-aligned. Port J at base, K at base+4, ...
    int     ports_ = 1;     // how many 6820s are populated (1..4)
};

} // namespace altair
