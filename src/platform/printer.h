#pragma once
//
// A real print queue on the host (DESIGN.md 2.1, 7.1; docs/printing.md 3).
//
// THE CONTRACT. Pure declarations, zero conditionals, no OS type anywhere in the
// signature -- no HANDLE, no cups_dest_t. Same rule as platform/serial.h: the
// moment an OS type appears in an interface, every caller needs an #ifdef to name
// it, and 2.1 is gone.
//
// One implementation file per family, chosen by CMake at configure time when the
// library is present (docs/printing.md 3.1):
//   src/platform/posix/printer_cups.cpp     -- CUPS (macOS + Linux), cupsPrintFile2
//   src/platform/win32/printer_windows.cpp  -- WinSpool, StartDocPrinter RAW
//
// A JOB, NOT A STREAM. A host print queue wants a finite blob handed over once --
// unlike a serial port, there is no byte-at-a-time write and no readback. The board
// side (host/printer_stream.h) decides where one job ends; this layer only submits
// a completed one. So the interface is free functions, not an object: there is no
// per-connection state to hold on this side of the queue.
//
// RAW / PASSTHROUGH, ALWAYS. The bytes a guest sent are the bytes the printer must
// see -- ESC/P and every printer control language needs the high bit and its own
// control codes untouched. Both backends force the queue into passthrough (CUPS
// `document-format application/vnd.cups-raw`, WinSpool datatype "RAW"), and the
// operator is expected to have created a raw queue (docs/printing.md 3.2).

#include <cstdint>
#include <string>
#include <vector>

namespace altair::platform {

// Submit `data` to the named host queue as one raw job. TRUE on success; on failure
// returns FALSE and sets `err` to a human sentence -- a queue that has gone away or
// a printer that is off is a fact the operator must be told, never silent data loss
// (docs/printing.md 4). May block briefly (it talks to a spooler/daemon), so it is
// called only off the emulation hot path -- on the pump()/teardown side, never from
// a board's write().
bool printRaw(const std::string& queue, const std::vector<uint8_t>& data, std::string& err);

// Same, from a file already on disk. The CUPS path is naturally file-based
// (cupsPrintFile2), so printRaw() spools a temp file and calls this; exposed in the
// interface so a caller that already has a file need not round-trip through memory.
bool printRawFile(const std::string& queue, const std::string& path, std::string& err);

// The queues this host has right now, for the "no such queue" error and for a SHOW.
// The same courtesy serial: pays with listSerialPorts(): "cannot reach the printer"
// with no further help is ten minutes of someone doubting the simulator. An empty
// list is a legitimate answer on a host with no printers configured.
std::vector<std::string> listQueues();

} // namespace altair::platform
