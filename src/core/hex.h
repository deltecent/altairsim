#pragma once
//
// Intel HEX (DESIGN.md 10.3) -- ONE implementation, four front ends: the LOAD
// command, the TOML `mount` of a ROM region, the built-in ROM registry, and the
// MCP mem_load tool. A built-in ROM is parsed by the same code as a file on
// disk, so the two cannot drift.

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace altair {

// A sparse image. Sparse because Intel HEX is: a file may place 16 bytes at
// FF00 and 16 more at 2C00 and say nothing about the gap, and pretending
// otherwise (by zero-filling) would invent bytes the file never contained.
struct Image {
    std::map<uint32_t, uint8_t> bytes;
    bool hasStart = false;
    uint32_t start = 0;

    // WHERE THE FILE STARTED TALKING -- the load address of its FIRST data record,
    // which is what `LOAD <file> AT <addr>` anchors to: the FIRST address record, and
    // not the lowest byte (Patrick, 2026-07-17).
    //
    // It has to be captured while PARSING, because `bytes` is a map and a map is
    // sorted: by the time anyone reads this image the file's ORDER is gone, and
    // lo() answers a different question. The two agree on every hex file whose
    // records ascend -- which is nearly all of them, and is exactly why getting
    // this wrong would be invisible until the day it was not.
    //
    // Not `start`: that is the EXECUTION address from a type 03/05 record, which is a
    // fact about the program. This is a fact about the file.
    bool hasFirst = false;
    uint32_t first = 0;

    bool empty() const { return bytes.empty(); }
    uint32_t lo() const { return bytes.empty() ? 0 : bytes.begin()->first; }
    uint32_t hi() const { return bytes.empty() ? 0 : bytes.rbegin()->first; }
    size_t size() const { return bytes.size(); }
    bool contiguous() const { return !bytes.empty() && bytes.size() == hi() - lo() + 1; }

    // lo()..hi() as a flat vector; gaps become `fill`.
    std::vector<uint8_t> flat(uint8_t fill = 0xFF) const;
};

// True if this looks like Intel HEX (leading ':' and hex digits). Backs the
// autodetect that lets LOAD take either format with no FORMAT= argument.
bool looksLikeHex(std::span<const uint8_t> data);

// Every record's checksum is verified. A bad record FAILS THE LOAD and names
// the record number -- a silently truncated load is a miserable bug to chase,
// and the whole point of a checksum is to not have that bug.
bool loadHex(std::span<const uint8_t> text, Image& out, std::string& err);

// A flat binary has no addresses of its own, so it needs one.
void loadBin(std::span<const uint8_t> data, uint32_t at, Image& out);

// `LOAD <file> AT <addr>` for a file that already carries addresses: move the whole
// image so its FIRST DATA RECORD lands at `at`, and shift everything else by the same
// delta. So AT means one thing whatever the format -- PUT IT HERE.
//
// Anchored to `first` (file order), NOT to lo() (lowest address). On the ordinary
// ascending hex file those are the same byte, which is exactly why anchoring to the
// wrong one would be invisible until the file that descends.
//
// THE DELTA WRAPS, MODULO 64K, and that is the format's own arithmetic rather than a
// convenience: hexfrmt defines the address as "[DRLO + DRI] MOD 64K" and says wraparound
// "from 0FFFFH to 00000H results in wrapping around from the end to the beginning". It is
// also what the hardware does -- an 8080 has sixteen address lines and no seventeenth to
// carry into. A file whose first record is F000, loaded AT 0, puts its F800 record at
// 0800 and an E000 record back up at F000.
//
// A no-op on an image with no data records (nothing to anchor to).
void relocateTo(Image& img, uint32_t at);

// Round-trip is a test case: saveHex then loadHex must reproduce byte-for-byte.
std::string saveHex(const Image& img, int recLen = 16);

} // namespace altair
