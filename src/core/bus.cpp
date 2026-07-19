#include "core/bus.h"

#include "core/board.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace altair {

void Bus::attach(Board* b) {
    boards_.push_back(b);
    b->attachBus(this);
    // It may ALREADY be pulling pin 73 -- a card does not stop asking to be
    // serviced because you moved it to another slot, and a card built on the bench
    // arrives with its pins in whatever state it left them in. (intWire() already
    // accounts for enabled: a disabled card drives nothing, and says so when it is
    // disabled, not when it is asked.)
    if (b->intWire()) ++intCount_;
    if (b->holdWire()) ++holdCount_;  // ...and it may arrive already pulling pHOLD
    viWireChanged(0, b->viWire());  // ...and whatever VI lines it arrived pulling
    invalidateDecode();  // a card went into a slot: the wiring changed
}

void Bus::detach(Board* b) {
    boards_.erase(std::remove(boards_.begin(), boards_.end(), b), boards_.end());
    if (b->intWire()) --intCount_;  // and it takes its interrupt out with it
    if (b->holdWire()) --holdCount_;  // ...and its pHOLD request comes out too
    viWireChanged(b->viWire(), 0);  // ...and its VI lines go with it
    b->attachBus(nullptr);
    invalidateDecode();  // ...and the wiring changed again when it came out
}

bool Bus::anyAssertsPhantom(const BusCycle& c) const {
    for (Board* b : boards_)
        if (b->enabled() && b->assertsPhantom(c)) return true;
    return false;
}

std::vector<Board*> Bus::decoders(const BusCycle& c) const {
    std::vector<Board*> out;
    for (Board* b : boards_)
        if (b->enabled() && b->decodes(c)) out.push_back(b);
    return out;
}

Bus::Decode Bus::scan(const BusCycle& c) const {
    Decode d;
    for (Board* b : boards_)
        if (b->enabled() && b->decodes(c)) {
            if (!d.first) d.first = b;
            ++d.n;
        }
    return d;
}

std::vector<Board*> Bus::respondersTo(const BusCycle& in) const {
    BusCycle c = in;
    c.phantom = anyAssertsPhantom(c);
    return decoders(c);
}

uint8_t Bus::peek(uint16_t addr) const {
    BusCycle c{Cycle::MemRead, addr, 0, false};
    c.phantom = anyAssertsPhantom(c);
    for (Board* b : decoders(c)) {
        uint8_t v = 0xFF;
        if (b->peek(addr, v)) return v;
    }
    return 0xFF;  // nobody could answer without side effects. Neither can we.
}

static const char* cycleName(Cycle t) {
    switch (t) {
    case Cycle::MemRead: return "read";
    case Cycle::MemWrite: return "write";
    case Cycle::IoRead: return "IN";
    case Cycle::IoWrite: return "OUT";
    case Cycle::IntAck: return "INTA";
    }
    return "?";
}

void Bus::reportContention(const BusCycle& c, const std::vector<Board*>& who) {
    if (policy_ == Contention::Silent) return;

    char buf[160];
    bool io = (c.type == Cycle::IoRead || c.type == Cycle::IoWrite);
    if (io)
        std::snprintf(buf, sizeof buf, "CONTENTION: port 0x%02X (%s) driven by", c.port(),
                      cycleName(c.type));
    else
        std::snprintf(buf, sizeof buf, "CONTENTION: 0x%04X (%s) driven by", c.addr,
                      cycleName(c.type));

    std::string m = buf;
    for (Board* b : who) m += " " + b->id;
    // Two boards both actually driving is a real electrical fault, and a real
    // backplane would hand you exactly this bug. We report it; we do not pick a
    // winner. Picking a winner is how a simulator lies to you.
    m += "  -- both boards drive. The bus does not arbitrate (DESIGN.md 4.6).";
    log_.push_back(m);
}

// THE CYCLE HANDED TO settle() IS THE FINISHED ONE, AND `data` IS ALWAYS VALID HERE.
//
// BusCycle::data is documented as "valid on writes", and during the cycle that is
// true -- on a READ nobody has driven the bus yet when decodes() and read() are
// asked. But settle() runs AFTER the cycle completes (board.h, snoop()), and by then
// a byte HAS been driven: by the board that answered, or by the floating bus, which
// drives 0xFF just as surely. So every read path back-fills `data` with what came
// back before calling this.
//
// That is not a convenience for the snoopers. It is what the wire is doing. The
// front panel's DATA lamps are eight LEDs soldered to D0..D7 and they do not care
// which direction the byte was going -- and neither does a TRACE, which is why the
// observers get the same corrected cycle.
void Bus::settle(const BusCycle& c) {
    // ONLY the cards that actually watch. Every board still SEES every cycle --
    // that is what a backplane is -- but a card with nothing wired to the address
    // bus latches nothing, and calling its do-nothing snoop() to discover that,
    // once per card per cycle, was pure ceremony. wantsSnoop() is the card telling
    // us it has a flip-flop on that bus. Almost none do.
    for (Board* b : snoopers_)
        if (b->enabled()) b->snoop(c);

    // And anyone watching from OUTSIDE the backplane -- the debugger, the tracer.
    // They see exactly what the cards see, because it is the same stream. That is
    // why BREAK IO and BREAK MEM needed no new machinery and cost no CPU support:
    // a bus cycle is a bus cycle no matter who originated it, so they work against
    // a DMA transfer and a front-panel DEPOSIT, not just against the processor.
    for (const auto& o : observers_) o.second(c);
}

int Bus::observe(Observer fn) {
    int h = nextObserver_++;
    observers_.emplace_back(h, std::move(fn));
    return h;
}

void Bus::unobserve(int handle) {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [&](const auto& o) { return o.first == handle; }),
                     observers_.end());
}

// READ PIN 73. Is anybody pulling it down?
//
// Not a question we ask the boards -- they TOLD us, when it changed. That is what
// a wire is, and it is the whole difference between this and a survey.
bool Bus::intPending() const {
    if (verify_) verifyInt();
    return intCount_ > 0;
}

bool Bus::holdPending() const {
    if (verify_) verifyHold();
    return holdCount_ > 0;
}

// The proof for pHOLD, the exact counterpart of verifyInt(): re-derive the wire from
// every board's requestsBus() and abort on the first board -- or the count -- that
// disagrees with what we cached. A board that starts or stops wanting the bus and
// forgets to call holdChanged() would not hang the guest the way a stale pin 73 does;
// it would do something quieter -- the transfer never runs, because the fast wire said
// nobody asked -- and that is the kind of silence this mode exists to break.
void Bus::verifyHold() const {
    int live = 0;
    for (Board* b : boards_) {
        bool actual = b->enabled() && b->requestsBus();
        if (b->holdWire()) ++live;
        if (actual == b->holdWire()) continue;

        std::fprintf(stderr,
                     "\npHOLD WIRE IS STALE -- board '%s'\n"
                     "  the wire says: %s\n"
                     "  the board says: %s\n"
                     "It changed its bus-request state and did not call holdChanged().\n",
                     b->id.c_str(), b->holdWire() ? "pulling" : "not pulling",
                     actual ? "pulling" : "not pulling");
        std::abort();
    }

    if (live != holdCount_) {
        std::fprintf(stderr,
                     "\npHOLD WIRE-OR COUNT IS WRONG\n"
                     "  the bus thinks %d board(s) are pulling pin 74\n"
                     "  %d actually are\n"
                     "A board went into or out of the backplane without the count following.\n",
                     holdCount_, live);
        std::abort();
    }
}

// READ VI0-VI7. Which of the eight are being pulled down?
//
// Same wire, same rules, same cache. The bus reports the lines and stops there; an
// 88-VI decides what they MEAN.
// It does NOT verify here, and that is deliberate: an 88-VI reads this from inside
// its own assertsInt(), which verifyInt() is in the middle of calling. Verifying the
// VI wires from verifyInt() instead (where it happens once per instruction) checks
// exactly the same thing without a card being asked to prove itself mid-answer.
uint8_t Bus::viLines() const { return viMask_; }

// A VI line moved. Tell the cards that watch them -- an 88-VI's pin 73 is a function
// of these eight wires, and it was not the one who moved them.
void Bus::notifyViWatchers() {
    if (inViNotify_) return;
    inViNotify_ = true;
    for (Board* b : boards_)
        if (b->watchesVi()) b->intChanged();
    inViNotify_ = false;
}

// The proof, and the exact counterpart of verifySlot(). Re-derive the wire from
// every board's combinational assertsInt() and compare it to what each board says
// it is driving, and to the running count.
//
// A board that changed its interrupt state and forgot to call intChanged() dies
// HERE, loudly, at the next instruction -- rather than hanging the guest in a HLT
// forever, waiting for an interrupt that already happened and was never carried.
// That is a bug worth days, and it would present as "the emulator locks up
// sometimes", which is worth several more.
void Bus::verifyInt() const {
    int live = 0;
    for (Board* b : boards_) {
        bool actual = b->enabled() && b->assertsInt();
        if (b->intWire()) ++live;
        if (actual == b->intWire()) continue;

        std::fprintf(stderr,
                     "\npINT WIRE IS STALE -- board '%s'\n"
                     "  the wire says: %s\n"
                     "  the board says: %s\n"
                     "It changed its interrupt state and did not call intChanged().\n",
                     b->id.c_str(), b->intWire() ? "pulling" : "not pulling",
                     actual ? "pulling" : "not pulling");
        std::abort();
    }

    // ...and the count itself, which attach()/detach() maintain by hand. Two
    // boards whose errors cancel would sail past the loop above; a bad count would
    // not, and this is the only place either could be caught.
    if (live != intCount_) {
        std::fprintf(stderr,
                     "\npINT WIRE-OR COUNT IS WRONG\n"
                     "  the bus thinks %d board(s) are pulling pin 73\n"
                     "  %d actually are\n"
                     "A board went into or out of the backplane without the count following.\n",
                     intCount_, live);
        std::abort();
    }

    // Pin 73 is not the only interrupt wire any more, and a VI line that goes stale
    // hangs the guest in exactly the same way. Checked from here so it is checked
    // every instruction (intPending() is), not only when an 88-VI happens to look.
    verifyVi();
}

// The same proof, eight wires wide.
void Bus::verifyVi() const {
    int live[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (Board* b : boards_) {
        uint8_t actual = b->enabled() ? b->assertsVi() : 0;
        for (int i = 0; i < 8; ++i)
            if (b->viWire() & (1u << i)) ++live[i];
        if (actual == b->viWire()) continue;

        std::fprintf(stderr,
                     "\nVI WIRES ARE STALE -- board '%s'\n"
                     "  the wires say: %02X\n"
                     "  the board says: %02X\n"
                     "It changed its interrupt state and did not call intChanged().\n",
                     b->id.c_str(), b->viWire(), actual);
        std::abort();
    }

    for (int i = 0; i < 8; ++i) {
        if (live[i] == viCount_[i]) continue;
        std::fprintf(stderr,
                     "\nVI%d WIRE-OR COUNT IS WRONG\n"
                     "  the bus thinks %d board(s) are pulling it\n"
                     "  %d actually are\n"
                     "A board went into or out of the backplane without the count following.\n",
                     i, viCount_[i], live[i]);
        std::abort();
    }
}

// ---------------------------------------------------------------------------
// The cycle. This is the whole bus.
//
//   pass 1  resolve PHANTOM* -- ask every board whether it pulls the pin
//   pass 2  decode          -- ask every board whether it drives; move the byte
//   pass 3  settle          -- show the finished cycle to every board, so any
//                             card watching the address bus can latch what it saw
//
// Pass 3 exists because a real card can watch a cycle it does not answer, and
// the Tarbell does exactly that. It is not a callback and it is not the bus
// telling anyone anything: the cycle was on the backplane, in front of every
// card, the entire time.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// THE EXACT PATH. This is the definition of correctness, and it is the code that
// was here before -- three passes, every board asked, nothing cached. The tables
// below are a CACHE OF THIS, and the verifier checks them AGAINST it.
//
// A read is answered by the FIRST board in slot order that drives it. A write is
// latched by EVERY board that drives it -- two cards both latching the same write
// is a real fault and we reproduce it rather than pick a winner.
//
// The n > 1 arm re-asks decoders(). That calls decodes() a second time, which is
// free of consequence because decodes() is COMBINATIONAL AND PURE by contract
// (board.h) -- and it happens only when the machine is already electrically
// broken and we are on our way to printing a paragraph about it.
// ---------------------------------------------------------------------------

uint8_t Bus::memReadExact(uint16_t addr) {
    BusCycle c{Cycle::MemRead, addr, 0, false};
    c.phantom = anyAssertsPhantom(c);

    Decode d = scan(c);
    if (d.n > 1) reportContention(c, decoders(c));

    unclaimed_ = (d.n == 0);
    contended_ = (d.n > 1);
    uint8_t v = d.first ? d.first->read(c)
                        : 0xFF;  // floating bus (DESIGN.md 4.6.1)
    c.data = v;                  // what got driven -- see settle()
    settle(c);
    return v;
}

void Bus::memWriteExact(uint16_t addr, uint8_t data) {
    BusCycle c{Cycle::MemWrite, addr, data, false};
    c.phantom = anyAssertsPhantom(c);

    Decode d = scan(c);
    if (d.n > 1) reportContention(c, decoders(c));

    unclaimed_ = (d.n == 0);
    contended_ = (d.n > 1);
    // Nobody latched it. The byte is simply gone -- the write half of the
    // floating bus. This is what a guest write to ROM does, and the bus needed
    // no rule about ROM to make it happen.
    if (d.n == 1) d.first->write(c);
    else if (d.n > 1) for (Board* b : decoders(c)) b->write(c);
    settle(c);
}

uint8_t Bus::ioReadExact(uint8_t port) {
    BusCycle c{Cycle::IoRead, port, 0, false};
    Decode d = scan(c);
    if (d.n > 1) reportContention(c, decoders(c));
    unclaimed_ = (d.n == 0);
    contended_ = (d.n > 1);
    uint8_t v = d.first ? d.first->read(c) : 0xFF;
    c.data = v;  // what got driven -- see settle()
    settle(c);
    return v;
}

void Bus::ioWriteExact(uint8_t port, uint8_t data) {
    BusCycle c{Cycle::IoWrite, port, data, false};
    Decode d = scan(c);
    if (d.n > 1) reportContention(c, decoders(c));
    unclaimed_ = (d.n == 0);
    contended_ = (d.n > 1);
    if (d.n == 1) d.first->write(c);
    else if (d.n > 1) for (Board* b : decoders(c)) b->write(c);
    settle(c);
}

// ---------------------------------------------------------------------------
// THE CACHE. Built by asking the boards -- the same two questions, in slot order.
// ---------------------------------------------------------------------------

Bus::Slot Bus::resolve(Cycle t, uint16_t addr) const {
    BusCycle c{t, addr, 0, false};
    c.phantom = anyAssertsPhantom(c);  // pass 1, exactly as a live cycle does it
    Decode d = scan(c);                // pass 2, likewise

    Slot s;
    s.phantom = c.phantom;
    s.who     = d.first;
    s.slow    = (d.n > 1);  // contention: send it down the exact path, which reports it
    return s;
}

void Bus::rebuild() {
    dirty_ = false;

    // IS ANY CARD IN THIS MACHINE DECODING A LOW ADDRESS LINE?
    //
    // Almost never. But the Tarbell gates its PROM and its PHANTOM* with A5, so it
    // answers 0x0000-0x001F and not 0x0020-0x003F -- two answers inside page 0. We
    // do not ASSUME our way past that and we do not pick a finer page size and hope
    // (the next such card will decode A0). We ask, and if the answer is yes we stop
    // guessing and PROBE.
    bool uniform = true;
    for (Board* b : boards_)
        if (!b->decodeIsPageUniform()) uniform = false;

    for (int p = 0; p < 256; ++p) {
        uint16_t a   = (uint16_t)(p << 8);
        memRead_[p]  = resolve(Cycle::MemRead, a);
        memWrite_[p] = resolve(Cycle::MemWrite, a);

        if (uniform) continue;

        // Probe all 256 addresses of the page. If they do not agree, the page has
        // no single answer, so it gets NO cached answer: it is served by the exact
        // two-pass path forever, which is the code that was always here.
        //
        // This costs a few milliseconds, it happens only on a rebuild (an operator
        // action, not a guest one), and only in a machine that actually contains
        // such a card. The Tarbell costs us ONE page out of 256.
        for (int lo = 1; lo < 256; ++lo) {
            uint16_t x = (uint16_t)(a | lo);
            Slot r = resolve(Cycle::MemRead, x);
            Slot w = resolve(Cycle::MemWrite, x);
            if (r.who != memRead_[p].who || r.phantom != memRead_[p].phantom ||
                r.slow != memRead_[p].slow)
                memRead_[p].slow = true;
            if (w.who != memWrite_[p].who || w.phantom != memWrite_[p].phantom ||
                w.slow != memWrite_[p].slow)
                memWrite_[p].slow = true;
        }
    }

    // I/O: all 256 ports, each probed exactly. No contract, no assumption -- a
    // card may decode any port it pleases, including one.
    for (int p = 0; p < 256; ++p) {
        ioRead_[p]  = resolve(Cycle::IoRead, (uint16_t)p);
        ioWrite_[p] = resolve(Cycle::IoWrite, (uint16_t)p);
    }

    snoopers_.clear();
    for (Board* b : boards_)
        if (b->wantsSnoop()) snoopers_.push_back(b);
}

// The proof. Re-derive the decode the slow way and compare it to what we cached.
// A board that changed its decode without saying so dies HERE, loudly, at the
// first cycle that touches it -- instead of somewhere downstream, quietly, in a
// month. Costs more than the original code; it is not a path, it is an assertion.
void Bus::verifySlot(const BusCycle& in, const Slot& s) const {
    BusCycle c = in;
    c.phantom = anyAssertsPhantom(c);
    Decode d = scan(c);

    bool ok = (d.first == s.who) && (c.phantom == s.phantom) && ((d.n > 1) == s.slow);
    if (ok) return;

    std::fprintf(stderr,
                 "\nBUS DECODE CACHE IS STALE at %s 0x%04X\n"
                 "  cached: who=%s phantom=%d slow=%d\n"
                 "  actual: who=%s phantom=%d n=%d\n"
                 "A board changed its decode and did not call decodeChanged().\n",
                 cycleName(c.type), c.addr, s.who ? s.who->id.c_str() : "-", (int)s.phantom,
                 (int)s.slow, d.first ? d.first->id.c_str() : "-", (int)c.phantom, d.n);
    std::abort();
}

// ---------------------------------------------------------------------------
// THE CYCLE. A table lookup, and the board that the table names.
// ---------------------------------------------------------------------------

uint8_t Bus::memRead(uint16_t addr) {
    if (dirty_) rebuild();
    const Slot& s = memRead_[addr >> 8];

    // A slow page has no cached answer to check -- that is what makes it slow.
    if (s.slow) return memReadExact(addr);  // contention, or a non-uniform page.
    if (verify_) verifySlot(BusCycle{Cycle::MemRead, addr, 0, false}, s);

    BusCycle c{Cycle::MemRead, addr, 0, s.phantom};
    unclaimed_ = (s.who == nullptr);
    uint8_t v = s.who ? s.who->read(c) : 0xFF;  // floating bus (DESIGN.md 4.6.1)
    c.data = v;                                 // what got driven -- see settle()
    settle(c);
    return v;
}

void Bus::memWrite(uint16_t addr, uint8_t data) {
    if (dirty_) rebuild();
    const Slot& s = memWrite_[addr >> 8];

    if (s.slow) { memWriteExact(addr, data); return; }
    if (verify_) verifySlot(BusCycle{Cycle::MemWrite, addr, data, false}, s);

    BusCycle c{Cycle::MemWrite, addr, data, s.phantom};
    unclaimed_ = (s.who == nullptr);
    // Nobody latched it. The byte is simply gone -- the write half of the floating
    // bus, and what a guest write to ROM does. No rule about ROM was needed.
    if (s.who) s.who->write(c);
    settle(c);
}

uint8_t Bus::ioRead(uint8_t port) {
    if (dirty_) rebuild();
    const Slot& s = ioRead_[port];

    if (s.slow) return ioReadExact(port);
    if (verify_) verifySlot(BusCycle{Cycle::IoRead, port, 0, false}, s);

    BusCycle c{Cycle::IoRead, port, 0, false};
    unclaimed_ = (s.who == nullptr);
    uint8_t v = s.who ? s.who->read(c) : 0xFF;
    c.data = v;  // what got driven -- see settle()
    settle(c);
    return v;
}

void Bus::ioWrite(uint8_t port, uint8_t data) {
    if (dirty_) rebuild();
    const Slot& s = ioWrite_[port];

    if (s.slow) { ioWriteExact(port, data); return; }
    if (verify_) verifySlot(BusCycle{Cycle::IoWrite, port, data, false}, s);

    BusCycle c{Cycle::IoWrite, port, data, false};
    unclaimed_ = (s.who == nullptr);
    if (s.who) s.who->write(c);
    settle(c);
}

uint8_t Bus::intAck() {
    BusCycle c{Cycle::IntAck, 0, 0, false};
    auto who = decoders(c);
    unclaimed_ = who.empty();
    contended_ = who.size() > 1;
    // No vector-interrupt board answered, so nothing drives the bus and it
    // floats high: 0xFF, which the 8080 executes as RST 7. That is the real
    // Altair's behavior, and we get it for free from the same rule that makes
    // unmapped memory read 0xFF. The bus does not know what a vector IS.
    uint8_t v = who.empty() ? 0xFF : who.front()->read(c);
    c.data = v;  // what got driven -- see settle()
    settle(c);
    return v;
}

} // namespace altair
