#include "core/roms.h"

#include <cctype>

namespace altair {

const BuiltinRom* findRom(const std::string& name) {
    std::string want;
    for (char c : name) want += (char)std::tolower((unsigned char)c);
    for (const auto& r : builtinRoms())
        if (want == r.name) return &r;
    return nullptr;
}

bool decodeRom(const BuiltinRom& r, uint32_t at, Image& out, std::string& err) {
    std::span<const uint8_t> bytes((const uint8_t*)r.data, r.size);
    if (r.format == Format::Hex) {
        if (!loadHex(bytes, out, err)) {
            err = std::string("builtin:") + r.name + " (" + r.file + "): " + err;
            return false;
        }
        return true;
    }
    loadBin(bytes, at, out);
    return true;
}

} // namespace altair
