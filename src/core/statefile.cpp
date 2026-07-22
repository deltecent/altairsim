#include "core/statefile.h"

namespace altair {

// ---- StateWriter -- low byte first, always. ----

void StateWriter::u16(uint16_t v) {
    buf_.push_back((uint8_t)(v & 0xFF));
    buf_.push_back((uint8_t)(v >> 8));
}

void StateWriter::u32(uint32_t v) {
    buf_.push_back((uint8_t)(v & 0xFF));
    buf_.push_back((uint8_t)((v >> 8) & 0xFF));
    buf_.push_back((uint8_t)((v >> 16) & 0xFF));
    buf_.push_back((uint8_t)((v >> 24) & 0xFF));
}

void StateWriter::u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) buf_.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}

void StateWriter::str(const std::string& s) {
    u32((uint32_t)s.size());
    buf_.insert(buf_.end(), s.begin(), s.end());
}

void StateWriter::raw(const uint8_t* p, size_t n) {
    buf_.insert(buf_.end(), p, p + n);
}

void StateWriter::blob(const std::vector<uint8_t>& v) {
    u32((uint32_t)v.size());
    buf_.insert(buf_.end(), v.begin(), v.end());
}

// ---- StateReader -- the mirror, bounds-checked. ----

bool StateReader::want(size_t n) {
    if (!ok_) return false;
    if (pos_ + n > n_) {
        ok_ = false;
        err_ = "snapshot is truncated";
        return false;
    }
    return true;
}

uint8_t StateReader::u8() {
    if (!want(1)) return 0;
    return p_[pos_++];
}

uint16_t StateReader::u16() {
    if (!want(2)) return 0;
    uint16_t v = (uint16_t)p_[pos_] | ((uint16_t)p_[pos_ + 1] << 8);
    pos_ += 2;
    return v;
}

uint32_t StateReader::u32() {
    if (!want(4)) return 0;
    uint32_t v = (uint32_t)p_[pos_] | ((uint32_t)p_[pos_ + 1] << 8) |
                 ((uint32_t)p_[pos_ + 2] << 16) | ((uint32_t)p_[pos_ + 3] << 24);
    pos_ += 4;
    return v;
}

uint64_t StateReader::u64() {
    if (!want(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p_[pos_ + i] << (8 * i);
    pos_ += 8;
    return v;
}

std::string StateReader::str() {
    uint32_t n = u32();
    if (!want(n)) return {};
    std::string s((const char*)p_ + pos_, n);
    pos_ += n;
    return s;
}

void StateReader::raw(uint8_t* out, size_t n) {
    if (!want(n)) return;
    for (size_t i = 0; i < n; ++i) out[i] = p_[pos_ + i];
    pos_ += n;
}

std::vector<uint8_t> StateReader::blob() {
    uint32_t n = u32();
    if (!want(n)) return {};
    std::vector<uint8_t> v(p_ + pos_, p_ + pos_ + n);
    pos_ += n;
    return v;
}

} // namespace altair
