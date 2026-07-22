#include "host/tapecodec.h"

#include "host/wav.h"

#include <cstdio>

namespace altair {

// ---------------------------------------------------------------------------
// AudioTapeMedia
// ---------------------------------------------------------------------------

AudioTapeMedia::AudioTapeMedia(std::unique_ptr<MediaFile> under, std::vector<uint8_t> decoded,
                               TapeFormat fmt, uint32_t rate)
    : under_(std::move(under)), bytes_(std::move(decoded)), fmt_(fmt), rate_(rate) {}

// See the header: the backstop for a recording nobody stopped. `under_` is declared
// first and so destroyed last, which is what makes it still valid in here.
AudioTapeMedia::~AudioTapeMedia() {
    if (!dirty_ || readOnly()) return;
    std::string ignored;
    commit(ignored);
}

bool AudioTapeMedia::readAt(uint64_t off, uint8_t* buf, size_t n) {
    if (off > bytes_.size() || bytes_.size() - off < n) return false;
    for (size_t i = 0; i < n; ++i) buf[i] = bytes_[size_t(off) + i];
    return true;
}

// INTO THE DECODED BYTES, which are the medium as far as everything above here is
// concerned. Growing them is a tape recording past where the old recording ended, which
// is what a cassette does; the audio does not exist again until commit().
bool AudioTapeMedia::writeAt(uint64_t off, const uint8_t* buf, size_t n) {
    if (readOnly()) return false;
    if (!n) return true;
    if (off + n > bytes_.size()) bytes_.resize(size_t(off + n), 0);
    for (size_t i = 0; i < n; ++i) bytes_[size_t(off) + i] = buf[i];
    dirty_ = true;
    return true;
}

bool AudioTapeMedia::resize(uint64_t n) {
    if (readOnly()) return false;
    bytes_.resize(size_t(n), 0);
    dirty_ = true;
    return true;
}

void AudioTapeMedia::setEncoding(double leaderSeconds, double trailerSeconds, Waveform wave) {
    leader_  = leaderSeconds;
    trailer_ = trailerSeconds;
    wave_    = wave;
}

// RE-MODULATE THE WHOLE TAPE AND REWRITE THE FILE. Not a punch-in: the audio for byte N
// begins at a fractional sample offset that depends on every byte before it, so there
// is no splice that is cheaper than doing it properly, and a splice would leave a phase
// discontinuity the receiver could read as a spurious cycle.
//
// THE TRUNCATE IS NOT OPTIONAL. Recording 400 bytes over a tape that held 4000 produces
// a shorter WAV, and without resize() the tail of the old recording survives past the
// end of the new one -- where the next mount demodulates it back as trailing garbage
// that no guest ever wrote. That is the whole reason MediaFile::resize() exists.
bool AudioTapeMedia::commit(std::string& err) {
    if (!dirty_) {
        under_->sync();
        return true;
    }
    if (readOnly()) {
        err = describe() + " is write-protected";
        return false;
    }

    const AudioBuffer          a   = modulate(bytes_, fmt_, rate_, leader_, trailer_, wave_);
    const std::vector<uint8_t> wav = buildWav(a);

    if (!under_->resize(wav.size())) {
        err = describe() + ": this medium cannot be resized, so the re-encoded tape "
                           "cannot be written back";
        return false;
    }
    if (!wav.empty() && !under_->writeAt(0, wav.data(), wav.size())) {
        err = describe() + ": could not write the re-encoded tape";
        return false;
    }
    under_->sync();

    dirty_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

namespace {

// The whole medium, in one go. MediaFile is already buffered (media.h: "the whole image
// is read at MOUNT"), so this is a copy out of memory and not a pass over the disk.
bool slurp(MediaFile& m, std::vector<uint8_t>& out) {
    out.resize(size_t(m.size()));
    return out.empty() || m.readAt(0, out.data(), out.size());
}

std::string hz(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f Hz", v);
    return b;
}

std::string pct(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f%%", v * 100.0);
    return b;
}

// "cuts1200, kcs300" -- for an error that has to say what this card CAN read.
std::string nameList(const std::vector<TapeFormat>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += i + 1 == v.size() ? " or " : ", ";
        s += v[i].name;
    }
    return s;
}

} // namespace

std::vector<std::string> tapeFormatChoices(const std::vector<TapeFormat>& candidates) {
    std::vector<std::string> c{"auto", "raw"};
    for (const TapeFormat& f : candidates) c.push_back(f.name);
    return c;
}

std::unique_ptr<MediaFile> openTapeMedia(const std::string& path, bool ro,
                                         const std::vector<TapeFormat>& candidates,
                                         const std::string& want, std::string& detected,
                                         std::vector<std::string>& log, std::string& err) {
    auto media = openMedia(path, ro, err);
    if (!media) return nullptr;

    std::vector<uint8_t> raw;
    if (!slurp(*media, raw)) {
        err = "could not read " + path;
        return nullptr;
    }

    // THE MAGIC DECIDES, NEVER THE EXTENSION. A .tap someone renamed .wav must not be
    // demodulated, and a .wav someone renamed .tap must be. `raw` forces the byte
    // reading even on a real WAV, which is how you look at a broken tape's container.
    const bool isWav = looksLikeWav(raw.data(), raw.size());
    if (want == "raw" || (want == "auto" && !isWav)) {
        detected = "raw";
        return media;
    }

    if (!isWav) {
        err = path + " is not a WAV file, so it cannot be demodulated as '" + want +
              "' -- mount it with format=raw or format=auto";
        return nullptr;
    }

    AudioBuffer audio;
    if (!parseWav(raw.data(), raw.size(), audio, err)) {
        err = path + ": " + err;
        return nullptr;
    }

    // WHICH FORMATS MAY WE EVEN TRY? Only the ones this card's modem can hear. An
    // explicit `format` narrows that to one; it does NOT widen it past the hardware.
    std::vector<TapeFormat> tryThese;
    if (want == "auto") {
        tryThese = candidates;
    } else {
        for (const TapeFormat& f : candidates)
            if (want == f.name) tryThese.push_back(f);
        if (tryThese.empty()) {
            err = "this board's modem does not demodulate '" + want + "' -- it reads " +
                  nameList(candidates);
            return nullptr;
        }
    }

    // Best confidence among the formats whose TONES are actually on the tape. Ruling a
    // format out by its tones is what stops a self-calibrating receiver from reading
    // any tape at all: it measures the tones present, so without the check a Kansas
    // City tape would decode cleanly under the ACR's FSK parameters.
    DemodResult best;
    TapeFormat  bestFmt;
    bool        any = false;

    // What the tape MEASURED as, kept for the error message when nothing matched: the
    // operator's next question is always "then what is on it?", and the answer is here.
    double sawMark = 0, sawSpace = 0;

    for (const TapeFormat& f : tryThese) {
        DemodResult r = demodulate(audio, f);
        if (sawMark == 0 && r.measuredMarkHz > 0) {
            sawMark = r.measuredMarkHz;
            sawSpace = r.measuredSpaceHz;
        }
        if (!r.tonesMatched) continue;
        if (!any || r.confidence() > best.confidence()) {
            best = r;
            bestFmt = f;
            any = true;
        }
    }

    if (!any) {
        err = path + ": this board's modem cannot hear that tape";
        if (sawMark > 0) {
            err += " -- it carries " + hz(sawMark) + " / " + hz(sawSpace) +
                   ", and this board reads " + nameList(candidates);
        } else {
            err += " -- no two separable tones are on it (a blank tape, or pure leader)";
        }
        return nullptr;
    }

    if (best.confidence() < kTapeConfidenceFloor) {
        err = path + ": decoded as " + bestFmt.name + " with only " + pct(best.confidence()) +
              " of frames intact (" + std::to_string(best.bytes.size()) + " bytes, " +
              std::to_string(best.framingErrors) +
              " framing errors) -- that is noise, not a tape. Set the unit's `format` "
              "property if this is the wrong modulation";
        return nullptr;
    }

    // Say what happened, always -- a clean mount narrates too, because "0 framing
    // errors" is information and its absence is not.
    //
    // "OF FRAMES INTACT", NOT "CLEAN". The percentage is bytes/(bytes+framingErrors):
    // it says every frame had its start and stop bits where they belonged, and it says
    // NOTHING about whether the eight bits between them are the ones that were recorded.
    // The two can come apart, and this exact tape once proved it: an earlier demodulator
    // read Philip Lord's TRK80.WAV recording at 99.7% framing yet got 6,778 of its 7,840
    // payload bytes wrong (the CUTS timings in reference/Sol-20.md were measured off it even
    // then). This decoder now reads that recording clean, but the number's meaning is
    // unchanged -- it is a verdict on the carrier, not on the bits the carrier held.
    // Called "clean" it would read as a verdict on the recording, which is the one thing
    // it cannot give. Named for what it measures, it is honest and still scales the
    // framing-error count against the confidence floor.
    log.push_back(path + ": " + bestFmt.name + ", " + std::to_string(best.bytes.size()) +
                  " bytes, " + std::to_string(best.framingErrors) + " framing errors (" +
                  pct(best.confidence()) + " of frames intact)");

    detected = bestFmt.name;
    return std::make_unique<AudioTapeMedia>(std::move(media), std::move(best.bytes), bestFmt,
                                            audio.rate);
}

} // namespace altair
