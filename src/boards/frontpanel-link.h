#pragma once
//
// The front-panel wire codec -- PURE FRAME FORMAT, no socket, no board state, no SDL.
//
// The contract is altairsim-fp/docs/panel-protocol.md (version 1), the authoritative
// cross-repo spec; this is the altairsim (CLIENT) side of it. altairsim dials out to
// the bridge and streams `L` lamp frames. The link is OUTPUT ONLY: this codec can still
// PARSE the bridge's `W`/`S` switch frames (forward-compat, so an old bridge that sends
// them does not desync the client), but the board no longer ACTS on them -- a graphical
// panel is a view and never moves a switch on the machine (altairsim-fp #7). Kept as
// free functions in their own namespace so the test can exercise every frame without
// opening a socket -- the same headless-core discipline the bridge keeps on its side.
//
// THE STATUS BYTE CROSSES VERBATIM. The bus already latches the real 8080 status word
// (Status8080 in core/bus.h), WO* active low; encodeL() formats it with no remap and
// no inversion and no translation table. That is altairsim-fp's rule -- "fix the
// emitter, not the pipe" -- and here altairsim IS the emitter.

#include <cstdint>
#include <string>
#include <string_view>

namespace altair::fplink {

// The highest protocol version this client speaks. Each side sends its own; both
// adopt min(local, remote). See parseLine() / PanelMsg::Hello.
inline constexpr int kProtocolVersion = 1;

// The machine-control `flags` byte (spec §3). These are driven from the monitor's
// operator-level state (Machine::setRunning / setHalted), NOT the 8080 status word --
// WAIT/HLTA/HLDA/INTE/PROT are panel pins, not bus signals. The bits match altairsim-fp's
// LampFlags (protocol.h), which the bridge already renders: HLTA (bit 1) composes the
// panel's D3 HLTA LED, WAIT (bit 2) the WAIT indicator. The rest of the group (RUN/HLDA/
// ...) is still passed as 0.
inline constexpr uint8_t FlHalted = 0x02;  // FlagHalted = 1u << 1
inline constexpr uint8_t FlWait   = 0x04;  // FlagWait   = 1u << 2

// ---- Outbound: client -> bridge --------------------------------------------

// "L <addr:04x> <data:02x> <status:02x> <flags:02x>\n" -- lowercase hex, zero-padded,
// fixed width. `status` is the 8080 status word verbatim (WO* active low); `flags` is
// the bridge-composed run/halt/wait group -- of which altairsim currently drives only
// WAIT (FlWait, bit 2); the remaining §3 machine-control indicators are passed as 0.
std::string encodeL(uint16_t addr, uint8_t data, uint8_t status, uint8_t flags);

// "HELLO altairsim-fp <ver>\n" -- sent once on connect.
std::string encodeHello(int ver = kProtocolVersion);

// ---- Inbound: bridge -> client ---------------------------------------------

// One parsed inbound frame. An unrecognised type or malformed line yields
// Kind::None, which the caller IGNORES (forward compatibility, spec §"Forward
// compatibility") -- a newer bridge degrades gracefully instead of desyncing us.
struct PanelMsg {
    enum class Kind { None, Hello, Switches, Sense };
    Kind     kind  = Kind::None;
    // Hello: the remote's advertised version. Switches: the full 16-bit switch word
    // (high byte = sense switches). Sense: the 8-bit sense byte alone.
    uint16_t value = 0;
};

// Parse exactly one line (one frame). No trailing '\n' required; a leading/trailing
// '\r' and surrounding spaces are tolerated, and trailing fields beyond those a type
// defines are ignored -- all per the spec.
PanelMsg parseLine(std::string_view line);

} // namespace altair::fplink
