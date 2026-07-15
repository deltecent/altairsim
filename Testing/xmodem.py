#!/usr/bin/env python3
"""A real host-side XMODEM sender/receiver over a real UART.

Not a simulator. No emulated clock, no emulated timeouts -- just a process on a real serial
port. If the simulator cannot complete a transfer against THIS, the simulator is the thing
that cannot keep up with real data, and no far-end 8080 can be blamed.

  xmodem.py send <port> <baud> <file>
  xmodem.py recv <port> <baud> <file>

Speaks both checksum and CRC-16 XMODEM: the RECEIVER chooses (NAK = checksum, 'C' = CRC), and
PCGET chooses checksum, so that is what we honor when sending to it.
"""
import os, sys, termios, time

SOH, EOT, ACK, NAK, CAN, SUB = 0x01, 0x04, 0x06, 0x15, 0x18, 0x1A
CRCCHAR = ord('C')

BAUDS = {300: termios.B300, 1200: termios.B1200, 2400: termios.B2400, 4800: termios.B4800,
         9600: termios.B9600, 19200: termios.B19200, 38400: termios.B38400,
         57600: termios.B57600, 115200: termios.B115200}


def openraw(path, baud):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0                                              # iflag: no xon/xoff, no cr/nl fixups
    a[1] = 0                                              # oflag: no post-processing
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL   # 8N1, ignore modem lines
    a[3] = 0                                              # lflag: raw, no echo
    a[4] = a[5] = BAUDS[baud]
    a[6] = list(a[6])
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def getbyte(fd, timeout):
    end = time.time() + timeout
    while time.time() < end:
        try:
            b = os.read(fd, 1)
            if b:
                return b[0]
        except BlockingIOError:
            pass
        time.sleep(0.0002)
    return None


def putall(fd, data):
    while data:
        try:
            n = os.write(fd, data)
            data = data[n:]
        except BlockingIOError:
            time.sleep(0.001)


def crc16(data):
    c = 0
    for byte in data:
        c ^= byte << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def send(fd, path):
    data = open(path, 'rb').read()
    blocks = [data[i:i + 128].ljust(128, bytes([SUB])) for i in range(0, len(data), 128)]
    print(f"sending {len(data)} bytes as {len(blocks)} blocks", flush=True)

    # The receiver opens the conversation: NAK = checksum mode, 'C' = CRC mode.
    mode = None
    end = time.time() + 60
    while time.time() < end and mode is None:
        b = getbyte(fd, 1.0)
        if b == NAK:
            mode = 'checksum'
        elif b == CRCCHAR:
            mode = 'crc'
    if mode is None:
        print("FAIL: receiver never asked (no NAK / 'C')")
        return 1
    print(f"receiver chose {mode}", flush=True)

    t0 = time.time()
    retries = 0
    for i, blk in enumerate(blocks, start=1):
        n = i & 0xFF
        frame = bytes([SOH, n, 0xFF - n]) + blk
        frame += (crc16(blk).to_bytes(2, 'big') if mode == 'crc'
                  else bytes([sum(blk) & 0xFF]))
        for attempt in range(10):
            putall(fd, frame)
            r = getbyte(fd, 10.0)
            if r == ACK:
                break
            retries += 1
            if r is None:
                print(f"  block {i}: no ACK (timeout), resending", flush=True)
            else:
                print(f"  block {i}: got {r:#04x}, resending", flush=True)
        else:
            print(f"FAIL: block {i} never acked")
            return 1
        if i % 100 == 0:
            print(f"  {i}/{len(blocks)} blocks", flush=True)

    for _ in range(10):
        putall(fd, bytes([EOT]))
        if getbyte(fd, 5.0) == ACK:
            break
    el = time.time() - t0
    print(f"DONE {el:.1f}s {int(len(blocks) * 128 / el)} B/s retries={retries}")
    return 0


def recv(fd, path):
    out = bytearray()
    expect = 1
    t0 = None
    # Ask for checksum mode (a plain NAK), repeatedly, until the sender starts.
    for _ in range(60):
        putall(fd, bytes([NAK]))
        b = getbyte(fd, 2.0)
        if b in (SOH, EOT):
            break
    else:
        print("FAIL: sender never started")
        return 1
    t0 = time.time()

    while True:
        if b == EOT:
            putall(fd, bytes([ACK]))
            break
        if b != SOH:
            b = getbyte(fd, 10.0)
            continue
        hdr = [getbyte(fd, 5.0) for _ in range(2)]
        blk = bytes(getbyte(fd, 5.0) or 0 for _ in range(128))
        csum = getbyte(fd, 5.0)
        ok = (hdr[0] is not None and hdr[1] == 0xFF - hdr[0]
              and csum == (sum(blk) & 0xFF) and len(blk) == 128)
        if ok and hdr[0] == (expect & 0xFF):
            out += blk
            expect += 1
            putall(fd, bytes([ACK]))
        else:
            putall(fd, bytes([NAK]))
        b = getbyte(fd, 10.0)
        if b is None:
            print("FAIL: sender stalled")
            return 1

    open(path, 'wb').write(out)
    el = time.time() - t0
    print(f"DONE {el:.1f}s {int(len(out) / el)} B/s wrote {len(out)} bytes to {path}")
    return 0


mode, port, baud, path = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
fd = openraw(port, baud)
sys.exit(send(fd, path) if mode == 'send' else recv(fd, path))
