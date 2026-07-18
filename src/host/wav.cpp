#include "host/wav.h"

#include <cmath>
#include <cstring>

namespace altair {
namespace {

// Little-endian readers. WAV is little-endian everywhere (it is RIFF, not RIFX), and
// doing this by hand rather than memcpy-ing a struct is what keeps the file portable
// to a big-endian host without a single #ifdef.
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

void wr16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x & 0xFF));
    v.push_back(uint8_t(x >> 8));
}
void wr32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x & 0xFF));
    v.push_back(uint8_t((x >> 8) & 0xFF));
    v.push_back(uint8_t((x >> 16) & 0xFF));
    v.push_back(uint8_t(x >> 24));
}
void wrTag(std::vector<uint8_t>& v, const char* t) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(t[i]));
}

bool tagIs(const uint8_t* p, const char* t) { return std::memcmp(p, t, 4) == 0; }

constexpr uint16_t kPcm        = 0x0001;
constexpr uint16_t kFloat      = 0x0003;
constexpr uint16_t kExtensible = 0xFFFE;

} // namespace

bool looksLikeWav(const uint8_t* data, size_t n) {
    return n >= 12 && tagIs(data, "RIFF") && tagIs(data + 8, "WAVE");
}

bool parseWav(const uint8_t* data, size_t n, AudioBuffer& out, std::string& err) {
    if (!looksLikeWav(data, n)) {
        err = "not a RIFF/WAVE file";
        return false;
    }

    // What `fmt ` told us, and whether it told us at all. A `data` chunk that
    // arrives before `fmt ` is legal RIFF and meaningless audio, so we hold the
    // data's extent and decode it only once the walk is done.
    bool     haveFmt = false;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t   dataOff = 0, dataLen = 0;
    bool     haveData = false;

    // THE WALK. Chunks are a list; neither order nor completeness is promised.
    size_t p = 12;
    while (p + 8 <= n) {
        const uint8_t* hdr = data + p;
        uint32_t       sz  = rd32(hdr + 4);
        size_t         body = p + 8;

        // A size that runs off the end is a truncated file. Clamp rather than
        // refuse: a recording cut short is still most of a recording, and the tape
        // it holds may well load. The demodulator will simply see less tape.
        size_t avail = (body <= n) ? (n - body) : 0;
        size_t take  = (sz <= avail) ? sz : avail;

        if (tagIs(hdr, "fmt ") && take >= 16) {
            format   = rd16(data + body + 0);
            channels = rd16(data + body + 2);
            rate     = rd32(data + body + 4);
            bits     = rd16(data + body + 14);

            // EXTENSIBLE is a wrapper, not a format: the real tag is the first two
            // bytes of the sub-format GUID at offset 24. Unwrap it and carry on as
            // if the file had been tagged honestly in the first place.
            if (format == kExtensible && take >= 26) format = rd16(data + body + 24);
            haveFmt = true;
        } else if (tagIs(hdr, "data")) {
            dataOff  = body;
            dataLen  = take;
            haveData = true;
        }

        // ...AND THE PAD BYTE. Odd-sized chunks are followed by one byte that is not
        // counted in the size. Forgetting it desynchronizes every later chunk.
        p = body + sz + (sz & 1);
        if (sz > avail) break;  // truncated: nothing valid can follow
    }

    if (!haveFmt)  { err = "WAV has no fmt chunk"; return false; }
    if (!haveData) { err = "WAV has no data chunk"; return false; }
    if (!channels) { err = "WAV declares zero channels"; return false; }
    if (!rate)     { err = "WAV declares a zero sample rate"; return false; }

    if (format != kPcm && format != kFloat) {
        err = "WAV is compressed (format tag " + std::to_string(format) +
              "); only uncompressed PCM and IEEE float are supported";
        return false;
    }
    if (format == kFloat && bits != 32 && bits != 64) {
        err = "WAV float samples must be 32- or 64-bit";
        return false;
    }
    if (format == kPcm && bits != 8 && bits != 16 && bits != 24 && bits != 32) {
        err = "WAV has " + std::to_string(bits) + "-bit samples; expected 8, 16, 24 or 32";
        return false;
    }

    const size_t bytesPer = size_t(bits) / 8;
    const size_t frameSz  = bytesPer * channels;
    if (!frameSz) { err = "WAV frame size is zero"; return false; }

    const size_t frames = dataLen / frameSz;
    out.rate = rate;
    out.s.clear();
    out.s.reserve(frames);

    // ONE SAMPLE, ANY FORMAT, AS A FLOAT -- and then averaged across channels. The
    // 8-bit case is the one people get wrong: PCM 8-bit is UNSIGNED with 128 as
    // silence, and every other width is signed two's complement.
    for (size_t f = 0; f < frames; ++f) {
        const uint8_t* fp  = data + dataOff + f * frameSz;
        double         acc = 0.0;
        for (uint16_t c = 0; c < channels; ++c) {
            const uint8_t* q = fp + size_t(c) * bytesPer;
            double         v = 0.0;
            if (format == kFloat) {
                if (bits == 32) {
                    float ff;
                    uint32_t u = rd32(q);
                    std::memcpy(&ff, &u, 4);
                    v = ff;
                } else {
                    double dd;
                    uint64_t u = uint64_t(rd32(q)) | (uint64_t(rd32(q + 4)) << 32);
                    std::memcpy(&dd, &u, 8);
                    v = dd;
                }
            } else if (bits == 8) {
                v = (double(q[0]) - 128.0) / 128.0;
            } else if (bits == 16) {
                v = double(int16_t(rd16(q))) / 32768.0;
            } else if (bits == 24) {
                int32_t x = int32_t(uint32_t(q[0]) | (uint32_t(q[1]) << 8) |
                                    (uint32_t(q[2]) << 16));
                if (x & 0x800000) x -= 0x1000000;  // sign-extend 24 -> 32
                v = double(x) / 8388608.0;
            } else {
                v = double(int32_t(rd32(q))) / 2147483648.0;
            }
            acc += v;
        }
        out.s.push_back(float(acc / channels));
    }

    if (out.s.empty()) { err = "WAV contains no samples"; return false; }
    return true;
}

std::vector<uint8_t> buildWav(const AudioBuffer& in) {
    const uint32_t rate     = in.rate ? in.rate : 44100;
    const uint32_t nSamples = uint32_t(in.s.size());
    const uint32_t dataLen  = nSamples * 2;  // 16-bit mono

    std::vector<uint8_t> v;
    v.reserve(44 + dataLen);

    wrTag(v, "RIFF");
    wr32(v, 36 + dataLen);  // everything after this field
    wrTag(v, "WAVE");

    wrTag(v, "fmt ");
    wr32(v, 16);          // PCM fmt chunk is 16 bytes
    wr16(v, kPcm);
    wr16(v, 1);           // mono
    wr32(v, rate);
    wr32(v, rate * 2);    // byte rate
    wr16(v, 2);           // block align
    wr16(v, 16);          // bits

    wrTag(v, "data");
    wr32(v, dataLen);
    for (float f : in.s) {
        // Clamp before scaling. A modulated square/sine can sit at exactly 1.0, and
        // 1.0 * 32768 wraps to -32768 -- a full-scale positive sample rendered as
        // full-scale NEGATIVE, which is an audible click and a decode error.
        double d = f;
        if (d > 1.0) d = 1.0;
        if (d < -1.0) d = -1.0;
        wr16(v, uint16_t(int16_t(std::lround(d * 32767.0))));
    }

    // dataLen is always even (16-bit mono), so no pad byte is ever required here.
    return v;
}

} // namespace altair
