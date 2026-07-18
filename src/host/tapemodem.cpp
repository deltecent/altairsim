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
std::vector<double> crossings(const AudioBuffer& a) {
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
    return out;
}

// ---------------------------------------------------------------------------
// Stage 4: WHERE IS THE LINE BETWEEN THE TWO TONES?
//
// Two-means in the log domain, seeded from the nominal ratio. Log, because the
// quantity that is stable across speed, shape and sample rate is the RATIO of the two
// intervals; an arithmetic split biases toward the longer cluster.
//
// Returns the dividing interval. Below it is the faster tone (mark).
// ---------------------------------------------------------------------------
double calibrate(std::vector<double> iv, double ratio) {
    if (iv.empty()) return 0.0;

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
        if (!na || !nb) return 0.0;
        double nlo = sa / na, nhi = sb / nb;
        if (std::fabs(nlo - lo) < 1e-9 && std::fabs(nhi - hi) < 1e-9) { lo = nlo; hi = nhi; break; }
        lo = nlo; hi = nhi;
    }

    // ...AND THE SAME JUDGEMENT ONCE IT HAS CONVERGED. Two clusters this close are
    // one tone that jitter has smeared in two, not two tones: the narrowest real pair
    // we carry is 2400/1850, a ratio of 1.30. Anything under 1.15 is noise being
    // asked to mean something.
    if (lo <= 0.0 || hi / lo < 1.15) return 0.0;

    return std::sqrt(lo * hi);  // geometric mean: the ratio-fair midpoint
}

} // namespace

// ---------------------------------------------------------------------------
DemodResult demodulate(const AudioBuffer& a, const TapeFormat& f) {
    DemodResult r;
    const std::vector<double> cr = crossings(a);
    if (cr.size() < 4 || !a.rate) return r;

    std::vector<double> iv;
    iv.reserve(cr.size());
    for (size_t i = 1; i < cr.size(); ++i) iv.push_back(cr[i] - cr[i - 1]);

    const double split = calibrate(iv, f.markHz / f.spaceHz);
    if (split <= 0.0) return r;

    // The level timeline: mark (1) or space (0) at every sample. Built once, so the
    // framer below can sample any instant in constant time. The idle line is MARK --
    // that is what an unrecorded stretch of tape sounds like to the card, and it is
    // what makes the start-bit hunt below terminate at the end of the tape.
    const size_t n = a.s.size();
    std::vector<uint8_t> lev(n, 1);
    for (size_t i = 1; i < cr.size(); ++i) {
        const uint8_t v = ((cr[i] - cr[i - 1]) < split) ? 1 : 0;
        size_t b = size_t(std::max(0.0, cr[i - 1]));
        size_t e = size_t(std::max(0.0, cr[i]));
        if (e > n) e = n;
        for (size_t j = b; j < e; ++j) lev[j] = v;
    }

    // ---- Stage 5: the UART framer. -----------------------------------------------
    //
    // Shared by both schemes, because both hand it the same thing: a line that is
    // mark or space at every instant.
    const double sp = double(a.rate) / f.baud;  // samples per bit
    if (sp < 2.0) return r;                     // baud too high for this rate

    auto L = [&](double x) -> bool {
        if (x < 0) return true;
        size_t i = size_t(x);
        return (i < n) ? (lev[i] != 0) : true;
    };

    // A BIT IS NOT ONE INSTANT. Sampling the level at the exact centre of the cell
    // trusts a single classified interval, and the interval that straddles a tone
    // change belongs to neither tone -- so a bit that follows a transition can be read
    // from the tail of the tone before it. Integrating across the middle half of the
    // cell and taking the majority is what a real receiver does with an RC network,
    // and it is the difference between one wrong byte in four thousand and none.
    auto bitAt = [&](double centre) -> bool {
        int votes = 0, mark = 0;
        for (double d = -0.25; d <= 0.25 + 1e-9; d += 0.1) {
            ++votes;
            if (L(centre + d * sp)) ++mark;
        }
        return mark * 2 > votes;
    };

    // EVERY FALLING EDGE ON THE LINE -- i.e. every candidate start bit. Anchoring on
    // the edge ITSELF, rather than on wherever a coarse scan happened to notice it,
    // is the whole of the framer's timing accuracy. A hunt that steps in tenths of a
    // bit and then guesses the edge is half a bit later carries up to half a bit of
    // phase error into the character; at 1200 baud a bit is under two cycles of tone,
    // so half a bit is most of a cycle and the last data bits sample the wrong cell.
    // Re-anchoring here, on every frame, is also what absorbs tape speed error: the
    // phase only has to survive the ten or eleven bit times of ONE character.
    std::vector<size_t> falls;
    for (size_t i = 1; i < n; ++i)
        if (lev[i - 1] && !lev[i]) falls.push_back(i);

    const double frame = (1.0 + f.dataBits + f.stopBits) * sp;
    size_t       fi    = 0;
    double       done  = -1.0;  // end of the last accepted character

    while (fi < falls.size()) {
        const double e = double(falls[fi]);
        if (e < done) { ++fi; continue; }          // inside the character just read
        if (e + frame >= double(n)) break;         // not enough tape left

        // IS THIS EDGE ACTUALLY A START BIT? An edge in the level timeline can be
        // manufactured by a SINGLE misclassified interval -- the one that straddles a
        // tone change is neither tone's length, so it may fall on the wrong side of
        // the split. A real start bit is space for a whole bit time and is preceded by
        // mark (idle, or the stop bits of the character before). Checking both costs
        // two lookups and removes the whole class of false starts; without it, two
        // bytes in four thousand decoded wrong on a clean synthetic tape.
        if (bitAt(e + 0.5 * sp) || !bitAt(e - 0.5 * sp)) { ++fi; continue; }

        // The start bit begins AT the edge, so the data bits sit at 1.5, 2.5 ... bit
        // times after it and the stop bit at 1.5 + dataBits.
        uint32_t by = 0;
        for (int b = 0; b < f.dataBits; ++b)
            if (bitAt(e + (1.5 + b) * sp)) by |= (1u << b);  // LSB first

        // ONLY THE FIRST STOP BIT IS CHECKED, and `stopBits` is therefore a MINIMUM.
        // An 8N2 tape read as 8N1 still frames correctly (the extra stop bit is more
        // mark, and the hunt skips mark); the reverse would reject every frame. Being
        // generous is what lets one decoder read tapes whose framing nobody recorded.
        if (bitAt(e + (1.5 + f.dataBits) * sp)) {
            r.bytes.push_back(uint8_t(by));
            done = e + (0.5 + f.dataBits) * sp;  // resume hunting inside the stop bits
        } else {
            // Never emit a suspect byte. Move to the next edge and try again -- the
            // edge list is already the set of places a character can begin.
            ++r.framingErrors;
        }
        ++fi;
    }
    return r;
}

// ---------------------------------------------------------------------------
AudioBuffer modulate(const std::vector<uint8_t>& bytes, const TapeFormat& f,
                     uint32_t rate, double leaderSeconds) {
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

    // THE LEADER. Idle is mark, and a loader needs carrier before the first start bit
    // -- both to find the tone and, on a real machine, to let the recorder's AGC
    // settle. A tape that begins at the first start bit loads nothing.
    if (leaderSeconds > 0) {
        const double cell = f.cycleCounted
                                ? std::max(1.0, std::floor(f.markHz / f.baud + 0.5)) / f.markHz
                                : 1.0 / f.baud;
        for (double t = 0; t < leaderSeconds; t += cell) bit(true);
    }

    for (uint8_t b : bytes) {
        bit(false);                                        // start
        for (int i = 0; i < f.dataBits; ++i) bit((b >> i) & 1);  // LSB first
        for (int i = 0; i < f.stopBits; ++i) bit(true);     // stop
    }

    // A short tail of mark, so the last stop bit is not the last sample on the tape.
    for (int i = 0; i < 16; ++i) bit(true);
    return a;
}

} // namespace altair
