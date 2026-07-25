#!/usr/bin/env python3
"""Definitive CUTS write-path model + a hardware-faithful read model.

WRITE (models Sol-20 U2): a JK flip-flop divides a continuous 2400 Hz master
clock by 2 (mark -> 1200 Hz, one full cycle/cell) or 4 (space -> 600 Hz, one
half cycle/cell). Every edge lands on the 2400 Hz grid, so crossings are on-grid
by construction (0% "blended"). Then a zero-phase RC low-pass rounds the edges
(R15/R16/R17 + cassette bandwidth), and a level scales the output.

READ (models the CUTS read path, reference/CUTS Assembly and Test.md §5.3.4):
comparator squares the signal -> transitions -> per bit cell, count transitions:
two (a full 1200 Hz cycle) = mark(1), one (a half 600 Hz cycle) = space(0). This
fires on TRANSITION TIMING, not tone energy, so off-grid crossings break it -
exactly like the real hardware, and unlike altairsim's energy matched filter.
"""
import sys, wave, numpy as np

RATE=44100; BAUD=1200.0; CELL=RATE/BAUD          # 36.75 samples/cell
MARKHP=RATE/2400.0                                # 18.375 (1200 Hz half-period)
SPACEHP=RATE/1200.0                               # 36.75  (600 Hz half-period)
MARK,SPACE=1,0

# ---------------------------------------------------------------- WRITE ------
def frame_bits(data, leader_cells, trailer_cells):
    bits=[MARK]*leader_cells
    for b in data:
        bits.append(SPACE)                        # start bit
        for i in range(8): bits.append((b>>i)&1)  # 8 data, LSB first
        bits+=[MARK,MARK]                         # 2 stop bits
    bits+=[MARK]*trailer_cells
    return bits

def u2_square(bits, os):
    """Flip-flop edges on the 2400 Hz grid: mark cell = 2 edges, space = 1."""
    t=[]; pos=0.0
    for v in bits:
        t.append(pos)
        if v==MARK: t.append(pos+CELL/2.0)
        pos+=CELL
    edges=np.array(t)*os
    n=int(np.ceil(pos))*os
    idx=np.arange(n)
    return np.where((np.searchsorted(edges,idx,side='right')&1)==0,1.0,-1.0)

def rc_lowpass_zerophase(x, fc, fs):
    """Two-pass one-pole (zero-phase => 2-pole magnitude, no crossing shift)."""
    a=np.exp(-2*np.pi*fc/fs)
    y=np.empty_like(x); acc=0.0
    for i in range(len(x)): acc=a*acc+(1-a)*x[i]; y[i]=acc
    acc=0.0
    for i in range(len(x)-1,-1,-1): acc=a*acc+(1-a)*y[i]; y[i]=acc
    return y

def encode(data, out, fc=6000.0, level=0.36, leader_s=4.0, trailer_s=2.0, os=16):
    bits=frame_bits(data,int(leader_s*BAUD),int(trailer_s*BAUD))
    sq=u2_square(bits,os)
    y=rc_lowpass_zerophase(sq, fc, RATE*os)
    m=len(y)//os*os
    sig=y[:m].reshape(-1,os).mean(axis=1)          # decimate to RATE
    sig=sig/np.abs(sig).max()*level                # scale to level (fraction of FS)
    pcm=np.clip(sig*32767,-32768,32767).astype('<i2')
    w=wave.open(out,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
    w.writeframes(pcm.tobytes()); w.close()
    return sig

# ----------------------------------------------------------------- READ ------
def load(path):
    w=wave.open(path,'rb'); n,sr,ch,sw=w.getnframes(),w.getframerate(),w.getnchannels(),w.getsampwidth()
    a=np.frombuffer(w.readframes(n),{1:np.int8,2:np.int16,4:np.int32}[sw]).astype(np.float64); w.close()
    if ch>1: a=a[::ch]
    return a,sr

def transitions(a):
    s=np.sign(a); s[s==0]=1
    idx=np.where(np.diff(s)!=0)[0]
    x0,x1=a[idx],a[idx+1]
    return idx + x0/(x0-x1)                         # fractional crossing times

def hw_decode(a, sr):
    """Transition-timing decoder. Walk inter-transition intervals; a long
    interval (~600 Hz half) = a space cell, two short intervals (~1200 Hz) = a
    mark cell. Recover the UART frame (8N2, LSB first) and return the bytes."""
    xc=transitions(a); iv=np.diff(xc)
    shortv=sr/2400.0; longv=sr/1200.0
    # classify each interval; None if it is neither (an off-grid "blended" edge)
    def kind(d):
        if abs(d-shortv) <= 0.35*shortv: return 'S'
        if abs(d-longv)  <= 0.35*longv:  return 'L'
        return '?'
    ks=[kind(d) for d in iv]
    # fold intervals into cells: 'L' -> space bit; 'S','S' -> mark bit
    cells=[]; i=0
    while i < len(ks):
        if ks[i]=='L':
            cells.append(SPACE); i+=1
        elif ks[i]=='S' and i+1<len(ks) and ks[i+1]=='S':
            cells.append(MARK); i+=2
        else:
            cells.append(-1); i+=1                  # unrecoverable (blended/odd)
    # UART framer: hunt for start bit (space) after mark idle, read 8N2 LSB-first
    out=bytearray(); j=0; N=len(cells); framing_err=0
    while j < N:
        if cells[j]!=SPACE:                         # wait for a start bit
            j+=1; continue
        if j+10 > N: break
        frame=cells[j+1:j+9]                         # 8 data bits
        stop=cells[j+9]
        if -1 in frame or cells[j]!=SPACE:
            framing_err+=1; j+=1; continue
        byte=0
        for b in range(8): byte |= (frame[b]&1)<<b
        out.append(byte)
        if stop!=MARK: framing_err+=1
        j+=11                                         # start+8+2 stop
    return bytes(out), framing_err

# --------------------------------------------------------------- METRICS -----
def blended_pct(a,sr):
    xc=transitions(a); hp=np.diff(xc)
    mk=np.abs(hp-sr/2400)<3; sp=np.abs(hp-sr/1200)<4
    return 100*(~(mk|sp)).mean()

def harmonics(a,sr,f0=1200.0,t0=0.5,dur=2.0):
    seg=a[int(t0*sr):int((t0+dur)*sr)].astype(float); seg=seg-seg.mean(); seg*=np.hanning(len(seg))
    S=np.abs(np.fft.rfft(seg)); fr=np.fft.rfftfreq(len(seg),1/sr)
    amp=lambda f:(lambda k:S[max(0,k-2):k+3].max())(int(np.argmin(np.abs(fr-f))))
    h1=amp(f0); return amp(3*f0)/h1, amp(5*f0)/h1

if __name__=='__main__':
    cmd=sys.argv[1] if len(sys.argv)>1 else 'demo'
    if cmd=='hwdecode':
        a,sr=load(sys.argv[2]); by,fe=hw_decode(a,sr)
        sys.stdout.buffer.write(by)
        sys.stderr.write(f"hw_decode: {len(by)} bytes, {fe} framing errors\n")
