//
// The CUPS backend for `printer:` (docs/printing.md 3.3). macOS AND Linux -- both
// sit on CUPS, so one file serves both and CMake links it whenever the CUPS headers
// are found (the SDL3 rule: optional, detected, never required).
//
// It forces every job through as RAW: the queue is expected to have been created raw
// (`lpadmin -m raw`), and the options below tell CUPS to skip its filter chain even
// if it was not. The bytes the guest sent reach the printer untouched, which is the
// whole point -- a printer control language is not text and must not be reflowed.
//
// This is the ONE place in the printer path allowed to know it is on a CUPS host;
// the interface (platform/printer.h) and the stream (host/printer_stream.h) know
// nothing of it. That is why the header carries no cups_dest_t and this file carries
// no board.

#include "platform/printer.h"

#include <cups/cups.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace altair::platform {
namespace {

// A private temp file for the job, in TMPDIR (or /tmp), created by mkstemp so the
// name is not predictable. CUPS is naturally file-based, so a job that arrives as
// bytes is spooled here and handed to cupsPrintFile2 by path.
bool spoolTemp(const std::vector<uint8_t>& data, std::string& path, std::string& err) {
    const char* d   = std::getenv("TMPDIR");
    std::string dir = (d && *d) ? d : "/tmp";
    if (dir.back() != '/') dir += '/';
    std::string tmpl = dir + "altairsim-print-XXXXXX";

    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = ::mkstemp(buf.data());
    if (fd < 0) {
        err = "could not create a temporary file for the print job";
        return false;
    }

    size_t off = 0;
    while (off < data.size()) {
        ssize_t w = ::write(fd, data.data() + off, data.size() - off);
        if (w <= 0) {
            ::close(fd);
            ::unlink(buf.data());
            err = "could not write the print job to its temporary file";
            return false;
        }
        off += (size_t)w;
    }
    ::close(fd);
    path.assign(buf.data());
    return true;
}

} // namespace

bool printRawFile(const std::string& queue, const std::string& path, std::string& err) {
    cups_option_t* options     = nullptr;
    int            numOptions  = 0;
    // Both keys, belt and braces: `raw` is the old spelling, `document-format`
    // application/vnd.cups-raw is the current one. Together they mean "do not filter
    // this, hand it to the device as-is" on every CUPS version we might meet.
    numOptions = cupsAddOption("raw", "true", numOptions, &options);
    numOptions = cupsAddOption("document-format", "application/vnd.cups-raw", numOptions, &options);

    int job = cupsPrintFile2(CUPS_HTTP_DEFAULT, queue.c_str(), path.c_str(), "altairsim",
                             numOptions, options);
    cupsFreeOptions(numOptions, options);

    if (job == 0) {
        err = "print to '" + queue + "' failed: " + cupsLastErrorString();
        return false;
    }
    return true;
}

bool printRaw(const std::string& queue, const std::vector<uint8_t>& data, std::string& err) {
    std::string path;
    if (!spoolTemp(data, path, err)) return false;
    bool ok = printRawFile(queue, path, err);
    ::unlink(path.c_str());
    return ok;
}

std::vector<std::string> listQueues() {
    std::vector<std::string> out;
    cups_dest_t*             dests = nullptr;
    int                      n     = cupsGetDests(&dests);
    for (int i = 0; i < n; ++i)
        if (dests[i].name) out.emplace_back(dests[i].name);
    cupsFreeDests(n, dests);
    return out;
}

} // namespace altair::platform
