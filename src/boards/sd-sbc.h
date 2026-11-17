#pragma once
//
// SD Systems SBC-100 / SBC-200 -- a Z80 single-board computer on the S-100 bus.
// See reference/SD Systems SBC-100 & SBC-200.md.
//
// The real card is a whole computer -- CPU, an 8251 USART, a Z80-CTC, a parallel port,
// RAM and boot-PROM sockets, and it is the bus master. This board models the parts the
// SD software actually touches, all in one 8-port I/O block (78-7F) plus an optional
// onboard PROM:
//
//   - THE 8251 CONSOLE at BASE (data) / BASE+1 (status/command), etch 7C/7D. Its RxD is
//     strapped to /DSR so MSMONR21 can auto-baud (see Intel8251/DsrSource).
//   - THE Z80-CTC at the block's low four ports (78-7B), modeled as the ONE thing that
//     is observable under the flat-out clock: the console keyboard interrupt. SD CP/M's
//     CBIOS (CONIO) arms CTC channel 1 for a mode-2 vectored interrupt whose trigger is
//     the 8251's RxRDY -- each received byte raises /INT with vector `base|2` (0x82),
//     and the ISR reads the data port. The baud/timer channels are not observable at
//     flat-out speed, so the Ctc struct below models only the vector + the arm bit and
//     absorbs the rest. It can graduate to a real src/chips/z80ctc.* if a timer ever
//     becomes observable.
//   - THE PARALLEL PORT at 7E/7F. `OUT 7F` bit 1 is the SBC-200 memory switch-out: it
//     drops the onboard PROM out of the map so RAM shows through (CP/M's cold boot does
//     this for a 64K system). Modeled as a phantom overlay, the same shape as the
//     Turnkey board's boot PROM (boards/mits-turnkey.h).
//   - THE ONBOARD MEMORY: four PROM sockets via [[board.socket]] (at + mount) and 1K of
//     static RAM. Where they sit is jumpered (X1/X3, manual Tables 2-3 to 2-5): `rom_size`
//     and `bank` pick eight slots, a socket goes in one of the first seven and the RAM is
//     the eighth. The etch (2K, bank 3) puts MSMONR21 at E000, DDBIOS at F000 and the RAM
//     at F800, over a plain 64K RAM board.
//   - AUTO-START (X16/X17/X18, manual Table 2-6). After a reset the board supplies
//     A12-A15 from the `start` jumpers, so the Z80 -- which really does start at 0000 --
//     reads the PROM at `start`. The PROM opens with JP X003 / IN A,(7FH); the read of
//     port 7F releases the override. Nothing is injected and nobody writes the PC.
//
// It is the structural twin of the 88-SIO (boards/mits-88sio.h): one UART embedded
// DIRECTLY as a member, with the card owning refresh()/nextEdge()/wake_. It is NOT a
// Sio2Port card -- the 8251 puts data at the LOW port and status/command at the HIGH
// port, the reverse of the 6850 section, and the RxD->/DSR auto-baud strap is silicon
// behavior that belongs on this card.

#include "chips/intel8251.h"
#include "core/board.h"
#include "core/hex.h"

#include <cstdint>
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

    // ---- the bus: the 78-7F I/O block, plus the onboard PROM's memory reads ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    // The clocked half: the auto-start release (a read of port 7F) and the write that
    // lands in the onboard RAM. The board does not DECODE that write -- the off-board RAM
    // takes it too, as the manual says, and two boards on one write is contention.
    bool wantsSnoop() const override { return true; }
    void snoop(const BusCycle& c) override;

    // THE KEYBOARD INTERRUPT. /INT is pulled when the CTC has armed channel 1 and the
    // 8251 has a byte waiting -- a level, cleared when the ISR reads the data port. The
    // vector is supplied by claiming the IntAck cycle (in decodes()/read()), not here
    // (DESIGN.md 4.4). This is a Z80 mode-2 vectored interrupt, not an S-100 VI line.
    bool    assertsInt() const override;
    uint8_t assertsVi() const override { return 0; }

    // The CTC's keyboard channel can be triggered from OFF this card -- a VDB-8024 video
    // console pulls an S-100 VI line -- so the card watches the VI wires and re-derives
    // /INT when they move (the bus calls intChanged() for a watcher). See ch1Triggered().
    bool    watchesVi() const override { return true; }

    // THE ONBOARD PROM SHADOWS RAM while it is switched in -- reads come from the PROM,
    // writes fall through to the RAM under it (which is why the RAM card is
    // honors_phantom = read). The same mechanism as the Turnkey boot PROM.
    bool assertsPhantom(const BusCycle& c) const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    uint64_t rxBytes() const override { return u_.rxBytes(); }
    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<Property> boardUnitProperties(const std::string& unit) override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<MapEntry> memMap() const override;

    // ---- the onboard PROM sockets: `[[board.socket]]` (at + mount), like the Turnkey ----
    std::vector<std::string> subUnitTables() const override { return {"socket"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

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

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    // Everything that could have moved a deadline has just happened: advance the
    // receiver and re-arm the one alarm for the next moment the chip changes on its
    // own (a frame completing, the transmitter draining).
    void     refresh();
    uint64_t nextEdge() const;

    // ---------------------------------------------------------------------------
    // THE Z80-CTC, AS MUCH OF IT AS IS OBSERVABLE. The chip has four counter/timer
    // channels, a vector register and a mode-2 daisy chain; at flat-out speed the only
    // thing a guest can read back is the ONE interrupt SD CP/M uses -- channel 1's
    // keyboard interrupt, whose trigger is the 8251's RxRDY. So this models exactly
    // that: the vector base (written to channel 0 with D0=0) and whether channel 1 is
    // armed (a control word with D7, the interrupt-enable bit). Everything else -- time
    // constants, the baud divider on channel 0 -- is absorbed and forgotten, because
    // nothing can tell. See reference/SD Systems SBC-100 & SBC-200.md and CONIO.
    // ---------------------------------------------------------------------------
    struct Ctc {
        uint8_t vectorBase_  = 0;      // channel-0 write with D0=0 (0x80 from CONIO)
        bool    ch1IntArmed_ = false;  // channel-1 control word had D7 (interrupt enable)
        bool    expectTc_[4] = {};     // last control word to channel n had D2 (TC follows)

        void writePort(uint8_t chan, uint8_t v);           // chan 0..3 -> ports 78..7B
        // Channel n's mode-2 vector is base|(n<<1); channel 1 -> base|2 (0x82).
        uint8_t ch1Vector() const { return (uint8_t)(vectorBase_ | 0x02); }
    };
    Ctc ctc_;

    // Is the CTC's keyboard channel (ch1) being triggered? On this card ch1 is "the
    // console keyboard channel": in the serial build the trigger is the 8251's own RxRDY;
    // in the video build (a VDB-8024 console) the keyboard strobe arrives on an S-100 VI
    // line -- reference/SD Systems SBC-100 & SBC-200.md 6 straps CTC ch1 <- VI2, and the
    // VDB pulls that line while a key waits. Either source arms the same mode-2 interrupt,
    // whose vector (base|2, so 0x82 serial / 0x02 video) we supply on IntAck.
    bool ch1Triggered() const;

    // Which board this is strapped as. The serial section is the same 8251 either way;
    // the memory switch-out is present on both here (harmless on a real SBC-100, which
    // simply lacks the latch -- but no SBC-100 machine uses the socket overlay).
    enum class Variant { Sbc100, Sbc200 };
    Variant variant_ = Variant::Sbc200;

    // The I/O block. The real card jumpers the 8-port window (78-7F on the etch); the
    // 8251 data port is `base_` (etch 7C) and the block starts four ports below it, so
    // CTC = 78-7B, USART = 7C-7D, parallel = 7E-7F.
    uint8_t base_ = 0x7C;
    uint8_t blockBase() const { return (uint8_t)(base_ - 4); }

    // The console USART. Its RxD is strapped to /DSR (set in the constructor) -- the
    // whole point of this card, and what the monitor's auto-baud watches.
    Intel8251 u_{"tty"};

    // ---- where the onboard memory sits: the X1/X3 jumpers (manual Tables 2-3 to 2-5) ----
    // X1 sets the size of a socket (1K/2K/4K/8K) and the BANK: eight slots of that size.
    // X3 puts a device in a slot: ROM 0/1/2 in slot 0/1/2 (low) or 4/5/6 (high), ROM 3 in
    // slot 3, the RAM in slot 7. The etch is 2K, bank 3: C000-FFFF, monitor in slot 4
    // (E000), disk BIOS in slot 6 (F000), RAM in slot 7 (F800).
    static constexpr int kSlots   = 8;
    static constexpr int kRamSlot = 7;
    static constexpr int kRamSize = 1024;
    int  romSizeLog_ = 1;   // 0..3 -> 1K, 2K, 4K, 8K
    int  bank_       = 3;
    bool ram_        = true;  // X3-15 to X3-16: the RAM is jumpered in

    uint32_t romBytes() const { return (uint32_t)1024 << romSizeLog_; }
    int      bankCount() const { return 8 >> romSizeLog_; }
    bool     bankValid() const { return bank_ < bankCount(); }
    uint32_t slotAddr(int n) const { return ((uint32_t)bank_ * kSlots + (uint32_t)n) * romBytes(); }
    // Which slot starts at `at`, or -1. Only meaningful while bankValid().
    int      slotAt(uint16_t at) const;
    // Why a socket cannot sit at `at` under the jumpers as they are -- empty if it can.
    // `skip` is the index of a socket to leave out of the one-socket-two-slots check.
    std::string socketFault(uint16_t at, size_t skip) const;
    bool inRamSlot(uint16_t a) const {
        return ram_ && bankValid() && a >= slotAddr(kRamSlot) &&
               (uint32_t)a < slotAddr(kRamSlot) + romBytes();
    }

    // WHICH ONBOARD BYTE ANSWERS A READ AT `a`? `t` is the address the onboard decode
    // sees: `a` itself, or -- while auto-start is armed -- `a` with A12-A15 replaced by
    // the `start` jumpers.
    enum class Onboard { None, Prom, Ram };
    Onboard onboard(uint16_t a, uint16_t& t) const {
        if (!promArmed_) return Onboard::None;
        t = autoStartArmed_ ? (uint16_t)(start_ | (a & 0x0FFF)) : a;
        if (promPresent_[t]) return Onboard::Prom;
        if (inRamSlot(t)) return Onboard::Ram;
        return Onboard::None;
    }

    // ---- the onboard PROMs (host-backed config, re-read on power; not serialized) ----
    void loadProm();  // (re)read the socket ROMs into prom_/promPresent_

    struct Socket {
        uint16_t    at;      // where the socket sits (E000, F000, ...)
        std::string mount;   // "builtin:msmonr21", a HEX/BIN path, ...
    };
    std::vector<Socket> sockets_;      // config (rebuilt from TOML)
    static constexpr int kMemSize = 0x10000;
    uint8_t prom_[kMemSize] = {};
    bool    promPresent_[kMemSize] = {};

    // ---- the onboard 1K static RAM (travels in a snapshot) ----
    // It answers its whole slot, so with a 2K slot the 1K shows twice. Neither reset
    // touches it; POWER fills it (DESIGN.md 6).
    uint8_t ramStore_[kRamSize] = {};

    // ---- auto-start: the X16/X17/X18 jumpers (manual Table 2-6) ----
    // A 4K boundary; 0000 = the board supplies nothing and the Z80 runs from 0000.
    uint16_t start_ = 0;

    // ---- runtime latches (travel in a snapshot) ----
    // The onboard memory (PROM + 1 KB RAM) is switched IN after reset; `OUT 7F` bit 1
    // switches it out (A=2), bit-1-clear switches it back in (A=0).
    bool promArmed_ = true;
    // Set by either reset when `start` is not 0000; cleared by a read of port 7F.
    bool autoStartArmed_ = false;

    std::vector<std::string> log_;
    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
