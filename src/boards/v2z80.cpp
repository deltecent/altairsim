#include "boards/v2z80.h"

#include "core/hex.h"
#include "core/statefile.h"

#include <algorithm>
#include <cstdio>

namespace altair {

V2Z80Board::V2Z80Board() {
    std::fill(std::begin(rom_), std::end(rom_), (uint8_t)0xFF);  // unprogrammed EEPROM reads FF
}

// ---------------------------------------------------------------------------
// Decode. The board answers the D3H control latch (write-only) and, while enabled, reads in the
// F000-FFFF EEPROM window.
// ---------------------------------------------------------------------------
bool V2Z80Board::decodes(const BusCycle& c) const {
    if (c.type == Cycle::IoWrite) return c.port() == port_;
    if (c.type == Cycle::MemRead) return romEnabled_ && inWindow(c.addr);
    return false;
}

uint8_t V2Z80Board::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead && romEnabled_ && inWindow(c.addr)) return romByte(c.addr);
    return 0xFF;
}

// The EEPROM is ROM: it never decodes a write. The only write it cares about is the D3H latch,
// which selects the page and enables/disables the chip. A MemWrite into F000-FFFF is not ours
// and falls through to the RAM underneath the shadow.
void V2Z80Board::write(const BusCycle& c) {
    if (c.type != Cycle::IoWrite || c.port() != port_) return;
    highPage_ = (c.data & 0x02) != 0;         // bit 1 -> EEPROM A12 (page select) -- content only
    bool enabled = (c.data & 0x01) == 0;      // bit 0 = 1 inactivates the onboard EEPROM
    if (enabled != romEnabled_) {
        romEnabled_ = enabled;
        decodeChanged();                      // the board enters/leaves the F000-FFFF decode
    }
}

// While enabled the EEPROM shadows RAM for READS in its window (the RAM board steps aside and
// the ROM byte wins); writes are never shadowed, so they reach the RAM under the ROM.
bool V2Z80Board::assertsPhantom(const BusCycle& c) const {
    return c.type == Cycle::MemRead && romEnabled_ && inWindow(c.addr);
}

bool V2Z80Board::peek(uint16_t addr, uint8_t& out) const {
    if (romEnabled_ && inWindow(addr)) {
        out = romByte(addr);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lifecycle. The D3H latch powers up 0x00 -- EEPROM enabled, low page -- and any reset returns
// it there. The ROM bytes are host-backed config, re-read on power (DESIGN.md 13).
// ---------------------------------------------------------------------------
void V2Z80Board::reset(Reset) {
    bool changed = !romEnabled_;   // re-enabling the chip changes the F000-FFFF decode
    highPage_   = false;
    romEnabled_ = true;
    if (changed) decodeChanged();
}

void V2Z80Board::power() {
    loadRoms();
    reset(Reset::PowerOn);
}

void V2Z80Board::configChanged() {
    decodeChanged();   // `port` may have moved the I/O decode
}

// ---------------------------------------------------------------------------
// The two page images. `builtin:master0`/`master1` travel the SAME Intel HEX parser as a memory
// card's ROM region (DESIGN.md 10.3.1). Both are assembled ORG F000; the low page lands in the
// first 4K of the store, the high page in the second.
// ---------------------------------------------------------------------------
void V2Z80Board::loadRoms() {
    std::fill(std::begin(rom_), std::end(rom_), (uint8_t)0xFF);

    struct Page { const char* name; uint32_t base; };
    const Page pages[] = {{"master0", 0}, {"master1", kPageSize}};

    for (const auto& pg : pages) {
        const BuiltinRom* r = findRom(pg.name);
        if (!r) {
            say(id + ": no built-in ROM named '" + pg.name + "'. SHOW ROMS lists them.");
            continue;
        }
        Image       img;
        std::string err;
        if (!decodeRom(*r, kWindowBase, img, err)) {
            say(id + ": " + err);
            continue;
        }
        for (const auto& [a, b] : img.bytes)
            if (a >= kWindowBase && (uint32_t)a < kWindowBase + kPageSize)
                rom_[pg.base + (a - kWindowBase)] = b;
    }
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> V2Z80Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Page/ROM-control latch (D3H): bit1 = EEPROM A12 page select, bit0 = ROM inactivate";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> V2Z80Board::memMap() const {
    return {{(uint32_t)kWindowBase, (uint32_t)(kWindowBase + kPageSize - 1), "read",
             "monitor EEPROM (paged: OUT D3H bit1 selects low/high 4K; bit0=1 disables)"}};
}

std::vector<MapEntry> V2Z80Board::ioMap() const {
    return {{(uint32_t)port_, (uint32_t)port_, "write",
             "page/ROM control -- bit1 = EEPROM A12 (0 low / 1 high page), bit0 = ROM inactivate"}};
}

std::vector<std::string> V2Z80Board::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. Only the two runtime latches travel; the ROM bytes and the `port` strap
// are config and are already correct in a matching machine (re-read on power, DESIGN.md 13).
// ---------------------------------------------------------------------------
void V2Z80Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.boolean(highPage_);
    w.boolean(romEnabled_);
}

void V2Z80Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    highPage_   = r.boolean();
    romEnabled_ = r.boolean();
    decodeChanged();
}

} // namespace altair
