#pragma once
//
// The cassette modem -- tones on one side, bytes on the other (DESIGN.md 7.3).
//
// A CARD DOES NOT APPEAR IN THIS FILE, AND MUST NOT. What a modem does is decided by
// four numbers and one flag (TapeFormat, below), not by which backplane it is
// plugged into. The 88-ACR and the Sol-20 differ here only as VALUES; a future card
// that records Kansas City tapes adds a TapeFormat constant and no code at all. That
// is the whole reason the codecs are named for the modulation and never for a card.
//
// TWO SCHEMES -- BUT ONLY THE TRANSMITTER CAN TELL THEM APART:
//
//   CONTINUOUS FSK (the 88-ACR's modem board). The tone is held for the bit cell and
//   nothing lines up: at 1850 Hz and 300 baud a cell is 6.17 cycles.
//
//   KANSAS CITY (the Sol's CUTS, and the ordinary microcomputer standard). A bit is a
//   WHOLE NUMBER of cycles -- the tones are 2:1 so that N cycles of the high tone and
//   N/2 of the low occupy the same time.
//
// `cycleCounted` therefore steers modulate() and NOT demodulate(). That is not a
// shortcut, it is what the signals are: in BOTH schemes the tone is simply held for
// the whole bit cell, so a receiver that classifies each cycle and samples the
// resulting level at the centre of each cell is correct for both. Kansas City's
// integer cycle counts make a transmitter's life tidy and buy the receiver nothing it
// did not already have. One demodulator, verified against real tapes in both
// modulations (see below).
//
// ---- THE RECEIVER CALIBRATES ITSELF, AND MUST --------------------------------------
//
// demodulate() does NOT slice at the nominal midpoint between markHz and spaceHz. It
// measures the two tones actually present and slices between THOSE, because the
// nominal threshold is wrong on real media in three independent ways:
//
//   * WAVEFORM SHAPE CHANGES THE COUNT. A recovered cassette signal may be a sine or
//     a sawtooth (the MITS modem shapes its square waves into one deliberately). A
//     sine trips a threshold comparator twice per cycle and a sawtooth once -- a
//     factor of TWO in every interval, from nothing but the shape. Measured, not
//     assumed, this cancels: both tones scale together and the ratio is untouched.
//   * TAPE SPEED DRIFTS. A tape that plays 5% slow moves both tones 5%.
//   * A DUB MAY BE AT ANOTHER SPEED ENTIRELY, and archive files are dubs.
//
// Two-means over the log of the interval, seeded from the nominal ratio: it converges
// in a few passes, and it is the difference between a decoder that works on our own
// recordings and one that works on the tapes people actually have.
//
// ---- WHERE THE NUMBERS COME FROM -------------------------------------------------
//
// The 88-ACR's are documented: 2 MHz / 104 / 8 = 2404 Hz for a mark, 2 MHz / 135 / 8
// = 1852 Hz for a space, wired for 300 baud, idle line = mark = a steady 2400 Hz
// tone (reference/Altair 88-ACR Cassette Interface.md, sections 6 and 7). The
// demodulator's PLL sits at 2125 Hz -- halfway -- and tolerates about +/-100 Hz.
//
// The Sol's CUTS cycle counts are NOT in any manual we hold: reference/Sol-20.md says
// only "Kansas-City-standard FSK audio at 300 or 1200 baud". They were MEASURED, off
// a real Sol tape (deramp.com's TRK80.WAV), and the measurement is what is encoded
// below: tones 2400/1200, a bit cell of exactly 1/1200 s, and ELEVEN bit times
// between consecutive frames -- i.e. 8N2, not 8N1. The tape decodes to a valid SOLOS
// header ("TRK80", SIZE 0x1EA0) whose length matches the payload, which is the
// oracle that makes the measurement a fact rather than a guess. See
// docs/boards/proctech-sol.md.
//
// ---- AND A CARD DOES NOT IMPLY A MODULATION ----------------------------------------
//
// The obvious mapping -- ACR means FSK -- is FALSE, and the archive says so. Of the
// Altair cassettes published as audio, the "2SIO as Cassette" tapes measure 2397/1852
// (the MITS modem, as documented) while the "KCACR Standard" and "KCS" tapes measure
// 2377/1201 (Kansas City), all four at 300 baud. Both were sold for the same machine;
// Kansas City was the later standard and the ACR was modified to meet it.
//
// So a board offers a LIST of candidate formats and the mount trial-decodes to pick
// one (host/tapecodec.h). Hard-wiring one modulation per card would refuse half of
// the surviving software, and would do it while insisting the tape was corrupt.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace altair {

struct AudioBuffer;

// ---------------------------------------------------------------------------
// A modulation, as data. Add a card by adding one of these.
// ---------------------------------------------------------------------------
struct TapeFormat {
    const char* name = "";       // what SHOW and the `format` property report
    double      markHz = 2400;   // logic 1. The idle line sits here.
    double      spaceHz = 1200;  // logic 0
    double      baud = 1200;
    bool        cycleCounted = true;  // true = Kansas City, false = continuous FSK
    int         dataBits = 8;
    int         stopBits = 2;
};

namespace tapeformats {

// Kansas City proper: 8 cycles of 2400 for a 1, 4 cycles of 1200 for a 0.
TapeFormat kcs300();

// CUTS fast mode -- the same tones, a quarter of the cycles: 2 cycles of 2400 for a
// 1, 1 cycle of 1200 for a 0. What SOLOS writes with `SE TA 0` (the default).
TapeFormat cuts1200();

// The MITS modem board: continuous FSK, 2400 mark / 1850 space, 300 baud, 8N1.
TapeFormat fsk300_1850();

// Everything known, for the trial-decode in tapecodec.cpp.
const std::vector<TapeFormat>& all();

// By `name`, for the `format` property. Null if unknown.
const TapeFormat* byName(const std::string& n);

} // namespace tapeformats

// ---------------------------------------------------------------------------
// What came off the tape, and how well it went.
// ---------------------------------------------------------------------------
struct DemodResult {
    std::vector<uint8_t> bytes;
    uint32_t             framingErrors = 0;

    // bytes / (bytes + framingErrors). The number the mount decides on, and the
    // number the operator is shown -- a tape that decoded at 0.4 is noise, and
    // saying so beats handing the guest 3000 bytes of garbage.
    double confidence() const {
        double t = double(bytes.size()) + framingErrors;
        return t > 0 ? double(bytes.size()) / t : 0.0;
    }
};

// Tones -> bytes. Never fails: a demodulated silence is zero bytes, which is a blank
// tape and not an error. Judge the result by confidence(), not by a bool.
DemodResult demodulate(const AudioBuffer& a, const TapeFormat& f);

// Bytes -> tones. `leaderSeconds` of idle mark goes on the front, as every recorder
// and every loader expects: the guest's loader needs carrier before the first start
// bit, and a tape that begins mid-byte loads nothing.
AudioBuffer modulate(const std::vector<uint8_t>& bytes, const TapeFormat& f,
                     uint32_t rate = 44100, double leaderSeconds = 5.0);

} // namespace altair
