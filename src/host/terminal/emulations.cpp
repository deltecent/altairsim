#include "host/terminal/emulations.h"

#include "host/terminal/emulator.h"
#include "host/terminal/vt100.h"

#include <cctype>

namespace altair {
namespace {

std::unique_ptr<TerminalEmulator> makeVt100() { return std::make_unique<Vt100Emulator>(); }

bool ieq(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return i == a.size() && b[i] == '\0';
}

} // namespace

const std::vector<TerminalEmulation>& terminalEmulations() {
    // vt100 first -- it is the default when a `terminal:` names no emulation. `ansi` is the
    // same engine under the name a lot of period software calls it. Task 3 adds adm3a,
    // vt52 and h19 rows here, each with its own TerminalEmulator subclass.
    static const std::vector<TerminalEmulation> table = {
        {"vt100", "DEC VT100 / ANSI", &makeVt100},
        {"ansi",  "DEC VT100 / ANSI (alias for vt100)", &makeVt100},
    };
    return table;
}

std::unique_ptr<TerminalEmulator> makeTerminalEmulator(const std::string& name) {
    for (const auto& e : terminalEmulations())
        if (ieq(name, e.name)) return e.make();
    return nullptr;
}

std::string terminalEmulationNames() {
    std::string s;
    for (const auto& e : terminalEmulations()) {
        if (!s.empty()) s += ", ";
        s += e.name;
    }
    return s;
}

} // namespace altair
