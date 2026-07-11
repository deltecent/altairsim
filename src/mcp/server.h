#pragma once
//
// MCP server (DESIGN.md 11).
//
// MCP IS A FIRST-CLASS INTERFACE, NOT A WRAPPER. It does not shell out to the
// monitor and it does not screen-scrape: it sits on the same Machine and the
// same Board::properties() the CLI does, so the two cannot drift, and every
// result is structured JSON rather than an ASCII table someone has to re-parse.
//
// The clearest proof of that is board_set: its input schema is GENERATED from
// properties() at request time. A board added next year is fully agent-drivable
// -- with correct enums, ranges and runtime-settability -- the day it lands,
// and not one line of this file changes.

#include "core/machine.h"

#include <iosfwd>

namespace altair {

// stdio JSON-RPC 2.0, line-delimited.
int runMcp(Machine& m, std::istream& in, std::ostream& out);

} // namespace altair
