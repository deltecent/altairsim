#pragma once

// ---------------------------------------------------------------------------
// RUNTIME DIAGNOSTIC LOG (DESIGN.md 7.2, "debug").
//
// A named-channel diagnostic facility. A "channel" is one debug source -- a board
// (named by its id) or an internal library (the 6850, the socket layer) -- and it
// carries a small set of named FLAGS the operator turns on and off independently:
//
//     SET mds0 DEBUG=sector,seek     enable two flags on the mds0 channel
//     SET mds0 NODEBUG=seek          turn one back off
//     SET CONSOLE DEBUG=stderr       point the one global sink somewhere
//     SHOW DEBUG                     list channels, flags, and what is on
//
// A channel caches its enabled flags as a bitmask, so the hot path a device runs on
// every cycle is one AND (`ch.on(SEEK)`) -- a disabled channel costs nothing and no
// string is formatted until a line is actually emitted:
//
//     if (ch.on(SEEK)) dbg::line(ch) << "seek track=" << t << "\n";
//
// This is a DIAGNOSTIC, not machine config: the sink and the enabled flags do not
// round-trip through CONFIG SAVE (like `log` in [console], they are the operator's session).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace altair::dbg {

// Where an emitted line goes. One global sink for the whole session. Stderr is the
// default because Stdout collides with the guest console during CONSOLE mode -- a
// debug line in the middle of the program's own output. File appends.
enum class Sink { Stderr, Stdout, File };

// Point the sink. For File, `path` is opened for append; on open failure `err` is set
// and the prior sink is kept (returns false). Stderr/Stdout ignore `path` and never
// fail. Re-setting the sink closes any file the previous setting opened.
bool setSink(Sink s, const std::string& path, std::string& err);

// The current sink as the operator would name it: "stderr", "stdout", or the path.
std::string sinkName();

// The stream a line is written to, resolved from the current sink.
std::ostream& out();

// The PC-prefix provider. Returns the address of the instruction that is "current"
// right now, or nullopt when there is nothing meaningful (machine stopped, emit at
// the prompt). Read LAZILY by line(), only when a line is actually emitted, so it
// costs a disabled channel nothing and the run loop need not push a value per cycle.
void setPcProvider(std::function<std::optional<uint16_t>()> p);

// THE TERMINAL, when the sink is stderr or stdout. While a guest runs, the console is in
// raw mode, and raw mode turns off the terminal's own "\n" -> "\r\n": a report line
// ending in a bare "\n" leaves the cursor where the line ended, and the next line starts
// in that column. And the guest may be half-way through a line of its own when a report
// comes. So while `raw()` is true, a line first starts a fresh line if `midLine()` says
// the screen is not at column 0, every "\n" goes out as "\r\n", and `lineDone()` is told
// that the screen is at column 0 again. A file sink is never touched.
struct Terminal {
    std::function<bool()> raw;       // is the console in raw mode now?
    std::function<bool()> midLine;   // flush the guest's output; is the cursor past column 0?
    std::function<void()> lineDone;  // a report line ended: the cursor is at column 0
};
void setTerminal(Terminal t);

// ---------------------------------------------------------------------------
// A CHANNEL -- one debug source with a fixed set of named flags.
//
// Constructed with its name and flag names (order fixes the bit index: flags[0] is
// bit 0). Self-registers in the process-global registry on construction and removes
// itself on destruction. A board owns one named by its id; a library owns a static
// one. Flag count is capped at 32 (the mask is a uint32_t) -- far more than any real
// source needs.
// ---------------------------------------------------------------------------
class Channel {
public:
    Channel(std::string name, std::vector<std::string> flags);
    ~Channel();

    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    // The hot path: is flag `bit` enabled? One AND on a cached mask.
    bool on(unsigned bit) const { return (mask_ & (1u << bit)) != 0; }

    // Apply a comma-separated flag list. `all` turns every flag on; `none` turns
    // every flag off; a named flag adds itself (DEBUG is additive -- NODEBUG removes).
    // An unknown flag sets `err` and changes nothing (atomic). Tokens are folded case.
    bool enable(const std::string& csv, std::string& err);

    // Remove a comma-separated flag list. `all` turns every flag off; `none` is a
    // no-op; a named flag clears itself. Unknown flag → `err`, no change.
    bool disable(const std::string& csv, std::string& err);

    const std::string&              name()  const { return name_; }
    const std::vector<std::string>& flags() const { return flags_; }
    uint32_t                        mask()  const { return mask_; }

private:
    std::string              name_;
    std::vector<std::string> flags_;
    uint32_t                 mask_ = 0;
};

// Every registered channel, in registration order -- the targets SHOW DEBUG lists and
// tab-completion offers.
std::vector<Channel*> channels();

// Look a channel up by name, case-insensitively. Null if there is none.
Channel* find(const std::string& name);

// Begin a diagnostic line: write the "<PC>  <name>: " prefix and return the sink so
// the caller streams the rest. PC is four uppercase hex digits, or "----" when the
// provider has nothing. The caller supplies the trailing newline.
std::ostream& line(const Channel& ch);

} // namespace altair::dbg
