#include "boards/pmmi-mm103.h"

#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/modemline.h"
#include "host/stream.h"

#include <string>
#include <utility>
#include <vector>

namespace altair {
namespace {

PmmiBoard::EndpointResolver g_resolver;

// A card in a backplane always has a clock, but Bus::attach() is public, so a board
// CAN be wired up without a machine around it. A UART with no clock cannot receive or
// time a character; it reads as a dead card rather than dereferencing a null pointer.
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

// ---------------------------------------------------------------------------
// IN BA+2 -- MODEM STATUS, THE FIXED "READY" STUB (reference §5).
//
// This milestone does not model the 6860 handshake, so the modem-status port returns
// one constant meaning "connected, off-hook, clear to send". The active-low bits are
// the trap here: Dial Tone, Ringing, CTS and Answer Phone all read 0 when ASSERTED.
//
//   bit 0  Dial Tone   = 1  (active low: no dial tone -- we are past dialing)
//   bit 1  Ringing     = 1  (active low: not ringing)
//   bit 2  CTS         = 0  (active low: CLEAR to send)
//   bit 3  Rx Break    = 0  (active high: no break received)
//   bit 4  Answer Phone= 0  (active low: OFF-HOOK, modem holding the line)
//   bit 5  Digital FO  = 0  (diagnostics only)
//   bit 6  Mode        = 1  (active high: originate)
//   bit 7  Timer Pulses= 0
//
// A later milestone replaces this constant with the real SH/RI/DTR + carrier state
// machine. See docs/boards/pmmi-mm103.md.
constexpr uint8_t kModemStatusReady = 0x43;

// IN BA+2 modem-status bits (reference §5). Bits 0,1,2,4 are ACTIVE LOW (a 0 means the
// named thing is present/asserted); bits 3,5,6,7 are active high.
constexpr uint8_t kMsDialTone = 0x01;  // active low: 0 = dial tone present
constexpr uint8_t kMsRinging  = 0x02;  // active low: 0 = ringing (toggles between bursts)
constexpr uint8_t kMsCts      = 0x04;  // active low: 0 = clear to send
constexpr uint8_t kMsAp       = 0x10;  // active low: 0 = off-hook (Answer Phone)
constexpr uint8_t kMsMode     = 0x40;  // active high: 1 = originate, 0 = answer
constexpr uint8_t kMsTimer    = 0x80;  // active high: the rate-derived 40/60 timer pulse

// OUT BA+0 modem-control shadow bits (reference §4).
constexpr uint8_t kSh = 0x01;  // Switch Hook -- 1 = off-hook / originate
constexpr uint8_t kRi = 0x02;  // Ring Indicator -- 1 = answer mode

// OUT BA+3 modem-control shadow bits (reference §4). DTR is unbarred: active high.
constexpr uint8_t kDtr = 0x40;  // 1 = modem enabled
constexpr uint8_t kSt  = 0x10;  // Self Test -- bit 4, ACTIVE LOW (0 = testing)

// ---------------------------------------------------------------------------
// THE HANDSHAKE TIMING (reference §3.2.6, §7, §10.2/§10.3). All in milliseconds; the
// state machine turns them into T-states through the Clock, so REPLAY stays
// deterministic and a faster/slower clock keeps the SAME emulated timing.
//
// Answer-mode CTS is billing (§3.2.6: 2 s inhibit after the phone goes off-hook to
// answer) PLUS the §10.2 echo-suppressor delay (CTS 450 ms after carrier). Originate
// has no billing delay -- §10.3's 750 ms is measured from receipt of carrier.
constexpr long long kBillingMs       = 2000;  // §3.2.6 incoming-answer inhibit
constexpr long long kAnswerCtsMs     = kBillingMs + 450;  // §10.2
constexpr long long kOriginateCtsMs  = 750;   // §10.3
constexpr long long kApResetMs       = 1500;  // §7.4.4.5 AP resets ~1.5 s after CTS lost
constexpr long long kHsTimeoutMs     = 17000; // §7.3.1.1/§10.2 no-handshake hangup

// US ring cadence: 2 s on, 4 s off. The Ringing bit holds 0 across the 'on' burst and
// reads 1 during the silence, so a guest counting transitions advances one per burst
// (§7.4.4.2). Exact seconds don't matter to the guest -- the TRANSITION does.
constexpr long long kRingOnMs  = 2000;
constexpr long long kRingOffMs = 4000;

} // namespace

void PmmiBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

PmmiBoard::PmmiBoard() {
    // -> NullStream. There is no null pointer in the stream path, ever: a card with
    // nothing plugged into it has a DEAD line, not a dangling one.
    u_.disconnect();
}

// A deadline is a lambda holding `this`. A card pulled from a running machine
// (BOARDS REMOVE) must not leave one on the books to fire into freed memory -- the
// same use-after-free the 2SIO guards in its destructor.
PmmiBoard::~PmmiBoard() {
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// The bus interface -- four consecutive ports, read != write at every one.
// ---------------------------------------------------------------------------

bool PmmiBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= base_ && p <= (uint8_t)(base_ + 3);
}

uint8_t PmmiBoard::read(const BusCycle& c) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    u_.poll(clk);  // the receiver runs on the UART's clock, not on ours

    switch ((c.port() - base_) & 3) {
    case 1:  return u_.readData();   // IN BA+1 -- receive data (clears DAV)
    case 2:  return modemStatus();   // IN BA+2 -- modem status
    case 3:  return 0xFF;            // IN BA+3 -- strobe: nothing drives the bus, floats
                                     // 0xFF (inferred, reference §5). It would latch the
                                     // staged interrupt mask -- inert without interrupts.
    default: return uartStatus();    // IN BA+0 -- UART status
    }
}

void PmmiBoard::write(const BusCycle& c) {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    switch ((c.port() - base_) & 3) {
    case 1:  // OUT BA+1 -- transmit data. The chip's /TDS strobe.
        u_.writeData(c.data, clk);
        break;
    case 2:  // OUT BA+2 -- rate divisor AND interrupt-mask staging (shadowed, inert).
        out2_ = c.data;
        programRate(c.data);
        break;
    case 3: {  // OUT BA+3 -- 6860 modem control. Shadowed; ST drives self-test, DTR the modem.
        uint8_t prev = out3_;
        out3_        = c.data;
        updateSelfTest();      // ST may pocket/unpocket the phone line first...
        decodeControl3(prev);  // ...then DTR edges dial / arm / hang up the modem.
        break;
    }
    default: {  // OUT BA+0 -- UART format / SH,RI / interrupt enable (enable bit inert).
        uint8_t prev = out0_;
        out0_        = c.data;
        programFrame(c.data);
        decodeControl0(prev);  // SH/RI edges originate / answer.
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// The two status ports.
// ---------------------------------------------------------------------------

// IN BA+0 -- UART status, ALL ACTIVE HIGH (unlike the 88-SIO, which inverts).
uint8_t PmmiBoard::uartStatus() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    bool    tbmt = u_.txBufferEmpty(clk);
    uint8_t s    = 0;
    if (tbmt)               s |= 0x01;  // bit 0 TBMT -- transmit buffer empty
    if (u_.dataAvailable()) s |= 0x02;  // bit 1 DAV  -- received char available
    if (tbmt)               s |= 0x04;  // bit 2 TEOC -- serializer done (shares tx deadline)
    // bits 3-5 RPE/OR/FE: the chip hardwires its error flags false -- there is no line to
    // have noise on (uart1602.h) -- so they read 0 here.
    if (u_.parityError())  s |= 0x08;
    if (u_.overrun())      s |= 0x10;
    if (u_.framingError()) s |= 0x20;
    // bits 6-7 Aux In 1/2: the aux-connector inputs, nothing wired -- read 0.
    return s;
}

// IN BA+2 -- modem status, computed LIVE from the phone line and the handshake clock.
//
// This reads only: the ModemLine's levels, the latched bits, and the timestamps
// refreshModem() armed. It arms nothing itself -- a status read must never have a side
// effect on emulated time. When the line is NOT our ModemLine (a CONNECTed test endpoint,
// or self-test's loopback), the handshake is meaningless and we hand back the same fixed
// "ready" constant the file-transfer milestone always did (fork 6).
uint8_t PmmiBoard::modemStatus() const {
    if (!modemActive()) return kModemStatusReady;

    const Clock& clk = clock_ ? *clock_ : deadCard();
    uint64_t     now = clk.now();
    uint8_t      s   = 0;

    // bit 0 Dial Tone (active low, 0 = present). Board-synthesized: the guest goes
    // off-hook (SH) to originate and polls this BEFORE dialing, while DTR is still off
    // (§8.2), so it cannot come from the ModemLine's off-hook flag -- it comes from the
    // SH relay bit plus "no call up yet". (Dialed digits are not decoded; the far end is
    // host config.)
    bool dialTone = canDial() && (out0_ & kSh) && !modem_->carrier() && !modem_->connecting();
    if (!dialTone) s |= kMsDialTone;

    // bit 1 Ringing (active low, 0 = ringing). Integrated over the burst: 0 across the
    // 'on' half, 1 in the silence -- the guest counts rings by counting transitions.
    if (!(modem_->ringing() && ringBurstOn(now))) s |= kMsRinging;

    // bit 2 CTS (active low, 0 = clear to send). Clear only after the handshake delay
    // following carrier -- until then the guest must WAIT (the delay is armed on the
    // carrier edge in refreshModem).
    bool cts = ctsClearAt_ != 0 && now >= ctsClearAt_ && modem_->carrier();
    if (!cts) s |= kMsCts;

    // bit 3 Rx Break (active high): not modeled -- 0.

    // bit 4 Answer Phone (active low, 0 = off-hook). Latched: set on (SH|RI)&DTR, held
    // across the guest's post-CTS SH/RI reset, cleared ~1.5 s after carrier is lost.
    if (!apLow_) s |= kMsAp;

    // bit 5 Digital FO (active high, diagnostics): 0.

    // bit 6 Mode (active high, 1 = originate): the active path.
    if (modeOriginate_) s |= kMsMode;

    // bit 7 Timer Pulses (active high): the rate-generator's 40/60 square wave. A guest
    // dialer times SH pulses and the 51 ms hold against it, so it must actually toggle.
    if (timerPulseHigh(now)) s |= kMsTimer;

    return s;
}

// ---------------------------------------------------------------------------
// OUT BA+0 / OUT BA+2 -- the software-programmable frame and baud rate.
// ---------------------------------------------------------------------------

// OUT BA+0 bits 2-6 are the UART word format (reference §4). Push them into the
// chip's straps and reprogram the line, exactly as a real port would be reopened.
void PmmiBoard::programFrame(uint8_t control) {
    u_.dataBits = 5 + ((control >> 2) & 0x03);              // NB1,NB2: 00->5 .. 11->8
    if (control & 0x10)                                     // NP: 1 = no parity
        u_.parity = LineParity::None;
    else
        u_.parity = (control & 0x20) ? LineParity::Even     // EPS: 1 = even
                                     : LineParity::Odd;      //      0 = odd
    u_.stopBits = (control & 0x40) ? 2 : 1;                 // TSB: 1 = 2 stop bits
    u_.programLine();
}

// OUT BA+2 as the rate generator: an 8-bit divisor N. Baud = 250,000 / (16 * N)
// (reference §6). N = 0 is undefined -- the 74193 down-counter wraps to 256 and no
// §8 program writes it -- so we leave the rate unchanged.
void PmmiBoard::programRate(uint8_t divisor) {
    if (divisor == 0) return;
    u_.baud = 250000 / (16LL * divisor);
    u_.programLine();
}

// ---------------------------------------------------------------------------
// SELF TEST -- the 6860 loopback (OUT BA+3 bit 4, ST). See the header.
// ---------------------------------------------------------------------------

// PLUG A LINE INTO THE CONNECTOR. While looped, the live wire on the UART is the
// internal loopback plug and the real line is pocketed -- so a fresh operator line
// replaces the POCKETED one, not the plug, and reappears on the pins when ST clears.
void PmmiBoard::attachStream(std::unique_ptr<ByteStream> s) {
    if (selfTestEngaged())
        savedLine_ = std::move(s);
    else
        u_.connect(std::move(s));
}

// Reconcile the loopback with the shadow. On real hardware ST is active low and only
// means anything with the modem enabled, so the loopback is engaged iff DTR is set and
// ST is asserted. Entering, we pocket the phone line and drop a LoopbackStream on the
// UART's pins (host/stream.h): the guest's TX returns on its RX. The LoopbackStream also
// loops the modem control pins, but that is INERT here -- the PMMI reads a fixed modem
// status stub (modemStatus) and never consults the stream's pins, so self-test on this
// board is a data loopback only. Leaving, we put the phone line back and discard the
// plug. Idempotent: a no-op if already there.
void PmmiBoard::updateSelfTest() {
    bool want = (out3_ & kDtr) && !(out3_ & kSt);
    if (want == selfTestEngaged()) return;
    if (want)
        savedLine_ = u_.swapStream(std::make_unique<LoopbackStream>());
    else
        u_.swapStream(std::move(savedLine_));  // plug returned and dropped; savedLine_ -> null
}

// ---------------------------------------------------------------------------
// THE MODEM -- the board as the phone line's policy (Phase 2).
//
// dial=/answer= install a ModemLine; the guest's SH/RI/DTR drive it; IN BA+2 is
// computed from its levels plus the handshake state machine below. The ModemLine holds
// no socket when idle, so "idle = no sockets" is literally true: the listener binds only
// when the guest raises DTR to enable the modem (the confirmed design choice), and every
// socket drops on DTR-drop / hangup.
// ---------------------------------------------------------------------------

// Is the live line our ModemLine? A CONNECTed endpoint replaces it (modem_ is cleared
// then, so no dangle), and self-test pockets it behind a loopback plug -- in both cases
// the modem semantics are inert and modemStatus() reverts to the fixed stub.
bool PmmiBoard::modemActive() const {
    return modem_ != nullptr && !selfTestEngaged() &&
           static_cast<ByteStream*>(modem_) == &const_cast<PmmiBoard*>(this)->lineStream();
}

// Build (or rebuild) the ModemLine from the current dial/answer config and install it as
// the line -- or tear it back down to a NullStream when neither is configured (so the
// state=="null" contract and the fixed-stub fallback both hold). A rebuild starts idle:
// config is declarative and applied before the machine runs, so there is no live call to
// preserve. attachStream() routes it through the self-test pocketing exactly as CONNECT
// does.
void PmmiBoard::syncModem() {
    if (canDial() || canAnswer()) {
        auto ml = std::make_unique<ModemLine>(dialHost_, dialPort_, answerPort_);
        modem_  = ml.get();
        attachStream(std::move(ml));
    } else if (modem_) {
        modem_ = nullptr;
        attachStream(std::make_unique<NullStream>());
    }
    resetModemState();
    refreshModem();
}

// AP goes -- and STAYS -- low on (SH|RI) & DTR (§7.4.4.5). The latch is the point: a
// correct guest resets SH/RI 51 ms after CTS (so automatic disconnect still works), and
// AP must hold the line off-hook across that reset. Only DTR-drop or a lost-carrier
// timeout clears it (in decodeControl3 / refreshModem).
void PmmiBoard::latchAp() {
    if ((out0_ & (kSh | kRi)) && (out3_ & kDtr)) apLow_ = true;
}

// SH/RI edges (OUT BA+0). Answering is the load-bearing one: RI 0->1 while the line
// rings picks up the call. Origination normally waits for DTR (decodeControl3), but if
// DTR is already up we dial on the SH edge too, so either write order works.
void PmmiBoard::decodeControl0(uint8_t prev) {
    if (modemActive()) {
        std::string e;
        bool riRose = (out0_ & kRi) && !(prev & kRi);
        bool shRose = (out0_ & kSh) && !(prev & kSh);

        if (riRose && modem_->ringing()) {  // ANSWER the ringing line (§7.4.4.2)
            modem_->answer();
            modeOriginate_ = false;
        }
        // ORIGINATE, if the modem is already enabled and this SH edge is not an answer.
        if (shRose && (out3_ & kDtr) && canDial() && !(out0_ & kRi) &&
            !modem_->ringing() && !modem_->connecting() && !modem_->carrier()) {
            modem_->dial(e);
            modeOriginate_ = true;
        }
        latchAp();
    }
    refreshModem();
}

// DTR edge (OUT BA+3). DTR IS the modem enable and the disconnect control (§7.3.4.7):
//  - 0->1 arms auto-answer (the only thing that opens a socket on an idle modem) and, if
//    the guest is already off-hook to originate, places the call.
//  - 1->0 is the authoritative on-hook: drop the call AND the listener, back to no
//    sockets. (SH going to 0 is NOT a hangup here -- a correct guest clears it after CTS
//    and pulses it while dialing; DTR is the wire that hangs up.)
void PmmiBoard::decodeControl3(uint8_t prev) {
    if (modemActive()) {
        std::string e;
        bool dtrRose = (out3_ & kDtr) && !(prev & kDtr);
        bool dtrFell = !(out3_ & kDtr) && (prev & kDtr);

        if (dtrRose) {
            if (canAnswer()) modem_->armAnswer(e);  // bind the listener (idempotent)
            if ((out0_ & kSh) && canDial() && !modem_->ringing() &&
                !modem_->connecting() && !modem_->carrier()) {
                modem_->dial(e);  // originate now that the modem is enabled
                modeOriginate_ = true;
            }
        }
        if (dtrFell) {  // ON-HOOK. Everything drops.
            modem_->hangup();
            apLow_       = false;
            ctsClearAt_  = 0;
            apResetAt_   = 0;
            hsTimeoutAt_ = 0;
        }
        latchAp();
    }
    refreshModem();
}

// THE HANDSHAKE / RING STATE MACHINE. Polls the ModemLine, advances the latched bits on
// their Clock deadlines, and re-arms the single earliest one -- cancel-before-rearm, the
// 2SIO's discipline (there is never more than one PMMI timer on the books). The status
// BITS are computed live in modemStatus(); this owns the edges and the deadlines.
void PmmiBoard::refreshModem() {
    if (!clock_) return;
    if (!modemActive()) {  // no line to service: make sure no stale deadline survives
        clock_->cancel(wake_);
        wake_ = Clock::kNone;
        return;
    }

    uint64_t now = clock_->now();
    uint64_t tps = clock_->tStatesPer(1000);  // T-states per millisecond

    // CARRIER edge. Rising: the far end answered -> arm CTS clear (billing+450 answer,
    // 750 originate) and cancel the no-handshake timeout. Falling: CTS is no longer
    // clear, and AP begins its ~1.5 s reset.
    bool car = modem_->carrier();
    if (car && !carrierPrev_) {
        long long ms = modeOriginate_ ? kOriginateCtsMs : kAnswerCtsMs;
        ctsClearAt_  = now + (uint64_t)ms * tps;
        hsTimeoutAt_ = 0;
    } else if (!car && carrierPrev_) {
        ctsClearAt_ = 0;
        if (apLow_) apResetAt_ = now + (uint64_t)kApResetMs * tps;
    }
    carrierPrev_ = car;

    // RING start edge: mark the burst-phase origin so the Ringing bit toggles from here.
    bool rng = modem_->ringing();
    if (rng && !ringingPrev_) ringStart_ = now;
    ringingPrev_ = rng;

    // Deadlines that have come due.
    if (apResetAt_ != 0 && now >= apResetAt_) {
        apLow_     = false;
        apResetAt_ = 0;
    }
    if (hsTimeoutAt_ != 0 && now >= hsTimeoutAt_) {  // 17 s, handshake never completed
        modem_->hangup();
        apLow_       = false;
        hsTimeoutAt_ = 0;
        ctsClearAt_  = 0;
    }

    // Off-hook for data but no carrier yet: arm the 17 s no-handshake hangup (the failure
    // path -- the far end never answered). Cleared the moment carrier appears, above.
    if (apLow_ && !car && ctsClearAt_ == 0) {
        if (hsTimeoutAt_ == 0) hsTimeoutAt_ = now + (uint64_t)kHsTimeoutMs * tps;
    } else if (car) {
        hsTimeoutAt_ = 0;
    }

    // Re-arm the earliest FUTURE deadline. A past CTS-clear needs no timer -- the bit is
    // already clear when read.
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    uint64_t next = 0;
    auto     consider = [&](uint64_t t) {
        if (t > now && (next == 0 || t < next)) next = t;
    };
    consider(ctsClearAt_);
    consider(apResetAt_);
    consider(hsTimeoutAt_);
    if (rng) consider(nextRingEdge(now));
    if (next) wake_ = clock_->at(next, [this] { refreshModem(); });
}

// Back to on-hook / idle: the state a fresh line and a restored machine both start from.
// Does NOT touch the ModemLine's sockets (syncModem/deserialize handle that) or the
// wake_ handle (refreshModem re-arms).
void PmmiBoard::resetModemState() {
    apLow_         = false;
    modeOriginate_ = true;
    ctsClearAt_    = 0;
    ringStart_     = 0;
    apResetAt_     = 0;
    hsTimeoutAt_   = 0;
    carrierPrev_   = false;
    ringingPrev_   = false;
}

// Inside a ring burst's 'on' half? The cadence origin is ringStart_; the bit is 0
// (ringing) for the first kRingOnMs of each kRingOnMs+kRingOffMs cycle.
bool PmmiBoard::ringBurstOn(uint64_t now) const {
    const Clock& clk    = clock_ ? *clock_ : deadCard();
    uint64_t     tps    = clk.tStatesPer(1000);
    uint64_t     on     = (uint64_t)kRingOnMs * tps;
    uint64_t     period = (uint64_t)(kRingOnMs + kRingOffMs) * tps;
    if (period == 0) return true;
    uint64_t phase = (now - ringStart_) % period;
    return phase < on;
}

// Absolute T-state of the next ring-burst edge -- the transition a ring-counting guest
// (or, later, a ring interrupt) watches for.
uint64_t PmmiBoard::nextRingEdge(uint64_t now) const {
    const Clock& clk    = clock_ ? *clock_ : deadCard();
    uint64_t     tps    = clk.tStatesPer(1000);
    uint64_t     on     = (uint64_t)kRingOnMs * tps;
    uint64_t     period = (uint64_t)(kRingOnMs + kRingOffMs) * tps;
    if (period == 0) return 0;
    uint64_t elapsed = now - ringStart_;
    uint64_t base    = ringStart_ + (elapsed / period) * period;
    uint64_t phase   = elapsed % period;
    return phase < on ? base + on : base + period;
}

// IN BA+2 bit 7 -- the rate generator's timer pulse: 250,000/(N*100) Hz at 40% high /
// 60% low, where N is the OUT BA+2 divisor (§6). N=0 means the divider is not loaded, so
// the bit is quiet.
bool PmmiBoard::timerPulseHigh(uint64_t now) const {
    if (out2_ == 0) return false;
    const Clock& clk     = clock_ ? *clock_ : deadCard();
    long long    timerHz = 250000 / ((long long)out2_ * 100);
    if (timerHz <= 0) return false;
    uint64_t period = clk.tStatesPer(timerHz);  // T-states per timer cycle
    if (period == 0) return false;
    return (now % period) < period * 40 / 100;  // high for the first 40%
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

void PmmiBoard::reset(Reset) {
    if (!clock_) return;
    u_.masterReset(*clock_);  // MR: clears the UART's receiver/flags, keeps the line

    // The write-only control registers clear on power-on-clear. The endpoint STAYS
    // CONNECTED (masterReset is careful about that) -- a warm reset does not unplug the
    // line.
    out0_ = out2_ = out3_ = 0;

    // DTR is now clear, so any self-test loopback tears down here and the pocketed
    // phone line comes back onto the pins -- the shadow and the live wire stay in step.
    updateSelfTest();

    // DTR is clear, so the modem is disabled: hang up and go idle, dropping any call and
    // listener. The state machine restarts on-hook, in step with the cleared shadows.
    if (modem_) modem_->hangup();
    resetModemState();
    refreshModem();
}

void PmmiBoard::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1). Pump the line, then let
// the state machine see any ring / carrier the pump just brought in.
void PmmiBoard::pump() {
    u_.pump();
    refreshModem();
}

// A jumper moved or the line was reconfigured (dial/answer rebuilt it): re-aim the timer.
void PmmiBoard::configChanged() {
    decodeChanged();  // `port` may have moved the card in the I/O space
    refreshModem();
}

void PmmiBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    u_.serialize(w);
    w.u8(out0_);
    w.u8(out2_);
    w.u8(out3_);
}

void PmmiBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    u_.deserialize(r);
    out0_ = r.u8();
    out2_ = r.u8();
    out3_ = r.u8();

    // Re-derive the loopback from the restored control latch, exactly as the 6860 would
    // come out of restore in the self-test state its ST bit says. RESTORE runs on the
    // already-connected machine, so u_.stream() here is the real line -- the one this
    // pockets. (savedLine_ is a synthesized plug and never travels.)
    updateSelfTest();

    // A LIVE CALL CANNOT BE RESTORED (a socket handle is not serializable -- DESIGN.md 13).
    // The ModemLine was rebuilt idle from the TOML dial/answer config; drop any call the
    // running machine had and come back ON-HOOK, even though the restored SH/DTR shadows
    // say off-hook. This is the honest analogue of pulling the line during a save: the
    // guest polls AP high, no CTS, and redials -- mirroring how self-test is re-derived
    // from the restored control latch rather than travelling as live state.
    if (modem_) modem_->hangup();
    resetModemState();
    refreshModem();
}

// ---------------------------------------------------------------------------
// SHOW -- the read-only status strings. Capitals = asserted, the Mc6850 `lines` idiom.
// ---------------------------------------------------------------------------

std::string PmmiBoard::frameString() const {
    char p = (u_.parity == LineParity::Odd)    ? 'O'
             : (u_.parity == LineParity::Even) ? 'E'
                                               : 'N';
    return std::to_string(u_.dataBits) + p + std::to_string(u_.stopBits);
}

std::string PmmiBoard::uartString() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();
    std::string  s;
    s += u_.txBufferEmpty(clk) ? "TBMT " : "tbmt ";
    s += u_.dataAvailable() ? "DAV" : "dav";
    return s;
}

std::string PmmiBoard::linesString() const {
    uint8_t     ms = modemStatus();
    std::string s;
    s += (out0_ & kSh) ? "SH " : "sh ";          // switch hook -- off-hook / originate
    s += (out0_ & kRi) ? "RI " : "ri ";          // ring indicator -- answer mode
    s += (out3_ & kDtr) ? "DTR " : "dtr ";       // data terminal ready -- modem enabled
    s += selfTestEngaged() ? "ST " : "st ";      // 6860 self-test -- the line looped on itself
    s += (ms & kMsDialTone) ? "dt " : "DT ";     // active low: 0 = dial tone present
    s += (ms & kMsRinging) ? "ring " : "RING ";  // active low: 0 = ringing
    s += (ms & kMsCts) ? "cts " : "CTS ";        // active low: 0 = clear to send
    s += (ms & kMsAp) ? "ap" : "AP";             // active low: 0 = off-hook
    return s;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------

std::vector<Property> PmmiBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address -- the 6-position DIP. Four ports at BASE..BASE+3; "
                  "MUST be on a 4-port boundary. Default C0 (North Star alternative E0)";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFC;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            // The board occupies four ports and the decode uses A0-A1 to pick the
            // register, so the base must be a multiple of four -- an off-boundary base
            // is not a card you could strap.
            if (v.i() & 3) {
                err = "the PMMI occupies BASE..BASE+3 -- the base must be a multiple of 4";
                return false;
            }
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the phone line (CONNECT sets this). in:/out: a file, ...";
        x.kind = Kind::Str;
        // Report the REAL line even mid-self-test (lineStream), never the internal plug.
        x.get  = [this] { return Value::ofStr(lineStream().describe()); };
        // Route through connect() so a declarative `[pmmi0.unit.line] connect = "out:x"`
        // rebases its PATH the same way the CONNECT command does -- one path, one rule.
        x.set  = [this](const Value& v, std::string& err) { return connect("line", v.s(), err); };
        p.push_back(std::move(x));
    }
    // ---- THE MODEM'S TWO KNOBS. dial=/answer= turn the line into a ModemLine and pick
    // the far end / the answer port; the guest's SH/RI/DTR then drive it. `dial` and
    // `answer` name the intent, so they take a bare `host:port` / `port` -- validated
    // through the grammar primitives (host/endpoint.h), which a board must not re-learn. ----
    {
        Property x;
        x.name = "dial";
        x.help = "Originate target host:port (empty = cannot dial). SH off-hook + DTR "
                 "dials it; the guest's pulse digits are not decoded";
        x.kind = Kind::Str;
        x.get  = [this] {
            return Value::ofStr(canDial() ? dialHost_ + ":" + std::to_string(dialPort_) : "");
        };
        x.set  = [this](const Value& v, std::string& err) {
            std::string s = v.s();
            if (s.empty()) {
                dialHost_.clear();
                dialPort_ = 0;
                syncModem();
                return true;
            }
            std::string host;
            uint16_t    port = 0;
            if (!parseHostPort(s, host, port, err)) return false;
            dialHost_ = host;
            dialPort_ = port;
            syncModem();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "answer";
        x.help = "Auto-answer TCP port (empty/0 = will not answer). DTR arms the listener; "
                 "an inbound call RINGS until the guest answers with RI";
        x.kind = Kind::Str;
        x.get  = [this] {
            return Value::ofStr(canAnswer() ? std::to_string(answerPort_) : "");
        };
        x.set  = [this](const Value& v, std::string& err) {
            std::string s = v.s();
            if (s.empty() || s == "0") {
                answerPort_ = 0;
                syncModem();
                return true;
            }
            uint16_t port = 0;
            if (!parsePort(s, port)) {
                err = "answer needs a TCP port 1..65535";
                return false;
            }
            answerPort_ = port;
            syncModem();
            return true;
        };
        p.push_back(std::move(x));
    }
    // ---- READ-ONLY STATUS. A property with no setter is a PIN, not a jumper: SHOW
    // renders it `(read-only)`. This is the whole of `SHOW <id>`'s status view. ----
    {
        Property x;
        x.name = "frame";
        x.help = "Live UART frame (read-only), e.g. 8N1. Set by OUT BA+0 bits 2-6";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(frameString()); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "baud";
        x.help  = "Live line rate (read-only), 250000/(16*N) from the OUT BA+2 divisor";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(u_.baud); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "uart";
        x.help = "Live UART status (read-only). CAPITALS = asserted: TBMT DAV";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(uartString()); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "lines";
        x.help = "Live modem lines (read-only). CAPITALS = asserted: SH RI DTR ST DT RING "
                 "CTS AP (DT/RING/CTS/AP from the phone line + handshake state machine)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(linesString()); };
        p.push_back(std::move(x));
    }
    return p;
}

// One serial unit -- the phone line. The unit exists because CONNECT names one. The
// state is the REAL line's, so a transient self-test loopback does not rewrite what
// SHOW and CONFIG SAVE report the operator connected.
std::vector<UnitDef> PmmiBoard::units() const {
    std::string ep = savedLine_ ? savedLine_->describe() : u_.endpoint();
    return {{"line", UnitKind::Serial, ep}};
}

std::vector<MapEntry> PmmiBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "PMMI -- UART status (R) / format + SH,RI + int-enable (W)"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "read/write",
         "PMMI -- receive data (R) / transmit data (W)"},
        {(uint32_t)base_ + 2, (uint32_t)base_ + 2, "read/write",
         "PMMI -- modem status (R) / rate generator + int-mask staging (W)"},
        {(uint32_t)base_ + 3, (uint32_t)base_ + 3, "read/write",
         "PMMI -- strobe, floats 0xFF (R) / 6860 modem control (W)"},
    };
}

bool PmmiBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "line") {
        err = "pmmi has no unit '" + unit + "' -- it has one, and it is called 'line'";
        return false;
    }
    // The modem advertises its own line as "modem:...", and CONFIG SAVE emits that through
    // the `connect` property. The dial=/answer= properties are what actually build the
    // ModemLine, so this spec is a NO-OP -- recognizing it keeps CONFIG SAVE round-tripping
    // without asking the resolver to parse a grammar it must never know (DESIGN.md 7.7),
    // and it does so regardless of whether `connect` is reloaded before or after dial/answer.
    if (ep.rfind("modem:", 0) == 0) return true;

    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens. The `connect` unit property routes here too, so both the CONNECT
    // command and a declarative connect are covered.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(ep, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    // A resolved endpoint REPLACES the modem: the phone line and a CONNECTed test line are
    // mutually-exclusive uses of the one serial unit (fork 3/6). Drop the modem_ pointer
    // so it can never dangle, and modemStatus() reverts to the fixed stub.
    modem_ = nullptr;
    attachStream(std::move(s));
    refreshModem();  // tears down any outstanding handshake deadline
    return true;
}

bool PmmiBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "line") {
        err = "pmmi has no unit '" + unit + "' -- it has one, and it is called 'line'";
        return false;
    }
    modem_ = nullptr;
    attachStream(std::make_unique<NullStream>());
    refreshModem();
    return true;
}

} // namespace altair
