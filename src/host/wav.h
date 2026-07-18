#pragma once
//
// RIFF/WAVE -- the container a cassette recording arrives in (DESIGN.md 7.3).
//
// THIS FILE KNOWS NOTHING ABOUT TAPES. It reads a WAV into samples and writes
// samples back out; what the samples MEAN is tapemodem.h's business, and what the
// bytes mean after that is the board's. The same layering media.h draws between
// "where the bytes are" and "what the bytes mean", one level further down: a WAV is
// a container, an FSK signal is a modulation, and a program image is a program
// image. Three questions, three files.
//
// MONO FLOAT IS THE ONLY THING THAT LEAVES HERE. Every reader below converts to
// -1..1 float and averages the channels away, because the demodulator has no use for
// stereo and no use for a sample format -- it wants a waveform. Doing the conversion
// once, here, is what keeps the modem free of container trivia.
//
// AND IT MUST SURVIVE A REAL ARCHIVE FILE. The reason this is not thirty lines:
//
//   * CHUNKS ARE A LIST, NOT A LAYOUT. `fmt ` is not necessarily first and `data`
//     is not necessarily second. Archive WAVs routinely carry `LIST`/`INFO` (the
//     ripping tool's name), `fact`, or a cue chunk, and a reader that assumes the
//     textbook order fails on files that are perfectly valid. So: walk the list.
//
//   * ODD-LENGTH CHUNKS ARE PADDED, AND THE PAD IS NOT IN THE SIZE. Miss it and
//     every chunk after the first odd one is read at a one-byte offset -- which
//     does not fail, it just quietly returns garbage.
//
//   * WAVE_FORMAT_EXTENSIBLE (0xFFFE) IS ORDINARY PCM WEARING A HAT. Anything
//     recorded by a modern tool may be tagged 0xFFFE with the real format in a
//     GUID's first two bytes. Refusing it would refuse files that are plain PCM.
//
// A REFUSAL IS ALWAYS SPECIFIC. Never a silent empty buffer: media.h's resolver
// contract is that a medium either opens or says why, and "this tape is empty" and
// "I do not read 32-bit float" must not look the same to the operator.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace altair {

// A waveform, mono, nominally -1..1. `rate` is samples per second.
struct AudioBuffer {
    uint32_t           rate = 0;
    std::vector<float> s;

    double seconds() const { return rate ? double(s.size()) / rate : 0.0; }
};

// False, with `err` set, on anything this cannot represent. Accepts PCM at 8 bits
// (unsigned, as the format requires) and 16/24/32 bits (signed), plus IEEE float;
// mono or multi-channel, which is averaged down.
bool parseWav(const uint8_t* data, size_t n, AudioBuffer& out, std::string& err);

// 16-bit mono PCM, which is what every recorder and every archive can read. There is
// no reason to write anything else: the signal is two tones and a hard decision, so
// bit depth past 16 buys precisely nothing.
std::vector<uint8_t> buildWav(const AudioBuffer& in);

// Is this a RIFF/WAVE at all? The whole of the format sniff (tapecodec.cpp), kept
// here because it is a fact about the container. A `.tap` renamed `.wav` must NOT be
// treated as audio, and a `.wav` renamed `.tap` must be -- so the magic decides, and
// the extension is never consulted.
bool looksLikeWav(const uint8_t* data, size_t n);

} // namespace altair
