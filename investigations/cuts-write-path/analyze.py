#!/usr/bin/env python3
"""Compare the zero-crossing / transition structure of CUTS 1200-baud tapes.

Real CUTS hardware decodes by COUNTING TRANSITIONS PER BIT CELL against a
PLL-regenerated 1200 Hz clock (reference/CUTS Assembly and Test.md, read path):
  * mark  (1) = one full cycle of 1200 Hz  -> 2 transitions in the 833 us cell
  * space (0) = one half cycle of  600 Hz  -> 1 transition in the cell
So the quantities that matter to real hardware are the *number* and the
*sub-cell placement* of zero-crossings, NOT tone energy (our decoder measures
energy and is therefore blind to placement drift).
"""
import sys, wave, numpy as np

RATE = 44100
BAUD = 1200.0
CELL = RATE / BAUD          # 36.75 samples per bit cell

def load(path):
    w = wave.open(path, 'rb')
    n, sr, ch, sw = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(n); w.close()
    dt = {1: np.int8, 2: np.int16, 4: np.int32}[sw]
    a = np.frombuffer(raw, dtype=dt).astype(np.float64)
    if ch > 1: a = a[::ch]
    a /= (np.abs(a).max() or 1.0)     # normalize to +-1 (AGC does this in HW)
    return a, sr

def crossings(a):
    """Indices (fractional) where the signal crosses zero. This is what the
    hardware comparator fires on."""
    s = np.sign(a); s[s == 0] = 1
    idx = np.where(np.diff(s) != 0)[0]
    # linear-interpolate the sub-sample crossing time
    x0, x1 = a[idx], a[idx + 1]
    frac = x0 / (x0 - x1)
    return idx + frac

def report(name, path):
    a, sr = load(path)
    print(f"\n=== {name}  ({path.split('/')[-1]}) ===")
    print(f"  samples {len(a)}  rate {sr}  peak {np.abs(a).max():.3f}  "
          f"dc {a.mean():+.4f}  rms {np.sqrt((a*a).mean()):.3f}")
    xc = crossings(a)
    ivals = np.diff(xc)                       # inter-crossing intervals, in samples
    # a mark half-period ~ RATE/2400 = 18.375 samp; a space half-period ~ RATE/1200 = 36.75
    # tone from an interval = rate/(2*interval)
    hz = sr / (2 * ivals)
    # bucket: "mark-ish" crossings sit near 1200 Hz spacing, "space-ish" near 600
    print(f"  crossings {len(xc)}   interval samples: "
          f"median {np.median(ivals):.2f}  mean {ivals.mean():.2f}")
    # histogram of implied tone
    edges = [0, 300, 450, 600, 800, 1000, 1400, 1800, 3000, 99999]
    h, _ = np.histogram(hz, bins=edges)
    print("  implied-tone histogram (Hz buckets):")
    for i in range(len(edges) - 1):
        if h[i]:
            print(f"      {edges[i]:>5}-{edges[i+1]:<5} : {h[i]:6d}  {'#'*int(40*h[i]/h.max())}")
    # the two dominant clusters
    markish  = hz[(hz > 900) & (hz < 1500)]
    spaceish = hz[(hz > 400) & (hz < 800)]
    if len(markish):
        print(f"  MARK  cluster: n={len(markish):5d}  mean {markish.mean():7.1f} Hz  "
              f"std {markish.std():5.1f}  (ideal 1200)")
    if len(spaceish):
        print(f"  SPACE cluster: n={len(spaceish):5d}  mean {spaceish.mean():7.1f} Hz  "
              f"std {spaceish.std():5.1f}  (ideal 600)")
    return a, xc

def cell_phase(name, a, xc, sr):
    """Where do crossings fall WITHIN a bit cell? Real hardware gates the
    'second transition' against a counter that expects it near mid-cell.
    We fold every crossing time modulo the cell period and look at the spread.
    A hardware-clean tape lands crossings at a few tight phases; a phase-carried
    synthesis smears them across the whole cell."""
    cell = sr / BAUD
    # use only the data region: skip first/last 3s of leader/trailer tone
    lo, hi = int(3.2 * sr), len(a) - int(2.2 * sr)
    d = xc[(xc > lo) & (xc < hi)]
    ph = np.mod(d, cell) / cell          # 0..1 within a cell
    # concentration: how peaked is the phase distribution?
    hbins = 24
    h, _ = np.histogram(ph, bins=hbins, range=(0, 1))
    frac_top = np.sort(h)[::-1][:4].sum() / h.sum()   # mass in the 4 busiest bins
    print(f"  [{name}] crossing-phase concentration "
          f"(mass in 4/{hbins} busiest bins): {frac_top*100:4.1f}%   "
          f"{'<-- tight, cell-aligned' if frac_top>0.6 else '<-- SMEARED across the cell'}")
    return ph

if __name__ == '__main__':
    SP = sys.argv[1]
    a_r, xc_r = report("REAL dub",        f"examples/sol/TRK80.WAV")
    a_q, xc_q = report("OURS square",     f"{SP}/ours_square.wav")
    a_s, xc_s = report("OURS sine",       f"{SP}/ours_sine.wav")
    print("\n--- crossing placement within the bit cell ---")
    cell_phase("REAL  ", a_r, xc_r, RATE)
    cell_phase("square", a_q, xc_q, RATE)
    cell_phase("sine  ", a_s, xc_s, RATE)
