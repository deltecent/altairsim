#include "host/modemline.h"

namespace altair {

ModemLine::ModemLine(std::string dialHost, uint16_t dialPort, uint16_t answerPort)
    : dialHost_(std::move(dialHost)), dialPort_(dialPort), answerPort_(answerPort) {}

// SHOW / CONFIG SAVE round-trip. Only the modes that are actually configured are
// named, so `modem:answer=2323` and `modem:dial=bbs.example:23,answer=2323` both
// echo exactly what the operator gave.
std::string ModemLine::describe() const {
    std::string s = "modem:";
    bool        first = true;
    if (dialConfigured()) {
        s += "dial=" + dialHost_ + ":" + std::to_string(dialPort_);
        first = false;
    }
    if (answerPort_ != 0) {
        if (!first) s += ",";
        s += "answer=" + std::to_string(answerPort_);
    }
    return s;
}

size_t ModemLine::read(uint8_t* buf, size_t n) {
    // THE RING GATE. Until the board answers, nothing has been drained from the
    // socket (pump() leaves it in the kernel), so rx_ is empty anyway -- but a card
    // that reached past the buffer must still find silence, so guard it explicitly.
    if (!offHook_) return 0;
    size_t k = rx_.size() < n ? rx_.size() : n;
    for (size_t i = 0; i < k; ++i) buf[i] = (uint8_t)rx_[i];
    rx_.erase(0, k);
    return k;
}

// ALWAYS TAKES ALL OF IT, on a live call -- what the kernel would not accept is
// ours to send in flush()/pump(), and the depth of that queue is what negates
// writable() (CTS). Off-hook-and-up is the gate: bytes written to a line that is
// on-hook or still ringing go nowhere, exactly as a 6850 transmitting into a modem
// that has not connected. (host/tcp.cpp makes the same call for a dead client.)
size_t ModemLine::write(const uint8_t* buf, size_t n) {
    if (!(offHook_ && conn_ && conn_->established())) return n;
    tx_.append((const char*)buf, n);
    flush();
    return n;
}

bool ModemLine::readable() const {
    // Off-hook and something to read. During a ring offHook_ is false, so the gate
    // holds; after a far-end hangup offHook_ stays true, so bytes that arrived
    // before the drop remain the guest's to collect (DESIGN.md 7.1: never
    // manufacture data loss the transport did not have).
    return offHook_ && !rx_.empty();
}

// BACKPRESSURE IS CTS. A full send buffer makes the guest WAIT rather than lose a
// byte. There is nothing to be clear-to-send TO unless the call is live, so this is
// false on an on-hook or ringing line as well.
bool ModemLine::writable() const {
    return offHook_ && conn_ && conn_->established() && tx_.size() < kTxCap;
}

void ModemLine::flush() {
    if (!conn_ || !conn_->established()) return;
    while (!tx_.empty()) {
        size_t w = conn_->write((const uint8_t*)tx_.data(), tx_.size());
        if (w == 0) break;  // send buffer full -- the backpressure. Retried in pump().
        tx_.erase(0, w);
    }
}

void ModemLine::pump() {
    // ANSWER THE PHONE: an inbound connection is a RING, not a session. Store it,
    // flag it ringing_, and drain NOTHING -- the bytes stay in the kernel buffer
    // until the board answer()s, so a caller cannot deliver a faceful of data before
    // the guest picks up. One call at a time; a second caller waits on the listener.
    if (listener_ && !conn_) {
        if (auto c = listener_->accept()) {
            conn_    = std::move(c);
            ringing_ = true;
            dialing_ = false;
        }
    }

    if (!conn_) return;
    conn_->poll();

    // An OUTBOUND dial whose handshake has completed: the far end answered.
    if (dialing_ && conn_->established()) dialing_ = false;

    // Move bytes only on a LIVE, ANSWERED call. A ringing (unanswered) line is held
    // silent on purpose -- see the accept above.
    if (offHook_ && conn_->established()) {
        uint8_t buf[512];
        for (;;) {
            size_t r = conn_->read(buf, sizeof buf);
            if (r == 0) break;
            rx_.append((const char*)buf, r);
        }
        flush();
    }

    // The far end hung up. Carrier drops (conn_ goes away), but the guest stays
    // OFF-HOOK -- it is holding the handset to a dead line until it hangs up -- and
    // the bytes already received stay in rx_ for it to collect. The listener is
    // untouched: it stays armed for the next call, exactly as a real auto-answer
    // modem does. A ringing caller that gives up before we answer lands here too.
    if (conn_->closed()) {
        conn_.reset();
        tx_.clear();
        ringing_ = false;
        dialing_ = false;
    }
}

// CARRIER IS THE ANSWERED SESSION. CTS is our own send buffer. RI is the ring
// LEVEL -- the board (Phase 2) counts its bursts; here it is simply "an unanswered
// caller is on the line". DSR follows carrier: a modem that has a connection is a
// data set that is ready.
LineStatus ModemLine::status() const {
    LineStatus s;
    s.carrier = carrier();
    s.dsr     = s.carrier;
    s.cts     = writable();
    s.ring    = ringing_;
    return s;
}

void ModemLine::setControl(const LineControl& c) {
    if (c.dtr) sawDtr_ = true;

    // THE GUEST HUNG UP. Dropping DTR after having raised it ends the call. The board
    // in Phase 2 also calls hangup() explicitly, so this is belt-and-suspenders -- it
    // closes the current call but LEAVES the listener armed (a DTR blip is not a
    // reconfiguration of the modem). RTS goes nowhere: TCP does its own flow control.
    if (sawDtr_ && !c.dtr && conn_) dropCall();
}

// ---------------------------------------------------------------------------
// The modem control surface. The board calls these; none of them is on the vtable.
// ---------------------------------------------------------------------------

// ORIGINATE. One call at a time: a busy line refuses rather than abandoning the
// call in progress. A non-blocking connect leaves us dialing_ until pump() sees the
// handshake finish -- the phone ringing at the far end.
bool ModemLine::dial(std::string& err) {
    if (!dialConfigured()) {
        err = "this modem has no number to dial (no dial= configured)";
        return false;
    }
    if (conn_) {
        err = "the line is busy";
        return false;
    }
    conn_ = platform::connectTcp(dialHost_, dialPort_, err);
    if (!conn_) return false;
    offHook_ = true;
    dialing_ = true;
    ringing_ = false;
    return true;
}

// AUTO-ANSWER: bind the listener so an inbound call can ring. Idempotent -- arming
// an already-armed line is not an error. This is the only thing that opens a socket
// on an idle modem, which is what makes "idle = no sockets" literally true.
bool ModemLine::armAnswer(std::string& err) {
    if (answerPort_ == 0) {
        err = "this modem has no port to answer on (no answer= configured)";
        return false;
    }
    if (listener_) return true;
    listener_ = platform::listenTcp(answerPort_, err);
    return listener_ != nullptr;
}

// PICK UP. Only meaningful on a ringing line: it clears the gate, so from the next
// pump() the connection is drained and carrier goes live.
void ModemLine::answer() {
    if (!ringing_) return;
    ringing_ = false;
    offHook_ = true;
}

// ON-HOOK. Drop the current call AND the listener -- the modem is no longer waiting
// for anyone. sawDtr_ is left alone: it records that DTR was once raised, which a
// subsequent power-on reset, not a hangup, is what clears.
void ModemLine::hangup() {
    dropCall();
    listener_.reset();
}

void ModemLine::dropCall() {
    if (conn_) conn_->close();
    conn_.reset();
    ringing_ = false;
    offHook_ = false;
    dialing_ = false;
    rx_.clear();
    tx_.clear();
}

// ---------------------------------------------------------------------------
// Queries. Pure reads of the state above -- Phase 2's handshake state machine is
// built entirely from these.
// ---------------------------------------------------------------------------

bool ModemLine::ringing() const { return ringing_; }

bool ModemLine::connecting() const {
    return dialing_ && conn_ && !conn_->established();
}

bool ModemLine::carrier() const {
    return offHook_ && !ringing_ && conn_ && conn_->established();
}

// Synthetic: a modem that is off-hook to originate hears a dial tone until the call
// connects. Phase 2 refines the handshake timing; here it is simply "lifted, not yet
// up". A pure answer-only line (no dial configured) never presents one.
bool ModemLine::dialTonePresent() const {
    return dialConfigured() && offHook_ && !carrier();
}

} // namespace altair
