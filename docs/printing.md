# Printing to a real printer

**Status: design note. Nothing here is built.** Discussion and the open
questions live on [issue #70](https://github.com/deltecent/altairsim/issues/70);
this document is the detail behind it. This is a working document in
the source tree — it is in neither the User Manual nor the Developer Guide,
because documenting an unshipped knob in either one is drift, and drift is a
bug. It exists so the platform research does not have to be done twice, and so
the design call it depends on is written down before someone starts typing.

The want: a guest program prints, and paper comes out of a printer attached to
this host. Today the only destinations for printed bytes are `file:PATH` and
`null` — a capture, not a printout.

---

## 1. Where this belongs: the endpoint, not the board

The instinct is to put printing in the 88-C700, because the C700 is the printer
card. That would be wrong, and expensively so.

A printer is something on the far end of a line, and `src/host/stream.h` is
emphatic about what that means: a board knows it has a line and must never
learn what is on the other end of it. The endpoint grammar lives in exactly one
place (`resolveEndpoint`, `src/host/endpoint.cpp`) and no board parses an
endpoint string. That is why `CONNECT sio0:a serial:/dev/tty.usbserial-AL009KFH`
needed not one line of code in the 2SIO.

So printing is a new `ByteStream`, `printer:QUEUE`, added to the resolver — and
every board that can put a byte on a line gets it the day it lands, with no
board changed:

- the **88-C700** parallel printer interface (`src/boards/mits-88c700.cpp`)
- the **88-LPC**, when it is built
- the **Sol-20**'s printer unit (`src/boards/proctech-sol.cpp`)
- the **88-SIO** and **88-2SIO**, because a serial printer was as ordinary as a
  parallel one and an operator will want `CONNECT sio2a printer:linewriter`
- any parallel or printer card written next year, for free

This is the same argument the `Display` seam settled and the same one
`ByteStream` was built to settle. Making it a C700 property would mean writing
it again for the LPC, again for the Sol, and again for the 2SIO — four
implementations that would drift.

**Read-only, in the other direction.** A printer is write-only: `read()` returns
0 forever and `readable()` is false. That is not a special case; it is what
`NullStream` already does, and a card wired to a printer polling for input finds
a quiet line, exactly as it would on the bench.

---

## 2. The hard part: when is the job finished?

Centronics has no end-of-job signal. Neither does a serial line. The guest
writes bytes and stops writing bytes, and stopping is not an event — this is a
real property of the hardware, not a gap in the emulation. A real line printer
resolved it mechanically: the paper was continuous, and the operator tore it
off.

A host print queue cannot work that way. It wants a *job*: a finite blob handed
over once. So something has to decide where one printout ends and the next
begins.

### 2.1 The idle timer (the proposal)

Buffer the bytes. Every byte written restarts a timer. When the timer expires
with no further byte, close the buffer and submit it as one job.

This is right because it matches what the guest actually does — a program
prints a report, then goes back to the user — and because it needs no
cooperation from guest software that was written in 1977 and cannot be changed.

Three things follow from it, and each is a finding, not a detail:

**The timer must run on WALL time, not the Clock.** The default clock is
free-running (`clock_hz = 0`), so emulated seconds have no fixed relation to
real ones; a five-second emulated timeout could fire in eighty milliseconds, or
never. The test the repo already uses is *can a guest read it back* — and it
cannot read this, because no register exposes it. It is the host's dwell time
before it gives up waiting for more paper. So it belongs on host seconds, like
the VDM-1's cursor blink (`Display::hostSeconds()`, PR #69), and not on the
Clock. Getting this backwards is the exact mistake that made the VDM-1 cursor
strobe.

**`pump()` stops when the guest stops.** `Machine::pump()` is called from inside
the run loop (`src/cli/monitor.cpp:1082`) and nowhere else. A timer ticked only
from `pump()` would never fire if the guest halts, breakpoints, or the operator
hits ATTN right after printing — the commonest case of all, since a program that
just finished a report is often a program that just finished. The buffer must
therefore also be flushed when the run loop exits, on `DISCONNECT`, on reset, on
`CONFIG LOAD` (which replaces the machine wholesale), and at exit. The idle
timer is the *usual* boundary, not the only one. `MediaFile::commit()` is the
precedent: the thing that guarantees the bytes land is not the thing that
usually pushes them.

**`flush()` is already taken and cannot mean end-of-job.** `ByteStream::flush()`
means "push what you are holding down the pipe", and the C700 calls it on *every
pump* (`src/boards/mits-88c700.cpp:132`) so a capture file is visible while the
machine runs. If `flush()` submitted a job, every idle slice would submit an
empty one. The job boundary needs its own name inside the printer stream; it
must not ride on `flush()`.

### 2.2 The other boundaries, and which are worth having

- **A form feed (`0x0C`)** is the closest thing the era had to "page done". It
  is a plausible boundary and a bad default: a multi-page report is one job, and
  splitting it makes N jobs that interleave with everyone else's on a shared
  queue. Offer it as an option (`onff`), do not assume it.
- **The C700's PRIME line** (D0 low on the control port, `kPrimeLow`) resets the
  printer and homes the head. It is a *start*-of-job signal, not an end — it is
  what a program sends before it prints, not after. Treating it as a boundary
  would submit the previous job at the right time only by accident.
- **An explicit operator verb** — something like `PRINT FLUSH` or
  `EJECT <board>` — should exist regardless. When the timer is set long, or the
  guest is paused mid-report, the operator needs a way to say "send it now"
  without disconnecting the line.

### 2.3 The parameters

**Settled (Patrick, 2026-07-19): the parameters go in the endpoint spec**, not
in `SET` properties on the board's unit. The spec is the resolver's business and
the resolver is the only thing permitted to know this grammar; putting them on
the unit would hand every board a printer-shaped property it has no business
having, and would need adding again to the next card. In the spec they also
round-trip for free — `describe()` returns what the operator typed, and that is
what `SHOW` prints and `CONFIG SAVE` writes back.

```
printer:QUEUE[?key[=value][&key[=value]...]]
```

**A boolean key may be written bare**: `?onff` means on, and an unmentioned key
keeps its default, so the common case never types `=1`. The `=value` form still
parses, and that is deliberate rather than redundant — it is the only way to
write *off*, which matters because whether `onff` should default on is an open
question (§2.2). If that answer ever flips, a bare-only grammar could not
express the other state and would have to change under specs people had already
written. One branch in the parser buys the door staying open: a token with no
`=` is `=true`, otherwise the value goes to `parseValue`.

| Name | Meaning | Default |
|---|---|---|
| `idle` | Host seconds of silence that end a job. `0` = never; the verb is the only boundary. | 5 |
| `onff` | Also end a job on a form feed (`0x0C`). | `false` |
| `max` | Byte ceiling on one job, so a runaway loop cannot eat memory. | a large finite number |

```
CONNECT c700 printer:linewriter
CONNECT c700 printer:linewriter?idle=15
CONNECT c700 printer:linewriter?onff
CONNECT sio2a "printer:Generic / Text Only?idle=0&onff"
```

**Parse the values with `parseValue`, not by hand.** Where a value *is* given,
the tree already has exactly one answer for what a boolean may be written as
(`parseValue`, `src/core/value.cpp:138`): `true`/`yes`/`on`/`1` and
`false`/`no`/`off`/`0`, case-insensitively, displayed always as `true`/`false`.
Calling it here means `onff=on`, `onff=1` and `onff=YES` all work and mean the
same thing as everywhere else in the simulator, and `idle` gets the same
radix-aware number parsing every other integer property gets. A hand-rolled
parser that accepted only `1` would be a second convention for no gain, and the
operator would find it by having a spelling rejected that works fine three
commands earlier.

`describe()` should print the bare form for a true boolean, so what `SHOW` and
`CONFIG SAVE` show is what a person would have typed.

**The three boundaries compose; they are not alternatives.** `idle`, `onff` and
`max` are independent, OR'd, and the first to fire ends the job. `idle=5&onff=1`
means a form feed ends the page at once *and* a report that simply stops is
still submitted five seconds later. `idle=0&onff=1` means the form feed is the
only automatic boundary — which is the right setting for a driver that emits
one, and is unreachable if the two are treated as exclusive.

That composition carries one hazard, and it is worth building for on the first
day: **an empty buffer must never submit.** A form feed closes the job, and the
idle timer then expires a few seconds later with nothing in hand. If that
submits, every printout is followed by a blank job — silent, and diagnosed only
by someone standing at the printer counting blank pages. Every boundary checks
that there is something to send.

Three more consequences to build to:

- **`?` is the separator, not `:`.** `socket:HOST:PORT` already spends the colon,
  and a queue name may contain one. `?` and `&` cannot appear in a CUPS queue
  name and are vanishingly unlikely in a Windows printer name, so the split is
  unambiguous: first `?` ends the queue name, everything after is
  `&`-separated `key=value`.
- **A Windows queue name can contain spaces** — `Generic / Text Only` is the
  driver this document tells people to install. The monitor splits a command
  line on whitespace, so either the operator quotes the spec or the name is
  unreachable. Whichever way that lands, it must be settled when this is built
  and not discovered by someone whose printer will not connect.
- **An unknown key is an error, with the list.** `printer:lw?idel=10` must say so
  and name the four it could have meant. The resolver never guesses — that rule
  is already written down in `endpoint.h`, and a silently ignored typo here
  means jobs that never come out and no clue why.

---

## 3. Reaching the printer on each host

Both target platforms fall into two implementation families, not three.

- **Windows** — talk to the Print Spooler directly through the **WinSpool API**
  (`OpenPrinter` / `StartDocPrinter` / `WritePrinter`), submitting with datatype
  `"RAW"`. This bypasses driver-level reformatting and delivers the bytes
  untouched.
- **macOS and Linux** — both sit on **CUPS**. One implementation over the CUPS C
  API (`cupsPrintFile2`) covers both, provided the destination queue is created
  as a **raw** queue (no PPD, no filters).

USB Centronics printers appear as ordinary printer queues on all three OSes
(through a USB-to-parallel adapter, or a built-in USB port on a newer line
printer). None of them expose the printer as a raw character device that can be
opened portably, so going through the OS's queue mechanism forced into
raw/passthrough mode is both the most portable route and the correct one.

### 3.1 Files and CMake wiring

- `printer.h` — the shared interface (`print_raw`, `print_raw_file`,
  `list_queues`)
- `printer_windows.cpp` — WinSpool
- `printer_cups.cpp` — CUPS (macOS + Linux)

```cmake
if(WIN32)
    target_sources(printer PRIVATE printer_windows.cpp)
    target_link_libraries(printer PRIVATE winspool)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CUPS REQUIRED cups)
    target_sources(printer PRIVATE printer_cups.cpp)
    target_include_directories(printer PRIVATE ${CUPS_INCLUDE_DIRS})
    target_link_libraries(printer PRIVATE ${CUPS_LIBRARIES})
endif()
```

Only one of the two is ever compiled in, so there is no `#ifdef` at any call
site and no dead code shipped per platform — the selection happens entirely at
configure time, matching the existing per-platform source pattern
(`src/platform/`).

**It must be optional, and this is a finding.** `REQUIRED` above is wrong for
this tree. CUPS headers are not guaranteed on a build host — a Linux CI runner
without `libcups2-dev` would stop configuring, which would break a build that
works today over a feature nobody asked that leg to test. Follow the SDL3
precedent exactly (`CMakeLists.txt:250`): an `ALTAIRSIM_ENABLE_PRINTER` option
defaulting to ON meaning *use it if it is here*, a quiet detect, a `message
(STATUS)` either way, and a build without it where `printer:` is simply not in
`endpointHelp()` and the resolver says so by name. The one endpoint that cannot
work on a given host must fail at `CONNECT` with a sentence, not at compile.

### 3.2 One-time queue setup

Configuring the queue is an install-time admin step. The application assumes a
queue that passes data through untouched and should not try to create one.

**Windows**

1. Install the printer with the **Generic / Text Only** driver, attached to the
   USB port the adapter enumerates as.
2. Use the exact name from **Devices and Printers** as the queue name. Sharing
   is not required — `OpenPrinter` addresses it by device name directly.

**macOS / Linux (CUPS)**

```sh
lpinfo -v                                               # what CUPS can see
sudo lpadmin -p linewriter -E -v usb://Vendor/Product -m raw
```

`-m raw` tells CUPS to skip all filtering; the bytes written are delivered as-is.

### 3.3 `print_raw`, both platforms

**Windows** — WinSpool, datatype `"RAW"` so the spooler does not reinterpret:

```cpp
bool print_raw(const std::string& queue_name,
               const std::vector<uint8_t>& data,
               std::string& error_out) {
  HANDLE hPrinter = nullptr;
  if (!OpenPrinterA(const_cast<char*>(queue_name.c_str()), &hPrinter, nullptr)) {
    error_out = "OpenPrinter failed for '" + queue_name + "' (error " +
                std::to_string(GetLastError()) + ")";
    return false;
  }

  DOC_INFO_1A docInfo = {};
  docInfo.pDocName    = const_cast<char*>("Raw Print Job");
  docInfo.pOutputFile = nullptr;
  docInfo.pDatatype   = const_cast<char*>("RAW");

  bool  ok    = false;
  DWORD jobId = StartDocPrinterA(hPrinter, 1, reinterpret_cast<BYTE*>(&docInfo));
  if (jobId == 0) {
    error_out = "StartDocPrinter failed (error " + std::to_string(GetLastError()) + ")";
    ClosePrinter(hPrinter);
    return false;
  }

  if (!StartPagePrinter(hPrinter)) {
    error_out = "StartPagePrinter failed (error " + std::to_string(GetLastError()) + ")";
    EndDocPrinter(hPrinter);
    ClosePrinter(hPrinter);
    return false;
  }

  DWORD written = 0;
  BOOL  wroteOk = WritePrinter(hPrinter, const_cast<uint8_t*>(data.data()),
                               static_cast<DWORD>(data.size()), &written);
  if (!wroteOk || written != data.size()) {
    error_out = "WritePrinter failed or short write (error " +
                std::to_string(GetLastError()) + ")";
  } else {
    ok = true;
  }

  EndPagePrinter(hPrinter);
  EndDocPrinter(hPrinter);
  ClosePrinter(hPrinter);
  return ok;
}
```

**macOS / Linux** — CUPS, via a temp file, with `raw` so filtering is skipped:

```cpp
bool print_raw(const std::string& queue_name,
               const std::vector<uint8_t>& data,
               std::string& error_out) {
  std::string tmp_path;
  if (!write_temp_file(data, tmp_path, error_out)) return false;
  bool ok = print_raw_file(queue_name, tmp_path, error_out);
  std::remove(tmp_path.c_str());
  return ok;
}

bool print_raw_file(const std::string& queue_name,
                    const std::string& file_path,
                    std::string& error_out) {
  cups_option_t* options     = nullptr;
  int            num_options = 0;
  num_options = cupsAddOption("raw", "true", num_options, &options);
  num_options = cupsAddOption("document-format", "application/vnd.cups-raw",
                              num_options, &options);

  int job_id = cupsPrintFile2(CUPS_HTTP_DEFAULT, queue_name.c_str(),
                              file_path.c_str(), "Raw Print Job",
                              num_options, options);
  cupsFreeOptions(num_options, options);

  if (job_id == 0) {
    error_out = std::string("cupsPrintFile2 failed: ") + cupsLastErrorString();
    return false;
  }
  return true;
}
```

Same signature and same header, two entirely different bodies — neither API
exists on the other platform, which is why this cannot collapse into one
function.

---

## 4. Findings that shape the build

**Submitting must not block the guest.** `ByteStream` is emphatic: the board
never blocks, because blocking stops emulated time and that is how an emulator
drops characters. `cupsPrintFile2` talks to a daemon and `StartDocPrinter` talks
to the spooler; either can take a noticeable moment, and neither may be called
from `write()`. Submission happens on the `pump()`/flush path, and if it turns
out to be slow enough to be felt, it wants a worker — but measure before
building one.

**A failed job must be loud and must not be silent data loss.** A queue that has
gone away, a printer that is off — these produce an error string from both APIs,
and it has to reach the operator through `Board::drainLog()`, the same route a
serial port uses to say it cannot do 76800 baud. The precedent to avoid is the
blank-tape trap in `TODO.md`: an operation that reports success and produces
nothing usable.

**`list_queues` earns a `SHOW`.** The `serial:` endpoint already names the ports
this host actually has when the open fails, because "cannot open the printer"
with no further help is ten minutes of someone doubting the simulator. A bad
queue name should print the queues that exist. It is the same courtesy and the
same three lines.

**The transforms are the console's, and a printer is not a console.** `upper`,
`strip7out` and `crlf` are console properties; a line to a printer is 8-bit
clean, because ESC/P and every other printer control language needs the high
bit. Whether a printer stream wants CR/LF translation of its own is a separate
question and should be answered separately, not by reaching for the console's
knobs.

**Testing it means not needing a printer.** No CI runner has one. The testable
half is the buffering and the boundary policy — feed bytes, advance host
seconds, assert one job of the right bytes came out — which works against a fake
submit sink and needs neither CUPS nor paper. Set the clock explicitly, the way
the VDM-1 blink test sets `hostSeconds` rather than sleeping. The platform
submit path stays untested by machine, and that is honest and worth saying out
loud rather than papering over.
