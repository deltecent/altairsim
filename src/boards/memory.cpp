#include "boards/memory.h"

#include "core/roms.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace altair {

// ---------------------------------------------------------------------------
// Banking -- five real cards, and no two alike (docs/boards/memory.md).
//
// Read this table twice before you are tempted to generalize it. Three ports,
// two encodings, and Cromemco has SEVEN banks because bit 7 is not a bank
// select on that card. `0x04` means bank 4 on an ExpandoRAM and bank 2 on a
// Vector. This is the strongest evidence in the whole project that boards must
// own their own decode -- and it is why there is no BANK= in the monitor.
// ---------------------------------------------------------------------------
static const BankSpec kBanks[] = {
    {"none", "(plain unbanked memory)", 0x00, 1, false, 0xFF},
    {"eram", "SD Systems ExpandoRAM", 0xFF, 8, false, 0x07},
    {"vram", "Vector Graphic", 0x40, 8, true, 0xFF},
    {"cram", "Cromemco", 0x40, 7, true, 0x7F},
    {"hram", "North Star Horizon", 0xC0, 16, false, 0x0F},
    {"b810", "AB Digital Design B810", 0x40, 16, false, 0x0F},
};

const BankSpec& bankSpec(BankType t) { return kBanks[(int)t]; }

bool parseBankType(const std::string& s, BankType& out) {
    for (int i = 0; i < 6; ++i) {
        if (s == kBanks[i].name) {
            out = (BankType)i;
            return true;
        }
    }
    return false;
}

std::string Region::describe() const {
    char buf[128];
    if (kind == RegionKind::Rom)
        std::snprintf(buf, sizeof buf, "rom  %04X-%04X  %s", at,
                      (unsigned)(at + size - 1), mount.c_str());
    else if (size >= 1024 && size % 1024 == 0)
        std::snprintf(buf, sizeof buf, "ram  %04X-%04X  %uK", at, (unsigned)(at + size - 1),
                      (unsigned)(size / 1024));
    else
        // A 256-byte region is not "0K". Integer division reporting a real region
        // as nothing is the kind of small lie that costs somebody an afternoon.
        std::snprintf(buf, sizeof buf, "ram  %04X-%04X  %u bytes", at,
                      (unsigned)(at + size - 1), (unsigned)size);
    return buf;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

static uint32_t roundUpPage(uint32_t n) { return (n + 0xFF) & ~0xFFu; }

bool MemoryBoard::addRegion(Region r, std::string& err) {
    if (r.at % 0x100) {
        err = "region `at` must be page-aligned (a multiple of 0x100)";
        return false;
    }

    if (r.kind == RegionKind::Rom) {
        if (r.mount.empty()) {
            err = "a rom region needs `mount` (a file, or builtin:<name>)";
            return false;
        }
        // None of the five real banked cards carries ROM, so whether a combo
        // card's ROM swaps with the RAM planes is UNKNOWN. We do not guess
        // (DESIGN.md 0.1) -- we refuse, and say why.
        if (bankType_ != BankType::None) {
            err = "a rom region on a banked card is unsourced and rejected: none of the five "
                  "real banked cards carried ROM, so what a bank select does to it is unknown. "
                  "Use a separate board, or bring a manual.";
            return false;
        }
    } else {
        if (r.size == 0) {
            err = "a ram region needs `size`";
            return false;
        }
    }

    regions_.push_back(std::move(r));
    size_t idx = regions_.size() - 1;

    if (regions_[idx].kind == RegionKind::Rom) {
        if (!loadRomRegion(idx, err)) {
            regions_.pop_back();
            return false;
        }
    } else {
        regions_[idx].size = roundUpPage(regions_[idx].size);
    }

    if ((uint32_t)regions_[idx].at + regions_[idx].size > 0x10000) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "region at %04X + %u bytes runs past FFFF",
                      regions_[idx].at, (unsigned)regions_[idx].size);
        err = buf;
        regions_.pop_back();
        return false;
    }

    rebuildPageMap();

    // Populate the chips we just plugged in. WITHOUT THIS, RAM added to a running
    // machine reads back 0xFF -- which is precisely what an EMPTY SOCKET reads, so
    // there would be no way to tell "there is RAM here and it is uninitialized"
    // from "nothing drives this address at all". Two very different bugs, one
    // symptom. `power()` refills everything; this is the same fill, on the spot.
    if (regions_[idx].kind == RegionKind::Ram) fillRegion(idx);

    return true;
}

// Read a ROM region's image into the store. A `builtin:` name and a host path
// travel the SAME Intel HEX parser -- that is the point of 10.3.1.
bool MemoryBoard::loadRomRegion(size_t idx, std::string& err) {
    Region& r = regions_[idx];
    Image img;

    if (r.mount.rfind("builtin:", 0) == 0) {
        std::string name = r.mount.substr(8);
        const BuiltinRom* rom = findRom(name);
        if (!rom) {
            err = "no built-in ROM named '" + name + "'. SHOW ROMS lists them.";
            return false;
        }
        if (!decodeRom(*rom, r.at, img, err)) return false;
    } else {
        std::ifstream f(r.mount, std::ios::binary);
        if (!f) {
            err = "cannot open '" + r.mount + "'";
            return false;
        }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
        if (looksLikeHex(raw)) {
            if (!loadHex(raw, img, err)) {
                err = r.mount + ": " + err;
                return false;
            }
        } else {
            loadBin(raw, r.at, img);
        }
    }

    if (img.empty()) {
        err = r.mount + ": no bytes";
        return false;
    }

    // A HEX file places itself, so the region's extent comes from the IMAGE --
    // and a short image decodes a short range. If the file disagrees with `at`,
    // say so rather than silently relocating it: a ROM at the wrong address is
    // a bug you would chase for an hour.
    if (img.lo() != r.at) {
        char buf[160];
        std::snprintf(buf, sizeof buf,
                      "%s places bytes at %04X but the region says at = %04X",
                      r.mount.c_str(), img.lo(), r.at);
        err = buf;
        return false;
    }
    r.size = roundUpPage(img.hi() - img.lo() + 1);

    growStore();
    for (const auto& [a, b] : img.bytes)
        if (a < 0x10000) store_[a] = b;

    return true;
}

// ---------------------------------------------------------------------------
// THE STORE IS THE BOARD'S CHIPS, AND ONLY THIS BOARD SAYS WHAT IS IN THEM.
//
// 0xFF DOES NOT APPEAR ANYWHERE IN HERE, and that is deliberate. 0xFF is the
// BUS's answer -- it is what the CPU reads when NOBODY drives the bus, and it
// belongs in bus.cpp and nowhere else. A RAM chip does not power up holding 0xFF;
// it powers up holding whatever it feels like, which is what `fill = random`
// means. Seeding this board's store with the bus's floating value would be the
// bus's rule leaking into a card, and it would make uninitialized RAM read back
// exactly like an empty socket -- two completely different faults, one symptom.
// ---------------------------------------------------------------------------

// New chips, per THIS board's fill policy. Each region gets its own rng stream so
// that adding a region does not change the bytes in the regions already there --
// `seed` has to mean something stable, or it is not reproducible, and being
// reproducible is the only reason it exists.
void MemoryBoard::fillRegion(size_t idx) {
    const Region& r = regions_[idx];
    if (r.kind != RegionKind::Ram) return;

    std::mt19937_64 rng(seed_ ^ (0x9E3779B97F4A7C15ULL * (idx + 1)));
    for (int b = 0; b < banks_; ++b)
        for (uint32_t k = 0; k < r.size; ++k)
            store_[(size_t)b * 0x10000 + r.at + k] =
                (fill_ == Fill::Zero) ? 0x00 : (uint8_t)(rng() & 0xFF);
}

void MemoryBoard::fillRam() {
    for (size_t i = 0; i < regions_.size(); ++i) fillRegion(i);
}

// Size the store to the card's banks. Growing it is ADDING CHIPS -- a new bank
// plane is real RAM and comes up per `fill`, exactly like every other region.
// Existing bytes are never disturbed: only POWER loses memory.
void MemoryBoard::growStore() {
    size_t want = (size_t)banks_ * 0x10000;
    if (store_.size() == want) return;
    size_t had = store_.size();
    store_.resize(want, 0x00);
    if (want > had) fillRam();  // the new planes; the old bytes are already right
}

void MemoryBoard::rebuildPageMap() {
    for (int& o : owner_) o = -1;
    for (size_t i = 0; i < regions_.size(); ++i) {
        const Region& r = regions_[i];
        size_t first = page(r.at);
        size_t last = page((uint16_t)(r.at + r.size - 1));
        for (size_t p = first; p <= last && p < 256; ++p) owner_[p] = (int)i;
    }
    growStore();
}

// ---------------------------------------------------------------------------
// The bus interface. This is the whole board.
// ---------------------------------------------------------------------------

bool MemoryBoard::assertsPhantom(const BusCycle& c) const {
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    const Region* r = owner(c.addr);
    if (!r || r->kind != RegionKind::Rom) return false;  // only my ROM shadows anything
    if (phantom_ == PhantomAssert::All) return true;
    return phantom_ == PhantomAssert::Read && c.type == Cycle::MemRead;
}

bool MemoryBoard::decodes(const BusCycle& c) const {
    // A bank-select port, if this card has one. Write-only on all five.
    if (c.type == Cycle::IoWrite && bankType_ != BankType::None)
        return c.port() == bankSpec(bankType_).port;

    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;

    // SOMEONE ELSE is pulling PHANTOM* and I am strapped to honor it, so I take
    // MYSELF off the bus. Nobody arbitrated; the asserting board is simply the
    // only one still answering.
    //
    // `!assertsPhantom(c)` is load-bearing and was a real bug: a ROM card pulls
    // PHANTOM* to shadow the RAM under it, and if it also HONORED its own
    // assertion it would switch itself off -- so nobody would drive the address
    // and the ROM would read back as FF. A card does not shut itself off with a
    // signal it is itself driving. This stays board-local knowledge: the board
    // is asking about its OWN output pin, and the bus is still not involved.
    if (c.phantom && honorsPhantom_ && !assertsPhantom(c)) return false;

    const Region* r = owner(c.addr);
    if (!r) return false;  // unpopulated page / empty socket -> floats to 0xFF

    // ***THE LINE THE WHOLE DESIGN RESTS ON***
    // A ROM does not reject a write. It never answers the cycle.
    if (c.type == Cycle::MemWrite && r->kind == RegionKind::Rom) return false;

    return true;
}

uint8_t MemoryBoard::read(const BusCycle& c) {
    // The bank port is write-only: decodes() never claims an IoRead of it, so this
    // board is not driving that cycle, nobody else is either, and the BUS floats it
    // to 0xFF. That is the bus's job and it does it without being told. This board
    // does not get to have an opinion about a cycle it did not answer.
    return store_[plane(c.addr)];
}

void MemoryBoard::write(const BusCycle& c) {
    if (c.type == Cycle::IoWrite) {
        const BankSpec& s = bankSpec(bankType_);
        uint8_t d = (uint8_t)(c.data & s.mask);
        int want = -1;

        if (!s.oneHot) {
            want = d;  // binary: the byte IS the bank number
        } else {
            // One-hot. The Vector Graphic card also decodes 0x41 and 0x42 as
            // banks 0 and 1 -- bit 6 is ignored. That is not a documented Vector
            // feature; it is that OASIS WRITES THOSE VALUES and the card
            // tolerates them. Get it wrong and OASIS does not boot, and it fails
            // in the worst way: a select that lands on the wrong plane, so the
            // machine runs and then behaves insanely later.
            uint8_t h = (bankType_ == BankType::Vram) ? (uint8_t)(d & 0xBF) : d;
            for (int i = 0; i < 8; ++i)
                if (h == (uint8_t)(1u << i)) {
                    want = i;
                    break;
                }
        }

        if (want < 0 || want >= banks_) {
            // NOT silently swallowed. A select the card cannot decode is nearly
            // always a bug in the guest or in your bank_type, and a silent one
            // is hours of your life.
            char buf[128];
            std::snprintf(buf, sizeof buf,
                          "bank: invalid select 0x%02X for %s (%s). bank unchanged (still %d).",
                          c.data, s.name, id.c_str(), bank_);
            log_.push_back(buf);
            return;
        }
        bank_ = want;
        return;
    }

    // No check. None. A real static RAM chip selected with WE asserted STORES
    // THE BYTE; it has no opinion about who asked. And a write can only reach
    // here if decodes() let it -- which a rom region never does.
    store_[plane(c.addr)] = c.data;
}

// ---------------------------------------------------------------------------
// Lifecycle (DESIGN.md 6)
// ---------------------------------------------------------------------------

void MemoryBoard::reset(Reset) {
    // POC* and RESET* do the SAME thing here, and it is not much: the bank latch
    // clears. NEITHER TOUCHES ONE BYTE OF RAM. A RAM chip has no POC* pin, and
    // memory survives a reset on a real machine -- that is why you can reset out
    // of a hung program and still DUMP what it was doing.
    bank_ = 0;
}

void MemoryBoard::power() {
    // Power APPLIED. This is the only event that loses RAM -- and what the RAM
    // comes back up holding is THIS BOARD'S business (`fill`), not the bus's.
    //
    // Real static RAM does not come up zeroed. `fill = random` is the honest
    // default, because software that ASSUMES zeroed memory is buggy software, and
    // a zero-filling simulator will never once catch it.
    store_.assign((size_t)banks_ * 0x10000, 0x00);
    fillRam();
    for (size_t i = 0; i < regions_.size(); ++i) {
        if (regions_[i].kind != RegionKind::Rom) continue;
        std::string err;
        if (!loadRomRegion(i, err)) log_.push_back("power: " + err);
    }
    bank_ = 0;
    enabled_ = true;
}

bool MemoryBoard::blit(const Image& img, std::string& err) {
    for (const auto& [a, b] : img.bytes) {
        if (a >= store_.size()) {
            err = "image runs past this board's store";
            return false;
        }
        store_[a] = b;
    }
    return true;
}

std::vector<std::string> MemoryBoard::takeLog() {
    auto v = std::move(log_);
    log_.clear();
    return v;
}

// ---------------------------------------------------------------------------
// Reflection -- SET/SHOW/TOML/MCP/completion all come from here and nowhere else
// ---------------------------------------------------------------------------

std::vector<Property> MemoryBoard::properties() {
    std::vector<Property> p;

    {
        Property x;
        x.name = "honors_phantom";
        x.help = "A JUMPER. Another board pulls PHANTOM* -- do I switch off?";
        x.kind = Kind::Bool;
        x.runtime = false;
        x.get = [this] { return Value::ofBool(honorsPhantom_); };
        x.set = [this](const Value& v, std::string&) {
            honorsPhantom_ = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "phantom";
        x.help = "What I ASSERT over my rom regions: none | read | all";
        x.kind = Kind::Enum;
        x.choices = {"none", "read", "all"};
        x.runtime = true;
        x.get = [this] {
            return Value::ofStr(phantom_ == PhantomAssert::None   ? "none"
                                : phantom_ == PhantomAssert::Read ? "read"
                                                                  : "all");
        };
        x.set = [this](const Value& v, std::string&) {
            phantom_ = v.s() == "none"   ? PhantomAssert::None
                       : v.s() == "read" ? PhantomAssert::Read
                                         : PhantomAssert::All;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "bank_type";
        x.help = "none|eram|vram|cram|hram|b810 -- five real cards, no two alike";
        x.kind = Kind::Enum;
        x.choices = {"none", "eram", "vram", "cram", "hram", "b810"};
        x.runtime = false;
        x.get = [this] { return Value::ofStr(bankSpec(bankType_).name); };
        x.set = [this](const Value& v, std::string& err) {
            BankType t;
            if (!parseBankType(v.s(), t)) {
                err = "unknown bank type";
                return false;
            }
            for (const auto& r : regions_)
                if (r.kind == RegionKind::Rom && t != BankType::None) {
                    err = "this card has a rom region; banking a card with ROM is unsourced "
                          "and rejected (docs/boards/memory.md)";
                    return false;
                }
            bankType_ = t;
            banks_ = bankSpec(t).banks;
            bank_ = 0;
            rebuildPageMap();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "banks";
        x.help = "How many banks this card has (set by bank_type)";
        x.kind = Kind::Int;
        x.runtime = false;
        x.get = [this] { return Value::ofInt(banks_); };
        x.set = [](const Value&, std::string& err) {
            err = "banks is set by bank_type -- the card decides, not you";
            return false;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "bank";
        x.help = "The live bank";
        x.kind = Kind::Int;
        x.runtime = true;
        x.min = 0;
        x.max = 15;
        x.get = [this] { return Value::ofInt(bank_); };
        x.set = [this](const Value& v, std::string& err) {
            if (v.i() >= banks_) {
                err = "this card has " + std::to_string(banks_) + " bank(s)";
                return false;
            }
            bank_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "fill";
        x.help = "RAM contents at power-on: zero | random (real RAM is not zeroed)";
        x.kind = Kind::Enum;
        x.choices = {"zero", "random"};
        x.runtime = false;
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
        x.help = "Seed for fill=random. Goes in the snapshot, or replay is dead.";
        x.kind = Kind::Int;
        x.runtime = false;
        x.get = [this] { return Value::ofInt((long long)seed_); };
        x.set = [this](const Value& v, std::string&) {
            seed_ = (uint64_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "pages";
        x.help = "Composite page map -- which pages this card answers for (derived)";
        x.kind = Kind::Str;
        x.runtime = false;
        x.get = [this] {
            std::string s;
            int run = -1;
            for (int i = 0; i <= 256; ++i) {
                bool own = (i < 256) && owner_[i] >= 0;
                if (own && run < 0) run = i;
                if (!own && run >= 0) {
                    char buf[32];
                    std::snprintf(buf, sizeof buf, "%s%04X-%04X", s.empty() ? "" : ",",
                                  run << 8, (i << 8) - 1);
                    s += buf;
                    run = -1;
                }
            }
            return Value::ofStr(s.empty() ? "(none)" : s);
        };
        x.set = [](const Value&, std::string& err) {
            err = "pages is derived from the regions";
            return false;
        };
        p.push_back(std::move(x));
    }
    return p;
}

// ---------------------------------------------------------------------------
// Introspection and sub-units
// ---------------------------------------------------------------------------

std::vector<MapEntry> MemoryBoard::memMap() const {
    std::vector<MapEntry> out;
    for (const auto& r : regions_) {
        MapEntry e;
        e.lo = r.at;
        e.hi = r.at + r.size - 1;
        e.what = (r.kind == RegionKind::Rom) ? "rom" : "ram";
        if (r.kind == RegionKind::Rom) {
            e.note = r.mount;
            if (phantom_ != PhantomAssert::None)
                e.note += "  phantom:" +
                          std::string(phantom_ == PhantomAssert::Read ? "read" : "all");
        } else if (bankType_ != BankType::None) {
            e.note = "bank " + std::to_string(bank_) + " of " + std::to_string(banks_);
        }
        out.push_back(e);
    }
    return out;
}

std::vector<MapEntry> MemoryBoard::ioMap() const {
    if (bankType_ == BankType::None) return {};
    const BankSpec& s = bankSpec(bankType_);
    MapEntry e;
    e.lo = e.hi = s.port;
    e.what = "write";
    e.note = std::string("bank select (") + s.card + ", " +
             (s.oneHot ? "one-hot" : "binary") + ", " + std::to_string(s.banks) + " banks)";
    return {e};
}

bool MemoryBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "region") {
        err = "memory has no [[board." + table + "]] table";
        return false;
    }
    Region r;
    bool haveType = false;
    for (const auto& [k, v] : kv) {
        if (k == "type") {
            if (v == "ram") r.kind = RegionKind::Ram;
            else if (v == "rom") r.kind = RegionKind::Rom;
            else {
                err = "region type must be ram or rom, got '" + v + "'";
                return false;
            }
            haveType = true;
        } else if (k == "at") {
            // An address. The machine sees it, so it is HEX: `at=F000` is F000h,
            // and it does not need a 0x to be believed.
            long long n;
            if (!parseNumber(v, n, err, 16)) return false;
            r.at = (uint16_t)n;
        } else if (k == "size") {
            // A size. The machine never sees it, so it is DECIMAL -- and K/M come
            // free from the one parser: "48K", "1024", "2M", or "0x400" if you say so.
            long long n;
            if (!parseNumber(v, n, err, 10)) return false;
            r.size = (uint32_t)n;
        } else if (k == "mount") {
            r.mount = v;
        } else {
            err = "unknown region key '" + k + "'";
            return false;
        }
    }
    if (!haveType) {
        err = "region needs `type` (ram or rom)";
        return false;
    }
    return addRegion(std::move(r), err);
}

bool MemoryBoard::mount(int unit, const std::string& path, bool ro, std::string& err) {
    (void)ro;
    if (unit < 0 || (size_t)unit >= regions_.size()) {
        err = "no region " + std::to_string(unit) + " on " + id;
        return false;
    }
    if (regions_[unit].kind != RegionKind::Rom) {
        err = "region " + std::to_string(unit) + " is ram -- nothing to mount. "
              "(To put bytes in RAM: LOAD <file>.)";
        return false;
    }
    std::string saved = regions_[unit].mount;
    regions_[unit].mount = path;
    if (!loadRomRegion((size_t)unit, err)) {
        regions_[unit].mount = saved;
        return false;
    }
    rebuildPageMap();
    return true;
}

bool MemoryBoard::dismount(int unit, std::string& err) {
    if (unit < 0 || (size_t)unit >= regions_.size()) {
        err = "no region " + std::to_string(unit) + " on " + id;
        return false;
    }
    if (regions_[unit].kind != RegionKind::Rom) {
        err = "region " + std::to_string(unit) + " is ram";
        return false;
    }
    // Pulling the chip. The socket is now EMPTY, so the board stops decoding
    // those pages entirely and they float to 0xFF -- which is exactly what an
    // empty socket does on the bench.
    regions_.erase(regions_.begin() + unit);
    rebuildPageMap();
    return true;
}

} // namespace altair
