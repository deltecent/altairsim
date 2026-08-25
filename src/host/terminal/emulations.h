#pragma once
//
// The built-in terminal emulations, as a name->factory table (issue #244).
//
// The one place that knows which dialects exist, mirroring serialBuiltins(): a row per
// emulation the `terminal:` endpoint can speak, keyed by the name the operator types
// (`emulation=vt100`). Adding a terminal is adding a row here plus its TerminalEmulator
// subclass -- the endpoint, the grammar and the help all read this table, so nothing
// else changes. Names match case-insensitively, the same as a board id or a property.

#include <memory>
#include <string>
#include <vector>

namespace altair {

class TerminalEmulator;

struct TerminalEmulation {
    const char* name;   // the operator's spelling (emulation=<name>)
    const char* blurb;  // one line, for SHOW / help
    std::unique_ptr<TerminalEmulator> (*make)();
};

// The table, in listing order. The first row is the default when no emulation is named.
const std::vector<TerminalEmulation>& terminalEmulations();

// A fresh emulator for `name` (case-insensitive), or null if there is no such emulation.
std::unique_ptr<TerminalEmulator> makeTerminalEmulator(const std::string& name);

// The names, comma-joined -- for an error message ("emulation is one of ...") and help.
std::string terminalEmulationNames();

} // namespace altair
