#pragma once
//
// `ModemLine` -- a SOFTWARE-CONTROLLED phone line, as a ByteStream.
//
// ---------------------------------------------------------------------------
// WHY THIS IS NOT `TcpStream`. The socket streams in host/tcp.h model a line
// whose fate is fixed the moment CONNECT runs: `socket:2323` LISTENS and
// AUTO-ANSWERS -- the instant a client connects, carrier is up -- and
// `socket:host:port` DIALS once, at construction. That is exactly right for a
// telnet server or `CONNECT sio:a socket:...`, and it stays untouched.
//
// A real Bell 103 originate/answer modem (the PMMI MM-103) cannot be expressed
// that way, because the guest SOFTWARE decides at runtime whether to dial, to
// wait for a ring, or to sit on-hook -- and answering is a deliberate act, not
// something the transport does for it. So this stream:
//
//   * holds NO sockets at all when idle (a fresh ModemLine binds nothing);
//   * dials out only when the board goes off-hook  -> dial();
//   * treats an inbound connection as a RING and does NOT raise carrier or
//     deliver a single byte until the board answers -> armAnswer()/answer();
//   * hangs up -- connection AND listener -- on command -> hangup().
//
// The board (Phase 2) drives all of that through the concrete MODEM CONTROL
// SURFACE below; it never touches a socket, exactly as DESIGN.md 7.1/7.7
// require. The stream reports LEVELS (ringing, carrier); the honoring card
// times the edges -- the pulsing RI bit the guest counts is synthesized in the
// board, not here.
// ---------------------------------------------------------------------------

#include "host/stream.h"
#include "platform/socket.h"

#include <memory>
#include <string>

namespace altair {

class ModemLine : public ByteStream {
public:
    // CONFIG ONLY -- opens no socket. An empty dialHost or a zero dialPort means
    // "cannot originate"; a zero answerPort means "cannot answer". A line with
    // neither is legal but inert (describe() still round-trips it).
    ModemLine(std::string dialHost, uint16_t dialPort, uint16_t answerPort);

    // ---- ByteStream ----
    std::string describe() const override;
    size_t      read(uint8_t* buf, size_t n) override;   // 0 while RINGING (not answered)
    size_t      write(const uint8_t* buf, size_t n) override;  // dropped unless off-hook & up
    bool        readable() const override;               // false until answered
    bool        writable() const override;               // send-buffer backpressure = CTS
    void        flush() override;
    void        pump() override;                          // accept / poll / drain: the host turn
    LineStatus  status() const override;                  // carrier = answered && established
    void        setControl(const LineControl&) override;  // DTR-drop hangup (belt-and-suspenders)

    // ---- MODEM CONTROL SURFACE (board -> stream), concrete, not via the vtable ----
    bool dial(std::string& err);       // ORIGINATE: connectTcp(dialHost, dialPort)
    bool armAnswer(std::string& err);  // AUTO-ANSWER: listenTcp(answerPort)
    void answer();                     // PICK UP: unclamp bytes, raise carrier
    void hangup();                     // ON-HOOK: close the call AND drop the listener

    // ---- QUERIES (stream -> board) ----
    bool ringing() const;         // inbound conn accepted, NOT yet answered
    bool connecting() const;      // outbound dial in flight (not yet established)
    bool carrier() const;         // answered && established -> Phase 2's CTS/AP source
    bool dialTonePresent() const; // synthetic: dial configured && off-hook, not yet up

private:
    bool dialConfigured() const { return !dialHost_.empty() && dialPort_ != 0; }

    // Drop the CURRENT call (connection + its buffers), leaving the listener up so
    // auto-answer stays armed. hangup() is this plus dropping the listener.
    void dropCall();

    std::string dialHost_;
    uint16_t    dialPort_   = 0;
    uint16_t    answerPort_ = 0;

    std::unique_ptr<platform::TcpListener> listener_;
    std::unique_ptr<platform::TcpConn>     conn_;

    std::string rx_, tx_;

    // THE RING GATE. A connection that arrived on the listener is held ringing_
    // until the board answer()s: while ringing, no byte is drained from the kernel
    // and carrier stays down. offHook_ is "the guest has the handset up" -- set by
    // answer() (picked up an inbound call) or dial() (originated one), cleared only
    // by hangup(). It survives a far-end disconnect: the guest is still holding the
    // handset to a dead line until it hangs up. dialing_ is an outbound call whose
    // TCP handshake has not completed -- a phone still ringing at the far end.
    bool ringing_ = false;
    bool offHook_ = false;
    bool dialing_ = false;

    // DTR-drop only hangs up if the card ever raised DTR -- the same guard as
    // TcpStream (host/tcp.h): a card that never asserted DTR is not a card that
    // hung up, and a bare de-asserted DTR at power-on must not tear down the call.
    bool sawDtr_ = false;

    static constexpr size_t kTxCap = 8192;
};

} // namespace altair
