#pragma once
//
// SSM PB1 -- 2708/2716 EPROM programmer & 4K/8K on-board EPROM board (Solid State Music /
// SSM Microcomputer Products, 1978). reference/SSM PB1 EPROM Programmer.md,
// docs/boards/ssm-pb1.md. altairsim's first PROM-burner board (issues #397, #382).
//
// ONE CARD, TWO JOBS -- a hybrid decode, like the iCOM interface board:
//
//   1) THE PROGRAMMER. A 4K memory window (default D000) reserved for two programming
//      sockets, U22 (a 2708, 1K) and U23 (a 5-volt 2716, 2K), plus ONE output port
//      (default 10H) that arms the board and picks the chip. The burn is SOFTWARE-DRIVEN:
//
//        OUT 10,01   ; arm + select 2708 timing (D0=1)   -- or 02 for the 2716 (D1=1)
//        (write bytes into the window)                   ; each STAX/MOV M -> one cell burned
//        (read a byte from the window)                   ; RESETS the flip-flop (LED off)
//
//      On the real card a window write stretches into a multi-millisecond programming
//      pulse held by the S-100 READY line; here a write just lands (see the header note in
//      the .cpp and docs/boards/ssm-pb1.md "Limitations" -- pulse timing / wait states /
//      the SW1 voltage gate carry no guest-visible state, so they are not modeled).
//      Programming an EPROM cell can only clear a 1 to a 0, so a burn ANDs into the socket
//      buffer (buf &= data) -- exactly what a real erased-then-programmed chip does.
//
//   2) THE ON-BOARD READ-ONLY EPROM AREA. U11-U14, mappable above 8000H, hold 4K (2708)
//      or 8K (2716) of read-only firmware. Declared with [[board.prom]] (at + mount);
//      decodes reads only, ignores writes, re-read from the host image on power -- the
//      plain ROM idiom (the memory card's `rom` region).
//
// Getting the burn OUT as a host hex file needs no code here: the burned window is normal
// readable memory on the bus, so the monitor's `SAVE EPROM.HEX D000 D3FF` reads it back and
// writes Intel HEX (src/core/hex.cpp saveHex). That is the "make a hex file" of issue #382.

#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Pb1Board : public Board {
public:
    std::string type() const override { return "pb1"; }

    // ---- bus: one output port + the 4K programming window + the on-board PROM area ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<MapEntry> memMap() const override;

    // ---- units / [[board.socket]] (programmer) + [[board.prom]] (on-board area) ----
    std::vector<std::string> subUnitTables() const override { return {"socket", "prom"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;
    std::vector<UnitDef>     units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    // ---- the two programming sockets -----------------------------------------------------
    enum class Chip { None, C2708, C2716 };
    static constexpr uint32_t k2708 = 1024;   // U22 -- 1K
    static constexpr uint32_t k2716 = 2048;   // U23 -- 2K
    static constexpr uint32_t kWindow = 0x1000;  // the 4K block the sockets sit in

    // ---- config / straps (rebuilt from TOML, never serialized) --------------------------
    uint16_t port_   = 0x10;     // control port; only A4-A7 decode, so it is an x0H address
    uint16_t window_ = 0xD000;   // the 4K programming-socket window, on a 4K boundary

    struct Socket {              // U22 (2708) and U23 (2716): writable, burned by the guest
        std::vector<uint8_t> buf;      // chip-sized, 0xFF where erased
        std::string          mount;    // preload image (as-written, for CONFIG SAVE)
    };
    Socket sock2708_{std::vector<uint8_t>(k2708, 0xFF), ""};
    Socket sock2716_{std::vector<uint8_t>(k2716, 0xFF), ""};

    struct Prom {                // U11-U14: read-only on-board firmware, re-read on power
        uint16_t             at   = 0x8000;
        uint32_t             size = 0;
        std::string          mount;    // as-written (file or builtin:)
        std::string          mountFile;// resolved for opening
        std::vector<uint8_t> bytes;
    };
    std::vector<Prom> proms_;

    // ---- runtime state (travels in serialize) -------------------------------------------
    bool  armed_ = false;        // the programming flip-flop (D2 LED). Set by OUT, reset by a
                                 // window read or power-on-clear
    Chip  type_  = Chip::C2708;  // which socket the window currently maps to (D0/D1 latch)

    // ---- helpers ----
    bool inWindow(uint16_t a) const { return a >= window_ && a < window_ + kWindow; }
    int  promAt(uint16_t a) const;   // index of the on-board PROM socket covering a, or -1
    Socket*       active();          // the socket the type latch selects
    const Socket* active() const;
    void  loadSocket(Socket& s);   // (re)load a socket from its mount, else erase (logs on error)
    void  loadProm(Prom& p);       // (re)load a read-only PROM socket image (logs on error)
    void  say(std::string s) { log_.push_back(std::move(s)); }

    std::vector<std::string> log_;
};

} // namespace altair
