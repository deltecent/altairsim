#pragma once
//
// PMMI MM-103 -- Modem and Communications Adapter (PMMI Communications, © 1982).
// See docs/boards/pmmi-mm103.md, and reference/PMMI MM-103 Modem and Comm Adapter.md
// for the recovered register map this board is built from.
//
// A Bell 103 originate/answer modem on one S-100 card: a Motorola MC6860L modem
// chip and a 1602-family UART (the manual never names the part -- see uart1602.h).
// THE MODEM IS NEVER A CONSOLE. Its one serial unit is the phone line, and CONNECT
// puts something on the far end of it (a file, later a socket).
//
// FOUR CONSECUTIVE PORTS, AND READ != WRITE AT EVERY ONE. The three control
// registers are WRITE-ONLY -- the port at the same address reads something else
// entirely (UART status, modem status, or nothing) -- so the board SHADOWS every
// value written. See read()/write().
//
// ---------------------------------------------------------------------------
// WHAT THIS MILESTONE MODELS, AND WHAT IT DELIBERATELY DOES NOT.
//
// This is the file-testable transmit/receive milestone. It implements the whole
// four-port decode, shadows every control write, drives the UART's data path onto
// a ByteStream, and honors the software-programmed frame (OUT BA+0) and baud
// divisor (OUT BA+2). It ALSO honors the 6860 Self Test (OUT BA+3 bit 4, ST): with
// the modem enabled (DTR), asserting ST loops the UART's line back on itself so a
// transmitted character returns on receive, exactly as the 6860's demodulator
// retuning to its own modulator does (updateSelfTest). It does NOT model:
//
//   - the modem handshake. IN BA+2 returns a FIXED "connected, clear-to-send,
//     off-hook" constant (see kModemStatusReady). No SH/RI/DTR timing, no billing
//     delay, no 6860 carrier handshake, no auto-answer. A later milestone replaces
//     the constant with the real state machine. In particular Self Test does NOT
//     flip the MODE status bit (IN BA+2 bit 6) -- the modem status stays the fixed
//     constant -- and the manual's "answer mode only / line disconnected" conditions
//     are documentation, not enforced.
//   - interrupts. The enable bit (OUT BA+0 bit 7) and the mask-staging (OUT BA+2 /
//     IN BA+3) are shadowed but inert; the board never drives pin 73.
//   - the pulse dialer. SH is a shadowed relay bit and nothing decodes pulses --
//     placing a call is CONNECT's job (docs/boards/pmmi-mm103.md).
// ---------------------------------------------------------------------------

#include "chips/uart1602.h"  // the 1602-family UART -- A CHIP IS NOT A CARD (DESIGN.md 7.8)
#include "core/board.h"
#include "core/clock.h"      // Clock::Handle -- the handshake state machine runs on deadlines

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class ModemLine;  // host/modemline.h -- the phone line this board's registers drive (Phase 2)

class PmmiBoard : public Board {
public:
    PmmiBoard();
    ~PmmiBoard() override;  // cancels the outstanding handshake/ring deadline (2SIO pattern)

    std::string type() const override { return "pmmi"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void power() override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13): the UART's state plus the three shadowed
    // write-only control registers. The straps (port) are config; the stream is a
    // host handle. Neither travels.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // One serial line -- a transfer arriving on it is traffic. (Board::rxBytes.)
    uint64_t rxBytes() const override { return u_.rxBytes(); }

    // What the far end said when the card tried to program its frame into a real port.
    std::vector<std::string> drainLog() override { return u_.drainLog(); }

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // A jumper moved (base address) or the line was reconfigured (dial/answer): re-aim
    // the handshake timer, exactly as the 2SIO re-arms its character deadline.
    void configChanged() override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not allowed
    // to know what a socket is (DESIGN.md 7.7).
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The connector, for an operator that owns the endpoint (the MCP console).
    // Non-owning; the UART owns the stream. During self-test the phone line is pocketed
    // in savedLine_, so hand THAT back -- an operator's view of its own line does not
    // change because the guest ran a diagnostic (see lineStream()).
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "line" ? &lineStream() : nullptr;
    }

private:
    // PLUG A LINE INTO THE CONNECTOR. The chip owns the stream; this is the card's
    // connector handing it down.
    void attachStream(std::unique_ptr<ByteStream> s);

    // ---- SELF TEST (OUT BA+3 bit 4, ST). The 6860's loopback lives HERE, on the
    // card, because it is the modem's demodulator retuning -- the UART knows nothing
    // of it. Engaged iff the shadow says so: DTR set (modem enabled) and ST asserted
    // (active low). While engaged the real phone line is pocketed in savedLine_ and a
    // LoopbackStream (host/stream.h) is on the UART's pins. ----
    void updateSelfTest();  // reconcile the loopback with out3_; call after out3_ moves
    bool selfTestEngaged() const { return savedLine_ != nullptr; }

    // The REAL phone line, whichever slot it is in: pocketed while looped, on the UART
    // otherwise. Reporting (SHOW, CONFIG SAVE, unitStream) goes through this so a
    // transient self-test never leaks the internal "loopback" endpoint out.
    ByteStream& lineStream() { return savedLine_ ? *savedLine_ : u_.stream(); }

    // The two status ports, assembled from chip pins and the shadowed registers.
    uint8_t uartStatus() const;   // IN BA+0
    uint8_t modemStatus() const;  // IN BA+2 -- computed from the phone line + handshake clock

    // ---- THE PHONE LINE, AND THE MODEM AS ITS POLICY (Phase 2). ---------------------
    //
    // dial=/answer= build a ModemLine (host/modemline.h) and install it as the line;
    // the guest's SH/RI/DTR writes then drive its dial()/armAnswer()/answer()/hangup(),
    // and IN BA+2 is computed from its ringing/carrier levels plus a Clock-driven
    // handshake state machine. When neither is configured the line stays a NullStream and
    // the modem status falls back to the fixed kModemStatusReady stub (fork 6).

    // Build (or rebuild) the ModemLine from the current dial/answer config and install it,
    // or -- if neither is configured -- tear it back down to a NullStream. Called by the
    // dial/answer setters.
    void syncModem();
    // Is the live line our ModemLine (and not pocketed by self-test)? A CONNECTed endpoint
    // replaces it, at which point modem semantics go inert (fork 6, and modem_ is cleared
    // so it can never dangle).
    bool modemActive() const;
    bool canDial() const { return !dialHost_.empty() && dialPort_ != 0; }
    bool canAnswer() const { return answerPort_ != 0; }

    // Decode SH/RI (OUT BA+0) and DTR (OUT BA+3) edges onto the ModemLine. `prev` is the
    // shadow value before this write, for edge detection.
    void decodeControl0(uint8_t prev);  // SH/RI: originate / answer
    void decodeControl3(uint8_t prev);  // DTR:   enable (arm + originate) / disconnect
    void latchAp();                     // AP goes -- and STAYS -- low on (SH|RI)&DTR

    // The handshake/ring state machine. Polls the ModemLine, advances the latched bits on
    // their Clock deadlines, and re-arms the earliest one (cancel-before-rearm, 2SIO-style).
    // Called from write(), pump(), configChanged() and deserialize(); never from a const
    // read -- modemStatus() computes the bits live from now() and the timestamps below.
    void     refreshModem();
    void     resetModemState();       // back to on-hook/idle: the restore + rebuild state
    bool     ringBurstOn(uint64_t now) const;   // inside a ring burst's 'on' half
    uint64_t nextRingEdge(uint64_t now) const;  // absolute T-state of the next burst edge
    bool     timerPulseHigh(uint64_t now) const;  // IN BA+2 bit 7, 40/60 duty at 250k/(N*100)


    // OUT BA+0: push the frame bits (2-6) into the UART straps and reprogram the line.
    void programFrame(uint8_t control);
    // OUT BA+2: the divisor half -- set the UART's baud from N (250,000/(16*N)).
    void programRate(uint8_t divisor);

    // SHOW helpers -- read-only status strings.
    std::string frameString() const;  // "8N1"
    std::string uartString() const;   // "TBMT dav" -- capitals = asserted
    std::string linesString() const;  // "SH ri DTR CTS AP" -- capitals = asserted

    // ---- THE UART. One 1602-family chip (src/chips/uart1602.h). ----
    Uart1602 u_{"line"};

    // Base address -- the 6-position DIP. The card occupies base_..base_+3 and sits
    // on a 4-port boundary (default 0xC0; the manual's North Star alternative is 0xE0).
    uint8_t base_ = 0xC0;

    // ---- The write-only control registers, SHADOWED. ----
    // The port at each of these addresses reads something else, so the only way to
    // know what was written is to keep it here (reference §4, "must shadow every write").
    uint8_t out0_ = 0;  // OUT BA+0 -- UART format / SH,RI / interrupt enable
    uint8_t out2_ = 0;  // OUT BA+2 -- rate divisor / staged interrupt mask (inert)
    uint8_t out3_ = 0;  // OUT BA+3 -- 6860 modem control (ST bit drives self-test)

    // ---- SELF TEST. The pocketed phone line while the loopback plug is on the UART's
    // pins; nullptr when not looped. Runtime state only -- a synthesized plug is not a
    // host handle and does not travel (re-derived from out3_ on restore). ----
    std::unique_ptr<ByteStream> savedLine_;

    // ---- THE MODEM (Phase 2). ----
    // Config, from the dial=/answer= properties. This TRAVELS as config (re-applied from
    // TOML), never in the snapshot; empty host or zero port means "cannot dial/answer".
    std::string dialHost_;
    uint16_t    dialPort_   = 0;
    uint16_t    answerPort_ = 0;

    // The installed ModemLine, or nullptr when the line is a NullStream / a CONNECTed
    // endpoint. NON-OWNING: the UART owns the stream (or savedLine_ pockets it during
    // self-test). Cleared whenever a non-modem endpoint replaces the line, so it can
    // never dangle.
    ModemLine* modem_ = nullptr;

    // The live handshake, all in ABSOLUTE T-states (Clock::now()); 0 = not armed. A live
    // call cannot be snapshotted (a socket handle is not serializable), so none of this
    // travels -- deserialize() rebuilds it on-hook and the guest redials.
    bool     apLow_        = false;  // Answer Phone LATCHED off-hook (survives SH/RI reset)
    bool     modeOriginate_ = true;  // IN BA+2 bit 6: last path (dial = originate = 1)
    uint64_t ctsClearAt_   = 0;      // when CTS goes clear (0) after carrier -- 450/750 ms
    uint64_t ringStart_    = 0;      // origin of the current ring's burst phase
    uint64_t apResetAt_    = 0;      // AP auto-reset ~1.5 s after CTS is lost
    uint64_t hsTimeoutAt_  = 0;      // 17 s no-handshake hangup (the failure path)
    bool     carrierPrev_  = false;  // edge detect: carrier rising -> arm the CTS delay
    bool     ringingPrev_  = false;  // edge detect: ring start -> mark the burst origin

    Clock::Handle wake_ = Clock::kNone;  // the single outstanding deadline; cancel-before-rearm
};

} // namespace altair
