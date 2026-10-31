#include "boards/bankmem.h"

#include "core/statefile.h"

#include <cstdio>

namespace altair {

// ---------------------------------------------------------------------------
// Card facts -- the small table of what differs. Everything else is shared.
// ---------------------------------------------------------------------------

static const char* cardName(MemBankBoard::Card c) {
    switch (c) {
        case MemBankBoard::Card::Vector:      return "vector";
        case MemBankBoard::Card::Cromemco:    return "cromemco64kz";
        case MemBankBoard::Card::Northstar:   return "northstar";
        case MemBankBoard::Card::Expandoram2: return "expandoram2";
        case MemBankBoard::Card::Expandoram3: return "expandoram3";
    }
    return "vector";
}

static bool parseCard(const std::string& s, MemBankBoard::Card& out) {
    if (s == "vector") { out = MemBankBoard::Card::Vector; return true; }
    if (s == "cromemco64kz") { out = MemBankBoard::Card::Cromemco; return true; }
    if (s == "northstar") { out = MemBankBoard::Card::Northstar; return true; }
    if (s == "expandoram2") { out = MemBankBoard::Card::Expandoram2; return true; }
    if (s == "expandoram3") { out = MemBankBoard::Card::Expandoram3; return true; }
    return false;
}

static const char* partitionName(MemBankBoard::Partition p) {
    switch (p) {
        case MemBankBoard::Partition::None: return "none";
        case MemBankBoard::Partition::Ex48: return "ex48";
        case MemBankBoard::Partition::Ex32: return "ex32";
    }
    return "none";
}

static bool parsePartition(const std::string& s, MemBankBoard::Partition& out) {
    if (s == "none") { out = MemBankBoard::Partition::None; return true; }
    if (s == "ex48") { out = MemBankBoard::Partition::Ex48; return true; }
    if (s == "ex32") { out = MemBankBoard::Partition::Ex32; return true; }
    return false;
}

// The common region sits at the top of the 64K space; the banked window is everything
// below it. base 0 means no common region (a plain whole-64K plane).
uint16_t MemBankBoard::partitionCommonBase() const {
    switch (partition_) {
        case Partition::None: return 0x0000;
        case Partition::Ex48: return 0xC000;   // top 16K common
        case Partition::Ex32: return 0x8000;   // top 32K common
    }
    return 0x0000;
}

uint32_t MemBankBoard::partitionBankedSize() const {
    uint16_t base = partitionCommonBase();
    if (card_ == Card::Vector) return (uint32_t)windowHi_ - windowLo_ + 1;
    return base ? (uint32_t)base : 0x10000u;   // bytes below the common region
}

uint32_t MemBankBoard::partitionCommonSize() const {
    uint16_t base = partitionCommonBase();
    return base ? (0x10000u - base) : 0u;
}

uint32_t MemBankBoard::ramBytes(int n) const {
    if (card_ == Card::Expandoram2 && partition_ == Partition::Ex48 && n == 11)
        return partitionCommonSize() + 10 * partitionBankedSize() + 0x4000;
    return partitionCommonSize() + (uint32_t)n * partitionBankedSize();
}

uint8_t MemBankBoard::cardDefaultPort() const {
    switch (card_) {
        case Card::Vector:      return 0x40;
        case Card::Cromemco:    return 0x40;
        case Card::Northstar:   return 0xC0;
        case Card::Expandoram2: return 0xFF;
        case Card::Expandoram3: return 0xFF;
    }
    return 0x40;
}

int MemBankBoard::cardMaxBanks() const {
    switch (card_) {
        case Card::Vector:      return 8;   // 3-bit board number, 0-7
        case Card::Cromemco:    return 8;   // 8-bit mask, BANK 0-7
        case Card::Northstar:   return 6;   // bits 1-7, one spent on parity -> <=6
        // pages 0-9 (Fig 2-4), and page A with a partition: a 32K page on ex32, one
        // 16K cell on ex48. P=B-F are reserved.
        case Card::Expandoram2: return partition_ == Partition::None ? 10 : 11;
        case Card::Expandoram3: return 10;  // pages 0-9 on two boards
    }
    return 8;
}

// ---------------------------------------------------------------------------
// The ExpandoRAM III PROM map -- Fig 2-2 page 1 (printed 2-9), transcribed in
// reference/SD Systems ExpandoRAM III.md 4. The figure draws each board as banks 0-3 by
// RAM quarters 0-3 (the two PROM outputs that drive RAM A14-A15); each cell names the
// page P and the CPU quarter A (bus A15-A14) it answers. Cells marked FIXED are printed
// wrong in the figure: the page needs three different quarters, and the figure repeats
// the common quarter three times. The II's Fig 2-4 draws its own pages 4 and 8 with
// three different quarters, which supports the fix. Which RAM quarter holds which CPU
// quarter is invisible to software unless a bank is switched off, and these fixes keep
// every cell in the bank the figure prints. A PROM dump (COSMOS-48K, SD 7010479)
// replaces this table and changes nothing else.
// ---------------------------------------------------------------------------

namespace {

constexpr int8_t kCommon = -1;   // the common cell: live for every page
constexpr int8_t kNoCell = -2;   // a cell the figure does not settle -- not built

struct Eram3Cell {
    int8_t page;                 // 0-9, kCommon or kNoCell
    int8_t a;                    // CPU quarter: 0 = 0000-3FFF ... 3 = C000-FFFF
};

// [board][bank][RAM quarter]. CP/M, MP/M, COSMOS: pages in 0000-BFFF, common C000-FFFF.
const Eram3Cell kEram3Cpm[2][4][4] = {
    {   // board 0
        {{0, 0}, {1, 0}, {2, 0}, {3, 0}},
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
        {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
        {{kCommon, 3}, {4, 0}, {4, 1}, {4, 2}},          // page 4 FIXED: printed A=3 x3
    },
    {   // board 1
        {{5, 0}, {6, 0}, {7, 0}, {8, 0}},                // page 8 FIXED: printed A=3 x3,
        {{5, 1}, {6, 1}, {7, 1}, {8, 1}},                //   given the A of pages 5-7
        {{5, 2}, {6, 2}, {7, 2}, {8, 2}},
        // Quarter 0 is printed "A=3 P=0", a second common cell. Two boards cannot both
        // answer C000-FFFF, and a one-board system must carry common on board 0, so the
        // cell is not built. (The II puts a page A in the same place.)
        {{kNoCell, 3}, {9, 2}, {9, 1}, {9, 0}},
    },
};

// OASIS: pages in 4000-FFFF, common 0000-3FFF. The CP/M map turned upside down.
const Eram3Cell kEram3Oasis[2][4][4] = {
    {   // board 0
        {{kCommon, 0}, {4, 1}, {4, 2}, {4, 3}},          // page 4 FIXED: printed A=0 x3
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
        {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
        {{0, 3}, {1, 3}, {2, 3}, {3, 3}},
    },
    {   // board 1
        // Quarter 3 is printed "A=2, A=1, P=A" -- two quarters and a page the text does
        // not list. Not built.
        {{9, 3}, {9, 2}, {9, 1}, {kNoCell, 0}},
        {{5, 1}, {6, 1}, {7, 1}, {8, 1}},                // page 8 FIXED: printed A=0 x3,
        {{5, 2}, {6, 2}, {7, 2}, {8, 2}},                //   given the A of pages 5-7
        {{5, 3}, {6, 3}, {7, 3}, {8, 3}},
    },
};

} // namespace

int MemBankBoard::eram3Pages() const { return eram3Boards_ * 5; }

// One segment per built cell: a 16K window at the cell's CPU quarter, keyed by its page.
void MemBankBoard::rebuildEram3() {
    const auto& map = (config_ == Config::Cpm) ? kEram3Cpm : kEram3Oasis;
    segs_.clear();
    for (int bd = 0; bd < eram3Boards_; ++bd)
        for (int bk = 0; bk < 4; ++bk)
            for (int q = 0; q < 4; ++q) {
                const Eram3Cell& c = map[bd][bk][q];
                if (c.page == kNoCell) continue;
                Segment s;
                s.lo = (uint16_t)(c.a * 0x4000);
                s.hi = (uint16_t)(s.lo + 0x3FFF);
                s.common = (c.page == kCommon);
                s.key = s.common ? 0 : (uint16_t)c.page;
                s.bank = (uint8_t)(bd * 4 + bk);
                s.quarter = (uint8_t)q;
                s.ram.assign(0x4000, 0x00);
                segs_.push_back(std::move(s));
            }
}

// The PROM drives a bank enable for each cell of the page (and for the common cell, on
// every page); the S3 bank-enable switch then passes it or not. A page with no cell drives
// no enable, so nothing on the board answers the switched window (manual 2-4).
bool MemBankBoard::selectEram3Page(int page) {
    bool carried = false;
    for (Segment& s : segs_) {
        bool on = ((bankEnable_ >> s.bank) & 1) != 0;
        if (s.common) {
            s.enabled = on;
        } else {
            bool mine = (s.key == (uint16_t)page);
            carried = carried || mine;
            s.enabled = mine && on;
        }
    }
    decodeChanged();
    return carried;
}

// ---------------------------------------------------------------------------
// Segment construction
// ---------------------------------------------------------------------------

void MemBankBoard::rebuildSegments() {
    if (card_ == Card::Expandoram3) {
        rebuildEram3();
        fillSegments();
        applyReset();
        return;
    }
    int n = wantBanks_;
    int hi = cardMaxBanks();
    if (n < 1) n = 1;
    if (n > hi) n = hi;

    // The banked window is everything below the common region (the whole 64K when
    // partition=none). Each bank gets its own copy of that window; the common region,
    // if any, is one shared segment appended after the banks.
    uint16_t bankedHi = (uint16_t)(partitionBankedSize() - 1);
    if (card_ == Card::Vector) bankedHi = windowHi_;

    segs_.clear();
    for (int i = 0; i < n; ++i) {
        Segment s;
        s.lo = card_ == Card::Vector ? windowLo_ : (uint16_t)0x0000;
        s.hi = bankedHi;
        // ex48's page A is a single 16K cell: board 7 bank 3, at A=2 in Fig 2-4. The
        // manual's text puts it at C000 instead (p. 12); the figure is the PROM program.
        if (card_ == Card::Expandoram2 && partition_ == Partition::Ex48 && i == 10)
            s.lo = 0x8000;
        switch (card_) {
            case Card::Vector:      s.key = (uint16_t)i;        break;  // plane index
            case Card::Cromemco:    s.key = (uint16_t)(1u << i); break;  // membership mask
            case Card::Northstar:   s.key = (uint16_t)(i + 1);  break;  // one-hot bit 1..N
            case Card::Expandoram2: s.key = (uint16_t)i;        break;  // page index
            case Card::Expandoram3: break;  // built by rebuildEram3()
        }
        // Reset default: exactly one plane comes up live so a machine has memory the
        // instant it powers on. On Cromemco that stands for a block strapped RESET=IN
        // (§4.3); on North Star it is a board jumpered "enable bank on reset" (JP1);
        // on Vector it is the POC-forced bank 0; on the ExpandoRAM II it is page 0, an
        // approximation (the manual does not describe reset).
        s.resetEnabled = (i == 0);
        s.ram.assign((size_t)s.hi - s.lo + 1, 0x00);
        segs_.push_back(std::move(s));
    }
    // The common region: one always-live segment every bank sees. It is not a "bank" --
    // select() never touches it and it is not counted by banks(); it just answers the top
    // of memory no matter which bank is in. This is the EX-48/EX-32 PROM's job on real
    // hardware, and what a banked CP/M's resident OS + ?bank routine live in.
    if (uint16_t base = partitionCommonBase()) {
        Segment s;
        s.lo = base;
        s.hi = 0xFFFF;
        s.common = true;
        s.resetEnabled = true;          // always live -- reset and select both leave it on
        s.ram.assign((size_t)s.hi - s.lo + 1, 0x00);
        segs_.push_back(std::move(s));
    }
    fillSegments();
    applyReset();
    decodeChanged();
}

// New chips, per this board's fill policy. Each segment gets its own rng stream so
// that changing the plane count does not disturb the bytes already in the others --
// the same idiom the `memory` board uses per region.
void MemBankBoard::fillSegments() {
    for (size_t i = 0; i < segs_.size(); ++i) {
        std::mt19937_64 rng(seed_ ^ (0x9E3779B97F4A7C15ULL * (i + 1)));
        for (uint8_t& b : segs_[i].ram)
            b = (fill_ == Fill::Zero) ? 0x00 : (uint8_t)(rng() & 0xFF);
    }
}

void MemBankBoard::applyReset() {
    latch_ = 0;
    // ExpandoRAM III: the board reset clears latch U13 (schematic), so page 0 is in.
    if (card_ == Card::Expandoram3) { selectEram3Page(0); return; }
    for (Segment& s : segs_) s.enabled = s.resetEnabled;
    decodeChanged();
}

// ---------------------------------------------------------------------------
// The decode: the select byte -> which segments drive the bus
// ---------------------------------------------------------------------------

void MemBankBoard::select(uint8_t data) {
    latch_ = data;
    switch (card_) {
        case Card::Vector: {
            // One-hot select-one. The card also tolerates 0x41/0x42 (bit 6 ignored)
            // because OASIS writes those and the hardware happens to decode them as
            // banks 0/1 -- miss it and OASIS boots into the wrong plane and runs mad
            // later (docs/boards/bankmem.md, reference/Vector Graphic 64K Dynamic RAM.md).
            uint8_t h = (uint8_t)(data & 0xBF);
            int want = -1;
            for (int i = 0; i < 8; ++i)
                if (h == (uint8_t)(1u << i)) { want = i; break; }
            // Each board compares the latch with its own switch, so a bank with no board
            // answers nothing, and the bank that was in goes out. OASIS tests for this.
            if (want >= banks()) {
                for (Segment& s : segs_)
                    if (!s.common) s.enabled = false;
                decodeChanged();
                return;
            }
            if (want < 0) {
                char buf[128];
                std::snprintf(buf, sizeof buf,
                              "bank: invalid one-hot select 0x%02X for vector (%s). unchanged.",
                              data, id.c_str());
                log_.push_back(buf);
                return;
            }
            for (size_t i = 0; i < segs_.size(); ++i)
                if (!segs_[i].common) segs_[i].enabled = ((int)i == want);
            break;
        }
        case Card::Cromemco: {
            // 8-bit mask: a segment is live iff any bank it belongs to is set. Several
            // banks -- and so several segments -- may be live at once, which is the
            // whole point (OUT 40H,28H lights banks 3 and 5). Overlapping live windows
            // are a real bus fight on this card; owner() reports it.
            for (Segment& s : segs_)
                if (!s.common) s.enabled = ((s.key & data) != 0);
            break;
        }
        case Card::Northstar: {
            // bit 0 = on(0)/off(1); bits 1-7 = a one-hot address of the bank to toggle.
            // Only the addressed segment changes -- software switches the old bank off
            // and the new one on itself (reference/North Star HRAM.md 3).
            uint8_t bankBits = (uint8_t)(data & 0xFE);
            bool on = (data & 0x01) == 0;
            int bit = -1;
            for (int b = 1; b <= 7; ++b)
                if (bankBits == (uint8_t)(1u << b)) { bit = b; break; }
            if (bit < 0) {
                char buf[128];
                std::snprintf(buf, sizeof buf,
                              "bank: no one-hot bank bit in 0x%02X for northstar (%s). unchanged.",
                              data, id.c_str());
                log_.push_back(buf);
                return;
            }
            bool hit = false;
            for (Segment& s : segs_)
                if (!s.common && s.key == (uint16_t)bit) { s.enabled = on; hit = true; }
            if (!hit) {
                char buf[128];
                std::snprintf(buf, sizeof buf,
                              "bank: select 0x%02X targets bank bit %d, which this board (%s) "
                              "does not carry (only %d bank(s)). unchanged.",
                              data, bit, id.c_str(), (int)segs_.size());
                log_.push_back(buf);
            }
            break;
        }
        case Card::Expandoram2: {
            // Only D0-D3 reach the PROM: latch U13 feeds BDO0-3 to PROM A2-A5, and BDO4-5
            // go nowhere (schematic). So OUT FF,13 is page 3. The PROM spreads a page
            // over banks on several boards; one segment per page shows software the
            // same thing. Only the banked window swaps; the common region (if any) stays
            // live so the resident OS and the ?bank routine survive the switch.
            int page = data & 0x0F;
            for (size_t i = 0; i < segs_.size(); ++i)
                if (!segs_[i].common) segs_[i].enabled = ((int)i == page);
            if (page >= banks()) {
                // No board carries the page, so the PROM drives no bank enable and
                // nothing answers the switched window (Fig 2-4).
                char buf[160];
                std::snprintf(buf, sizeof buf,
                              "bank: page %X has no RAM on expandoram2 (%s, pages 0-%X). "
                              "Nothing answers 0000-%04X%s.",
                              page, id.c_str(), banks() - 1,
                              (unsigned)(partitionBankedSize() - 1),
                              partitionCommonBase() ? "; common still does" : "");
                log_.push_back(buf);
            }
            break;
        }
        case Card::Expandoram3: {
            // Only D0-D3 reach the PROM: latch U13 feeds BDO0-3 to PROM A2-A5, BDO4-5 go
            // nowhere and D6-D7 are not latched (schematic). So OUT FF,F3 is page 3.
            int page = data & 0x0F;
            if (!selectEram3Page(page)) {
                char buf[160];
                std::snprintf(buf, sizeof buf,
                              "bank: page %X has no RAM on expandoram3 (%s, pages 0-%d). "
                              "Nothing answers %s; common still does.",
                              page, id.c_str(), eram3Pages() - 1,
                              config_ == Config::Cpm ? "0000-BFFF" : "4000-FFFF");
                log_.push_back(buf);
            }
            break;
        }
    }
    decodeChanged();
}

// ---------------------------------------------------------------------------
// owner() -- the live segment for an address, and the bus-fight report
// ---------------------------------------------------------------------------

MemBankBoard::Segment* MemBankBoard::owner(uint16_t a) {
    Segment* first = nullptr;
    int count = 0;
    for (Segment& s : segs_) {
        if (!s.enabled) continue;
        if (a < s.lo || a > s.hi) continue;
        if (!first) first = &s;
        ++count;
    }
    if (count > 1) {
        char buf[128];
        std::snprintf(buf, sizeof buf,
                      "bank: %d live banks answer %04X on %s -- a bus fight; reads are garbage.",
                      count, a, id.c_str());
        log_.push_back(buf);
    }
    return first;
}

const MemBankBoard::Segment* MemBankBoard::owner(uint16_t a) const {
    const Segment* first = nullptr;
    for (const Segment& s : segs_) {
        if (!s.enabled) continue;
        if (a < s.lo || a > s.hi) continue;
        if (!first) first = &s;
    }
    return first;
}

// ---------------------------------------------------------------------------
// The bus interface
// ---------------------------------------------------------------------------

bool MemBankBoard::honors(const BusCycle& c) const {
    if (honors_ == PhantomHonor::All) return true;
    return honors_ == PhantomHonor::Read && c.type == Cycle::MemRead;
}

bool MemBankBoard::decodes(const BusCycle& c) const {
    if (c.type == Cycle::IoWrite) return c.port() == port_;   // the write-only select port
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;

    // SOMEONE ELSE pulls PHANTOM* -- an SBC-200 / VersaFloppy boot PROM shadowing the RAM
    // it sits under -- and I am strapped to honor it, so I take myself off the bus for this
    // cycle. With honors=read I keep answering WRITES, which is what lets a cold-boot loader
    // write the system into the RAM the PROM is shadowing (the ExpandoRAM-under-SBC-PROM
    // case, exactly like the `memory` board under a Tarbell PROM). No self-shutoff guard is
    // needed here because bankmem carries no ROM and never asserts phantom itself.
    if (c.phantom && honors(c)) return false;

    return owner(c.addr) != nullptr;
}

uint8_t MemBankBoard::read(const BusCycle& c) {
    Segment* s = owner(c.addr);
    return s ? s->ram[(size_t)(c.addr - s->lo)] : 0xFF;
}

void MemBankBoard::write(const BusCycle& c) {
    if (c.type == Cycle::IoWrite) { select((uint8_t)(c.data & 0xFF)); return; }
    if (Segment* s = owner(c.addr)) s->ram[(size_t)(c.addr - s->lo)] = c.data;
}

bool MemBankBoard::peek(uint16_t addr, uint8_t& out) const {
    const Segment* s = owner(addr);
    if (!s) return false;
    out = s->ram[(size_t)(addr - s->lo)];
    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void MemBankBoard::reset(Reset) {
    // POC* and RESET* do the same thing: the select latch clears and every segment
    // returns to its reset-enable default. Not one byte of RAM is touched.
    applyReset();
}

void MemBankBoard::power() {
    // Power applied -- the only event that loses RAM.
    fillSegments();
    applyReset();
    enabled_ = true;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The store and the live enables travel; the card geometry is
// config (already correct in a matching machine), exactly as `memory` does it.
// ---------------------------------------------------------------------------

void MemBankBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u32((uint32_t)segs_.size());
    for (const Segment& s : segs_) {
        w.blob(s.ram);
        w.u8(s.enabled ? 1 : 0);
    }
    w.u8(latch_);
}

void MemBankBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && i < segs_.size(); ++i) {
        segs_[i].ram = r.blob();
        segs_[i].enabled = r.u8() != 0;
    }
    latch_ = r.u8();
    decodeChanged();
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

int MemBankBoard::banks() const {
    if (card_ == Card::Expandoram3) return eram3Pages();
    int n = 0;
    for (const Segment& s : segs_)
        if (!s.common) ++n;
    return n;
}

uint32_t MemBankBoard::activeMask() const {
    // Bit i for each live BANK; the common region is not a bank and is not counted. Banks
    // occupy segs_[0..banks()-1] (the common segment, if any, is appended last), so the
    // segment index doubles as the bank index here.
    uint32_t m = 0;
    if (card_ == Card::Expandoram3) {   // bit p for each live PAGE (its cells are many)
        for (const Segment& s : segs_)
            if (!s.common && s.enabled) m |= (1u << s.key);
        return m;
    }
    for (size_t i = 0; i < segs_.size(); ++i)
        if (!segs_[i].common && segs_[i].enabled) m |= (1u << i);
    return m;
}

std::vector<Property> MemBankBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name = "card";
        x.help = "which banked card this is -- each owns its own decode: "
                 "vector | cromemco64kz | northstar | expandoram2 | expandoram3";
        x.kind = Kind::Enum;
        x.choices = {"vector", "cromemco64kz", "northstar", "expandoram2", "expandoram3"};
        x.get = [this] { return Value::ofStr(cardName(card_)); };
        x.set = [this](const Value& v, std::string& err) {
            Card c;
            if (!parseCard(v.s(), c)) { err = "unknown card"; return false; }
            card_ = c;
            port_ = cardDefaultPort();
            if (wantBanks_ > cardMaxBanks()) wantBanks_ = cardMaxBanks();
            // The common-memory partition is an ExpandoRAM II concept -- drop it if the
            // card is no longer that board.
            if (card_ != Card::Expandoram2) partition_ = Partition::None;
            // The S3 switches are ExpandoRAM III ones -- back to factory on any other card.
            if (card_ != Card::Expandoram3) {
                config_ = Config::Cpm;
                bankEnable_ = 0xFF;
            }
            rebuildSegments();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "port";
        x.help = "the write-only bank-select port. Card default (vector/cromemco 40, "
                 "northstar C0, expandoram2/expandoram3 FF); overridable, as the real boards relocate it";
        x.kind = Kind::Int;
        x.radix = 16;
        x.min = 0;
        x.max = 0xFF;
        x.get = [this] { return Value::ofInt(port_); };
        x.set = [this](const Value& v, std::string&) {
            port_ = (uint8_t)v.i();
            decodeChanged();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // Before banks: CONFIG SAVE writes properties in this order, and ex32/ex48
        // raise the bank cap to 11.
        Property x;
        x.name = "partition";
        x.help = "ExpandoRAM II common-memory partition (the EX-48/EX-32 PROM): "
                 "none | ex48 | ex32. ex48 = 48K banked (0000-BFFF) + 16K common "
                 "(C000-FFFF); ex32 = 32K banked + 32K common (8000-FFFF). The common "
                 "region is identical in every bank -- what a banked CP/M's resident OS "
                 "and bank-switch routine live in. `none` (default) is a whole 64K plane";
        x.kind = Kind::Enum;
        x.choices = {"none", "ex48", "ex32"};
        x.get = [this] { return Value::ofStr(partitionName(partition_)); };
        x.set = [this](const Value& v, std::string& err) {
            Partition pt;
            if (!parsePartition(v.s(), pt)) { err = "partition is none|ex48|ex32"; return false; }
            if (pt != Partition::None && card_ == Card::Expandoram3) {
                err = "the ExpandoRAM III's PROM sets its own common area; "
                      "use config=cpm|oasis";
                return false;
            }
            if (pt != Partition::None && card_ != Card::Expandoram2) {
                err = "partition (common memory) is an expandoram2 concept; "
                      "the other cards are whole-64K planes";
                return false;
            }
            partition_ = pt;
            if (wantBanks_ > cardMaxBanks()) wantBanks_ = cardMaxBanks();
            rebuildSegments();
            return true;
        };
        p.push_back(std::move(x));
    }
    // Before banks and ram, because the window sizes a bank.
    auto windowEnd = [this](const char* name, const char* help, uint16_t& end, uint16_t factory) {
        Property x;
        x.name  = name;
        x.help  = help;
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFFFF;
        x.get   = [&end] { return Value::ofInt(end); };
        x.set   = [this, &end, factory](const Value& v, std::string& err) {
            if (v.i() != factory && card_ != Card::Vector) {
                err = "the plane window is a vector setting";
                return false;
            }
            if ((v.i() & 0xFF) != (factory & 0xFF)) {
                err = "a window starts on a page and ends on one: xx00-xxFF";
                return false;
            }
            end = (uint16_t)v.i();
            if (windowLo_ > windowHi_) {
                end = factory;
                err = "the window ends before it starts";
                return false;
            }
            rebuildSegments();
            return true;
        };
        return x;
    };
    p.push_back(windowEnd("window_lo",
                          "vector: the first address of each plane. A 64K board starts at 0000",
                          windowLo_, 0x0000));
    p.push_back(windowEnd("window_hi",
                          "vector: the last address of each plane, jumper area D on a 64K board: "
                          "FFFF, DFFF (as shipped) or BFFF",
                          windowHi_, 0xFFFF));
    {
        Property x;
        x.name = "banks";
        x.help = "how many switchable banks/planes/pages this subsystem carries (one per "
                 "board). Card-capped: vector/cromemco 8, northstar 6, expandoram2 10 (11 with a "
                 "partition: page A). The "
                 "common region (partition) is not a bank. Setting `ram` derives this. "
                 "expandoram3 carries 5 pages per board, so it takes 5 or 10";
        x.kind = Kind::Int;
        x.min = 1;
        x.max = 11;
        x.get = [this] { return Value::ofInt(banks()); };
        x.set = [this](const Value& v, std::string& err) {
            if (card_ == Card::Expandoram3) {
                if (v.i() != 5 && v.i() != 10) {
                    err = "expandoram3 carries 5 pages per board, so banks is 5 or 10 "
                          "(or set ram=256|512)";
                    return false;
                }
                eram3Boards_ = (int)(v.i() / 5);
                rebuildSegments();
                return true;
            }
            if (v.i() < 1 || v.i() > cardMaxBanks()) {
                err = std::string(cardName(card_)) + " takes 1.." +
                      std::to_string(cardMaxBanks()) + " banks";
                return false;
            }
            wantBanks_ = (int)v.i();
            rebuildSegments();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "config";
        x.help = "ExpandoRAM III S3 switch 5, which PROM map is in force: cpm (ON, the "
                 "factory setting) = pages in 0000-BFFF under a common 16K at C000; oasis "
                 "(OFF) = pages in 4000-FFFF over a common 16K at 0000";
        x.kind = Kind::Enum;
        x.choices = {"cpm", "oasis"};
        x.get = [this] { return Value::ofStr(config_ == Config::Cpm ? "cpm" : "oasis"); };
        x.set = [this](const Value& v, std::string& err) {
            Config c = (v.s() == "oasis") ? Config::Oasis : Config::Cpm;
            if (c != Config::Cpm && card_ != Card::Expandoram3) {
                err = "config is an expandoram3 switch";
                return false;
            }
            config_ = c;
            rebuildSegments();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "bank_enable";
        x.help = "ExpandoRAM III S3 bank enables: bit = board x 4 + bank, set = enabled "
                 "(factory FF). On each board, S3 switch 4 is bank 0 and switch 1 is bank 3. "
                 "A switched-off bank answers nothing";
        x.kind = Kind::Int;
        x.radix = 16;
        x.min = 0;
        x.max = 0xFF;
        x.get = [this] { return Value::ofInt(bankEnable_); };
        x.set = [this](const Value& v, std::string& err) {
            if (v.i() != 0xFF && card_ != Card::Expandoram3) {
                err = "bank_enable is an expandoram3 switch";
                return false;
            }
            bankEnable_ = (uint8_t)v.i();
            if (card_ == Card::Expandoram3) selectEram3Page(latch_ & 0x0F);
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "ram";
        x.help = "total board RAM in KB. Sets the bank count from the current partition: "
                 "with ex48, 256 -> 5 banks; with none (whole plane), 256 -> 4 banks of "
                 "64K. The real ExpandoRAM II is 64K (16K chips) or 256K (64K chips); "
                 "with ex48 or ex32, ram counts 64K boards of 16K chips (ex48 512 = 8 boards, "
                 "pages 0-A). "
                 "expandoram3: 256 (one board) or 512 (two boards)";
        x.kind = Kind::Int;
        x.min = 1;
        x.max = 640;                       // 10 whole-64K planes; the setter card-caps it
        x.get = [this] {
            if (card_ == Card::Expandoram3) return Value::ofInt(eram3Boards_ * 256);
            return Value::ofInt((int)(ramBytes(banks()) / 1024));
        };
        x.set = [this](const Value& v, std::string& err) {
            long long kb = v.i();
            if (card_ == Card::Expandoram3) {
                if (kb != 256 && kb != 512) {
                    err = "the ExpandoRAM III is 256K per board: ram is 256 (one board) "
                          "or 512 (two boards)";
                    return false;
                }
                eram3Boards_ = (int)(kb / 256);
                rebuildSegments();
                return true;
            }
            // With a partition, ram counts 64K boards of 16K chips: up to 8 on ex48 (512K)
            // and 6 on ex32 (384K), the board counts of Fig 2-4. Without one, it is one
            // board of 4164s.
            if (card_ == Card::Expandoram2 && partition_ == Partition::None && kb > 256) {
                err = "the ExpandoRAM II board is at most 256K (64K chips in four banks); "
                      "with partition=ex48 or ex32, ram counts 64K boards";
                return false;
            }
            if (kb * 1024 > (long long)ramBytes(cardMaxBanks())) {
                err = std::string(cardName(card_)) + " tops out at " +
                      std::to_string(cardMaxBanks()) + " banks (" +
                      std::to_string(ramBytes(cardMaxBanks()) / 1024) + "K)";
                return false;
            }
            int n = 1;
            while (n < cardMaxBanks() && (long long)ramBytes(n + 1) <= kb * 1024) ++n;
            // Snap-down note: RAM that is not a whole number of banks lands on the bank below.
            long long exact = (long long)ramBytes(n);
            if (exact != kb * 1024) {
                char buf[128];
                std::snprintf(buf, sizeof buf,
                              "ram=%lldK is not a whole number of banks on %s; using %lldK "
                              "(%d bank(s)).",
                              kb, id.c_str(), exact / 1024, n);
                log_.push_back(buf);
            }
            wantBanks_ = n;
            rebuildSegments();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "honors_phantom";
        x.help = "A JUMPER. Another board pulls PHANTOM* (a boot PROM shadowing the RAM "
                 "this card sits under) -- do I switch off? none | read | all. `read` keeps "
                 "answering writes so a cold-boot loader falls through to this RAM while the "
                 "PROM shadows reads (an ExpandoRAM II under the SBC-200 boot PROM). Default "
                 "none -- a plain banked-RAM machine has no PROM overlay";
        x.kind = Kind::Enum;
        x.choices = {"none", "read", "all"};
        x.get = [this] {
            return Value::ofStr(honors_ == PhantomHonor::None   ? "none"
                                : honors_ == PhantomHonor::Read ? "read"
                                                                : "all");
        };
        x.set = [this](const Value& v, std::string&) {
            honors_ = v.s() == "none"   ? PhantomHonor::None
                      : v.s() == "read" ? PhantomHonor::Read
                                        : PhantomHonor::All;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "active";
        x.help = "the live bank(s) right now -- the guest sets this by writing the select "
                 "port. Read-only here";
        x.kind = Kind::Str;
        x.get = [this] {
            if (card_ == Card::Expandoram3) {
                bool page = false, common = false;
                for (const Segment& sg : segs_) {
                    if (!sg.enabled) continue;
                    if (sg.common) common = true; else page = true;
                }
                char buf[48];
                std::snprintf(buf, sizeof buf, "page %X%s%s", latch_ & 0x0F,
                              page ? "" : " (no RAM)", common ? " + common" : "");
                return Value::ofStr(buf);
            }
            std::string s;
            for (size_t i = 0; i < segs_.size(); ++i)
                if (!segs_[i].common && segs_[i].enabled) {
                    if (!s.empty()) s += ",";
                    s += std::to_string(i);
                }
            std::string out = s.empty() ? "(none)" : ("bank " + s);
            if (partitionCommonBase()) out += " + common";
            return Value::ofStr(out);
        };
        // No setter: read-only is only visible to SHOW/CONFIG/MCP as the absence of one.
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "fill";
        x.help = "RAM contents at power-on: zero | random (real RAM is not zeroed)";
        x.kind = Kind::Enum;
        x.choices = {"zero", "random"};
        x.get = [this] { return Value::ofStr(fill_ == Fill::Zero ? "zero" : "random"); };
        x.set = [this](const Value& v, std::string&) {
            fill_ = (v.s() == "zero") ? Fill::Zero : Fill::Random;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "seed";
        x.help = "seed for fill=random -- the same seed fills RAM the same way at every "
                 "POWER, so a run is repeatable";
        x.kind = Kind::Int;
        x.get = [this] { return Value::ofInt((long long)seed_); };
        x.set = [this](const Value& v, std::string&) {
            seed_ = (uint64_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> MemBankBoard::memMap() const {
    std::vector<MapEntry> out;
    for (size_t i = 0; i < segs_.size(); ++i) {
        MapEntry e;
        e.lo = segs_[i].lo;
        e.hi = segs_[i].hi;
        e.what = "ram";
        if (card_ == Card::Expandoram3) {
            const Segment& s = segs_[i];
            e.note = (s.common ? std::string("common") : "page " + std::to_string(s.key)) +
                     ", board " + std::to_string(s.bank / 4) + " bank " +
                     std::to_string(s.bank % 4) + (s.enabled ? " (live)" : "");
        } else if (segs_[i].common)
            e.note = "common (always live)";
        else
            e.note = "bank " + std::to_string(i) + (segs_[i].enabled ? " (live)" : "");
        out.push_back(e);
    }
    return out;
}

std::vector<MapEntry> MemBankBoard::ioMap() const {
    MapEntry e;
    e.lo = e.hi = port_;
    e.what = "write";
    const char* how = card_ == Card::Vector      ? "one-hot select"
                      : card_ == Card::Cromemco  ? "8-bit bank mask"
                      : card_ == Card::Northstar ? "on/off + one-hot toggle"
                      : card_ == Card::Expandoram3 ? "page select, low nibble, PROM map"
                      : partition_ == Partition::None ? "page select, low nibble (approx)"
                                                      : "page select, low nibble";
    e.note = std::string("bank select (") + cardName(card_) + ", " + how + ")";
    return {e};
}

std::vector<std::string> MemBankBoard::statusLines() const {
    std::vector<std::string> out;
    if (card_ == Card::Expandoram3) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "config %s  ram %dK  page %X  bank_enable %02X",
                      config_ == Config::Cpm ? "cpm" : "oasis", eram3Boards_ * 256,
                      latch_ & 0x0F, bankEnable_);
        out.push_back(buf);
        for (const Segment& s : segs_) {
            std::snprintf(buf, sizeof buf, "%-7s  %04X-%04X  board %d bank %d  %s",
                          s.common ? "common" : ("page " + std::to_string(s.key)).c_str(),
                          s.lo, s.hi, s.bank / 4, s.bank % 4, s.enabled ? "live" : "off");
            out.push_back(buf);
        }
        return out;
    }
    if (partition_ != Partition::None) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "partition %s  ram %dK", partitionName(partition_),
                      (int)(ramBytes(banks()) / 1024));
        out.push_back(buf);
    }
    for (size_t i = 0; i < segs_.size(); ++i) {
        char buf[96];
        if (segs_[i].common)
            std::snprintf(buf, sizeof buf, "common  %04X-%04X  live", segs_[i].lo, segs_[i].hi);
        else
            std::snprintf(buf, sizeof buf, "bank %zu  %04X-%04X  %s", i, segs_[i].lo, segs_[i].hi,
                          segs_[i].enabled ? "live" : "off");
        out.push_back(buf);
    }
    return out;
}

std::vector<std::string> MemBankBoard::drainLog() {
    auto v = std::move(log_);
    log_.clear();
    return v;
}

} // namespace altair
