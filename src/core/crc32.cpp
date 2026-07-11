#include "core/crc32.h"

namespace altair {

// Ordinary CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) -- the one `crc32`
// and Python's zlib produce, so a value in docs/roms.md can be checked with a
// one-liner by anyone who doubts us.
uint32_t crc32(std::span<const uint8_t> data) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace altair
