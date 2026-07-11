#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace altair {
uint32_t crc32(std::span<const uint8_t> data);
}
