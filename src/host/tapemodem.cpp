#include "host/tapemodem.h"

#include "host/wav.h"

#include <algorithm>
#include <cmath>

namespace altair {

namespace tapeformats {

TapeFormat kcs300() {
    TapeFormat f;
    f.name = "kcs300";
    f.markHz = 2400; f.spaceHz = 1200; f.baud = 300;
    f.cycleCounted = true; f.stopBits = 2;
    return f;
}

TapeFormat cuts1200() {
    TapeFormat f;
    f.name = "cuts1200";
    // GENUINE PT CUTS, 1200-baud mode: mark = 1200 Hz, space = 600 Hz -- NOT the
    // 2400/1200 of Kansas City. A bit is 0.833 ms either way (1200 baud), which makes a
    // mark ONE cycle of 1200 Hz and a space a HALF cycle of 600 Hz. That half-cycle is
    // why this is `cycleCounted = false` (hold the tone for the cell) and not the
    // integer-cycle Kansas City scheme: the cell is not a whole number of space cycles,
    // and forcing it to be would double the space's length and break the bit rate. The
    // 2400/1200 tones belong to this same card's OTHER mode -- 300-baud KCS, see
    // kcs300() -- which is the confusion the code carried until it was measured against
    // real hardware. (H. Holden, "The SOL-20 Computer's Cassette interface", 2018;
    // matches all five deramp.com Sol dubs at 1200/600 Hz. reference/Sol-20.md.)
    f.markHz = 1200; f.spaceHz = 600; f.baud = 1200;
    f.cycleCounted = false; f.stopBits = 2;
    return f;
}

TapeFormat fsk300_1850() {
    TapeFormat f;
    f.name = "fsk300";
    f.markHz = 2400; f.spaceHz = 1850; f.baud = 300;
    f.cycleCounted = false; f.stopBits = 1;
    return f;
}

const std::vector<TapeFormat>& all() {
    static const std::vector<TapeFormat> v = {cuts1200(), kcs300(), fsk300_1850()};
    return v;
}

const TapeFormat* byName(const std::string& n) {
    for (const TapeFormat& f : all())
        if (n == f.name) return &f;
    return nullptr;
}

} // namespace tapeformats

const char* waveformName(Waveform w) { return w == Waveform::Sine ? "sine" : "square"; }
Waveform    waveformByName(const std::string& s) {
    return s == "sine" ? Waveform::Sine : Waveform::Square;
}

namespace {

// ---------------------------------------------------------------------------
// Stage 1-3: audio -> a list of threshold crossings, in fractional samples.
//
// The three defences against real media, in order. Each exists because a recovered
// cassette signal breaks a decoder that omits it -- see the header.
// ---------------------------------------------------------------------------
std::vector<double> crossings(const AudioBuffer& a, std::vector<float>* hpOut = nullptr) {
    const size_t n = a.s.size();
    std::vector<double> out;
    if (n < 4 || !a.rate) return out;

    // (1) DC BLOCK. A one-pole high-pass at ~100 Hz, not a mean: real recordings
    // WANDER (a slow baseline drift from the recorder's electronics), and a single
    // global mean removes the average of that wander while leaving the wander.
    const double k = std::exp(-2.0 * 3.14159265358979 * 100.0 / a.rate);
    std::vector<float> hp(n);
    double y = 0.0, px = 0.0;
    for (size_t i = 0; i < n; ++i) {
        y = k * y + k * (double(a.s[i]) - px);
        px = a.s[i];
        hp[i] = float(y);
    }

    // (2) ENVELOPE. Fast attack, ~50 ms decay. The comparator threshold is a
    // FRACTION of this rather than an absolute, so a tape whose level falls by 20 dB
    // halfway through -- a dying battery, a stretched belt -- keeps decoding.
    const double ea = std::exp(-1.0 / (0.05 * a.rate));
    std::vector<float> env(n);
    double e = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double m = std::fabs(hp[i]);
        e = (m > e) ? m : (e * ea + m * (1.0 - ea));
        env[i] = float(e);
    }

    // (3) SCHMITT TRIGGER, WITH SUB-SAMPLE INTERPOLATION.
    //
    // The hysteresis is what keeps the noise floor from manufacturing crossings in
    // quiet passages: a plain sign test on a signal that is 3% noise emits edges
    // wherever the waveform loiters near zero, and each one becomes a phantom cycle.
    //
    // The interpolation is not a refinement, it is the error budget. At 22 kHz a
    // 2400 Hz cycle is 9.2 samples, so integer edges quantise every period by up to
    // +/-5% -- and the real 88-ACR's PLL tolerates about +/-100 Hz at 2125 Hz, which
    // IS 5%. Rounding to the sample spends the whole budget before the tape has
    // contributed any error of its own.
    int sign = 0;
    for (size_t i = 1; i < n; ++i) {
        const double t = 0.15 * env[i];
        const double p = hp[i - 1], c = hp[i];
        if (sign <= 0 && c > t && p <= t) {
            out.push_back(double(i - 1) + ((c != p) ? (t - p) / (c - p) : 0.0));
            sign = 1;
        } else if (sign >= 0 && c < -t && p >= -t) {
            out.push_back(double(i - 1) + ((c != p) ? (-t - p) / (c - p) : 0.0));
            sign = -1;
        }
    }
    // The DC-blocked signal is also the input to the matched filter in demodulate():
    // the crossing detector already paid for the high-pass, so hand it on rather than
    // computing it twice.
    if (hpOut) *hpOut = std::move(hp);
    return out;
}

// ---------------------------------------------------------------------------
// Stage 4: WHERE IS THE LINE BETWEEN THE TWO TONES?
//
// Two-means in the log domain, seeded from the nominal ratio. Log, because the
// quantity that is stable across speed, shape and sample rate is the RATIO of the two
// intervals; an arithmetic split biases toward the longer cluster.
//
// Returns the two cluster centres as intervals: {mark, space}, mark the shorter.
// {0,0} when the audio does not hold two separable tones.
// ---------------------------------------------------------------------------
std::pair<double, double> calibrate(std::vector<double> iv, double ratio) {
    if (iv.empty()) return {0.0, 0.0};

    std::sort(iv.begin(), iv.end());
    const double med = iv[iv.size() / 2];
    const double r   = std::sqrt(ratio > 1.0 ? ratio : 1.0 / ratio);
    double lo = med / r, hi = med * r;

    for (int pass = 0; pass < 40; ++pass) {
        double sa = 0, sb = 0;
        size_t na = 0, nb = 0;
        for (double x : iv) {
            if (x <= 0) continue;
            if (std::fabs(std::log(x / lo)) < std::fabs(std::log(x / hi))) { sa += x; ++na; }
            else                                                           { sb += x; ++nb; }
        }
        // ONE CLUSTER MEANS ONE TONE, AND ONE TONE IS NOT DATA -- it is leader, or a
        // blank tape. Refuse to split it (see below); splitting anyway would put the
        // dividing line straight through the middle of the only tone present and
        // classify half a silent tape as space, which manufactures start bits out of
        // an idle line. A carrier must decode to NOTHING.
        if (!na || !nb) return {0.0, 0.0};
        double nlo = sa / na, nhi = sb / nb;
        if (std::fabs(nlo - lo) < 1e-9 && std::fabs(nhi - hi) < 1e-9) { lo = nlo; hi = nhi; break; }
        lo = nlo; hi = nhi;
    }

    // ...AND THE SAME JUDGEMENT ONCE IT HAS CONVERGED. Two clusters this close are
    // one tone that jitter has smeared in two, not two tones: the narrowest real pair
    // we carry is 2400/1850, a ratio of 1.30. Anything under 1.15 is noise being
    // asked to mean something.
    if (lo <= 0.0 || hi / lo < 1.15) return {0.0, 0.0};

    return {lo, hi};  // shorter interval first: that is the faster tone, the mark
}

} // namespace

// ---------------------------------------------------------------------------
DemodResult demodulate(const AudioBuffer& a, const TapeFormat& f) {
    DemodResult r;
    std::vector<float> hp;
    const std::vector<double> cr = crossings(a, &hp);
    if (cr.size() < 4 || !a.rate) return r;

    std::vector<double> iv;
    iv.reserve(cr.size());
    for (size_t i = 1; i < cr.size(); ++i) iv.push_back(cr[i] - cr[i - 1]);

    const auto [ivMark, ivSpace] = calibrate(iv, f.markHz / f.spaceHz);
    if (ivMark <= 0.0) return r;  // no two tones here: a blank tape, or pure leader

    // ---- HOW MANY CROSSINGS IS A CYCLE, AND ARE THESE EVEN THE RIGHT TONES? ------
    //
    // The comparator fires ONCE per cycle on a sawtooth and TWICE on a sine, and
    // recovered cassette audio is either (the MITS modem shapes its output into a
    // sawtooth on purpose). So a measured interval is a whole cycle or a half one,
    // and nothing in the signal says which -- a factor of two, from waveform shape
    // alone.
    //
    // The CARD settles it. Both readings are computed and compared against the tones
    // this modem is built to hear; the one that fits wins. That is also, and more
    // importantly, the check that stops a self-calibrating receiver from reading
    // anything at all: calibration finds whatever tones are present, so without this
    // an 88-ACR would cheerfully decode a Kansas City tape its PLL could never lock
    // to. If NEITHER reading fits, the tape is in some other modulation and this card
    // returns nothing -- see TapeFormat::tonesTolerance.
    const double fit = f.tonesTolerance;
    for (double perCycle : {1.0, 2.0}) {
        const double m = double(a.rate) / (ivMark * perCycle);
        const double s = double(a.rate) / (ivSpace * perCycle);
        if (std::fabs(m / f.markHz - 1.0) <= fit && std::fabs(s / f.spaceHz - 1.0) <= fit) {
            r.measuredMarkHz  = m;
            r.measuredSpaceHz = s;
            r.tonesMatched    = true;
            break;
        }
    }
    if (!r.tonesMatched) {
        // Report what was actually there, so the refusal can name it -- "wrong format"
        // without the tones is the least useful diagnostic there is. Neither reading
        // fits, so quote the CLOSER of the two rather than picking one arbitrarily: a
        // sawtooth tape reported at half its real frequency sends the reader hunting
        // for a fault that is not there.
        double best = 0;
        for (double perCycle : {1.0, 2.0}) {
            const double m = double(a.rate) / (ivMark * perCycle);
            const double s = double(a.rate) / (ivSpace * perCycle);
            const double d = std::fabs(std::log(m / f.markHz)) +
                             std::fabs(std::log(s / f.spaceHz));
            if (best == 0 || d < best) { best = d; r.measuredMarkHz = m; r.measuredSpaceHz = s; }
        }
        return r;
    }

    const size_t n = a.s.size();
    double       sp = double(a.rate) / f.baud;  // samples per bit -- the NOMINAL rate. It
    if (sp < 2.0) return r;                      // is adapted to the real one below, because
    const double spNominal = sp;                 // a tape running 3% fast has a 3% short bit

    // ---- Stage 5a: the TONE DISCRIMINATOR IS A MATCHED FILTER, not a slicer. ------
    //
    // The obvious discriminator classifies each zero-crossing interval as mark or space
    // by its length. It works on our own clean re-encodes and FAILS on real Sol dubs:
    // a recovered cassette waveform carries the occasional spurious crossing (a baseline
    // wobble that clears the hysteresis, an asymmetric half-cycle), and one stray
    // crossing splits a long space interval into two short ones -- flipping the whole
    // bit to mark. On the deramp.com corpus that is a wrong byte every few hundred, and
    // since a SOLOS block carries a checksum that a SINGLE bad byte fails, every one of
    // those tapes is unloadable while a real Sol reads them without trouble.
    //
    // The fix is what the hardware does: integrate over the bit, don't trust one edge.
    // For each candidate bit centre we run a one-cell Goertzel at the mark tone and at
    // the space tone and take whichever holds more energy. A lone spurious crossing
    // barely moves either integral, so the bit is read from the WHOLE cell's shape.
    //
    // The tones are the ACTUAL fundamentals present, derived from the measured crossing
    // spacing as rate/(2*interval): recovered cassette audio is a sine (two crossings a
    // cycle), and its exact frequency is not the nominal one. It drifts with tape speed,
    // and a dub may be at another speed entirely -- so the matched filter measures rather
    // than assumes. The perCycle fit above answered a different question (is this even
    // our format, within tolerance); the matched filter needs the frequency the energy is
    // really at, and that is the crossing rate halved.
    const double markF  = double(a.rate) / (2.0 * ivMark);
    const double spaceF = double(a.rate) / (2.0 * ivSpace);
    const double wM = 2.0 * 3.14159265358979 * markF  / a.rate;
    const double wS = 2.0 * 3.14159265358979 * spaceF / a.rate;
    // ONE BIT CELL, RECTANGULAR, NOT WINDOWED -- and the window width tracks `sp`, which
    // the framer eases toward the real tape speed below. Rectangular is deliberate: a cell
    // is a WHOLE number of both tones (the 2:1 ratio and the cycle count guarantee it -- at
    // any rate, a cuts mark cell is two mark periods and one space period), so a plain
    // integral over exactly the cell makes the two tones ORTHOGONAL and a pure mark scores
    // zero in the space bin. A Hann taper would break that integer-period orthogonality and
    // bleed a space tone into the mark bin -- invisible at 44 kHz, a wrong bit at 11 kHz
    // where a cell is only nine samples and there is no resolution to spare.
    auto energy = [&](double centre, double w) -> double {
        const double halfWin = 0.5 * sp;
        long lo = long(std::floor(centre - halfWin));
        long hi = long(std::ceil(centre + halfWin));
        if (hi - lo < 2) return 0.0;
        double re = 0.0, im = 0.0;
        for (long i = lo; i < hi; ++i) {
            if (i < 0 || i >= long(n)) continue;
            const double ang = w * double(i);
            re += double(hp[size_t(i)]) * std::cos(ang);
            im += double(hp[size_t(i)]) * std::sin(ang);
        }
        return std::hypot(re, im);
    };
    auto isMark  = [&](double c) { return energy(c, wM) >= energy(c, wS); };
    // A start bit is space for its whole cell and is preceded by mark (idle, or the
    // stop bits of the previous character). Both halves checked, as the old slicer did.
    auto isStart = [&](double p) { return !isMark(p + 0.5 * sp) && isMark(p - 0.5 * sp); };
    // Re-anchor to the actual mark->space edge within +/-0.4 bit, so phase never drifts
    // more than a fraction of a cell into the character however the coarse anchor landed.
    auto refine = [&](double p) -> double {
        double best = -1.0, bp = p;
        for (double o = -0.4; o <= 0.4 + 1e-9; o += 0.1) {
            const double q = p + o * sp;
            if (isMark(q - 0.5 * sp) && !isMark(q + 0.5 * sp)) {
                const double sc = (energy(q - 0.5 * sp, wM) - energy(q - 0.5 * sp, wS)) -
                                  (energy(q + 0.5 * sp, wM) - energy(q + 0.5 * sp, wS));
                if (best < 0.0 || sc > best) { best = sc; bp = q; }
            }
        }
        return bp;
    };

    // ---- Stage 5b: candidate start edges, for lock and re-acquire. ----------------
    //
    // A coarse level timeline (interval < split = mark) still gives the list of places a
    // character can BEGIN -- it does not have to be right about every bit, only to mark
    // the falling edges. The matched filter above decides what the bits actually are.
    const double split = std::sqrt(ivMark * ivSpace);
    std::vector<uint8_t> lev(n, 1);
    for (size_t i = 1; i < cr.size(); ++i) {
        const uint8_t v = ((cr[i] - cr[i - 1]) < split) ? 1 : 0;
        size_t b = size_t(std::max(0.0, cr[i - 1]));
        size_t e = size_t(std::max(0.0, cr[i]));
        if (e > n) e = n;
        for (size_t j = b; j < e; ++j) lev[j] = v;
    }
    std::vector<size_t> falls;
    for (size_t i = 1; i < n; ++i)
        if (lev[i - 1] && !lev[i]) falls.push_back(i);

    // ---- Stage 5c: the UART framer, WITH A PREDICTED CLOCK. -----------------------
    //
    // SOLOS/CUTS writes its bytes BACK TO BACK -- eleven bit times apart, no gap inside a
    // record. So once locked, the next start bit is at +11 bits whether or not a clean
    // falling edge was recovered there. The old framer hunted the edge list for every
    // start and, on a real dub, would now and then find no edge where a start truly was
    // (its mark stop bits mis-sliced to space, so no mark->space transition) and DROP the
    // byte -- a deletion that desynchronises the 256-byte block and every block after it.
    // Predicting the contiguous start and only falling back to the edge list at a genuine
    // idle gap is what turns "eight of thirty blocks" into "thirty of thirty".
    auto firstLock = [&](size_t from) -> double {
        for (size_t k = from; k < falls.size(); ++k) {
            const double e = double(falls[k]);
            if (e + (1.0 + f.dataBits + f.stopBits) * sp < double(n) && isStart(e))
                return refine(e);
        }
        return -1.0;
    };

    // THE BIT CLOCK ADAPTS TO THE REAL TAPE SPEED. `sp` starts at rate/baud, but a tape
    // that plays 3% fast has a bit cell 3% short, and the NOMINAL cell would then sample
    // the eighth data bit a quarter of a cell into the ninth -- correct at the start bit,
    // wrong by the stop bit, which is the deletion that desyncs a block. So every contiguous
    // byte MEASURES its own length -- the refined start of the next character is exactly
    // `frameBits` cells past this one -- and eases `sp` toward it. Speed is not the tone
    // (a sine trips the comparator twice a cycle and a sawtooth once, so the tone reads a
    // factor of two off from shape alone) but the DATA rate, and the data rate is what the
    // frame spacing is. A tape at a steady 3% is tracked within a byte or two;
    // one that drifts is followed. The blend is slow enough that a single mis-refined start
    // cannot yank the clock, and the update is clamped to a sane band around the nominal.
    const double frameBits = 1.0 + f.dataBits + f.stopBits;

    // SEED THE CLOCK FROM THE DATA, not the nominal rate, so byte zero is already at the
    // right speed rather than easing toward it. The spacing between validated start edges
    // is a whole number of bit cells; divide it out and the median of those quotients is
    // the real cell, robust to the odd mis-anchored edge. A relabelled sample rate (a tape
    // played fast) moves every span together, so the median moves with it -- which the
    // per-cell nominal cannot. Clamped to a sane band so noise never seeds a wild clock.
    {
        std::vector<double> cell;
        double prev = -1.0;
        for (size_t k = 0; k < falls.size(); ++k) {
            const double e = double(falls[k]);
            if (!isStart(e)) continue;
            if (prev >= 0.0) {
                const double bits = std::round((e - prev) / spNominal);
                if (bits >= 1.0 && bits <= frameBits + 1.0) cell.push_back((e - prev) / bits);
            }
            prev = e;
        }
        if (!cell.empty()) {
            std::sort(cell.begin(), cell.end());
            const double med = cell[cell.size() / 2];
            if (med > 0.8 * spNominal && med < 1.25 * spNominal) sp = med;
        }
    }

    size_t       fi  = 0;
    double       pos = firstLock(0);

    while (pos >= 0.0 && pos + frameBits * sp < double(n)) {
        const double e = pos;

        uint32_t by = 0;
        for (int b = 0; b < f.dataBits; ++b)
            if (isMark(e + (1.5 + b) * sp)) by |= (1u << b);  // LSB first

        // ONLY THE FIRST STOP BIT IS CHECKED, so stopBits is a MINIMUM: an 8N2 tape read
        // as 8N1 still frames. A framing error still yields its eight data bits -- a real
        // UART latches the byte and merely raises a flag -- so the byte is EMITTED and
        // counted, never dropped. Dropping it is the deletion that desyncs a block.
        if (!isMark(e + (1.5 + f.dataBits) * sp)) ++r.framingErrors;
        r.bytes.push_back(uint8_t(by));

        const double pred = e + frameBits * sp;
        if (pred + frameBits * sp < double(n) && isStart(pred)) {
            pos = refine(pred);                 // contiguous next byte: trust the clock
            const double measured = (pos - e) / frameBits;   // ...but measure the cell it
            if (measured > 0.8 * spNominal && measured < 1.25 * spNominal)  // just spanned
                sp += 0.10 * (measured - sp);   // and follow slow drift (wow and flutter)
        } else {
            // An idle gap (or the end): re-acquire from the next real falling edge, and
            // forget the tracked speed -- the gap gives no phase to carry across it.
            while (fi < falls.size() && double(falls[fi]) < pred - 0.5 * sp) ++fi;
            sp  = spNominal;
            pos = firstLock(fi);
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
AudioBuffer modulate(const std::vector<uint8_t>& bytes, const TapeFormat& f,
                     uint32_t rate, double leaderSeconds, double trailerSeconds,
                     Waveform wave) {
    AudioBuffer a;
    a.rate = rate ? rate : 44100;

    double phase = 0.0;  // carried across every tone, so there is never a click

    // WHERE THE TAPE SHOULD BE, in fractional samples. A bit cell is very often not a
    // whole number of them -- 1200 baud at 44.1 kHz is 36.75 -- so rounding each tone
    // on its own would round in the SAME direction every time and the tape would run
    // fast by a quarter sample per bit. That is invisible for ten bits and fatal over
    // four thousand bytes: it cost 4 bytes and 21 framing errors on the first
    // round-trip of this file. Keeping the target in doubles and emitting up to it
    // means the error never accumulates past half a sample.
    double want = 0.0;

    // THE WAVEFORM. Square by default, because that is what the real Sol-PC and 88-ACR
    // modems lay down -- and what a genuine dub sounds like: a square carries more energy
    // than a sine at the same amplitude, which is the loudness difference an ear hears.
    //
    // BUT A ROUNDED SQUARE, NOT AN IDEAL ONE, and that is not a compromise -- it is the
    // hardware. A real modem is a flip-flop into an RC network; its edges are curved and
    // its harmonics roll off. An IDEAL square is worse than merely inauthentic here: its
    // high odd harmonics fold past Nyquist at cassette rates (a 2400 Hz mark's 3rd harmonic
    // aliases below 11 kHz) and land in the matched filter's bins as energy that was never
    // at that frequency, and its half-cycle CUTS "0" becomes a broadband click. Both wreck
    // the very round trip a writer exists to survive -- measured: an ideal square failed to
    // decode its own tape back at every rate. So `Square` is synthesized from a few odd
    // harmonics, each kept below Nyquist: audibly square, and its fundamental reads back
    // clean. `Sine` is the smooth, quieter alternative.
    constexpr double kPi   = 3.14159265358979;
    constexpr int    kHarm = 5;  // odd harmonics 1,3,5,7,9 -- squarish, still band-limited
    auto tone = [&](double hz, double seconds) {
        want += seconds * a.rate;
        const double dp  = 2.0 * kPi * hz / a.rate;
        const double nyq = 0.45 * a.rate;
        // A SEGMENT SHORTER THAN ONE CYCLE HAS NO SQUARE. The genuine CUTS "0" is a HALF
        // cycle of 600 Hz -- there is no flat top to it, so "square" is meaningless, and
        // stamping harmonics onto that half-hump is exactly what turned it into a
        // broadband click the matched filter misread (byte-wrong round trips, measured).
        // Emit it as the clean half-hump the hardware's RC filter would round it into
        // anyway. Full-cycle tones (every mark, and all of KCS/FSK) still go square.
        const bool square = (wave == Waveform::Square) && (hz * seconds >= 0.9);
        while (double(a.s.size()) < want) {
            double v;
            if (square) {
                double sum = 0.0;
                for (int h = 0; h < kHarm; ++h) {
                    const double k = 2.0 * h + 1.0;  // 1, 3, 5, ...
                    if (k * hz >= nyq) break;        // never synthesize past Nyquist
                    sum += std::sin(k * phase) / k;
                }
                v = std::clamp(sum * (4.0 / kPi), -1.0, 1.0);  // ~unit fundamental
            } else {
                v = std::sin(phase);
            }
            a.s.push_back(float(0.8 * v));
            phase += dp;
            if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
        }
    };

    // ONE BIT. This is the only place the two schemes differ.
    auto bit = [&](bool mark) {
        const double hz = mark ? f.markHz : f.spaceHz;
        if (f.cycleCounted) {
            // START EVERY CELL AT ZERO PHASE. Kansas City's whole point is that a bit
            // is a WHOLE NUMBER of cycles; carrying phase across the boundary produces
            // a partial cycle at every tone change, and that fragment is neither
            // tone's length, so the receiver cannot classify it. At 300 baud (8 or 4
            // cycles a bit) one bad fragment is lost in the crowd. kcs300 is the only
            // cycle-counted format we carry; the hold-for-the-cell branch keeps the
            // phase carry -- see the else branch.
            phase = 0.0;
            // Kansas City: a whole number of cycles, so the cell is exactly 1/baud
            // only if the tones divide evenly -- which is the point of choosing them
            // 2:1. Round, so a format whose numbers do not divide still produces a
            // cell of very nearly the right length rather than drifting.
            const double cycles = std::max(1.0, std::floor(hz / f.baud + 0.5));
            tone(hz, cycles / hz);
        } else {
            // HOLD THE TONE FOR THE BIT and let the cycle fall where it may -- the phase
            // carry above keeps it glitch-free. This is the 88-ACR's continuous FSK AND
            // the Sol's 1200-baud CUTS: a mark holds one 1200 Hz cycle and a space a HALF
            // cycle of 600 Hz, which is exactly what falls out of holding each tone for
            // one 833 us cell. The half-cycle "0" is genuine CUTS (Manchester-style), not
            // an approximation -- see reference/CUTS Assembly and Test.md.
            tone(hz, 1.0 / f.baud);
        }
    };

    // A stretch of idle line, measured in seconds rather than bits. Emitted as whole
    // mark cells so that a cycle-counted format's tail is still a whole number of
    // cycles -- silence is never written, because idle tape is a TONE (see the header).
    const double markCell = f.cycleCounted
                                ? std::max(1.0, std::floor(f.markHz / f.baud + 0.5)) / f.markHz
                                : 1.0 / f.baud;
    auto idle = [&](double seconds) {
        for (double t = 0; t < seconds; t += markCell) bit(true);
    };

    // THE LEADER. A loader needs carrier before the first start bit -- both to find
    // the tone and, on a real machine, to let the recorder's AGC settle and the
    // plastic leader clear the head. A tape that begins at the first start bit loads
    // nothing.
    //
    // WITH A FLOOR OF SIXTEEN BIT TIMES, for the same reason the trailer has one, and
    // it is not a nicety: a start bit is found by a MARK-TO-SPACE TRANSITION, so a tape
    // that opens on the start bit itself has no edge there and the first frame cannot
    // be found at all. Measured: at 300 baud into this file's own demodulator, a zero
    // leader costs exactly one byte and two framing errors, and 50 ms of mark is enough
    // to fix it. `leader = 0` is a legitimate thing to ask for -- it means trim the
    // file to its data -- and it must not quietly mean "and lose the first byte".
    idle(std::max(leaderSeconds, 16.0 * markCell));

    for (uint8_t b : bytes) {
        bit(false);                                        // start
        for (int i = 0; i < f.dataBits; ++i) bit((b >> i) & 1);  // LSB first
        for (int i = 0; i < f.stopBits; ++i) bit(true);     // stop
    }

    // THE TRAILER, with a floor of sixteen bit times whatever was asked for. The floor
    // is not the same thing as the trailer: it exists so the last stop bit is never
    // the last sample on the tape (a receiver needs to see the cell END), and zero is
    // a legitimate thing to ask for when you want a file trimmed to its data.
    idle(std::max(trailerSeconds, 16.0 * markCell));
    return a;
}

} // namespace altair
