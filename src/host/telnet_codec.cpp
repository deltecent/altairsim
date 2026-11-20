#include "host/telnet_codec.h"

namespace altair {

namespace {

// RFC 854 / 855. Only the commands and options this engine handles are named; every
// other command byte is a 2-byte no-op we consume and ignore.
constexpr uint8_t IAC  = 255;  // Interpret As Command -- the escape
constexpr uint8_t DONT = 254;
constexpr uint8_t DO   = 253;
constexpr uint8_t WONT = 252;
constexpr uint8_t WILL = 251;
constexpr uint8_t SB   = 250;  // begin subnegotiation
constexpr uint8_t SE   = 240;  // end subnegotiation

constexpr uint8_t OPT_ECHO = 1;  // RFC 857
constexpr uint8_t OPT_SGA  = 3;  // RFC 858 -- suppress go ahead == character mode

// Tri-state cells for the negotiation arrays (0 = never mentioned).
enum { MyUnknown = 0, MyWill, MyWont };
enum { HisUnknown = 0, HisDo, HisDont };

// The role's DESIRED end state. A server (we host the line) echoes and runs both ends
// in character mode; a client (we dial out) asks the far end to do the same.
bool wantMyWill(bool server, uint8_t opt) {
    return server && (opt == OPT_ECHO || opt == OPT_SGA);
}
bool wantHisDo(bool server, uint8_t opt) {
    return server ? (opt == OPT_SGA) : (opt == OPT_ECHO || opt == OPT_SGA);
}

} // namespace

void TelnetCodec::reset(bool server) {
    server_    = server;
    st_        = St::Data;
    verb_      = 0;
    lastWasCR_ = false;
    crPending_ = false;
    myState_.fill(MyUnknown);
    hisState_.fill(HisUnknown);

    auto offerWill = [&](uint8_t opt) {
        out_.push_back((char)IAC);
        out_.push_back((char)WILL);
        out_.push_back((char)opt);
        myState_[opt] = MyWill;
    };
    auto offerDo = [&](uint8_t opt) {
        out_.push_back((char)IAC);
        out_.push_back((char)DO);
        out_.push_back((char)opt);
        hisState_[opt] = HisDo;
    };

    if (server_) {
        // We host the line: WE echo, and both ends run character-at-a-time. This is
        // the trio that makes a stock `telnet` client drop its local echo.
        offerWill(OPT_ECHO);
        offerWill(OPT_SGA);
        offerDo(OPT_SGA);
    } else {
        // We dial out as the client: let the far end echo and suppress go-ahead.
        offerDo(OPT_ECHO);
        offerDo(OPT_SGA);
    }
}

// A data 0xFF is an IAC to the far end, so it must be DOUBLED or it reads as a command
// introducer.
//
// RFC 854 allows a CR only as CR LF or CR NUL, and the far end drops the NUL of a CR
// NUL. So a guest CR that is not followed by LF gets a NUL after it: guest 0D 00 goes
// out as 0D 00 00 and arrives as 0D 00 (issue #957: without it, an XMODEM block that
// held 0D 00 lost the 00 on every retry). The guest writes one byte at a time, so the
// CR goes out at once and the NUL, when it is owed, goes in front of the NEXT byte --
// nothing is held back. Every other byte passes through untouched.
void TelnetCodec::send(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = buf[i];
        if (crPending_) {
            crPending_ = false;
            if (b != '\n') out_.push_back('\0');
        }
        out_.push_back((char)b);
        if (b == IAC) out_.push_back((char)IAC);
        if (b == '\r') crPending_ = true;
    }
}

// Loop-free by construction: we emit a verb only when the target state differs from
// what we last told the far end, so a settled option is never re-answered.
void TelnetCodec::respond(uint8_t verb, uint8_t opt) {
    auto send3 = [&](uint8_t v) {
        out_.push_back((char)IAC);
        out_.push_back((char)v);
        out_.push_back((char)opt);
    };
    switch (verb) {
        case DO: {  // the far end asks US to enable opt
            uint8_t target = wantMyWill(server_, opt) ? MyWill : MyWont;
            if (myState_[opt] != target) {
                send3(target == MyWill ? WILL : WONT);
                myState_[opt] = target;
            }
            break;
        }
        case DONT:  // the far end forbids us opt -- comply
            if (myState_[opt] != MyWont) {
                send3(WONT);
                myState_[opt] = MyWont;
            }
            break;
        case WILL: {  // the far end offers to enable opt on ITS side
            uint8_t target = wantHisDo(server_, opt) ? HisDo : HisDont;
            if (hisState_[opt] != target) {
                send3(target == HisDo ? DO : DONT);
                hisState_[opt] = target;
            }
            break;
        }
        case WONT:  // the far end refuses opt on its side -- acknowledge
            if (hisState_[opt] != HisDont) {
                send3(DONT);
                hisState_[opt] = HisDont;
            }
            break;
        default:
            break;
    }
}

void TelnetCodec::recv(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = buf[i];
        switch (st_) {
            case St::Data:
                if (b == IAC) {
                    st_        = St::Iac;
                    lastWasCR_ = false;
                    break;
                }
                // A CR the guest already saw is followed by LF or NUL as the NVT line
                // terminator's second half -- swallow it, deliver a bare CR.
                if (lastWasCR_) {
                    lastWasCR_ = false;
                    if (b == 0 || b == '\n') break;
                }
                data_.push_back((char)b);
                if (b == '\r') lastWasCR_ = true;
                break;

            case St::Iac:
                if (b == IAC) {  // IAC IAC -- a literal 0xFF data byte
                    data_.push_back((char)IAC);
                    st_ = St::Data;
                } else if (b == WILL || b == WONT || b == DO || b == DONT) {
                    verb_ = b;
                    st_   = St::Opt;
                } else if (b == SB) {
                    st_ = St::Sub;
                } else {  // GA / NOP / other 2-byte command -- consumed, ignored
                    st_ = St::Data;
                }
                break;

            case St::Opt:
                respond(verb_, b);
                st_ = St::Data;
                break;

            case St::Sub:  // skip subnegotiation payload until IAC SE
                if (b == IAC) st_ = St::SubIac;
                break;

            case St::SubIac:
                if (b == SE) st_ = St::Data;  // end of subnegotiation
                else st_ = St::Sub;           // IAC IAC (data) or stray -- stay in SB
                break;
        }
    }
}

} // namespace altair
