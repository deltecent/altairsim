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
    }
    return "vector";
}

static bool parseCard(const std::string& s, MemBankBoard::Card& out) {
    if (s == "vector") { out = MemBankBoard::Card::Vector; return true; }
    if (s == "cromemco64kz") { out = MemBankBoard::Card::Cromemco; return true; }
    if (s == "northstar") { out = MemBankBoard::Card::Northstar; return true; }
    if (s == "expandoram2") { out = MemBankBoard::Card::Expandoram2; return true; }
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
    return base ? (uint32_t)base : 0x10000u;   // bytes below the common region
}

uint32_t MemBankBoard::partitionCommonSize() const {
    uint16_t base = partitionCommonBase();
    return base ? (0x10000u - base) : 0u;
}

uint8_t MemBankBoard::cardDefaultPort() const {
    switch (card_) {
        case Card::Vector:      return 0x40;
        case Card::Cromemco:    return 0x40;
        case Card::Northstar:   return 0xC0;
        case Card::Expandoram2: return 0xFF;
    }
    return 0x40;
}

int MemBankBoard::cardMaxBanks() const {
    switch (card_) {
        case Card::Vector:      return 8;   // 3-bit board number, 0-7
        case Card::Cromemco:    return 8;   // 8-bit mask, BANK 0-7
        case Card::Northstar:   return 6;   // bits 1-7, one spent on parity -> <=6
        case Card::Expandoram2: return 10;  // pages 0-9
    }
    return 8;
}

// ---------------------------------------------------------------------------
// Segment construction
// ---------------------------------------------------------------------------

void MemBankBoard::rebuildSegments() {
    int n = wantBanks_;
    int hi = cardMaxBanks();
    if (n < 1) n = 1;
    if (n > hi) n = hi;

    // The banked window is everything below the common region (the whole 64K when
    // partition=none). Each bank gets its own copy of that window; the common region,
    // if any, is one shared segment appended after the banks.
    uint16_t bankedHi = (uint16_t)(partitionBankedSize() - 1);

    segs_.clear();
    for (int i = 0; i < n; ++i) {
        Segment s;
        s.lo = 0x0000;
        s.hi = bankedHi;
        switch (card_) {
            case Card::Vector:      s.key = (uint16_t)i;        break;  // plane index
            case Card::Cromemco:    s.key = (uint16_t)(1u << i); break;  // membership mask
            case Card::Northstar:   s.key = (uint16_t)(i + 1);  break;  // one-hot bit 1..N
            case Card::Expandoram2: s.key = (uint16_t)i;        break;  // page index
        }
        // Reset default: exactly one plane comes up live so a machine has memory the
        // instant it powers on. On Cromemco that stands for a block strapped RESET=IN
        // (§4.3); on North Star it is a board jumpered "enable bank on reset" (JP1);
        // on Vector it is the POC-forced bank 0; on the ExpandoRAM II it is the page-0
        // approximation (the real reset-to-page behavior is unsourced).
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
    for (Segment& s : segs_) s.enabled = s.resetEnabled;
    latch_ = 0;
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
            if (want < 0 || want >= banks()) {
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
            // APPROXIMATION. The real board decodes the page through an 82S130 PROM
            // against the board-select switches and the top address bits into a 32K/48K
            // partition; that per-cell map is not transcribable from the scan, so we
            // model a binary page-select over 64K planes and say so loudly. Get a PROM
            // dump and this is where the faithful decode goes.
            int page = data;
            if (page < 0 || page >= banks()) {
                char buf[128];
                std::snprintf(buf, sizeof buf,
                              "bank: page %d out of range for expandoram2 (%s, %d page(s)). "
                              "unchanged.",
                              page, id.c_str(), banks());
                log_.push_back(buf);
                return;
            }
            // Only the banked window swaps; the common region (if any) stays live so the
            // resident OS and the ?bank routine survive the switch.
            for (size_t i = 0; i < segs_.size(); ++i)
                if (!segs_[i].common) segs_[i].enabled = ((int)i == page);
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
                 "vector | cromemco64kz | northstar | expandoram2";
        x.kind = Kind::Enum;
        x.choices = {"vector", "cromemco64kz", "northstar", "expandoram2"};
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
            rebuildSegments();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "port";
        x.help = "the write-only bank-select port. Card default (vector/cromemco 40, "
                 "northstar C0, expandoram2 FF); overridable, as the real boards relocate it";
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
        Property x;
        x.name = "banks";
        x.help = "how many switchable banks/planes/pages this subsystem carries (one per "
                 "board). Card-capped: vector/cromemco 8, northstar 6, expandoram2 10. The "
                 "common region (partition) is not a bank. Setting `ram` derives this";
        x.kind = Kind::Int;
        x.min = 1;
        x.max = 10;
        x.get = [this] { return Value::ofInt(banks()); };
        x.set = [this](const Value& v, std::string& err) {
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
    {
        Property x;
        x.name = "ram";
        x.help = "total board RAM in KB. Sets the bank count from the current partition: "
                 "with ex48, 256 -> 5 banks; with none (whole plane), 256 -> 4 banks of "
                 "64K. The real ExpandoRAM II is 64K (16K chips) or 256K (64K chips)";
        x.kind = Kind::Int;
        x.min = 1;
        x.max = 640;                       // 10 whole-64K planes; the setter card-caps it
        x.get = [this] {
            uint32_t bytes = partitionCommonSize() + (uint32_t)banks() * partitionBankedSize();
            return Value::ofInt((int)(bytes / 1024));
        };
        x.set = [this](const Value& v, std::string& err) {
            long long kb = v.i();
            if (card_ == Card::Expandoram2 && kb > 256) {
                err = "the ExpandoRAM II board is at most 256K (64K chips in four banks)";
                return false;
            }
            uint32_t bankedSize = partitionBankedSize();
            uint32_t commonSize = partitionCommonSize();
            long long banked = kb * 1024 - (long long)commonSize;
            int n = (banked > 0) ? (int)(banked / bankedSize) : 0;
            if (n < 1) n = 1;
            if (n > cardMaxBanks()) {
                err = std::string(cardName(card_)) + " tops out at " +
                      std::to_string(cardMaxBanks()) + " banks";
                return false;
            }
            // Snap-down note: RAM that is not a whole number of banks lands on the bank below.
            long long exact = (long long)commonSize + (long long)n * bankedSize;
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
        if (segs_[i].common)
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
                                                 : "page select (approx)";
    e.note = std::string("bank select (") + cardName(card_) + ", " + how + ")";
    return {e};
}

std::vector<std::string> MemBankBoard::statusLines() const {
    std::vector<std::string> out;
    if (partition_ != Partition::None) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "partition %s  ram %dK", partitionName(partition_),
                      (int)((partitionCommonSize() + (uint32_t)banks() * partitionBankedSize()) / 1024));
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
