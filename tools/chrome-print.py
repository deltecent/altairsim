#!/usr/bin/env python3
# Print an HTML file to PDF with headless Chrome, WAITING for Paged.js to finish first.
#
# WHY THIS EXISTS, AND WHY IT IS NOT JUST `chrome --print-to-pdf`.
#
# The PDFs get their running page numbers, their page-numbered table of contents and their
# honest page breaks from Paged.js (docs/pagedjs/) -- a polyfill that runs inside the browser
# and re-paginates the DOM. Paged.js finishes ASYNCHRONOUSLY, well after the page's load event.
# `chrome --print-to-pdf` captures at load, so on its own it prints the un-paginated document.
# The obvious fix -- `--virtual-time-budget`, which makes --print-to-pdf wait -- was tried and
# does NOT work here: Paged.js's layout loop never completes under a virtual clock (its after()
# hook never fires; the print truncates to two pages). Verified on both --headless and
# --headless=new, at every budget from 5s to 300s. Paged.js needs REAL time.
#
# So we do what pagedjs-cli does, minus the Node/Puppeteer: talk to Chrome over the DevTools
# Protocol, WATCH for the completion signal the injected hook raises (docs/print.css's build
# wires window.PagedConfig.after to set document.title = "PAGES_<n>"), and only THEN ask for
# the PDF -- with preferCSSPageSize so each Paged.js page becomes one sheet. Stdlib only: no
# pip, no node. socket + hashlib do the WebSocket by hand because a single request/response to
# localhost does not justify a dependency the rest of altairsim refuses to carry.
#
#   usage: chrome-print.py <chrome-binary> <input.html> <output.pdf>
#
# Exit non-zero (with a reason on stderr) if the browser cannot be reached, if Paged.js never
# signals done within the timeout, or if the PDF does not come back -- build-docs.sh turns any
# of these into a failed build rather than a silently wrong document.

import base64
import hashlib
import json
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.request

DONE_TIMEOUT = 120.0   # seconds of REAL time to let Paged.js paginate before giving up
POLL = 0.1


def die(msg):
    sys.stderr.write("chrome-print: " + msg + "\n")
    sys.exit(1)


# --- the smallest WebSocket client that can carry one CDP conversation ------------------
class WS:
    def __init__(self, url):
        # ws://host:port/devtools/page/<id>
        assert url.startswith("ws://"), url
        rest = url[len("ws://"):]
        hostport, _, path = rest.partition("/")
        host, _, port = hostport.partition(":")
        self.sock = socket.create_connection((host, int(port or "80")), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            "GET /%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % (path, hostport, key)
        )
        self.sock.sendall(req.encode())
        resp = self._recv_until(b"\r\n\r\n")
        if b" 101 " not in resp.split(b"\r\n", 1)[0]:
            die("WebSocket upgrade refused: " + resp.split(b"\r\n", 1)[0].decode("latin1"))
        self._buf = b""

    def _recv_until(self, marker):
        data = b""
        while marker not in data:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            data += chunk
        return data

    def _recv_exact(self, n):
        while len(self._buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                die("WebSocket closed mid-frame")
            self._buf += chunk
        out, self._buf = self._buf[:n], self._buf[n:]
        return out

    def send(self, obj):
        payload = json.dumps(obj).encode()
        header = bytearray([0x81])  # FIN + text frame
        n = len(payload)
        mask = os.urandom(4)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        header += mask
        masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        self.sock.sendall(bytes(header) + masked)

    def recv(self):
        # Reassemble one (possibly fragmented) server text message. Server->client is unmasked.
        chunks = []
        while True:
            b0, b1 = self._recv_exact(2)
            fin = b0 & 0x80
            opcode = b0 & 0x0F
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._recv_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._recv_exact(8))[0]
            data = self._recv_exact(length) if length else b""
            if opcode == 0x8:
                die("WebSocket close from browser")
            if opcode in (0x1, 0x2, 0x0):
                chunks.append(data)
                if fin:
                    return b"".join(chunks)
            # ping/pong (0x9/0xA) are ignored


def main():
    if len(sys.argv) != 4:
        die("usage: chrome-print.py <chrome> <input.html> <output.pdf>")
    chrome, html, pdf = sys.argv[1], sys.argv[2], sys.argv[3]
    html = os.path.abspath(html)
    url = "file://" + html

    # A private profile and an ephemeral port so parallel builds never collide.
    profile = pdf + ".chrome-profile"
    port = _free_port()
    proc = subprocess.Popen(
        [chrome, "--headless=new", "--disable-gpu", "--no-sandbox",
         "--remote-debugging-port=%d" % port,
         "--user-data-dir=" + profile, url],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        ws_url = _wait_for_target(port, html)
        ws = WS(ws_url)
        _drive(ws, pdf)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except Exception:
            proc.kill()


def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def _wait_for_target(port, html_path):
    base = os.path.basename(html_path)
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            raw = urllib.request.urlopen("http://127.0.0.1:%d/json" % port, timeout=2).read()
            for tgt in json.loads(raw):
                if tgt.get("type") == "page" and base in tgt.get("url", ""):
                    if tgt.get("webSocketDebuggerUrl"):
                        return tgt["webSocketDebuggerUrl"]
        except Exception:
            pass
        time.sleep(POLL)
    die("Chrome never exposed the page target on the debugging port")


def _drive(ws, pdf):
    mid = [0]

    def call(method, params=None):
        mid[0] += 1
        this = mid[0]
        ws.send({"id": this, "method": method, "params": params or {}})
        while True:
            msg = json.loads(ws.recv())
            if msg.get("id") == this:
                if "error" in msg:
                    die(method + " failed: " + json.dumps(msg["error"]))
                return msg.get("result", {})
            # ignore events

    call("Runtime.enable")
    call("Page.enable")

    # Wait -- in REAL time -- for Paged.js's after() hook to stamp the title. The hook is wired
    # by build-docs.sh (window.PagedConfig.after -> document.title = "PAGES_<n>").
    deadline = time.time() + DONE_TIMEOUT
    pages = None
    while time.time() < deadline:
        r = call("Runtime.evaluate", {"expression": "document.title", "returnByValue": True})
        title = r.get("result", {}).get("value", "") or ""
        if title.startswith("PAGES_"):
            pages = title[len("PAGES_"):]
            break
        time.sleep(POLL)
    if pages is None:
        die("Paged.js did not finish within %.0fs (its after() hook never fired)" % DONE_TIMEOUT)

    # preferCSSPageSize: honor the @page size Paged.js set, so one Paged.js page == one sheet.
    # Margins zero: Paged.js already put the margins inside each page box.
    r = call("Page.printToPDF", {
        "printBackground": True,
        "preferCSSPageSize": True,
        "marginTop": 0, "marginBottom": 0, "marginLeft": 0, "marginRight": 0,
    })
    data = r.get("data")
    if not data:
        die("Page.printToPDF returned no data")
    with open(pdf, "wb") as fh:
        fh.write(base64.b64decode(data))
    sys.stderr.write("chrome-print: %s (%s pages)\n" % (pdf, pages))


if __name__ == "__main__":
    main()
