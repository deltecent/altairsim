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

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// Rebase the host PATHs a spec carries, using a board-supplied resolver (the board
// is the only thing that knows its config dir). Grammar lives HERE, not in the
// board: only `in:PATH` / `out:PATH` name a path, and each is rewritten in place --
// `in:tape.tap?cps=300` keeps its options, `in:a,out:b` rebases both parts, and
// console/socket/serial/null pass through untouched. A board does not call this to open
// a line: it calls openEndpoint() below, which also keeps what the operator typed.
std::string rebaseEndpointPaths(const std::string&                              spec,
                                const std::function<std::string(const std::string&)>& rebase);

// Open an endpoint for a board: rebase its PATHs with `rebase` (the board's resolvePath;
// null leaves them as typed), open the result with `open` (the board's injected resolver,
// g_resolver; null is an error), and have the stream's
// describe() give back the spec AS TYPED -- `out:o.txt`, not the `out:sub/o.txt` it opened.
// SHOW and CONFIG SAVE print describe(), so a saved machine file keeps the relative path and
// a reload rebases it once (issue #953). Null with `err` set on failure; `paths`, when
// given, receives each PATH the spec named, so the board can add its pathNote() to `err`.
std::unique_ptr<ByteStream> openEndpoint(
    const std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>& open,
    const std::string& ep, const std::function<std::string(const std::string&)>& rebase,
    std::string& err, std::vector<std::string>* paths = nullptr);

// A resolver that runs every spec through rebaseEndpointPaths() before opening it, so a
// machine-file relative in:/out: PATH is config-relative. Hand this to a serial chip's
// `properties(resolve)` -- the chip's `connect` property setter is the OTHER way a
// machine file connects a line (the first being the board's connect()), and without it a
// `[board.unit.a] connect = "in:tape.tap"` on a 6850/8251 card would resolve the file
// against the shell's cwd instead of the machine file's dir. Identity when `rebase` is
// null (or resolves to itself, e.g. configDir empty -> a typed path stays shell-relative).
std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)> rebasingResolver(
    std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)> base,
    std::function<std::string(const std::string&)>                              rebase);

// Grammar primitives (DESIGN.md 7.7), exported so a board's property setter can
// VALIDATE an operator's `PORT` or `HOST:PORT` string without re-learning the
// grammar -- the PMMI's `dial`/`answer` settings call these. A board must not parse
// endpoint strings itself; this keeps the grammar in the one place that owns it.
// parsePort: "2323" -> 2323 (false on empty, non-numeric, 0, or > 65535).
// parseHostPort: "bbs.example:23" -> host + port (false + err on a missing/empty
// half or a bad port); the split is the same rfind(':') socket:HOST:PORT uses.
bool parsePort(const std::string& s, uint16_t& out);
bool parseHostPort(const std::string& spec, std::string& host, uint16_t& port, std::string& err);

// The spec with its grammar words in lower case: the scheme (`SOCKET:2323`, `Loopback`,
// `IN:tape.tap`) and the `?key=value` options (`?FMT=Text`). A path, a host, a device and a
// pty link keep their case -- they are the host's, not ours. resolveEndpoint() and
// rebaseEndpointPaths() fold through this first, so every endpoint word is case-insensitive.
std::string canonicalEndpoint(const std::string& spec);

// True when `path` names a mirror sink (`socket:PORT`, `pty`, `pty:LINK`, `serial:DEVICE`),
// in any case -- what may follow a `|` other than a log file. The `--mirror` option asks it.
bool isMirrorSink(const std::string& path);

// Returns null and sets `err` on anything it does not understand. It never
// guesses: `CONNECT sio:a consle` is an error with the list of what it could
// have meant, not a silent NullStream that leaves you wondering why the terminal
// is dead.
std::unique_ptr<ByteStream> resolveEndpoint(const std::string& spec, std::string& err);

// The endpoint grammar, for help text and tab completion -- one list, so the
// help and the parser cannot drift. `printer:` appears only where the build found
// a host print system; pass all=true to force the full list regardless, which the
// docs generator does so the committed manual is one platform-independent document.
std::string endpointHelp(bool all = false);

} // namespace altair
