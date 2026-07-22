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
//   HOLD THE TONE FOR THE CELL. The transmitter emits one tone for the whole bit time and
//   lets the cycle fall where it may. The 88-ACR's continuous FSK is this (at 1850 Hz and
//   300 baud a cell is 6.17 cycles), and so is the Sol's 1200-baud CUTS -- there a mark
//   cell holds exactly one 1200 Hz cycle and a space cell a half cycle of 600 Hz
//   (Manchester-style), which is a nice fraction only by coincidence of the numbers.
//
//   KANSAS CITY (the Sol's 300-baud mode, and the ordinary microcomputer standard). A bit
//   is a WHOLE NUMBER of cycles -- the tones are 2:1 so that N cycles of the high tone and
//   N/2 of the low occupy the same time.
//
// `cycleCounted` therefore steers modulate() and NOT demodulate(). That is not a
// shortcut, it is what the signals are: in BOTH schemes the tone is simply held for
// the whole bit cell, so a receiver that measures which tone's energy fills each cell
// is correct for both. Kansas City's integer cycle counts make a transmitter's life
// tidy and buy the receiver nothing it did not already have. One demodulator, verified
// against real tapes in both modulations (see below).
//
// ---- THE RECEIVER CALIBRATES ITSELF, AND MUST --------------------------------------
//
// demodulate() does NOT assume the nominal markHz/spaceHz. It measures the two tones
// actually present and runs its matched filter at THOSE, because the nominal frequency
// is wrong on real media in three independent ways:
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
// The Sol's CUTS cycle counts ARE in a manual: Processor Technology's CUTS Assembly and
// Test Instructions (1977), distilled in reference/CUTS Assembly and Test.md and
// corroborated by H. Holden's 2018 teardown of the Sol-PC modem. At its 1200-baud default
// a mark is ONE cycle of 1200 Hz and a space a HALF cycle of 600 Hz -- an octave below
// Kansas City, whose 2400/1200 Hz tones the Sol uses only in its 300-baud (SE TA 1) mode.
// Framing is 8N2 (one start, eight data, TWO stop). deramp.com's TRK80.WAV agrees with the
// manual and decodes to a valid SOLOS header ("TRK80", SIZE 0x1EA0) whose length matches
// the payload -- the oracle that makes it a fact. See reference/Sol-20.md and
// docs/boards/proctech-sol.md.
//
// ---- A CARD DECLARES WHAT ITS OWN MODEM CAN HEAR, AND REFUSES THE REST -------------
//
// A board does NOT trial-decode its way through this list looking for something that
// sticks. It names the format(s) the physical hardware demodulates, and a tape in any
// other modulation is REFUSED -- with a message saying what the tape actually is.
//
// Because the alternative is inventing hardware. Not all published Altair cassette
// audio is in the 88-ACR's format: the "2SIO as Cassette" tapes measure 2397/1852 (the
// MITS modem, confirming the manual's arithmetic off real media), but the "KCS" and
// "KCACR" -- Kansas City ACR -- tapes measure 2377/1201, and those are a DIFFERENT
// STANDARD, not ACR tapes recorded oddly. ("2SIO as cassette" names which serial card
// the loader talks to; the modem, and so the audio, is unchanged.)
//
// The 88-ACR physically cannot read the Kansas City ones, and its manual says why: the
// demodulator is a PLL centred at 2125 Hz that accommodates about +/-100 Hz of tape
// drift. A 1200 Hz space tone is some 925 Hz outside that. A real card fed such a tape
// does not read it badly -- it reads nothing. A simulator that decoded it anyway would
// be handing the guest data no 88-ACR could ever have produced, which is the exact
// failure DESIGN.md forbids.
//
// The Sol-20 is a genuinely different case and gets two formats HONESTLY: its CUTS
// UART really does run at 300 or 1200 baud, and the GUEST picks which at OUT FAh D5.
// That is a switch on the hardware, not a guess by the host.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace altair {

struct AudioBuffer;

// ---------------------------------------------------------------------------
// The SHAPE of the emitted carrier -- audible, not semantic.
//
// The real Sol-PC and 88-ACR modems lay down a SQUARE wave (flip-flop outputs, only
// gently rounded by the recorder's own bandwidth), so `Square` is the default: it is
// what a genuine cassette dub sounds like, and it is louder than a sine at the same
// amplitude, which is exactly the difference an ear notices between our old recordings
// and a real one. `Sine` is offered because a square's odd harmonics are bandwidth a
// real recorder removes, and a naive comparator could miscount them as extra cycles.
//
// It is only a SHAPE. A square and a sine of the same frequency cross zero at the same
// instants, and this demodulator reads crossings and integrates the fundamental -- so it
// decodes either one identically. Nothing downstream can tell them apart; only a speaker
// can. Selected per tape by the unit's `waveform` property and by `tapetool encode`.
// ---------------------------------------------------------------------------
enum class Waveform { Square, Sine };

const char* waveformName(Waveform w);              // "square" | "sine"
Waveform    waveformByName(const std::string& s);  // anything but "sine" -> Square

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

    // HOW FAR THE MEASURED TONES MAY SIT FROM THE ONES ABOVE before this format is
    // ruled out. It is what stops a self-calibrating receiver from reading ANY tape:
    // calibration measures the tones actually present, so without this check a Kansas
    // City tape decodes cleanly under the 88-ACR's FSK parameters and the simulator
    // recovers data the physical card never could (its PLL sits at 2125 Hz and takes
    // about +/-100 Hz; a 1200 Hz space is 925 Hz outside it).
    //
    // 20% is deliberately looser than any real card's capture range -- we are ruling
    // out a WRONG STANDARD, not modelling the PLL. It passes ordinary tape drift and
    // still refuses 1200 against 1850 (35% away) and every 2:1 confusion.
    double tonesTolerance = 0.20;
};

namespace tapeformats {

// Kansas City proper: 8 cycles of 2400 for a 1, 4 cycles of 1200 for a 0. The Sol's SLOW
// mode (SE TA 1).
TapeFormat kcs300();

// CUTS fast mode -- an octave BELOW Kansas City: one cycle of 1200 Hz for a 1, a HALF
// cycle of 600 Hz for a 0 (Manchester-style: hold the tone for the 833 us cell). What
// SOLOS writes with `SE TA 0`, the default. NOT 2400/1200 -- those are the slow mode's.
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

    // The tones the tape ACTUALLY carries, as measured. Zero if the audio held no
    // two separable tones at all (a blank, or pure leader).
    double measuredMarkHz = 0;
    double measuredSpaceHz = 0;

    // False when those tones are not the ones this card's modem demodulates -- see
    // `tonesTolerance` below. The bytes are then EMPTY, deliberately: a card must not
    // hand the guest data its hardware could never have recovered.
    bool tonesMatched = false;

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

// Bytes -> tones. `leaderSeconds` of idle mark goes on the front and
// `trailerSeconds` on the back, each with a floor of sixteen bit times: a receiver
// finds a start bit by its mark-to-space EDGE and needs to see the cell end, so a tape
// that opened on the start bit itself would lose its first byte and one that ended on
// the last stop bit would lose its last. Zero is a legitimate request -- it means trim
// the file to its data -- and it must not quietly mean "and lose a byte".
//
// BOTH ARE TONE, NOT SILENCE, and that is measured rather than assumed: the UART's
// output pin idles high, high is mark, and the modem board has no squelch and no
// carrier-off -- its oscillator runs whenever the card is powered. So a card
// recording nothing lays down continuous mark. Blank tape and idle tape are
// acoustically different things. (reference/Altair 88-ACR Cassette Interface.md,
// "What idle tape carries, and where the leader really lives".)
//
// THE DURATIONS MUST BE SYNTHESIZED, because they cannot be recovered. A byte image
// holds no time -- verified on the corpus: nothing on those tapes is encoded as
// duration, the longest interior run of mark being one all-ones frame. So the leader
// a loader needs in BYTES survives a round trip through .TAP, and the leader the
// TRANSPORT needs in SECONDS does not exist until something puts it back. That is
// this argument's job, and it is why the callers make it a property rather than a
// constant: 15 s is what the MITS manual asks for and 3 s is what a real Sol dub
// carries, and neither is a fact about the modulation.
AudioBuffer modulate(const std::vector<uint8_t>& bytes, const TapeFormat& f,
                     uint32_t rate = 44100, double leaderSeconds = 5.0,
                     double trailerSeconds = 0.0, Waveform wave = Waveform::Square);

} // namespace altair
