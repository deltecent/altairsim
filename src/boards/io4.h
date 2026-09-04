#pragma once
//
// SSM IO-4 (2P + 2S) -- the real, fully-emulated Solid State Music I/O board.
// See docs/manual/boards.md and reference/SSM IO-4 2P+2S IO Board.md.
//
// THIS IS NOT THE gsio STRAP BOARD. `gsio` (src/boards/gsio.h) is the generic
// chip-LESS strap-serial engine -- "describe a status bit, read/write a data byte".
// THIS card is the opposite: a specific 1970s product modeled register- and
// pin-for-pin, built on the real 1602-family UART (src/chips/uart1602.h) the board
// actually carried (U9 = Serial A, U8 = Serial B; a TMS6011 / AY5-1013 / TR-1602).
//
// The IO-4 puts two full-duplex serial channels AND a four-port parallel section on
// one S-100 card. The SERIAL section is two real UART channels with programmable word
// length / parity / stop bits, at a 4-port block set by switch S3, plus the full
// status-word strap-up (headers W1/W2) that lets each channel imitate almost any other
// card's status port. The PARALLEL section is four 8212 latched ports -- two in
// (J4/J6), two out (J3/J5) -- with a service-request flip-flop per input, on their own
// 2-port block set by switch S4.
//
// INTERRUPTS ARE STRAPS ON HEADER W4 (section 3.3). Six sources -- each serial channel's
// receive (DAV) and transmit (TBMT), and each parallel input's service request -- are each
// jumpered to a VI line, to pin 73 (int), or to nothing. THERE IS NO SOFTWARE INTERRUPT
// ENABLE on this card (unlike the 88-SIO's IC-B flip-flops): the installed jumper IS the
// enable, and it wires the source's LEVEL straight to the bus. A parallel input interrupt
// rises on the external strobe even when the port is not addressed. The canonical W4 map is
// VI1/VI0 = Serial A/B RX, VI2/VI3 = Serial A/B TX, VI6/VI5 = Parallel in A/B -- but the
// straps are per-unit and default to `none`, so a stock io4 raises nothing and boots polled.
//
// TWO HALVES, ADDRESSED EXCLUSIVELY (reference "At a glance"): each section decodes its
// own block, and if the two blocks are ever set to OVERLAP, NEITHER section responds in
// the contended ports -- a deliberate mutual-exclusion, not a bus fight. sectionAt()
// arbitrates that per port.
//
// The serial section answers as a 4-PORT BLOCK on a 4-port boundary (S3 decodes
// A7-A2 -- one switch for the whole section, so `port` is a BOARD property, not a
// per-channel one). In the default 0-3 layout: Serial A status/data at 0/1, Serial B
// at 2/3. Each channel's word format (S1 = Serial B, S2 = Serial A on the real card)
// is a per-unit strap: `[board.unit.a]` / `[board.unit.b]`.
//
// The parallel section answers as a 2-PORT BLOCK on a 2-port boundary (S4 decodes
// A7-A1, board property `par_port`, default 4-5). Parallel A = J6-in / J5-out at
// PAR+0, Parallel B = J4-in / J3-out at PAR+1. Each port's line and its status straps
// are a per-unit table: `[board.unit.pa]` / `[board.unit.pb]`.
//
// THE STATUS PORT IS SHAPED, NOT FIXED. Six UART status signals (DAV, ROR, RPE, RFE,
// TEOC, TBMT) come to a 16-pin header (W2 = Serial A, W1 = Serial B) and can be jumpered
// to ANY data-bus bit, in either polarity (the status buffer is a 74367 for positive
// sense or a 74368 for negative -- U18 = Serial A, U16 = Serial B). The two port
// addresses of a channel can also be swapped (S1/S2-PR). Each channel therefore carries
// a status map, a polarity bit and a port-reversal bit, and a `profile` selector presets
// all three from a documented host personality (the SSM 8080 monitor, an 8251, an Altair
// SIO, a Processor Technology or IMSAI port). The default profile is `altair-rev1` -- the
// strapping the SSM 8080 System Monitor's console expects, which is why a stock io4 boots it.
//
#include "chips/uart1602.h"  // the 1602-family UART -- A CHIP IS NOT A CARD (DESIGN.md 7.8)
#include "core/board.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Io4Board : public Board {
public:
    Io4Board();
    ~Io4Board() override;

    std::string type() const override { return "io4"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // PIN 73 and the VI lines (DESIGN.md 4.4.1), combinational and pure. Header W4: six
    // sources, each strapped independently, so this card can be pulling several lines at
    // once -- which is why assertsVi() is a bitmask. There is NO software enable to AND
    // against: an installed strap wires the source's level straight to the wire.
    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // Both channels sum into the run loop's live-traffic signal (Board::rxBytes).
    uint64_t rxBytes() const override { return a_.uart.rxBytes() + b_.uart.rxBytes(); }

    // What either wire said when the card tried to program its straps into it.
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    // Install a PRE-BUILT stream (the --mcp console's filtered scripted line) on a named
    // channel. Body out-of-line (.cpp): a by-value unique_ptr<ByteStream> needs the complete
    // type to destroy, and MSVC instantiates the deleter at the declaration otherwise.
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;

    // The monitor resolves an endpoint string to a stream; the BOARD never learns what a
    // socket is (DESIGN.md 7.7). Installed once in each main.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The six UART status signals brought to the W1/W2 status header, in header-pin order.
    // Each may be jumpered to any data-bus bit or left unconnected. PUBLIC so the profile
    // table in io4.cpp can name them (Io4Board::Dav ...); `kNumStat` is the count and the
    // status-map array size.
    enum StatSig { Dav, Ror, Rpe, Rfe, Teoc, Tbmt, kNumStat };

private:
    // ---- ONE SERIAL CHANNEL: a real UART plus the straps that shape its status port. ----
    // U9 = Serial A, U8 = Serial B. The UART is TRUE SENSE; everything that makes the status
    // byte look like some other card (the map, the polarity, the address order) is the CARD's
    // and lives out here (DESIGN.md 7.8), exactly as the 88-SIO's inversion does.
    struct SerialChannel {
        explicit SerialChannel(const char* n) : uart(n) {}
        Uart1602 uart;
        // W1/W2: each status signal -> a data-bus bit (0-7), or -1 = not jumpered (drives
        // no bit). Indexed by StatSig.
        int  statBit[kNumStat] = {-1, -1, -1, -1, -1, -1};
        // U16/U18: a 74368 (negative sense) inverts every driven status bit; a 74367
        // (positive sense) does not. ONE polarity for the whole channel's status byte.
        bool invert = false;
        // S1/S2-PR: swap the status and data port addresses within this channel.
        bool portReversal = false;

        // Header W4: where this channel's two interrupt sources are jumpered. RX rises
        // with DAV (a new character), TX while TBMT (the transmit buffer is empty). No
        // software enable -- the strap is the enable. Default `none` = not wired.
        IrqJumper rxIrq = IrqJumper::None;  // canonical W4: VI1 (A) / VI0 (B)
        IrqJumper txIrq = IrqJumper::None;  // canonical W4: VI2 (A) / VI3 (B)
    };

    // The channel a port belongs to, and whether it is the DATA port (vs status/control),
    // honoring that channel's port-reversal strap. False if the port is not ours.
    bool decodePort(uint8_t port, SerialChannel*& ch, bool& isData) const;

    // The channel named by a unit ("a"/"b"), or null.
    SerialChannel*       channel(const std::string& unit);
    const SerialChannel* channel(const std::string& unit) const;

    // The status byte the CPU reads for a channel: compose the mapped signals onto their
    // data bits, each XORed against the channel's polarity (74368 inverts). An unjumpered
    // bit reads 0.
    uint8_t statusByte(const SerialChannel& ch) const;

    // Preset a channel's status map + polarity + port-reversal from io4Profiles()[idx]; and
    // the inverse -- the name of the profile the current straps match, or "custom".
    void        applyProfile(SerialChannel& ch, int idx);
    std::string profileName(const SerialChannel& ch) const;

    // The straps/format/connect properties for ONE channel, captured by pointer (members
    // never move). Shared by unitProperties("a") and ("b").
    std::vector<Property> channelProperties(SerialChannel& ch);

    // Push a channel's word-format straps at a real serial port (ignored by every other
    // endpoint); collect any refusal into the board log.
    void programChannel(SerialChannel& ch);

    // ---- ONE PARALLEL PORT: an 8212 input latch + an 8212 output latch, plus its line. ----
    // Parallel A = J6-in (U13) / J5-out (U12); Parallel B = J4-in (U11) / J3-out (U10). The
    // input side latches a byte on the external strobe and sets the service-request FF; the
    // CPU reading the port acknowledges (clears it). The output side latches under CPU control.
    // A single bidirectional line carries both directions to the endpoint: a WRITE is the
    // output latch, and a byte the far end sends (readable) is the strobed-in input byte.
    struct ParallelChannel {
        explicit ParallelChannel(const char* n) : name(n) {}
        std::string                 name;
        std::unique_ptr<ByteStream> stream;  // never null (NullStream when idle)
        uint8_t                     inLatch = 0;      // 8212 input latch -- last byte strobed in
        uint8_t                     outReg  = 0;      // 8212 output latch -- last byte the CPU wrote
        bool                        srq     = false;  // service-request FF: a byte is latched, unread

        // §3.2.2 status/data "8080 console" idiom: strap a data-available flag onto a bit of
        // THIS port's read, sourced from this port's OR its sibling's service-request FF. This
        // is external J-connector wiring (e.g. J4-2 -> J6-9), so it is a strap, not hardwired.
        int  davBit         = -1;     // data-bus bit the flag lands on when this port is read, or -1 = none
        bool davFromSibling = false;  // whose service request: false = this port's, true = the other's
        bool davActiveLow   = false;  // §3.2.2 "flag = D0 going low": present it active-low

        // Header W4: where this port's input interrupt is jumpered. Rises with the service
        // request FF (a byte strobed in), even when the port is not addressed. No software
        // enable -- the strap is the enable. Default `none` = not wired.
        IrqJumper inIrq = IrqJumper::None;  // canonical W4: VI6 (A) / VI5 (B)
    };

    // Which section (if any) answers a port, honoring the two blocks' mutual exclusion: a port
    // BOTH blocks would decode is dead on both. The one place the overlap rule lives.
    enum Sect { SectNone, SectSerial, SectParallel };
    Sect sectionAt(uint8_t port) const;

    // The parallel port named by a unit ("pa"/"pb"), or the port a decoded address lands on.
    ParallelChannel*       parallelChannel(const std::string& unit);
    ParallelChannel*       parallelAt(uint8_t port);
    ParallelChannel&       sibling(ParallelChannel& pc);  // the other parallel port

    // The byte the CPU reads at a parallel input port (the latched byte, with any strapped
    // data-available flag overlaid) -- and it acknowledges, clearing this port's service request.
    uint8_t parallelRead(ParallelChannel& pc);
    // Latch a byte out on a parallel output port and send it down the line.
    void    parallelWrite(ParallelChannel& pc, uint8_t v);
    // The line + status straps for one parallel port. Shared by unitProperties("pa"/"pb").
    std::vector<Property> parallelProperties(ParallelChannel& pc);

    // ---- Interrupts (header W4). THE CARD'S OWN CLOCK (DESIGN.md 4.4.1, 7.5). ----
    // Re-drive pin 73 and the VI lines from the live UART/service-request state, then set an
    // alarm for the next moment a strapped source could move on its own with nobody touching
    // the card -- a transmitter draining to TBMT, or a paced character arriving at the receiver.
    void     refresh();
    // The next T-state a strapped source could change on its own; 0 = never. Strictly future.
    uint64_t nextEdge() const;

    // ---- THE TWO SERIAL CHANNELS. U9 = Serial A, U8 = Serial B. ----
    SerialChannel a_{"a"};
    SerialChannel b_{"b"};

    // ---- THE TWO PARALLEL PORTS. Parallel A = J6/J5, Parallel B = J4/J3. ----
    ParallelChannel pa_{"pa"};
    ParallelChannel pb_{"pb"};

    // ---- Switch S3: the serial 4-port block base (A7-A2), snapped to a 4-boundary. ----
    uint8_t base_ = 0x00;  // Serial A at base_+0/+1, B at base_+2/+3

    // ---- Switch S4: the parallel 2-port block base (A7-A1), snapped to a 2-boundary. ----
    uint8_t par_base_ = 0x04;  // Parallel A at par_base_+0, B at par_base_+1

    // The alarm this card sets for its own interrupt edges (a draining transmitter, a paced
    // receive). Cancelled in the destructor -- a deadline firing into a freed board is a UAF.
    Clock::Handle wake_ = Clock::kNone;

    std::vector<std::string> log_;
};

} // namespace altair
