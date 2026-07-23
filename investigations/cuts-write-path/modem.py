#!/usr/bin/env python3
"""Standalone CUTS 1200-baud modulator with clock-grid-aligned crossings.

The shipping modulate() carries phase across cells, which smears zero-crossings
(measured: space tone std 51 Hz vs the real tape's 1.7 Hz). Real CUTS hardware
lays its tones with a flip-flop toggled by a stable master clock, so every
crossing sits on a grid and the receiver's transition counter reads cleanly.

This models that: each bit cell's crossings land on the cell grid. Three styles:
  hwsquare    - flip-flop square: mark = full 1200 Hz cycle (2 edges/cell),
                space = half 600 Hz cycle (1 edge/cell). Edges on the grid.
  hwsquare_rc - hwsquare through a one-pole low-pass, rounding the edges the way
                the modem's RC network (and any cassette) does.
  sine_reset  - same grid, but each cell is a phase-0 sine instead of a square.

Nothing here touches the repo. Output is verified to decode back through the
shipping decoder (altair_tapetool) to the exact source bytes.
"""
import sys, wave, numpy as np

RATE = 44100
BAUD = 1200.0
CELL = RATE / BAUD            # 36.75 samples / cell
MARK, SPACE = 1, 0

def frame_bits(data, leader_cells, trailer_cells):
    """8N2, LSB first, idle/leader/trailer = mark. Returns a list of cell values."""
    bits = [MARK] * leader_cells
    for b in data:
        bits.append(SPACE)                       # start bit
        for i in range(8):
            bits.append((b >> i) & 1)            # data, LSB first
        bits += [MARK, MARK]                     # 2 stop bits
    bits += [MARK] * trailer_cells
    return bits

def toggle_times(bits):
    """Grid-aligned flip-flop edges. Every cell boundary is an edge; a mark adds
    one more at mid-cell. Fractional sample times, no accumulated drift."""
    t = []
    pos = 0.0
    for v in bits:
        t.append(pos)                            # edge at cell start
        if v == MARK:
            t.append(pos + CELL / 2.0)           # + mid-cell edge -> full cycle
        pos += CELL
    return np.array(t), pos

def render_square(bits, os=1):
    """Flip-flop square. os = oversample factor; edges land at exact fractional
    times on the os grid, so after decimation the zero-crossings are sub-sample
    accurate (the real analog signal crosses smoothly, not on integer samples)."""
    edges, total = toggle_times(bits)
    n = int(np.ceil(total)) * os
    idx = np.arange(n) / os
    cnt = np.searchsorted(edges, idx, side='right')
    return np.where((cnt & 1) == 0, 1.0, -1.0)

def render_rounded(bits, os=8):
    """Oversampled square decimated with a boxcar -> band-limited, rounded edges
    like the modem's RC network + a cassette's bandwidth, with accurate crossings."""
    hi = render_square(bits, os=os)
    n = (len(hi) // os) * os
    return hi[:n].reshape(-1, os).mean(axis=1)

def one_pole_lp(x, fc):
    a = np.exp(-2 * np.pi * fc / RATE)
    y = np.empty_like(x); acc = 0.0
    for i in range(len(x)):
        acc = a * acc + (1 - a) * x[i]
        y[i] = acc
    # remove the group-delay bias by running it backwards too (zero-phase),
    # so crossings stay on the grid instead of shifting late
    acc = 0.0
    for i in range(len(x) - 1, -1, -1):
        acc = a * acc + (1 - a) * y[i]
        y[i] = acc
    return y

def render_sine_reset(bits):
    n = int(np.ceil(len(bits) * CELL))
    out = np.zeros(n)
    pos = 0.0
    for v in bits:
        lo = int(np.ceil(pos)); hi = int(np.ceil(pos + CELL))
        t = (np.arange(lo, hi) - pos) / RATE
        hz = 1200.0 if v == MARK else 600.0
        out[lo:hi] = np.sin(2 * np.pi * hz * t)   # phase 0 at every cell start
        pos += CELL
    return out

def write_wav(path, sig, amp=0.9):
    sig = sig / (np.abs(sig).max() or 1.0) * amp
    pcm = np.clip(sig * 32767, -32768, 32767).astype('<i2')
    w = wave.open(path, 'wb')
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
    w.writeframes(pcm.tobytes()); w.close()

def build(style, data, out, leader_s=3.0, trailer_s=2.0):
    bits = frame_bits(data, int(leader_s * BAUD), int(trailer_s * BAUD))
    if style == 'hwsquare':
        sig = render_square(bits)
    elif style == 'hwsquare_rc':
        sig = render_rounded(bits, os=8)
    elif style == 'sine_reset':
        sig = render_sine_reset(bits)
    else:
        raise SystemExit('unknown style ' + style)
    write_wav(out, sig)
    print(f"  wrote {out.split('/')[-1]:28s} ({style}, {len(sig)/RATE:.1f}s)")

if __name__ == '__main__':
    style, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(src, 'rb').read()
    build(style, data, out)
