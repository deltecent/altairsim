#pragma once
//
// The MITS Altair 8800 front panel -- docs/boards/mits-frontpanel.md.
//
// THE PANEL IS A CARD, AND THE SENSE SWITCHES ARE NOT A CONFIG VALUE.
//
// They used to be: `Machine::sense`, a byte on the machine, parsed from TOML,
// printed by SHOW MACHINE -- and wired to NOTHING. No board decoded port 0xFF, so
// `IN 0FFH` fell through to the floating bus and returned 0xFF forever, whatever
// the operator had configured. The DBL boot PROM reads bit 4 of it. It was reading
// a floating bus.
//
// The fix is not to teach the bus about a machine-level byte. It is to put the
// switches where the schematic puts them: on the Display/Control board, which is a
// card in the machine like any other (DESIGN.md 3).
//
// ---------------------------------------------------------------------------
// THE SENSE SWITCHES ARE THE TOP EIGHT ADDRESS SWITCHES. THERE IS ONE ROW.
//
// Not "eight switches that happen to be next to the address switches" -- the SAME
// EIGHT SWITCHES, SA8..SA15, and the reason is visible on schematic 880-106. The
// D/C board already had a bank of 7405 open-collector buffers whose whole job was
// strobing SA8..SA15 onto D0..D7: that is the HIGH BYTE OF THE JMP that EXAMINE
// jams into the processor (Theory of Operation, "EXM" -- count 1 strobes 303, the
// JMP opcode; count 2 strobes SA0..SA7; count 3 strobes SA8..SA15).
//
// The sense-switch input is that same bank, enabled a second time. So the panel
// gets an input port for the cost of one gate, and the operator gets eight
// switches that mean two things depending on what the machine is doing. That is
// not a quirk to be modeled around. It is why `sense` is a SLICE of sw_ below and
// not a field of its own -- two fields would let them disagree, and on the real
// panel they physically cannot.
//
// ---------------------------------------------------------------------------
// THE DECODE, FROM THE SCHEMATIC (880-106) AND NOT FROM MEMORY.
//
// An 8-input NAND (IC L, 7430) fed by A8..A15, gated with sINP. All eight high and
// an input cycle in progress -> the SA8..SA15 buffers drive the data bus.
//
//   * It is on A8..A15, not A0..A7, and that is NOT a second port. The 8080 puts
//     the port byte on BOTH halves of the address bus during IN, so decoding the
//     high half IS decoding the port. `port() == 0xFF` is the same wire.
//
//   * A FULL EIGHT-BIT DECODE. Every address line is in the NAND; there is no
//     don't-care and no mask. Port 0xFF and nothing else.
//
//   * INPUT ONLY. The enable is gated with sINP. There is no sOUT anywhere near
//     this bank -- an `OUT 0FFH` is not ours, goes unclaimed, and the byte is
//     discarded by the backplane, which is exactly what the hardware does with it.
//
// The Theory of Operation says the rest out loud: SSW DSB (bus pin 53) "disables
// the data input buffers so the input from the sense switches may be strobed onto
// the bidirectional data bus right at the processor... This is necessary since the
// sense switch inputs are tied directly to the bidirectional data bus at the
// processor." We model the byte, not the pin: nothing else in this machine can
// observe SSW DSB, because nothing else is inside the 8080's own data bus buffers.
//
// ---------------------------------------------------------------------------
// THE LAMPS ARE WIRED TO THE BACKPLANE. That is not a metaphor, and it is why this
// card needs no new bus concept to have LEDs: snoop() already hands a board every
// cycle that crosses the bus, which is precisely what an LED on a bus line sees.
//
// THE STATUS WORD IS A BUS SIGNAL, AND THIS CARD FORWARDS IT -- it does not invent
// it. snoop() copies BusCycle::status (core/bus.h) into the latch, verbatim: the same
// 8080 status word the CPU latched at SYNC and the backplane carried. There is no
// switch on `c.type` here and no lookup table -- that logic used to live on this card
// and it was the wrong place for it. The bus is the emitter; the panel is a display;
// a graphical bridge renders the word to LEDs verbatim -- WO* included, drawn RAW off
// the active-low line (WO lit on read, dark on write), the way the real panel does it.
//
// M1 AND STACK ARE NOW LIT -- the CPU asserts them at the cycle origin (Status8080 in
// bus.h, cpu8080.cpp), and this card forwards c.status unchanged, so they arrived here
// the day the CPU started sending them, with no edit to this file. M1 lights on every
// opcode fetch; STACK lights on an 8080 push/pop (a Z80 has no STACK output, so it never
// does -- see cpuZ80.cpp). One status-word bit is still 0 at the source:
//
//   * HLTA -- halt acknowledge is a machine-control indicator, not something a memory
//     cycle carries; the bridge composes it from the CPU's halt flag, not from c.status.
//
//   * INTE, PROT, WAIT, HLDA -- NOT the status word (Operator's Manual §3, the
//     machine-control indicators). These are PINS the panel drives or watches, and
//     they belong to the wire `flags` byte, not `status`. Deferred entirely.
//
// AND THE MANUAL'S OWN DISCLAIMER, which is the best argument for not overfitting
// this: "While running a program, however, LEDs may appear to give erroneous
// indications." (Operator's Manual, INDICATOR LEDs.) The lamps show the last cycle
// that went by. At 2 MHz that is a blur, and it was a blur in 1975.

#include "core/board.h"
#include "boards/frontpanel-link.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// The old StatusLamp enum lived here -- a bespoke bit layout this card synthesized
// from c.type. It is gone: the status word is now a bus signal (Status8080 in
// bus.h), latched on BusCycle and forwarded verbatim by snoop(). See the header note.

class FrontPanelBoard : public Board {
public:
    // stream_ holds a NullStream from birth -- there is no null pointer in the stream
    // path, ever (host/stream.h). Both are out of line because ByteStream is only
    // forward-declared here (board.h), so a unique_ptr<ByteStream> member cannot be
    // constructed or destroyed until the .cpp sees the full type.
    FrontPanelBoard();
    ~FrontPanelBoard() override;

    std::string type() const override { return "fp"; }

    // Port 0xFF, read, and nothing else. See the decode note above.
    bool decodes(const BusCycle& c) const override {
        return enabled_ && c.type == Cycle::IoRead && c.port() == 0xFF;
    }

    // SA8..SA15 onto D0..D7.
    uint8_t read(const BusCycle&) override { return sense(); }

    // A READ WITH NO SIDE EFFECT IS A PEEK, and this one qualifies: eight switches
    // and a buffer. Nothing latches, nothing is consumed. (peek() is memory-only,
    // so this card never gets asked -- but the contract in board.h is about side
    // effects, and it is worth being able to say we honor it.)

    bool wantsSnoop() const override { return true; }
    void snoop(const BusCycle& c) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // A TOGGLE IS A TOGGLE. Nothing on a real panel moves a switch except a finger,
    // and neither RESET* nor POC* is a finger. Both resets are deliberately absent.

    // ...but the LAMPS go out. There is no light without power.
    void power() override;

    // THE WAIT LAMP. The operator started or stopped the machine; remember it so
    // pump() can drive the panel's WAIT indicator (lit when stopped, dark while a
    // RUN session turns). This is the operator-level run state fanned from
    // Machine::setRunning -- not the debugger's per-slice flag. See runningFlags().
    // Any RUN clears the halt latch: HLTA cannot outlive the halt it acknowledged.
    void setRunning(bool running) override {
        running_ = running;
        if (running) halted_ = false;
    }

    // THE HLTA LAMP. The CPU stopped on a HLT; remember it so pump() can light the panel's
    // halt-acknowledge indicator. Fanned from Machine::setHalted, cleared by any RUN above.
    void setHalted(bool halted) override { halted_ = halted; }

    // ---- THE GRAPHICAL PANEL BRIDGE -- one serial-style connector, "gui". --------
    //
    // The fp board is a CARD, and a graphical front panel is a VIEW onto it (DESIGN.md
    // 3, line 104). The view lives in another process -- altairsim-fp, a TCP server --
    // and this card dials OUT to it: `CONNECT fp0:gui socket:HOST:PORT`. The board
    // never learns what a socket is; the monitor resolves the endpoint to a ByteStream
    // and hands it down, exactly as it does for a serial card's tty (DESIGN.md 7.7).
    // No new monitor code and no CLI flag -- a declared unit is the whole seam.
    std::vector<UnitDef> units() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // THE ONE HOST TURN (DESIGN.md 7.1). Speaks the wire protocol to the bridge:
    // greets on connect, streams `L` status frames (throttled to fps, only when they
    // change and the socket can take them), and redials a line that dropped -- on a
    // capped backoff, since the bridge is opened and closed out of band. The link is
    // OUTPUT ONLY: it drains and parses the bridge's `W`/`S` switch frames to stay in
    // frame sync but IGNORES them -- a graphical panel is a view and never moves a
    // switch on this machine (altairsim-fp #7). No thread: one non-blocking pass per
    // emulated slice.
    void pump() override;

    // The bridge went away, or answered again -- said ONCE per edge, not per retry. The
    // base pulls the socket layer's own log (the `socket` debug channel); this adds the
    // board's. Both reach the operator through the monitor's post-command drain.
    std::vector<std::string> drainLog() override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not allowed to
    // know what a socket is (DESIGN.md 7.7). Injected once, in main().
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The connector, for an operator that owns the endpoint. NON-OWNING: this card owns
    // the stream (unlike a serial card, where the UART owns it). See Board::unitStream.
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "gui" ? stream_.get() : nullptr;
    }

    // The operator's raw CONNECT spec -- "" when nothing is plugged in. This is the
    // REDIAL TARGET pump() uses to reopen a bridge that dropped (Checkpoint ③); it is
    // deliberately NOT what SHOW/CONFIG SAVE render (that is the stream's canonical
    // describe(), via units()), because the two can differ and only describe() is a
    // round-trip fixed point.
    std::string endpoint() const { return endpoint_; }

    // ---- The switch row, and the lamps. The graphical panel reads THESE. ----

    uint16_t switches() const { return sw_; }
    void setSwitches(uint16_t v) { sw_ = v; }

    // A8..A15 -- what `IN 0FFH` returns.
    uint8_t sense() const { return (uint8_t)(sw_ >> 8); }
    void setSense(uint8_t v) { sw_ = (uint16_t)((sw_ & 0x00FF) | ((uint16_t)v << 8)); }

    uint16_t addressLamps() const { return addrLeds_; }
    uint8_t dataLamps() const { return dataLeds_; }

    // The 8080 status word off the last bus cycle (Status8080 in bus.h), forwarded
    // verbatim -- NOT lamp bits. The graphical bridge renders it to the status LEDs
    // raw, WO* included (active low: lit on read, dark on write -- no inversion on
    // either side). Named for what it is: a bus signal.
    uint8_t busStatus() const { return status_; }

    // SNAPSHOT/RESTORE (DESIGN.md 13). The switch row is machine-visible state -- the
    // guest reads the sense switches at port FF, and only a finger moves them, so
    // they survive resets and must travel. The lamp latches travel too; the next
    // snooped cycle would refresh them, but restoring them makes the panel look right
    // the instant the machine stops.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    // ONE ROW OF SIXTEEN ADDRESS SWITCHES, SA0..SA15 -- there is no separate data bank
    // on the panel. The low eight are the byte DEPOSIT writes; the high eight are also
    // the sense switches. Model the row once and they cannot drift apart, which is the
    // situation on the actual sheet metal.
    uint16_t sw_ = 0x0000;

    // The lamp latch. Written ONLY in snoop(), which is the clocked half of the
    // board interface (board.h) -- the combinational halves, decodes() and
    // assertsPhantom(), must stay pure and this is why the LEDs are not updated
    // from read().
    uint16_t addrLeds_ = 0;
    uint8_t  dataLeds_ = 0;
    uint8_t  status_   = 0;

    // The WAIT lamp, driven from the operator-level run state (setRunning), NOT the
    // 8080 status word. False = machine stopped = WAIT lit; true = a RUN session is
    // turning = WAIT dark. Transient SESSION state, deliberately NOT serialized: it
    // is re-established the moment the monitor next runs or stops, like the debugger's
    // host-side breakpoints (a fresh machine sits in WAIT -- power() sets false).
    bool running_ = false;

    // The HLTA lamp, driven from the operator-level halt state (setHalted), NOT the 8080
    // status word -- the emulator runs HLT atomically, so no snooped cycle carries HLTA.
    // True = the CPU stopped on a HLT = HLTA lit; cleared by any RUN (setRunning true).
    // Transient SESSION state like running_, deliberately NOT serialized.
    bool halted_ = false;

    // ---- The line to the graphical panel bridge. -------------------------------
    //
    // OWNED HERE, not by a chip -- this card has no UART; the panel IS the peripheral.
    // Never null: a card with nothing plugged in holds a NullStream, not a dangling
    // pointer (host/stream.h). endpoint_ is the spec CONNECT dialled, kept so pump()
    // can redial the bridge if it is opened/closed out of band (Checkpoint ③).
    std::unique_ptr<ByteStream> stream_;
    std::string                 endpoint_;

    // Panel refresh cap -- L frames per second on the wire. A board property, so
    // `SET fp0 FPS=<n>` retunes it live (the DESIGN-native seam, no CLI flag). The host
    // is the scarce resource; a flat-out guest must not become tens of thousands of
    // frames/sec. Consumed in pump()'s throttle gate.
    int fps_ = 256;

    // ---- pump() state: handshake, throttle, reconnect. --------------------------
    //
    // ALL WALL-CLOCK, NOT EMULATED (std::chrono::steady_clock). The panel is a VIEW; how
    // often it refreshes and how soon it redials are facts about the host and the human
    // watching, not about the guest -- so a flat-out guest and an idle one feed the bridge
    // at the same rate, and RECORD/REPLAY is unaffected because none of this is on the
    // clock the guest can measure.
    bool wasUp_ = false;  // carrier last slice -- to catch the connect/drop EDGE, not the level

    // The negotiated protocol version, min(ours, the bridge's). One version exists, so
    // nothing gates on it yet; stored because the handshake is where a v2 client learns
    // it is talking to a v1 bridge, and that belongs here, not rediscovered per frame.
    int wireVer_ = fplink::kProtocolVersion;

    std::string rxLine_;    // inbound bytes, accumulated until '\n' completes one frame
    std::string lastFrame_; // the last L we PUT on the wire -- the diff gate compares to this

    // The throttle and the backoff. lastSend_ paces L frames to fps_; nextDial_/backoff_
    // are the redial clock -- min on a fresh or just-dropped line, doubling toward a cap
    // while dials keep failing, reset to min the moment carrier rises. Set in the ctor.
    std::chrono::steady_clock::time_point lastSend_{};
    std::chrono::steady_clock::time_point nextDial_{};
    std::chrono::milliseconds             backoff_{};

    bool loggedDown_ = false;         // "cannot reach the bridge" -- said once, not per retry
    std::vector<std::string> log_;    // board-owned lines for drainLog() (connect/drop edges)
};

} // namespace altair
