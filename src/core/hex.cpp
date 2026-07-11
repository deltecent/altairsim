#include "core/hex.h"

#include <cctype>
#include <cstdio>

namespace altair {

std::vector<uint8_t> Image::flat(uint8_t fill) const {
    if (bytes.empty()) return {};
    std::vector<uint8_t> v(hi() - lo() + 1, fill);
    for (const auto& [a, b] : bytes) v[a - lo()] = b;
    return v;
}

bool looksLikeHex(std::span<const uint8_t> d) {
    size_t i = 0;
    while (i < d.size() && (d[i] == ' ' || d[i] == '\r' || d[i] == '\n' || d[i] == '\t')) ++i;
    if (i >= d.size() || d[i] != ':') return false;
    // ':' plus at least a length byte of hex digits.
    for (size_t k = i + 1; k < d.size() && k < i + 3; ++k)
        if (!std::isxdigit(d[k])) return false;
    return true;
}

static int hexNib(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool loadHex(std::span<const uint8_t> text, Image& out, std::string& err) {
    size_t i = 0;
    int recNo = 0;
    uint32_t base = 0;  // from type 02/04
    bool sawEof = false;

    auto fail = [&](const std::string& what) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "record %d: ", recNo);
        err = buf + what;
        return false;
    };

    while (i < text.size()) {
        while (i < text.size() && (text[i] == '\r' || text[i] == '\n' || text[i] == ' ' ||
                                   text[i] == '\t'))
            ++i;
        if (i >= text.size()) break;
        ++recNo;

        if (text[i] != ':') return fail("expected ':' to start a record");
        ++i;

        // Read the record's hex digits into bytes.
        std::vector<uint8_t> rec;
        while (i + 1 < text.size()) {
            int h = hexNib(text[i]), l = hexNib(text[i + 1]);
            if (h < 0 || l < 0) break;
            rec.push_back((uint8_t)((h << 4) | l));
            i += 2;
        }
        if (rec.size() < 5) return fail("record too short");

        uint8_t len = rec[0];
        uint32_t addr = (uint32_t)((rec[1] << 8) | rec[2]);
        uint8_t type = rec[3];

        if (rec.size() != (size_t)len + 5)
            return fail("length byte says " + std::to_string(len) + " data bytes but record holds " +
                        std::to_string((long)rec.size() - 5));

        // Every record's checksum, every time. This is the one thing a HEX file
        // gives you for free and it is worth more than it costs.
        uint8_t sum = 0;
        for (uint8_t b : rec) sum = (uint8_t)(sum + b);
        if (sum != 0) return fail("bad checksum");

        switch (type) {
        case 0x00:  // data
            for (uint8_t k = 0; k < len; ++k) out.bytes[base + addr + k] = rec[4 + k];
            break;
        case 0x01:  // EOF
            sawEof = true;
            break;
        case 0x02:  // extended segment address
            if (len != 2) return fail("type 02 must carry 2 bytes");
            base = (uint32_t)(((rec[4] << 8) | rec[5]) << 4);
            break;
        case 0x03:  // start segment address (CS:IP)
            if (len != 4) return fail("type 03 must carry 4 bytes");
            out.hasStart = true;
            out.start = (uint32_t)((rec[6] << 8) | rec[7]);  // IP; CS ignored on an 8080
            break;
        case 0x04:  // extended linear address
            if (len != 2) return fail("type 04 must carry 2 bytes");
            base = (uint32_t)(((rec[4] << 8) | rec[5]) << 16);
            if (base > 0xFFFF) return fail("type 04 addresses beyond 64K; this is an 8080");
            break;
        case 0x05:  // start linear address
            if (len != 4) return fail("type 05 must carry 4 bytes");
            out.hasStart = true;
            out.start = (uint32_t)((rec[4] << 24) | (rec[5] << 16) | (rec[6] << 8) | rec[7]);
            break;
        default:
            return fail("unknown record type 0x" + std::to_string(type));
        }
        if (sawEof) break;
    }

    if (out.bytes.empty() && !sawEof) {
        err = "no records found -- is this an Intel HEX file?";
        return false;
    }
    return true;
}

void loadBin(std::span<const uint8_t> data, uint32_t at, Image& out) {
    for (size_t k = 0; k < data.size(); ++k) out.bytes[at + (uint32_t)k] = data[k];
}

std::string saveHex(const Image& img, int recLen) {
    if (recLen < 1) recLen = 16;
    if (recLen > 255) recLen = 255;

    std::string s;
    char buf[16];
    auto emit = [&](uint32_t addr, const std::vector<uint8_t>& data, uint8_t type) {
        std::vector<uint8_t> rec;
        rec.push_back((uint8_t)data.size());
        rec.push_back((uint8_t)(addr >> 8));
        rec.push_back((uint8_t)(addr & 0xFF));
        rec.push_back(type);
        for (uint8_t b : data) rec.push_back(b);
        uint8_t sum = 0;
        for (uint8_t b : rec) sum = (uint8_t)(sum + b);
        rec.push_back((uint8_t)(-(int)sum));
        s += ':';
        for (uint8_t b : rec) {
            std::snprintf(buf, sizeof buf, "%02X", b);
            s += buf;
        }
        s += '\n';
    };

    // Break at gaps, so a sparse image stays sparse on the way out.
    auto it = img.bytes.begin();
    while (it != img.bytes.end()) {
        uint32_t start = it->first;
        std::vector<uint8_t> run;
        uint32_t expect = start;
        while (it != img.bytes.end() && it->first == expect && (int)run.size() < recLen) {
            run.push_back(it->second);
            ++expect;
            ++it;
        }
        emit(start, run, 0x00);
    }
    if (img.hasStart) {
        std::vector<uint8_t> d{(uint8_t)(img.start >> 24), (uint8_t)(img.start >> 16),
                               (uint8_t)(img.start >> 8), (uint8_t)(img.start)};
        emit(0, d, 0x05);
    }
    emit(0, {}, 0x01);
    return s;
}

} // namespace altair
