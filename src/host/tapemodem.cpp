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
    f.markHz = 2400; f.spaceHz = 1200; f.baud = 1200;
    f.cycleCounted = true; f.stopBits = 2;
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
    const double sp = double(a.rate) / f.baud;  // samples per bit
    if (sp < 2.0) return r;                     // baud too high for this rate

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
    // cycle), and a Sol dub often sits an octave below nominal -- 600/1200 Hz where the
    // format names 1200/2400. The perCycle fit above answered a different question (is
    // this even our format, within tolerance); the matched filter needs the frequency
    // the energy is really at, and that is the crossing rate halved.
    const double markF  = double(a.rate) / (2.0 * ivMark);
    const double spaceF = double(a.rate) / (2.0 * ivSpace);
    const double wM = 2.0 * 3.14159265358979 * markF  / a.rate;
    const double wS = 2.0 * 3.14159265358979 * spaceF / a.rate;
    const double halfWin = 0.5 * sp;  // one bit cell, Hann-weighted

    auto energy = [&](double centre, double w) -> double {
        long lo = long(std::floor(centre - halfWin));
        long hi = long(std::ceil(centre + halfWin));
        long len = hi - lo;
        if (len < 2) return 0.0;
        double re = 0.0, im = 0.0;
        for (long idx = 0, i = lo; i < hi; ++i, ++idx) {
            if (i < 0 || i >= long(n)) continue;
            const double han = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * idx / double(len - 1));
            const double s   = han * double(hp[size_t(i)]);
            const double ang = w * double(i);
            re += s * std::cos(ang);
            im += s * std::sin(ang);
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

    const double frameAdv = (1.0 + f.dataBits + f.stopBits) * sp;
    size_t       fi  = 0;
    double       pos = firstLock(0);

    while (pos >= 0.0 && pos + frameAdv < double(n)) {
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

        const double pred = e + frameAdv;
        if (pred + frameAdv < double(n) && isStart(pred)) {
            pos = refine(pred);                 // contiguous next byte: trust the clock
        } else {
            // An idle gap (or the end): re-acquire from the next real falling edge.
            while (fi < falls.size() && double(falls[fi]) < pred - 0.5 * sp) ++fi;
            pos = firstLock(fi);
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
AudioBuffer modulate(const std::vector<uint8_t>& bytes, const TapeFormat& f,
                     uint32_t rate, double leaderSeconds, double trailerSeconds) {
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

    // A sine, not a square: a square's harmonics are what a real recorder's bandwidth
    // removes anyway, and a decoder that sees them may count them as cycles.
    auto tone = [&](double hz, double seconds) {
        want += seconds * a.rate;
        const double dp = 2.0 * 3.14159265358979 * hz / a.rate;
        while (double(a.s.size()) < want) {
            a.s.push_back(float(0.8 * std::sin(phase)));
            phase += dp;
            if (phase > 2.0 * 3.14159265358979) phase -= 2.0 * 3.14159265358979;
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
            // cycles a bit) one bad fragment is lost in the crowd -- at 1200 baud,
            // where a bit is one or two cycles, it IS the bit. Continuous FSK is the
            // opposite case and keeps the carry: see the else branch.
            phase = 0.0;
            // Kansas City: a whole number of cycles, so the cell is exactly 1/baud
            // only if the tones divide evenly -- which is the point of choosing them
            // 2:1. Round, so a format whose numbers do not divide still produces a
            // cell of very nearly the right length rather than drifting.
            const double cycles = std::max(1.0, std::floor(hz / f.baud + 0.5));
            tone(hz, cycles / hz);
        } else {
            // Continuous FSK: hold the tone for the bit time and let the cycle fall
            // where it may. The phase carry above is what keeps this glitch-free.
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
