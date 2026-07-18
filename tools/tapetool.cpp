//
// tapetool -- read and write cassette audio, with no simulator attached.
//
// A TOOL, NOT A FEATURE, and it is here for the same reason gen-reference.cpp is: the
// shipping binary has no business carrying it, and the job it does is one you need
// BEFORE any of the simulator works. When a tape decodes badly the question is always
// "what is actually on it?", and the answer must not require booting a machine.
//
//   tapetool info   TAPE.WAV              what modulation, what rate, how clean
//   tapetool decode TAPE.WAV OUT.BIN      tones -> bytes
//   tapetool encode IN.BIN TAPE.WAV [fmt] bytes -> tones
//
// `info` trial-decodes every known format and ranks them, which is exactly what a
// mount does (host/tapecodec.h) -- so when a mount picks the wrong one, this shows you
// why in one line.

#include "host/tapemodem.h"
#include "host/wav.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

bool slurp(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool spit(const std::string& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(f);
}

int usage() {
    std::fprintf(stderr,
                 "usage: tapetool info   TAPE.WAV\n"
                 "       tapetool decode TAPE.WAV OUT.BIN [FORMAT]\n"
                 "       tapetool encode IN.BIN TAPE.WAV [FORMAT] [RATE]\n"
                 "\nFORMAT is one of:");
    for (const TapeFormat& f : tapeformats::all())
        std::fprintf(stderr, " %s", f.name);
    std::fprintf(stderr, "  (default: best match)\n");
    return 2;
}

// Rank every format by how cleanly it decodes. The same decision a mount makes.
std::vector<std::pair<TapeFormat, DemodResult>> rank(const AudioBuffer& a) {
    std::vector<std::pair<TapeFormat, DemodResult>> v;
    for (const TapeFormat& f : tapeformats::all()) v.push_back({f, demodulate(a, f)});
    std::sort(v.begin(), v.end(), [](const auto& x, const auto& y) {
        if (x.second.confidence() != y.second.confidence())
            return x.second.confidence() > y.second.confidence();
        return x.second.bytes.size() > y.second.bytes.size();  // break ties by yield
    });
    return v;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];

    if (cmd == "encode") {
        if (argc < 4) return usage();
        std::vector<uint8_t> in;
        if (!slurp(argv[2], in)) {
            std::fprintf(stderr, "tapetool: cannot read %s\n", argv[2]);
            return 1;
        }
        TapeFormat f = tapeformats::cuts1200();
        if (argc >= 5) {
            const TapeFormat* p = tapeformats::byName(argv[4]);
            if (!p) { std::fprintf(stderr, "tapetool: unknown format %s\n", argv[4]); return usage(); }
            f = *p;
        }
        const uint32_t rate = (argc >= 6) ? uint32_t(std::stoul(argv[5])) : 44100u;
        AudioBuffer    a    = modulate(in, f, rate);
        if (!spit(argv[3], buildWav(a))) {
            std::fprintf(stderr, "tapetool: cannot write %s\n", argv[3]);
            return 1;
        }
        std::printf("%s: %zu bytes -> %s, %.1fs at %u Hz\n", f.name, in.size(), argv[3],
                    a.seconds(), a.rate);
        return 0;
    }

    // Both remaining verbs read a WAV first.
    std::vector<uint8_t> raw;
    if (!slurp(argv[2], raw)) {
        std::fprintf(stderr, "tapetool: cannot read %s\n", argv[2]);
        return 1;
    }
    AudioBuffer a;
    std::string err;
    if (!parseWav(raw.data(), raw.size(), a, err)) {
        std::fprintf(stderr, "tapetool: %s: %s\n", argv[2], err.c_str());
        return 1;
    }

    if (cmd == "info") {
        std::printf("%s: %.1fs, %u Hz, %zu samples\n", argv[2], a.seconds(), a.rate,
                    a.s.size());
        for (const auto& [f, r] : rank(a))
            std::printf("  %-10s %7zu bytes  %5u framing errors  confidence %.3f\n",
                        f.name, r.bytes.size(), r.framingErrors, r.confidence());
        return 0;
    }

    if (cmd == "decode") {
        if (argc < 4) return usage();
        TapeFormat  f;
        DemodResult r;
        if (argc >= 5) {
            const TapeFormat* p = tapeformats::byName(argv[4]);
            if (!p) { std::fprintf(stderr, "tapetool: unknown format %s\n", argv[4]); return usage(); }
            f = *p;
            r = demodulate(a, f);
        } else {
            auto v = rank(a);
            f = v.front().first;
            r = std::move(v.front().second);
        }
        if (!spit(argv[3], r.bytes)) {
            std::fprintf(stderr, "tapetool: cannot write %s\n", argv[3]);
            return 1;
        }
        std::printf("%s: %s, %zu bytes, %u framing errors, confidence %.3f\n", argv[2],
                    f.name, r.bytes.size(), r.framingErrors, r.confidence());
        return 0;
    }

    return usage();
}
