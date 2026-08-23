#pragma once
//
// `ENDPOINT|socket:PORT` -- a live, BIDIRECTIONAL socket mirror of a serial line
// (issue #381). The tee's twin: same decorator shape, but the sink is a SOCKET, not a
// file, and it is a two-way wire -- a human telnets in, WATCHES the session the guest
// is having, and can TYPE, injecting keystrokes back onto the line (take-over).
//
// WHY IT IS NOT A `log=` TAP AND NOT A TeeStream. `log=` (host/console.cpp) is a
// SCREEN capture -- diagnostic, one-way, and it does not round-trip through CONFIG
// SAVE because it is not part of the wire. A TeeStream (host/tee_stream.h) is a
// protocol-analyzer file: one-way, formatted hex, on ANY line. This is neither: it is
// a second terminal on the same line, and the whole point is the reverse channel. So
// it is its own decorator -- built from the tee's structure (wrap inner_, forward
// everything) and the socket's I/O (host/tcp.cpp: accept in pump(), backpressure,
// carrier from the session), but keeping the two apart because they answer different
// questions.
//
// THE CLIENT NEVER PACES THE GUEST. This is the load-bearing rule. A watcher that
// fell behind -- a slow link, a paused terminal -- must NOT stall emulated time, or a
// human idly watching would freeze the AI driving the session. So the guest's flow
// control (writable/status/paces*) is the INNER line's, untouched; the client is a
// passive rider whose output queue is bounded and DROPPED-OLDEST when it overflows. A
// watcher loses scrollback, never the guest a byte. (Contrast host/tcp.cpp, where the
// socket IS the line and its backpressure legitimately negates CTS.)
//
// THE MIRROR ADDS NO ECHO -- the guest is the echo authority, exactly as on a real
// serial console. What the client sees is the guest's OUTPUT (every write, copied
// through); what the client TYPES is injected as INPUT the guest reads and -- if it
// echoes, as a monitor or CP/M does -- writes back, which the client then sees. So a
// command typed at the watcher appears once, its answer follows, and a password the
// guest does NOT echo stays unseen at the mirror too, faithfully. Echoing the read
// side here would double every character and leak the unechoed ones. (This is a
// deliberate reading of the plan's "faithful two-wire view": faithful to the TERMINAL
// the guest presents, which is what "watch and take over the console" means.)
//
// describe() ECHOES ITSELF -- `inner|socket:PORT[?ro]` -- so SHOW prints the mirror
// and CONFIG SAVE round-trips it (the `|` re-triggers the branch on reload, and a
// socket: right side selects the mirror over the tee). Same contract as the tee.

#include "host/stream.h"
#include "platform/socket.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class MirrorStream : public ByteStream {
public:
    // `inner` is the wrapped line (the guest's real endpoint -- scripted, console, a
    // real serial port, anything). `sinkSpec` is the operator's original `socket:PORT`
    // text (plus `?ro`), echoed after the `|` by describe(). `listener` is the
    // already-bound TCP listener (the resolver binds it so it can REFUSE cleanly at
    // CONNECT -- a port in use is a failed CONNECT, not a half-built mirror).
    // `readOnly` drops the inject path: the watcher can see but not type.
    MirrorStream(std::unique_ptr<ByteStream>              inner,
                 std::string                              sinkSpec,
                 std::unique_ptr<platform::TcpListener>   listener,
                 bool                                     readOnly);

    // The mirror is `inner|socket:PORT[?ro]`, so SHOW / CONFIG SAVE round-trip it.
    std::string describe() const override { return inner_->describe() + "|" + sinkSpec_; }

    // The data path:
    //   read  -> INJECTED client bytes first (a human taking over), else inner_.read().
    //            Nothing is echoed to the client here; the guest echoes what it reads.
    //   write -> forward to inner_, then COPY the accepted bytes to the client's queue.
    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    // readable() is true when the guest has input from EITHER source. The inject buffer
    // is checked first so a human mid-take-over does not spin the inner's empty-poll
    // counter (ScriptedStream::readable() increments `hungry` on a miss, and the MCP
    // run loop reads that delta -- a line with bytes waiting is not a hungry one).
    bool readable() const override { return !injectBuf_.empty() || inner_->readable(); }

    // Flow control, pacing and the modem pins are the INNER line's, verbatim -- the
    // watcher must never leak into what the guest can measure (see the header note).
    bool writable() const override { return inner_->writable(); }
    bool pacesItself() const override { return inner_->pacesItself(); }
    bool pacedReceive() const override { return inner_->pacedReceive(); }

    // flush() pushes the inner's buffer AND drains the client queue, so a watcher sees
    // guest output as promptly as the guest emits it.
    void flush() override;

    // pump() is where the socket lives: forward to inner_, answer a waiting watcher,
    // poll the session, drain the client -> injectBuf_ (unless read-only), flush queued
    // output -> client, and drop the session cleanly on hang-up (the listener stays up
    // for the next watcher, host/tcp.cpp's precedent).
    void pump() override;

    // The far end's pins, the card's pins, the line rate -- all the inner's. A watcher
    // connecting or leaving does NOT move the guest's carrier: the mirror is a rider on
    // the line, not the line itself.
    LineStatus status() const override { return inner_->status(); }
    void       setControl(const LineControl& c) override { inner_->setControl(c); }
    bool       setParams(const LineParams& p, std::string& err) override {
        return inner_->setParams(p, err);
    }

    std::vector<std::string> drainLog() override { return inner_->drainLog(); }

    // For the --mcp attachment (Phase 2): reach the wrapped line so the MCP server can
    // feed()/out() an inner ScriptedStream directly while the guest talks to the mirror.
    ByteStream* inner() { return inner_.get(); }

private:
    // Queue guest output for the watcher, bounded and drop-oldest: a slow watcher loses
    // scrollback, never stalls the guest. A no-op when no watcher is connected (like
    // host/tcp.cpp, output with nobody on the line goes nowhere -- a watcher who
    // connects later must not open to an hour-old faceful of text).
    void queueToClient(const uint8_t* buf, size_t n);
    void sendToClient();  // push txQueue_ -> conn_, stopping on backpressure

    std::unique_ptr<ByteStream>            inner_;
    std::string                            sinkSpec_;
    std::unique_ptr<platform::TcpListener> listener_;
    std::unique_ptr<platform::TcpConn>     conn_;  // at most one watcher, one wire
    bool                                   readOnly_;

    // Guest output waiting for the watcher (drop-oldest past the cap), and watcher
    // keystrokes waiting for the guest (a bounded take-over channel).
    std::string txQueue_;
    std::string injectBuf_;

    // Bound the watcher's output backlog: past this we drop the OLDEST bytes so the
    // watcher always catches up to the live tail. Generous, so a brief stall keeps the
    // stream intact; a genuinely stuck watcher just loses history.
    static constexpr size_t kTxCap     = 256 * 1024;
    // And bound the take-over backlog, so a client that floods input cannot grow us
    // without limit; the guest drains it at its own line rate.
    static constexpr size_t kInjectCap = 64 * 1024;
};

} // namespace altair
