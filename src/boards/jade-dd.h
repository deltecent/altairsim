#pragma once
//
// Jade Double D -- the intelligent double-density floppy controller (Rev C, 1980). See
// reference/Jade Double D Disk Controller Rev C Manual.md and docs/boards/jade-doubled.md.
//
// A BOARD WITH ITS OWN PROCESSOR. The Double D carries a Z80A, 2 K of static RAM and a WD
// FD1791. The host never touches the FD1791: it takes the board's RAM onto the S-100 bus
// through a 1 K window, writes a command (or a whole program), gives the RAM back and
// interrupts the Z80, then waits for the Z80 to HALT. What the Z80 runs is whatever the host
// loaded -- Jade's DCM2 comes off the disk -- so this board does not know any command set. It
// runs the board Z80 against the board's own bus, and the program does the rest.
//
// THE PRIVATE MACHINE. Inside the board are a CpuZ80, a Bus and a Clock of its own. The
// private bus holds two private boards: the 2 K RAM (mirrored 32 times) and the local I/O
// (ports 00 and 04-07, decoded on A0-A2). The private clock counts BOARD T-states at the
// board's crystal (4 MHz with 8" drives, 2 MHz with 5"), and the FD1791 times itself on it.
//
// WHEN THE BOARD Z80 RUNS. Lazily. A host cycle the board decodes (its port or the window)
// first runs the board Z80 up to the host's present, converted to board T-states through
// the host clock's hz() (catchUp). Nothing the host can see changes between those cycles,
// so nothing is lost. The one exception is the optional VI interrupt: while it is strapped
// and the board Z80 runs, a host deadline catches up every millisecond so the halt reaches
// the VI line on time. A halted board Z80 (DCM2's idle state) costs nothing: its clock jumps
// straight to the next FD1791 edge or to the target.
//
// The board Z80 is the board's own and stays on emulated time. It is tied to its FD1791 by
// NMI* and the WAIT strobe, so it is never on wall time (compare the #825 rule for a board's
// free-running oscillator).

#include "boards/floppy-drive.h"
#include "chips/wd17xx.h"
#include "core/board.h"
#include "core/bus.h"
#include "core/clock.h"
#include "cpu/cpuZ80.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class JadeDdBoard : public Board {
public:
    JadeDdBoard();
    ~JadeDdBoard() override;

    std::string type() const override { return "jadedd"; }

    // ---- the host side: the status/control port and the 1 K memory window ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;
    bool    assertsPhantom(const BusCycle& c) const override;
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void configChanged() override;
    void clockAttached() override;

    std::vector<Property>    properties() override;
    std::vector<std::string> statusLines() const override;
    std::vector<MapEntry>    ioMap() const override;
    std::vector<MapEntry>    memMap() const override;

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For tests, without going through the bus.
    CpuZ80&  z80() { return cpu_; }
    Wd17xx&  fdc() { return fdc_; }
    Clock&   boardClock() { return bclk_; }
    uint8_t  ram(uint16_t a) const { return ram_[a & 0x7FF]; }
    bool     hostHasRam() const { return slvrq_; }
    int      headTrack(int d) const { return drive_[(size_t)d].drv.headTrackRaw(); }
    void     catchUp();

    // The timer's period, in board T-states. DM74LS123 data sheet (Fairchild, 2000), p. 2
    // note 3, for CX >> 1000 pF: tW = K x RX x CX with K = 0.37. R12 = 240 K, C16 = 47 uF: 4.1736 s. It runs on the
    // board clock, because what it times is the board Z80's own disk activity.
    uint64_t timerPeriod() const { return 417360000ull * (uint64_t)boardHz() / 100000000ull; }

    static constexpr int kDrives = 4;

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    // ---- the private bus's two boards ----

    // 2114 x 4: 2 K, A0-A10 decoded, so it repeats 32 times in the board Z80's 64 K. The
    // board Z80 reaches it only while it has not granted its bus (SLVACK* high), and it
    // cannot ask for it while it has, because it is stopped on BUSRQ*.
    struct Ram : Board {
        JadeDdBoard* d = nullptr;
        std::string type() const override { return "jadedd-ram"; }
        std::vector<Property> properties() override { return {}; }
        bool decodes(const BusCycle& c) const override {
            return c.type == Cycle::MemRead || c.type == Cycle::MemWrite;
        }
        uint8_t read(const BusCycle& c) override { return d->ram_[c.addr & 0x7FF]; }
        void    write(const BusCycle& c) override { d->ram_[c.addr & 0x7FF] = c.data; }
        bool    peek(uint16_t a, uint8_t& out) const override {
            out = d->ram_[a & 0x7FF];
            return true;
        }
    };

    // The board-level ports and the FD1791, as the board Z80 sees them (Table 2-3), and the
    // two wires into the Z80: INT* = ZINT, NMI* = FD1791 INTRQ gated by DSE (CR18).
    struct LocalIo : Board {
        JadeDdBoard* d = nullptr;
        std::string type() const override { return "jadedd-io"; }
        std::vector<Property> properties() override { return {}; }
        bool decodes(const BusCycle& c) const override {
            return c.type == Cycle::IoRead || c.type == Cycle::IoWrite;
        }
        uint8_t read(const BusCycle& c) override { return d->localIn(c.port()); }
        void    write(const BusCycle& c) override { d->localOut(c.port(), c.data); }
        bool assertsInt() const override { return d->zint_; }
        bool assertsNmi() const override { return d->fdc_.intrq() && (d->cmd_ & kDse); }
    };

    // Board Level Command (OUT 00, Table 2-6), a 74LS273 cleared by BR*.
    static constexpr uint8_t kDsa  = 0x01;
    static constexpr uint8_t kDsb  = 0x02;
    static constexpr uint8_t kDse  = 0x04;  // drive select enable; also gates NMI*
    static constexpr uint8_t kSero = 0x08;  // EIA output
    static constexpr uint8_t kDden = 0x10;  // double density
    static constexpr uint8_t kSid1 = 0x20;  // side, and step direction during a step

    // Board Level Status (IN 00, Table 2-5).
    static constexpr uint8_t kU0   = 0x01;
    static constexpr uint8_t kU1   = 0x02;
    static constexpr uint8_t kTst  = 0x04;
    static constexpr uint8_t kZint = 0x08;
    static constexpr uint8_t kSeri = 0x10;
    static constexpr uint8_t kToff = 0x20;
    static constexpr uint8_t kTsd  = 0x40;  // ILP* in the manual; TWO SIDED* since ECN 5B
    static constexpr uint8_t kChng = 0x80;

    uint8_t localIn(uint8_t port);
    void    localOut(uint8_t port, uint8_t v);
    void    writeControl(uint8_t v);  // the host's OUT
    uint8_t hostStatus();             // the host's IN

    // Run the board Z80 to `target` board T-states. Halted, or with its bus granted to the
    // host, it does not step: the clock jumps to the next FD1791 edge.
    void runTo(uint64_t target);
    void fdcRefresh();     // advance the FD1791 on the board clock, re-drive NMI*, re-arm
    void applySelection(); // the command latch -> the FD1791's drive, side, density, rate
    void hostWake();       // the VI deadline: catch up, re-drive VI, re-arm if still running
    void rearmHost();
    void setGranted(bool host);
    long long boardHz() const { return size8_ ? 4000000 : 2000000; }
    Clock& hostClk() const;
    bool viActive() const { return cpu_.halted() && !slvrq_ && !zint_; }
    bool windowHit(uint16_t a) const { return slvrq_ && (uint16_t)(a & 0xFC00) == window_; }

    // ---- the private machine ----
    Clock   bclk_;
    Bus     bbus_;
    CpuZ80  cpu_;
    Ram     ramBoard_;
    LocalIo ioBoard_;
    Wd1791  fdc_{"fdc"};
    uint8_t ram_[2048] = {};
    Clock::Handle fdcWake_ = Clock::kNone;

    // ---- the drives ----
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string    path;  // as WRITTEN (round-trips through SAVE)
        DiskImageDrive drv;
    };
    std::vector<Drive> drive_;

    // ---- the latches ----
    bool    slvrq_ = true;  // the host has the RAM (BR* sets it)
    bool    zint_  = true;  // interrupt to the Z80, and IA10 while the host has the RAM
    uint8_t cmd_   = 0;     // the 74LS273
    uint64_t timerEnd_ = 0; // the board T-state one-shot 0M times out at (0 = cleared)

    // ---- the host-to-board time conversion (catchUp) ----
    uint64_t  anchorH_ = 0;   // the host T-state the board has caught up to
    uint64_t  rem_     = 0;   // the remainder of the last conversion, in host-hz units
    long long lastHz_  = 0;   // the host hz() the last conversion used
    uint64_t  targetB_ = 0;   // the board T-state the board Z80 runs to
    bool      inCatchUp_ = false;
    Clock::Handle hostWake_ = Clock::kNone;

    // ---- the straps ----
    uint8_t   port_    = 0x43;    // P0, P1: 40-43h. The Jade software uses 43h
    uint16_t  window_  = 0xE000;  // M10-M12: a 1 K block in E000-FC00
    bool      phantom_ = false;   // the PHAN* jumper (ECN 6)
    bool      size8_   = true;    // the CLOCK SELECT jumper: 8" (4 MHz) or 5" (2 MHz)
    IrqJumper irq_     = IrqJumper::None;

    std::vector<std::string> log_;
};

} // namespace altair
