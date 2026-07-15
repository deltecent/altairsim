#!/usr/bin/env python3
"""Prove the null-modem cable before blaming the simulator.

Opens both cu.* ports raw at 9600 8N1, writes a known string on each, reads it
on the other. If this fails, nothing downstream means anything.
"""
import os, sys, termios, time

A = "/dev/cu.usbserial-AB0NW409"
B = "/dev/cu.usbserial-AL009KFH"


def openraw(path, baud=termios.B9600):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    # iflag oflag cflag lflag ispeed ospeed cc
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    a[4] = baud
    a[5] = baud
    a[6] = list(a[6])
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def drain(fd):
    try:
        while os.read(fd, 256):
            pass
    except BlockingIOError:
        pass


def xfer(src, dst, msg, label):
    drain(dst)
    os.write(src, msg)
    got = b""
    deadline = time.time() + 3.0
    while time.time() < deadline and len(got) < len(msg):
        try:
            got += os.read(dst, 256)
        except BlockingIOError:
            time.sleep(0.01)
    ok = got == msg
    print(f"  {label}: sent {msg!r} got {got!r}  {'OK' if ok else 'FAIL'}")
    return ok


fa = openraw(A)
fb = openraw(B)
print(f"A = {A}\nB = {B}\n")

ok = True
ok &= xfer(fa, fb, b"HELLO-FROM-A\r\n", "A -> B")
ok &= xfer(fb, fa, b"HELLO-FROM-B\r\n", "B -> A")

# 8-bit clean? XMODEM is binary; a 7-bit strap or a line discipline would eat this.
binary = bytes(range(256))
ok &= xfer(fa, fb, binary, "A -> B (all 256 byte values)")

os.close(fa)
os.close(fb)
print("\n" + ("CABLE OK" if ok else "CABLE FAILED"))
sys.exit(0 if ok else 1)
