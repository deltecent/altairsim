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

// Round-trip is a test case: saveHex then loadHex must reproduce byte-for-byte.
std::string saveHex(const Image& img, int recLen = 16);

} // namespace altair
