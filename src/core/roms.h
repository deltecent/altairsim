#pragma once
//
// Built-in ROMs (DESIGN.md 10.3.1).
//
// The image file from roms/<NAME>/ is embedded VERBATIM -- the generated table
// holds the bytes of DBL.HEX, not a decoded image. The decode happens here, at
// runtime, with the same Intel HEX parser that backs LOAD. A built-in and a
// mounted file therefore travel identical code, and cannot drift.
//
// Provenance for every ROM is in docs/roms.md, and a test checks each CRC32.
// A ROM image is a hardware fact (DESIGN.md 0.1): an embedded blob of unknown
// lineage is the worst kind of second-hand fact, because everything above it
// gets debugged against the wrong ground truth and it looks like a software bug
// for a very long time.

#include "core/hex.h"

#include <cstdint>
#include <span>
#include <string>

namespace altair {

enum class Format { Hex, Bin };

struct BuiltinRom {
    const char* name;      // "dbl" -- what `mount = "builtin:dbl"` names
    const char* file;      // "DBL.HEX" -- what it came from, for SHOW ROMS
    Format format;
    const unsigned char* data;
    size_t size;           // bytes of the FILE, not of the decoded image
};

std::span<const BuiltinRom> builtinRoms();  // defined by the generated TU

const BuiltinRom* findRom(const std::string& name);

// Decode a built-in into an Image. `at` is used only for Format::Bin, since a
// flat binary carries no addresses; a HEX file places itself.
bool decodeRom(const BuiltinRom& r, uint32_t at, Image& out, std::string& err);

} // namespace altair
