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

bool looksLikeSrec(std::span<const uint8_t> d) {
    size_t i = 0;
    while (i < d.size() && (d[i] == ' ' || d[i] == '\r' || d[i] == '\n' || d[i] == '\t')) ++i;
    if (i >= d.size() || (d[i] != 'S' && d[i] != 's')) return false;
    // 'S', a type digit 0-9, then a count byte of hex digits.
    if (i + 1 >= d.size() || d[i + 1] < '0' || d[i + 1] > '9') return false;
    for (size_t k = i + 2; k < d.size() && k < i + 4; ++k)
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

        // Ctrl-Z (0x1A) is the CP/M / DOS soft end-of-file, and period HEX files
        // are routinely padded with a run of it (AMON.HEX, for one, ends in 118
        // of them). It is not part of Intel HEX, so treat it as the end of the
        // stream rather than choking on a "missing ':'" a byte later.
        if (text[i] == 0x1A) break;
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
            // The FIRST data record's address, in file order -- LOAD's AT anchors to
            // it. Grab it here because `bytes` is sorted and cannot answer this later.
            if (!out.hasFirst) {
                out.hasFirst = true;
                out.first = base + addr;
            }
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

bool loadSrec(std::span<const uint8_t> text, Image& out, std::string& err) {
    size_t i = 0;
    int recNo = 0;
    bool sawTerm = false;

    auto fail = [&](const std::string& what) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "record %d: ", recNo);
        err = buf + what;
        return false;
    };

    // Address bytes carried by each S-record type; -1 marks a type we reject.
    // S0 header, S1/2/3 data, S5/6 record count, S7/8/9 termination -> start.
    auto addrLen = [](int t) -> int {
        switch (t) {
        case 0: case 1: case 5: case 9: return 2;
        case 2: case 6: case 8: return 3;
        case 3: case 7: return 4;
        default: return -1;  // S4 is reserved; anything else is not S-record
        }
    };

    while (i < text.size()) {
        while (i < text.size() && (text[i] == '\r' || text[i] == '\n' || text[i] == ' ' ||
                                   text[i] == '\t'))
            ++i;
        if (i >= text.size()) break;

        // Ctrl-Z (0x1A) is the CP/M / DOS soft end-of-file; tolerate a trailing run
        // of it exactly as loadHex does.
        if (text[i] == 0x1A) break;
        ++recNo;

        if (text[i] != 'S' && text[i] != 's') return fail("expected 'S' to start a record");
        ++i;
        if (i >= text.size()) return fail("truncated after 'S'");
        int type = hexNib(text[i]);
        if (type < 0 || type > 9) return fail("record type is not a digit");
        ++i;
        int al = addrLen(type);
        if (al < 0) return fail("S" + std::to_string(type) + " is not a supported record type");

        // The count field and everything it counts (address + data + checksum).
        std::vector<uint8_t> rec;
        while (i + 1 < text.size()) {
            int h = hexNib(text[i]), l = hexNib(text[i + 1]);
            if (h < 0 || l < 0) break;
            rec.push_back((uint8_t)((h << 4) | l));
            i += 2;
        }
        if (rec.empty()) return fail("record too short");

        uint8_t count = rec[0];
        // rec[0] is the count byte itself; the count covers what FOLLOWS it.
        if (rec.size() != (size_t)count + 1)
            return fail("count byte says " + std::to_string(count) + " bytes but record holds " +
                        std::to_string((long)rec.size() - 1));
        if (count < (uint8_t)(al + 1))
            return fail("count too small for a type S" + std::to_string(type) + " record");

        // Checksum: count + address + data + checksum all sum to 0xFF.
        uint8_t sum = 0;
        for (uint8_t b : rec) sum = (uint8_t)(sum + b);
        if (sum != 0xFF) return fail("bad checksum");

        uint32_t addr = 0;
        for (int k = 0; k < al; ++k) addr = (addr << 8) | rec[1 + k];
        int dataLen = (int)count - al - 1;  // minus address bytes, minus checksum

        switch (type) {
        case 1: case 2: case 3:  // data
            if (!out.hasFirst) {
                out.hasFirst = true;
                out.first = addr;
            }
            for (int k = 0; k < dataLen; ++k) out.bytes[addr + (uint32_t)k] = rec[1 + al + k];
            break;
        case 7: case 8: case 9:  // termination carries the entry address
            out.hasStart = true;
            out.start = addr;
            sawTerm = true;
            break;
        case 0: case 5: case 6:  // header / record-count -- informational, skip
            break;
        }
        if (sawTerm) break;
    }

    if (out.bytes.empty() && !sawTerm) {
        err = "no records found -- is this a Motorola S-record file?";
        return false;
    }
    return true;
}

void loadBin(std::span<const uint8_t> data, uint32_t at, Image& out) {
    for (size_t k = 0; k < data.size(); ++k) out.bytes[at + (uint32_t)k] = data[k];
    out.hasFirst = true;  // a flat binary's first byte is where you put it
    out.first = at;
}

void relocateTo(Image& img, uint32_t at) {
    if (!img.hasFirst) return;  // nothing to anchor to

    // uint16_t so the subtraction wraps by construction rather than by an `if`.
    uint16_t delta = (uint16_t)(at - img.first);
    if (delta == 0) return;

    Image b;
    b.hasStart = img.hasStart;
    b.start = img.hasStart ? (uint32_t)((img.start + delta) & 0xFFFF) : 0;
    b.hasFirst = true;
    b.first = at & 0xFFFF;
    for (const auto& [A, v] : img.bytes) b.bytes[(A + delta) & 0xFFFF] = v;
    img = b;
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

std::string saveSrec(const Image& img, int recLen) {
    if (recLen < 1) recLen = 16;
    if (recLen > 250) recLen = 250;  // 2 addr + data + 1 checksum must fit a count byte

    std::string s;
    char buf[16];
    // type: 1 for a data (S1) record, 9 for the terminator (S9). Both carry a
    // 16-bit address, so the count is 2 (address) + data + 1 (checksum).
    auto emit = [&](int type, uint16_t addr, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> rec;
        rec.push_back((uint8_t)(data.size() + 3));  // count: addr(2) + data + cksum(1)
        rec.push_back((uint8_t)(addr >> 8));
        rec.push_back((uint8_t)(addr & 0xFF));
        for (uint8_t b : data) rec.push_back(b);
        uint8_t sum = 0;
        for (uint8_t b : rec) sum = (uint8_t)(sum + b);
        rec.push_back((uint8_t)(0xFF - sum));  // one's complement of the running sum
        s += 'S';
        s += (char)('0' + type);
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
        emit(1, (uint16_t)start, run);
    }
    emit(9, (uint16_t)(img.hasStart ? img.start : 0), {});
    return s;
}

} // namespace altair
