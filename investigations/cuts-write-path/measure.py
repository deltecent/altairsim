#!/usr/bin/env python3
"""Rank tapes by how closely their zero-crossing tones match a real Sol dub.
The real dub's crossings are metronome-clean (space std ~1.7 Hz). A tape that
real hardware can read should look the same; the shipping (phase-carried)
encodings are smeared 30x wider."""
import sys, wave, numpy as np

def load(path):
    w = wave.open(path, 'rb'); n, sr, ch, sw = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(n); w.close()
    a = np.frombuffer(raw, {1:np.int8,2:np.int16,4:np.int32}[sw]).astype(np.float64)
    if ch > 1: a = a[::ch]
    return a / (np.abs(a).max() or 1.0), sr

def stats(path):
    a, sr = load(path)
    s = np.sign(a); s[s==0] = 1
    idx = np.where(np.diff(s) != 0)[0]
    frac = a[idx] / (a[idx] - a[idx+1])
    xc = idx + frac
    hz = sr / (2 * np.diff(xc))
    mk = hz[(hz>900)&(hz<1500)]; sp = hz[(hz>400)&(hz<800)]
    return (mk.mean(), mk.std(), sp.mean(), sp.std())

rows = [(name, stats(p)) for name, p in (
    ("REAL dub (loads on HW)",   "examples/sol/TRK80.WAV"),
    ("A  current square (SHIP)", sys.argv[1]+"/ours_square.wav"),
    ("   current sine",          sys.argv[1]+"/ours_sine.wav"),
    ("D  hw square (grid)",      sys.argv[1]+"/cand_D_hwsquare.wav"),
    ("E  hw square + RC (grid)", sys.argv[1]+"/cand_E_hwsquare_rc.wav"),
)]
print(f"{'tape':28s} {'mark mean/std':>18s}   {'space mean/std':>18s}")
print("-"*72)
for name,(mm,ms,pm,ps) in rows:
    print(f"{name:28s}  {mm:7.1f} /{ms:6.1f}     {pm:7.1f} /{ps:6.1f}")
print("\nideal: mark 1200, space 600.  Real space std = the target (~1.7 Hz).")
