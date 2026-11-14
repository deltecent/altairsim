#pragma once
//
// The monitor (DESIGN.md 10). SIMH/AltairZ80-flavored: stable and greppable.
//
// In milestone 1a there is no CPU, so THE MONITOR IS THE BUS MASTER. Every
// DUMP, DEPOSIT and LOAD runs a real bus cycle through the real decode -- which
// means the bus design is under test from the first keystroke, with no
// processor in the way to blame.

#include "core/machine.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace altair {

class Display;      // host/display.h -- the run loop only asks it a question
class Joystick;     // host/joystick.h -- the host game controllers, for SHOW JOYSTICKS
class LineEditor;   // cli/lineedit.h -- an interactive command reads follow-up lines through it
struct Completions; // cli/lineedit.h -- what complete() hands the editor for Tab

// EXACTLY ONE UNIT MAY HOLD THE CONSOLE (DESIGN.md 7.2, 9). `holds` says whether a serial
// unit holds it now. The CLI's answer is wiredToConsole(); under --mcp the holder shows
// `scripted`, so the server adds "its line is the session's console line" (mcp/server.cpp).
using ConsoleHolds = std::function<bool(Board& b, const std::string& unit)>;
bool wiredToConsole(Board& b, const std::string& unit);

// Take the console for keep:keepUnit: disconnect every other serial unit `holds` names, and
// return one "console taken from <id>:<unit>" line for each. The one routine for the rule --
// CONNECT ... console calls it, and so does the --mcp console binding (issue #779).
std::vector<std::string> takeConsole(Machine& m, const Board* keep, const std::string& keepUnit,
                                     const ConsoleHolds& holds);

class Monitor {
public:
    explicit Monitor(Machine& m) : m_(m) {}

    // Run one command. Returns false when the monitor should exit.
    bool exec(const std::string& line, std::ostream& out);

    // Read-eval-print. `echo` prints each command first, for -c scripts.
    int repl(std::istream& in, std::ostream& out, bool interactive);

    // Run a `-s` script: repl() over the file's lines, with the file's own directory as
    // the base for every path in them -- exactly as DO roots a DO file's lines (#575).
    // `file` is where the caller opened it; it names the directory and guards against a
    // script that DOes itself.
    int runScript(std::istream& in, const std::string& file, std::ostream& out);

    // Tab completion (DESIGN.md 10.4). Given the command line up to the cursor, return the
    // candidates for the word being typed -- command names, then a board id, then that
    // board's property names, then a property's legal values -- all off the same
    // reflection SET reads, so there is no table to keep in step. READS ONLY: it must
    // never emit an error or trip `failed_`, so it resolves boards with a quiet local
    // scan rather than board(). The editor owns none of this grammar (cli/lineedit.h).
    Completions complete(const std::string& lineToCursor);

    bool failed() const { return failed_; }
    int exitCode() const { return failed_ ? 1 : 0; }

    // MCP mode: the server is single-threaded, so a command that enters the unbounded run
    // loop would hang the whole connection. When set, RUN sets PC and returns instead of
    // blocking -- the client advances with the non-blocking `run` tool (mcp/server.h).
    void setMcpMode(bool b) { mcpMode_ = b; }

    // Who holds the console, for SHOW CONSOLE and CONNECT, and how CONNECT ... console takes
    // it. The defaults are wiredToConsole() and takeConsole() with it. --mcp sets both, to
    // find the unit on the session's scripted line too, so SHOW CONSOLE names it and CONNECT
    // takes it during the command (issue #779).
    using ConsoleTaker = std::function<std::vector<std::string>(Board* b, const std::string& unit)>;
    void setConsoleHolds(ConsoleHolds h) { consoleHolds_ = std::move(h); }
    void setConsoleTaker(ConsoleTaker t) { consoleTaker_ = std::move(t); }

    // Run a machine's startup list (DESIGN.md 10.0). Anything you can type, a
    // config can do -- so `startup` is not a second language.
    void runStartup(std::ostream& out);

    // Run the operator's own script (`~/altairsim.ini`) from `path`, as if typed, echoing
    // each line behind `ini> `. A file that is not there is silent -- it is optional. A bad
    // line is reported by the command itself and the rest still run, and none of it
    // changes the exit status: the file is the operator's, not part of any machine or
    // test. The caller decides WHEN (main.cpp: a plain interactive launch only).
    void runUserInit(const std::string& path, std::ostream& out);

    // THE WINDOW, so the run loop can be told it was closed (host/display.h).
    //
    // Injected by the composition root, like every other host service the boards get
    // -- and deliberately NOT hung on Machine, which stays free of hosts. The run
    // loop is the only thing that can stop a guest, so it is the only thing that can
    // act on a close box; the video board that owns the drawing cannot and must not.
    //
    // Static because the display is: one window, one session, outliving every
    // Machine (src/main.cpp). Null everywhere it is not set -- a headless build, a
    // test, an -c script -- and the run loop then never asks.
    static void setDisplay(Display* d);
    static void setJoystick(Joystick* j);

private:
    // ---- NUMBER BASE (Patrick, 2026-07-11; was open finding F3) ----
    //
    //     ON THE WIRE -> HEX or OCTAL.   NEVER ON THE WIRE -> DECIMAL.
    //
    // The base belongs to the OPERAND, not to the command line. `addr()` is for
    // the things the 8080 itself sees -- addresses, ports, data bytes -- and they
    // are hex, bare, as they are on the front panel and in every listing ever
    // printed. `count()` is for the things only the operator sees -- step counts,
    // dump widths, history depth, unit numbers -- and they are decimal, because
    // the machine never holds one of them.
    //
    // The one operator choice is HOW the wire class is spelled: hex (the default)
    // or OCTAL (split octal, the MITS front-panel and manual convention). That is
    // `[console] base` -- Console::base() -- and it moves ONLY the wire class:
    // addr()'s default parse base and the fmtByte()/fmtWord() display below. The
    // decimal class does not move, because octal-vs-hex was never its question.
    // This is not the "global base" that F3 ruled out: that was hex-vs-decimal
    // spanning both classes (baud can be neither), and this spans neither class.
    //
    // A PROPERTY carries its own `radix` in the reflection layer, which is the
    // same rule reaching the same answer: `SET sio2a port=10` is port 0x10 (a
    // wire), `SET sio2a baud=9600` is nine thousand six hundred (not a wire).
    //
    // `0x`/`$`/trailing-`h` force hex and `#` forces decimal, everywhere, in both
    // directions. A `K`/`M` suffix is always decimal and always wins.
    bool addr(const std::string& t, uint32_t& out, std::ostream& err);
    // addr(), but a loaded symbol's name resolves to its value first. ONLY for a
    // true address -- never a port or a byte, which addr() also parses (core/symbols.h).
    bool addrSym(const std::string& t, uint32_t& out, std::ostream& err);
    bool count(const std::string& t, uint32_t& out, std::ostream& err);
    bool range(const std::string& t, uint32_t& lo, uint32_t& hi, std::ostream& err);

    Board* board(const std::string& id, std::ostream& err);

    // WHAT THE COMMAND IS ABOUT TO DO WITH THE UNIT -- which is the only thing that
    // decides whether the unit's KIND is legal. It was a `bool wantMountable`, and
    // the bool had a third case hiding inside its `false`: SET is neither mounting
    // nor connecting, and it was being told a cassette is "not a serial port". Every
    // unit the 2SIO has is a serial one, so nothing noticed until a tape turned up.
    enum class UnitUse {
        Mount,    // MOUNT/UNMOUNT: media only -- a disk, a ROM, a tape
        Connect,  // CONNECT/DISCONNECT: a serial line only
        Any,      // SET/SHOW: a unit is a unit. Its properties are its own business.
    };

    // `id:unit` -> board + NAMED unit, with the kind checked against the command.
    bool subunit(const std::string& spec, Board*& b, UnitDef& u, UnitUse use,
                 std::ostream& err);

    // Every verb the cards in the machine declare right now, deduped by name, each
    // with the type of a card that brings it. Empty on a machine with no such card --
    // which is the point of the whole mechanism.
    //
    // BY VALUE, AND THAT IS NOT A STYLE CHOICE -- IT IS A SEGFAULT I ALREADY WROTE.
    // Board::commands() returns its table BY VALUE, so a `const CommandDef*` taken
    // into it dangles the moment the temporary dies, which is at the end of the very
    // loop that collected it. `REW` crashed the monitor. The struct is five
    // `const char*` pointing at string LITERALS: copying it is free, and the literals
    // outlive every board in the machine.
    std::vector<std::pair<std::string, CommandDef>> boardVerbs() const;

    // A VERB A CARD BROUGHT WITH IT (Board::commands()). Called ONLY after the
    // built-in table has failed to prefix-match, so the static menu always wins and
    // no card can move a built-in abbreviation by being plugged in.
    //
    // False means "nothing in the machine answers to this word" -- and the caller
    // then prints `unknown command`, which is the truth: with no 88-ACR in a slot
    // there IS no REWIND. True means it was handled, including handled badly.
    bool boardCommand(const std::vector<std::string>& a, std::ostream& out);

    // One line of disassembly, in a listing's shape: address, the raw bytes, the
    // instruction. Returns the instruction's length so the caller can walk on.
    //
    // It PEEKS. A disassembler that ran real bus cycles would consume a byte from
    // a UART sitting in the range you asked about -- so DISASM would silently eat
    // the console's input, and only when the memory map happened to be unlucky.
    uint8_t disasmLine(uint32_t addr, const class Disassembler& d, std::ostream& out);

    // Decode ONE instruction, no printing -- the status line wants the mnemonic
    // and nothing else. Peeks, for the same reason.
    struct Insn insnAt(uint32_t addr, const class Disassembler& d);

    // Rewrite a disassembled operand as a symbol when one is loaded: `CALL 0005`
    // becomes `CALL BDOS`. The decoder tells us the 16-bit operand's VALUE (in.operand,
    // set when in.operandBits==16), so we look the symbol up by value and swap it in
    // for the rendered operand -- which works whether that operand reads `0005` or
    // `000 005`. Returns the text unchanged when nothing is loaded, so a symbol-less
    // machine disassembles exactly as before.
    std::string annotateOperands(const struct Insn& in) const;

    // The active core, or null with a message already printed. Every CPU command
    // starts here, and none of them assume the machine has a processor -- because
    // a backplane without one is a machine you can build and the one 1a ran.
    CpuCore* needCpu(std::ostream& err);

    // A HALTED PROCESSOR FETCHES NOTHING (#770), so the front panel's RUN, EXAMINE and
    // DEPOSIT do nothing until RESET. True -- with the refusal printed and the command
    // failed -- when the panel command must not act. `finalOnly`: refuse only a halt that
    // nothing can end (a bare RUN, NEXT or STEP still waits on an EI/HLT for its
    // interrupt); otherwise refuse any halt (a command that moves the PC or writes).
    bool refuseHalted(CpuCore* cpu, std::ostream& out, bool finalOnly);
    // The processor's interrupts are off -- for the stop line of a final HLT.
    bool intsOff();
    // After RESET or RESET CPU: HLTA out, and the bus lamps no longer floating.
    void panelAfterReset();
    void showRegs(std::ostream& out);

    // The "C0Z0... A=00 BC=0000 ... PC=0000  <insn>" status line -- flags clustered, then
    // fields -- built from a register set and a source for each register's value. Shared by
    // the LIVE status line (showRegs) and the RECORDED one (renderInsn / CPU HISTORY), so the
    // two spellings of the same line can never drift. `valueAt(i)` is the i-th RegDef's value.
    // The layout (which line, what label, `=` vs `'`) is the CORE's, declared in registers();
    // this renderer knows no CPU. A core may wrap onto several lines (RegDef::line): `insn`,
    // the already-decoded instruction, is appended after the line that carries PC -- so the
    // eye still reads PC and its instruction together even when PC is not on the last line.
    std::string regLine(const std::vector<RegDef>& regs,
                        const std::function<uint32_t(size_t)>& valueAt,
                        const std::string& insn = "");
    // One recorded instruction as its DDT-style line -- exactly what STEP printed as it
    // ran. The register line comes from regLine over the record's stored values; the
    // mnemonic is disassembled from the record's stored bytes, not live memory.
    std::string renderInsn(const Debugger::InsnRec& rec);

    void showBoard(Board* b, std::ostream& out);
    void showBoards(std::ostream& out, const Machine& m);  // the backplane: BOARDS
    void showProps(const std::vector<Property>& ps, std::ostream& out);

    // A sub-unit table's KEYS -- no value column, because the thing they describe does
    // not exist yet. (Board::subUnitProperties.)
    void showSchema(const std::vector<Property>& ps, std::ostream& out);
    void showBus(const std::vector<std::string>& args, std::ostream& out);

    // SHOW BUS IRQ -- the eight VI lines, who is strapped to them, who is pulling
    // them, and who wins. `table` is false for the summary bare SHOW BUS prints.
    void showBusIrq(std::ostream& out, bool table);

    // RUN. The machine runs until a breakpoint, a HLT nothing can wake, or ATTN.
    // If a unit holds the console the guest owns the keyboard while it does; if
    // none does, there is nothing to hand over and it simply runs. That is not a
    // mode -- it is a fact about the backplane, and the machine already knows it.
    // `stepOver` is NEXT's quiet mode: no preamble banner, no instruction tally,
    // and a clean step-over completion (StopReason::StepTarget) is silent -- only a
    // real stop (ATTN, ^C, HLT, a user breakpoint in the callee) is reported.
    void runMachine(std::ostream& out, bool stepOver = false);
    void showConsole(std::ostream& out);
    // SET CONSOLE k=v, and CONSOLE k=v, which is the same command spelled short:
    // one key, DEBUG= included. False (and failed_) on a bad key or value.
    bool setConsoleKey(const std::string& k, const std::string& v, std::ostream& out);
    void showDisplay(std::ostream& out);
    void showTerminal(std::ostream& out);  // the [terminal] transform chain (issue #244)
    void showJoysticks(std::ostream& out);  // the host game controllers (SDL builds)
    void showSerial(std::ostream& out);     // the host serial ports, and who holds each
    void showDebug(std::ostream& out);  // the diagnostic channels, their flags, the sink
    void showRoms(std::ostream& out);
    void showMounts(std::ostream& out);  // every mountable unit, across every board
    void showPaths(std::ostream& out);   // what a path resolves against -- 3 answers
    void showVersion(std::ostream& out); // which build this is, and which commit
    void showClock(std::ostream& out);   // emulated time: T-states since POWER, in seconds
    void showSymbols(const std::vector<std::string>& args, std::ostream& out);
    void flush(std::ostream& out);  // print anything the bus or a board said

    Machine& m_;
    bool failed_ = false;
    bool quit_ = false;
    bool mcpMode_ = false;  // RUN parks instead of blocking -- see setMcpMode()
    ConsoleHolds consoleHolds_;  // empty = wiredToConsole
    ConsoleTaker consoleTaker_;  // empty = takeConsole() with consoleHolds_

    // WHOSE DIRECTORY THE FILE CURRENTLY RUNNING CAME OUT OF -- meaningful only while a
    // file's lines are running (fileDepth_ > 0): a `startup` list, a `-s`/DO script.
    // A path WRITTEN IN a file is relative to that file (core/paths.h), so for the length
    // of that list this is the file's directory. At the prompt it is not read at all --
    // inputBase() returns the MACHINE's directory there instead (see below).
    std::string startupDir_;

    // HOW MANY FILES ARE RUNNING RIGHT NOW -- 0 at the prompt, 1 inside a startup list or
    // a `-s`/DO script, more when they nest. It is the one bit that says "typed by a human"
    // (0) versus "written in a file" (>0), which decides two things: whether a leading `~`
    // is the shell's to expand (only a human's is), and which base inputBase() hands back.
    int fileDepth_ = 0;

    // THE ONE BASE every relative path resolves against. While a file runs it is that
    // file's directory (startupDir_); at the prompt it is the MACHINE's own directory
    // (m_.dir) -- so `MOUNT scratch.dsk` you type finds the scratch.dsk beside the machine
    // you loaded, the SAME folder the machine file's own `mount =` names. The two used to
    // differ (typed paths went to the shell's cwd); collapsing them to one directory is
    // the whole point. For a built-in, m_.dir is "" and that means the launch directory --
    // the only anchor a machine with no file of its own can have. hostdir is the lone
    // exception and is not a base (SHOW PATHS).
    std::string inputBase() const { return fileDepth_ > 0 ? startupDir_ : m_.dir; }

    // Resolve a path from the CURRENT source against inputBase(). A human's leading `~`
    // (fileDepth_ == 0) is expanded as the shell would; a file's `~` is left literal, as
    // it always was (a machine file with a `~` in it is not portable). Everything typed
    // that names a file -- MOUNT, LOAD, SAVE, DO, SYMBOLS, SNAPSHOT, CONFIG -- goes through
    // here, so they all root at one directory.
    std::string resolveInput(const std::string& p) const;

    // Run a list of command lines as if typed, with `dir` as the base for the relative
    // paths in them, echoing each behind `echoTag` (null for no echo). This is the ONE
    // batch engine: runStartup() feeds it the machine's `startup`, and the DO command
    // feeds it a file's lines. `startupDir_` and each board's config dir are saved and
    // restored around the run, so it NESTS -- a DO inside a startup, or a DO inside a DO,
    // each hands the caller's directory back when it returns.
    void runLines(const std::vector<std::string>& lines, const std::string& dir,
                  const char* echoTag, std::ostream& out);

    // The DO files open right now, innermost last, by canonical path. A `DO` that reaches
    // for itself -- directly or round a ring of files -- would otherwise recurse until the
    // stack gives out, and a depth cap alone cannot save it: each level nests a whole
    // exec() frame, and enough of those overflow a small stack (Windows' is 1 MB, an
    // eighth of macOS's) BEFORE any depth ceiling can fire. So the cycle is caught by
    // identity instead -- if a file is already in this stack, DO refuses at depth 1,
    // before it recurses. size() also serves as the depth for the backstop cap below.
    std::vector<std::string> doStack_;

    // The last command line the operator entered, so `.` can repeat it. A `.` is
    // never recorded here, so pressing it again re-runs the SAME line -- which is the
    // point: `.` `.` `.` walks a DISASM or STEPs the CPU. Shell escapes (`!...`) return
    // before this is set, so `.` repeats monitor commands only.
    std::string lastLine_;

    // Where a bare DUMP resumes. A range moves it; nothing else does.
    uint32_t dumpNext_ = 0;

    // Where a bare DISASM resumes -- its own cursor, for the same reason EXAMINE
    // has one: you disassemble forward through a routine while dumping the table
    // it points at, and neither should drag the other along.
    //
    // A STEP or a GO moves it to the PC, because after the machine stops the thing
    // you want to look at is what it is about to do next, every time.
    uint32_t disasmNext_ = 0;

    // EXAMINE HAS NO CURSOR HERE, AND MUST NOT GROW ONE. The panel has no address
    // latch of its own: it jams the switches into the PROGRAM COUNTER and lets the CPU
    // drive the address lines. The PC therefore IS the examine cursor, and EXAMINE NEXT
    // steps it (Patrick, 2026-07-12). A private copy would be a second counter
    // shadowing the real one, and the two would diverge the moment you STEP.
    //
    // There was one, for `EXAMINE RAW <id>` -- the burner's own offset, which needed no
    // CPU because it ran no cycle. Reading behind the bus is gone (§10.2: a ROM answers
    // reads like anything else), and the second cursor went with it.

    // THE MONITOR'S OWN INPUT, while a REPL is running -- so an interactive command
    // (EDIT) can read the follow-up lines it prompts for. Null on every non-interactive
    // path: a `startup` list, an MCP `command` call, before repl() and after it. A
    // command that needs them must check, and say so when they are absent rather than
    // dereference null. Set at the top of repl(), cleared on the way out.
    std::istream* in_ = nullptr;
    LineEditor*   ed_ = nullptr;
};

std::vector<std::string> tokenize(const std::string& line);

// ---- WHAT THE GUEST DID WITH ONE SLICE, and nothing else ----
//
// Four deltas. The run loop's idle judgement -- the thing that decides whether to stand
// down and stop pinning a host core at a prompt -- is a PURE FUNCTION of these, and it is
// written that way for one reason: the inline expression it used to be could not be tested,
// and it was WRONG in a way no test could have been written to catch. `received` counted
// bytes on the CONSOLE line only, so a transfer running on any OTHER line looked exactly
// like an idle prompt and got napped straight through (bug #6).
//
// A COUNTER IS THE WHOLE MACHINE'S OR IT IS A LIE. These deltas are the backplane's.
struct SliceWork {
    uint64_t steps    = 0;  // instructions retired
    uint64_t wrote    = 0;  // bytes the guest SAID -- on any line
    uint64_t received = 0;  // bytes that ARRIVED for it -- on any line
    uint64_t hungry   = 0;  // times it went looking for a byte and found none
};

// IS THE GUEST WAITING FOR A HUMAN, or is it working?
//
// It said nothing, it received nothing, and it came to a line and found it empty at least
// once every `ratio` instructions. A CP/M CONIN spin is three instructions and trips this
// twenty times over; a program that computes and checks for an abort key every few hundred
// never does. Receiving ANYTHING disqualifies it outright -- that is what tells a transfer
// from a prompt, and it is the only thing that does.
bool guestIsWaiting(const SliceWork& w, uint64_t ratio = 32);

// SHOW CLOCK's `pacing` row. A CRYSTAL IS ALWAYS KEPT (#731): a run is paced when the CPU
// card has one (`!free`) and flat out when it has not, from a terminal, a pipe, `-x`, `-s`
// and `--mcp` alike. So there are two rows and the crystal alone picks one. The run loop
// used to keep a crystal only for an observer (a terminal, a remote line, a speaker that
// played), and the row had a third answer for the script that had none; runMachine has
// the history.
std::string pacingRow(bool free);

// DOES A SERIAL UNIT'S WIRE END AT HOST DEVICE `device`? `state` is the unit's endpoint as it
// describes itself: `serial:/dev/cu.x`, with `?k=v` options after it, or `inner|sink` when the
// line is mirrored -- and either half of a mirror can be the port. SHOW SERIAL's IN USE BY
// column. The name has to end where the device does, so /dev/ttyS1 is not /dev/ttyS10.
bool unitHoldsDevice(const std::string& state, const std::string& device);

// MAY THE RUN LOOP NAP at an idle prompt this run?
//
// Only when the CPU card allows it (`idle`), only on an interactive console, and only when
// the throttle is NOT pacing. A paced run already sleeps for most of every slice, so a nap
// saves nothing there -- and a nap taken IN PLACE of the throttle leaves the slice before it
// unpaced: at 2 MHz a 2,000-instruction slice is about 8 ms of guest time and it got a 4 ms
// nap, so a guest at a prompt ran 1.3 times its crystal (#606).
bool mayNap(bool idle, bool anyConsole, bool tty, bool paced);

// THE ACHIEVED CRYSTAL: T-states per real second, published to the CPU card for SHOW
// (`achieved_hz`). Every loop that runs the guest keeps one -- the monitor's RUN, the MCP
// `run` tool and the MCP free run (#903: the two MCP loops kept none, and SHOW read 0).
//
// The caller passes its own T-state count, so a loop that counts its own (the free run, which
// a CONFIG LOAD between two slices can give a new machine clock) is not thrown off. The window
// counts ALL real time -- naps and pacing sleeps too: a machine that naps through a prompt
// really is retiring few T-states a second, and that is the honest reading. It is sampled long
// enough (kWindow) that the divide is not noise, short enough that SHOW reflects what the
// machine is doing now rather than an average over the whole run. A null card is a no-op.
class AchievedHzMeter {
public:
    using clk = std::chrono::steady_clock;
    static constexpr double kWindow = 0.25;  // seconds; ~4 updates/sec while running
    static constexpr double kFloor  = 0.02;  // shortest run worth a reading at all

    void start(uint64_t t, clk::time_point now) { t_ = t; w_ = now; }

    // Report and start the next window once this one has kWindow of real time behind it.
    // True when it did, so a caller can hang other once-a-window work on it.
    bool sample(CpuCard* card, uint64_t t, clk::time_point now);

    // The tail, when the run stops: a run shorter than one window (a CPU test, a GO to a
    // HLT) still leaves a reading. Below kFloor the divide is noise, so the last good
    // sample stands.
    void finish(CpuCard* card, uint64_t t, clk::time_point now);

private:
    uint64_t        t_ = 0;
    clk::time_point w_{};
};

// THE THROTTLE: how long a run loop sleeps so wall time keeps up with the crystal. The
// monitor's RUN and the MCP `run` tool use it. (The MCP free run paces between requests,
// not inside one call, and keeps its own count -- FreeRun in mcp/server.cpp.)
//
// The baseline is the T-state and the wall time at which the CURRENT rate began. A new rate
// starts a new baseline, because the T-states run at the old rate say nothing about the new
// one. The rate CAN change while a run is under way: SIMCLK asks the Host Bridge for it from
// inside the guest (#918). A baseline kept from the start of the run counted a whole flat-out
// build at the new 2 MHz, and the loop then slept for as long as that build "should" have
// taken at 2 MHz -- tens of seconds of a stopped guest.
//
// `hz` is 0 for flat out (Clock::free()), the crystal otherwise. The caller passes its own
// T-state count and wall time, so a test can drive it with no clock at all.
class CrystalPacer {
public:
    using clk = std::chrono::steady_clock;

    void start(uint64_t t, long long hz, clk::time_point now) { t_ = t; hz_ = hz; w_ = now; }

    // Seconds to sleep now. 0 when flat out, at a new rate (the new baseline starts here), or
    // when the guest is behind wall time.
    double sleepFor(uint64_t t, long long hz, clk::time_point now);

private:
    uint64_t        t_  = 0;
    long long       hz_ = 0;
    clk::time_point w_{};
};

} // namespace altair
