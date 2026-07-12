#pragma once
//
// Endpoint resolution -- turning `console` or `socket:2323` into a ByteStream.
//
// THE DIVISION OF LABOR (DESIGN.md 7.7): the MONITOR opens the endpoint; the
// BOARD decides what the bytes mean. `CONNECT` and `MOUNT` are generic commands,
// not per-board ones, which is why a serial card written next year gets both for
// free without one line changing in the monitor.
//
// This is the seam. It is the only place in the program that knows the endpoint
// GRAMMAR, and no board is permitted to know it at all.

#include "host/stream.h"

#include <memory>
#include <string>

namespace altair {

// Returns null and sets `err` on anything it does not understand. It never
// guesses: `CONNECT sio:a consle` is an error with the list of what it could
// have meant, not a silent NullStream that leaves you wondering why the terminal
// is dead.
std::unique_ptr<ByteStream> resolveEndpoint(const std::string& spec, std::string& err);

// The endpoint grammar, for help text and tab completion -- one list, so the
// help and the parser cannot drift.
std::string endpointHelp();

} // namespace altair
