#pragma once
//
// `TelnetCodec` -- the Telnet NVT protocol (RFC 854/855/857/858) as a pure
// byte-level engine, with NO sockets and NO ByteStream. It is the shared heart of
// every telnet line in the simulator: the `telnet:` endpoint (host/telnet_stream.h)
// wraps it around a TcpStream, and the PMMI modem's answer/dial line
// (host/modemline.h) runs it inline when its `telnet` strap is set -- so a person
// telnetting into a BBS behaves whether the BBS listens on a plain `telnet:` port
// or answers on the modem.
//
// WHAT IT DOES, all in terms of two buffers:
//
//   reset(server)  -- a new session. Clears the parser and per-option state, then
//                     QUEUES the initial option offers onto the outbound buffer: a
//                     server offers WILL ECHO / WILL SGA / DO SGA (which makes a
//                     stock client drop its local echo and go character-at-a-time);
//                     a client asks the far end to do the same (DO ECHO / DO SGA).
//   recv(bytes)    -- inbound WIRE bytes -> decoded data (takeData), with IAC
//                     commands stripped, CR LF / CR NUL folded to a bare CR, and
//                     IAC IAC delivered as one literal 0xFF. Any negotiation reply
//                     it must send is appended to the outbound buffer.
//   send(bytes)    -- outbound GUEST bytes -> the outbound buffer, a data 0xFF
//                     doubled to IAC IAC so the far end reads it as data, and a NUL
//                     after a CR that is not followed by LF, as RFC 854 requires.
//
// The owner moves the two buffers with takeData() (decoded data for the guest) and
// takeOut() (wire bytes for the socket). Negotiation is loop-free: a verb is emitted
// only when it changes what we last told the far end.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace altair {

class TelnetCodec {
public:
    // A new session. `server` is true when WE host the line (we offer to echo), false
    // when we are the client dialling out (we ask the far end to echo).
    void reset(bool server);

    void recv(const uint8_t* buf, size_t n);  // inbound wire -> decoded data + replies
    void send(const uint8_t* buf, size_t n);  // outbound guest -> wire (0xFF doubled)

    // Move-and-clear: decoded data for the guest, wire bytes for the socket.
    std::string takeData() {
        std::string s;
        s.swap(data_);
        return s;
    }
    std::string takeOut() {
        std::string s;
        s.swap(out_);
        return s;
    }
    bool hasData() const { return !data_.empty(); }
    bool hasOut() const { return !out_.empty(); }

private:
    void respond(uint8_t verb, uint8_t opt);  // negotiate one option, loop-free

    bool        server_ = true;
    std::string data_;  // decoded inbound, waiting for the guest
    std::string out_;   // wire bytes (protocol + encoded data), waiting for the socket

    // NVT input parser state, persisted across recv() calls so a command split over a
    // buffer boundary is still parsed correctly.
    enum class St { Data, Iac, Opt, Sub, SubIac };
    St      st_        = St::Data;
    uint8_t verb_      = 0;      // the DO/DONT/WILL/WONT awaiting its option byte
    bool    lastWasCR_ = false;  // fold the CR's LF/NUL tail
    bool    crPending_ = false;  // a guest CR went out; the next byte decides if a NUL is owed

    // What we have TOLD the far end about our options (myState_) and asked of its
    // (hisState_). Tri-state so "never mentioned" is distinct from "told WONT"; a verb
    // is sent only when the target differs from what we last told -- the loop guard.
    std::array<uint8_t, 256> myState_{};   // MyUnknown / MyWill / MyWont
    std::array<uint8_t, 256> hisState_{};  // HisUnknown / HisDo / HisDont
};

} // namespace altair
