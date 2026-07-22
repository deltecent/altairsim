#include "boards/proctech-sol.h"

#include "boards/proctech-vdm1.h"  // VdmBoard::setScroll -- the OUT 0xFE target
#include "core/bus.h"              // bus_->boards(), to find the VDM
#include "core/statefile.h"
#include "host/media.h"

#include <cstdio>

namespace altair {
namespace {

// The injected endpoint resolver (setResolver), borrowed. The board hands an endpoint
// string to it and gets a stream back; it never learns what a socket is (DESIGN.md 7.7).
SolBoard::EndpointResolver g_resolver;

}  // namespace

void SolBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

// A stopped clock, for the bench and for a card with no crystal attached. The serial
// UART needs a Clock& to answer "is the transmit buffer empty yet?"; with no clock,
// now() never advances, so TBMT (a deadline) never comes due -- exactly right.
Clock& SolBoard::deadCard() {
    static Clock dead;
    return dead;
}

SolBoard::SolBoard()
    : kb_(std::make_unique<NullStream>()), printer_(std::make_unique<NullStream>()) {
    // -> NullStream. There is no null pointer in the stream path: a UART with nothing
    // plugged in has a DEAD line, not a dangling one (as SioBoard does for its own).
    serial_.disconnect();
    tapeUart_.disconnect();

    // THE CUTS FRAME, AND IT IS 8N2 -- which no manual we hold states. It was measured
    // off a genuine Sol cassette (deramp.com's TRK80.WAV: eleven bit times between
    // consecutive start bits, not ten), and the tape decodes to a well-formed SOLOS
    // header whose length matches its payload, which is what makes it a fact rather
    // than a guess. See reference/Sol-20.md, "CUTS audio format (MEASURED)".
    tapeUart_.dataBits = 8;
    tapeUart_.stopBits = 2;
    tapeUart_.parity   = LineParity::None;
    programTapeBaud();  // ...and the SPEED is the guest's, at 0FAh D5. See below.
}

// ---------------------------------------------------------------------------
// Bus decode: the seven contiguous ports F8..FE. FF (sense) is the fp board; the
// VDM screen RAM is the vdm1 board. Both are elsewhere on the backplane.
// ---------------------------------------------------------------------------
bool SolBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p >= base_ && p <= (uint8_t)(base_ + 6);
}

uint8_t SolBoard::read(const BusCycle& c) {
    switch ((uint8_t)(c.port() - base_)) {
        case 0:  // F8 -- serial status
            if (clock_) serial_.poll(*clock_);  // the receiver runs on its own clock
            return serialStatus();
        case 1:  // F9 -- serial data (the read strobe clears Data Available)
            if (clock_) serial_.poll(*clock_);
            return serial_.readData();
        case 2:  // FA -- general/tape status
            // ADVANCE THE TAPE RECEIVER FIRST, exactly as the serial status read does
            // above (case 0). SOLOS's cassette loader tests TDR here, in a tight poll,
            // and never touches 0FBh until it is set -- so if this did not fetch, the
            // only thing that ever would is pump(), once a slice, and the tape would
            // crawl at the slice rate no matter what clock the deck is on. With the
            // fetch here, the loader's own polling clocks the tape: full speed empties
            // it as fast as SOLOS looks, and `real` still holds each byte to its baud
            // through readable() (host/tape.h).
            if (clock_) tapeUart_.poll(*clock_);
            return generalStatus();
        case 3:  // FB -- tape (CUTS) data. The read strobe clears Tape Data Ready.
            if (clock_) tapeUart_.poll(*clock_);
            return tapeUart_.readData();
        case 4: {  // FC -- keyboard data. Return the latched char and clear the strobe.
            uint8_t v = kbData_;
            kbHave_ = false;
            return v;
        }
        case 5:  // FD -- parallel data IN. No parallel input source; float.
            return 0xFF;
        case 6:  // FE -- DSTAT is write-only (scroll). A read floats.
            return 0xFF;
    }
    return 0xFF;
}

void SolBoard::write(const BusCycle& c) {
    switch ((uint8_t)(c.port() - base_)) {
        case 1:  // F9 -- serial data out
            serial_.writeData(c.data, clock_ ? *clock_ : deadCard());
            break;
        case 2: {  // FA (OUT) -- tape motors (D7/D6) + 300-baud select (D5)
            // A MOTOR FALLING IS A TRANSPORT STOPPING, and this card is the only one
            // that can see it happen -- the 88-ACR has no motor line at all, so there
            // the operator's finger is the only stop there is. A guest that SAVEs and
            // then drops the motor has finished a recording, and the file should exist
            // without anyone having to UNMOUNT first.
            //
            // NOTED HERE, DONE IN pump(). Committing an audio tape re-modulates the
            // whole recording, and this is a BUS CYCLE -- the one place the codec seam
            // promises no DSP will ever run (host/tapecodec.h). The guest could not
            // observe the stall (its clock does not advance while we are in here), but
            // the rule is worth more than the shortcut, and pump() is a few
            // instructions away.
            const bool was1 = deck1_.motor, was2 = deck2_.motor;
            tapeCtl_      = c.data;
            deck1_.motor  = (c.data & 0x80) != 0;
            deck2_.motor  = (c.data & 0x40) != 0;
            if (was1 && !deck1_.motor) stop1_ = true;
            if (was2 && !deck2_.motor) stop2_ = true;
            programTapeBaud();
            retape();  // a transport just started or stopped: the line moved with it
            break;
        }
        case 3:  // FB -- tape data out
            tapeUart_.writeData(c.data, clock_ ? *clock_ : deadCard());
            break;
        case 5: {  // FD -- parallel data out -> the printer line
            uint8_t b = c.data;
            printer_->write(&b, 1);
            printer_->flush();
            break;
        }
        case 6:  // FE -- VDM display parameter. Forward the scroll row to the VDM.
            if (VdmBoard* v = vdm()) v->setScroll(c.data);
            break;
        default:  // F8 (status) and FC (keyboard) are read-only; a write is ignored.
            break;
    }
}

// ---------------------------------------------------------------------------
// Status words.
// ---------------------------------------------------------------------------

bool SolBoard::serialTxEmpty() const {
    return serial_.txBufferEmpty(clock_ ? *clock_ : deadCard());
}

bool SolBoard::tapeTxEmpty() const {
    return tapeUart_.txBufferEmpty(clock_ ? *clock_ : deadCard());
}

// IN 0xF8 -- serial status, ACTIVE HIGH (ready = 1). SOLOS's drivers test only D6
// (data ready) and D7 (transmitter empty); the modem-handshake and error bits are
// present in the manual but unused, so they read 0 (there is no line to have noise on).
uint8_t SolBoard::serialStatus() const {
    uint8_t s = 0;
    if (serial_.dataAvailable()) s |= 0x40;  // D6 SDR  -- a character is waiting
    if (serialTxEmpty())         s |= 0x80;  // D7 STBE -- OK to send the next one
    return s;
}

// IN 0xFA -- the shared status register, MIXED polarity (reference/Sol-20.md). The
// keyboard and parallel readies are active LOW (0 = ready), the tape flags active HIGH.
uint8_t SolBoard::generalStatus() const {
    uint8_t s = 0;
    if (!kbHave_)           s |= 0x01;  // D0 KDR  active low: 0 = a key is waiting
    s |= 0x02;                          // D1 PDR  active low: no parallel input source
    if (!printerWritable()) s |= 0x04;  // D2 PXDR active low: 0 = printer can take a byte

    // D3 TFE / D4 TOE -- tape framing and overrun errors, and they stay 0. A
    // ByteStream delivers the byte that was recorded or it delivers nothing; there is
    // no line to have noise on, and synthesizing one would mean inventing a
    // probability (chips/uart1602.h says the same about its own three).
    if (tapeUart_.dataAvailable()) s |= 0x40;  // D6 TDR  active high: a byte is waiting
    if (tapeTxEmpty())             s |= 0x80;  // D7 TTBE active high: OK to record

    return s;
}

// ---------------------------------------------------------------------------
// The host turn. Serial line and keyboard both come in here, once per slice, on the
// main thread -- never inside a bus cycle (DESIGN.md 7.1).
// ---------------------------------------------------------------------------
void SolBoard::pump() {
    serial_.pump();
    if (clock_) serial_.poll(*clock_);
    kb_->pump();
    latchKeyboard();
    printer_->pump();

    // The cassette runs on its own clock too -- and it must be polled even when the
    // guest is not reading 0FBh, or an interrupt-free loader that waits on TDR would
    // wait forever for a byte nothing had asked the receiver to fetch.
    tapeUart_.pump();
    if (clock_) tapeUart_.poll(*clock_);

    // A transport that stopped since the last pump, finished off here rather than in
    // the bus cycle that stopped it -- see the OUT 0FAh case. Only a deck that was
    // RECORDING has anything to write back; one that was playing is already identical
    // to its file.
    if (stop1_) { stop1_ = false; if (deck1_.mode == TapeStream::Mode::Record) commitTape(&deck1_); }
    if (stop2_) { stop2_ = false; if (deck2_.mode == TapeStream::Mode::Record) commitTape(&deck2_); }
}

void SolBoard::latchKeyboard() {
    if (kbHave_) return;                 // the strobe is still set: the line waits
    if (!kb_ || !kb_->readable()) return;
    uint8_t b = 0;
    if (kb_->read(&b, 1) != 1) return;
    kbData_ = b;
    kbHave_ = true;
    ++kbRx_;  // a keystroke crossed into the guest -- the run loop's live-traffic proof
}

void SolBoard::reset(Reset) {
    if (clock_) serial_.masterReset(*clock_);
    if (clock_) tapeUart_.masterReset(*clock_);
    kbHave_ = false;

    // RESET drops both motor lines, so both transports stop -- which is the machine:
    // the latch behind 0FAh clears, and a Sol that has just been reset is not one with
    // a tape still running. The cassette does NOT come out, and the head does not move:
    // pressing RESET is not the same as ejecting.
    tapeCtl_     = 0;
    deck1_.motor = false;
    deck2_.motor = false;
    programTapeBaud();
    retape();
}

void SolBoard::power() { reset(Reset::PowerOn); }

void SolBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    serial_.serialize(w);
    tapeUart_.serialize(w);
    for (const Deck* d : {&deck1_, &deck2_}) {
        w.u8(d->mode == TapeStream::Mode::Record ? 1 : 0);
        w.boolean(d->motor);
        w.u64(d->tape ? d->tape->pos() : 0);
    }
    w.u8(kbData_);
    w.boolean(kbHave_);
    w.u64(kbRx_);
    w.u8(tapeCtl_);
    w.boolean(stop1_);
    w.boolean(stop2_);
}

void SolBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    serial_.deserialize(r);
    tapeUart_.deserialize(r);
    for (Deck* d : {&deck1_, &deck2_}) {
        d->mode = r.u8() ? TapeStream::Mode::Record : TapeStream::Mode::Play;
        d->motor = r.boolean();
        uint64_t pos = r.u64();
        if (d->tape) d->tape->setPos(pos);
    }
    kbData_ = r.u8();
    kbHave_ = r.boolean();
    kbRx_   = r.u64();
    tapeCtl_ = r.u8();
    stop1_ = r.boolean();
    stop2_ = r.boolean();
    programTapeBaud();  // the baud select in tapeCtl_ is now restored
    retape();           // put the CUTS line back on the running deck at its head position
}

VdmBoard* SolBoard::vdm() const {
    if (!bus_) return nullptr;
    for (Board* b : bus_->boards())
        if (b->type() == "vdm1") return static_cast<VdmBoard*>(b);
    return nullptr;
}

std::vector<std::string> SolBoard::drainLog() {
    std::vector<std::string> v = std::move(log_);
    log_.clear();
    for (std::string& s : serial_.drainLog()) v.push_back(std::move(s));
    for (std::string& s : tapeUart_.drainLog()) v.push_back(std::move(s));
    return v;
}

// ---------------------------------------------------------------------------
// The cassette decks
// ---------------------------------------------------------------------------

// `tape` is `tape1`. A machine file written before this card had two transports names
// the line it knew about, and it still means the deck it always meant.
int SolBoard::deckOf(const std::string& unit) {
    const std::string u = lowerAscii(unit);
    if (u == "tape" || u == "tape1") return 1;
    if (u == "tape2") return 2;
    return 0;
}

SolBoard::Deck*       SolBoard::deck(int n) { return n == 1 ? &deck1_ : n == 2 ? &deck2_ : nullptr; }
const SolBoard::Deck* SolBoard::deck(int n) const {
    return n == 1 ? &deck1_ : n == 2 ? &deck2_ : nullptr;
}

const TapeImage* SolBoard::tape(int n) const {
    const Deck* d = deck(n);
    return d ? d->tape.get() : nullptr;
}

// THE BAUD STRAP THAT IS NOT A STRAP. On the 88-ACR the cassette speed is soldered --
// the kit says wire it for 300 and there it stays. On the Sol the GUEST picks, with
// one bit, while the machine runs: `SE TA 1` in SOLOS writes 0FAh with D5 set and the
// deck records at 300 instead of 1200. So this is called on every OUT 0FAh, and the
// value it lands on is state the guest can observe by timing its own loader.
void SolBoard::programTapeBaud() {
    tapeUart_.baud = (tapeCtl_ & 0x20) ? 300 : 1200;  // D5 set = slow
    tapeUart_.programLine();
}

// Put the UART's line on the deck that is turning. See the header: one modem, two
// transports, and a stopped transport is NO LINE -- not a quiet one.
//
// ORDER MATTERS, AND IT IS A LIFETIME, NOT A STYLE. TapeStream holds a REFERENCE to
// the TapeImage (host/tape.h), so the old stream must die before the tape it points
// at is replaced or destroyed.
void SolBoard::retape() {
    tapeUart_.disconnect();

    Deck* d = nullptr;
    if (deck1_.motor && deck1_.tape)      d = &deck1_;
    else if (deck2_.motor && deck2_.tape) d = &deck2_;
    if (!d) return;  // both stopped, or the running deck is empty: dead line

    // THE DECK'S CLOCK, from the baud the guest just selected. `full` -> 0 -> as fast as
    // the loader reads; `real` -> the byte time in nanoseconds at the current 300/1200,
    // a wall clock clock_hz cannot drag. retape() runs on every OUT 0FAh, so a guest
    // that changes speed mid-load gets the new cadence without a re-mount (host/tape.h).
    uint64_t nsPerByte = 0;
    if (d->rate == "real" && tapeUart_.baud > 0)
        nsPerByte = (uint64_t)(1000000000ull * tapeUart_.bitsPerChar() / tapeUart_.baud);

    tapeUart_.connect(std::make_unique<TapeStream>(*d->tape, d->mode, nsPerByte));
}

bool SolBoard::mount(const std::string& unit, const std::string& path, bool ro,
                     std::string& err) {
    int n = deckOf(unit);
    if (!n) {
        err = "sol has no cassette deck '" + unit + "' -- tape1 and tape2";
        return false;
    }

    // Look where the machine file is; remember what the machine file said -- the same
    // bargain the disks and the 88-ACR's tape make (core/board.h).
    Deck* d = deck(n);

    // openTapeMedia() and NOT openMedia(): a WAV is demodulated here, once, before the
    // guest runs. A byte tape comes back unwrapped, so .CUTS files are untouched.
    std::string detected;
    std::vector<std::string> said;
    auto media =
        openTapeMedia(resolvePath(path), ro, modem(), d->format, detected, said, err);
    if (!media) { err += pathNote(path); return false; }

    if (!ro && media->readOnlyForced())
        said.push_back("sol: " + path + " is write-protected on the host -- mounted read-only");
    for (std::string& s : said) log_.push_back(std::move(s));

    // An observing pointer, taken before the medium is handed over -- see the 88-ACR's
    // mount(), which has the long version of why this is safe across a board move.
    d->audio = dynamic_cast<AudioTapeMedia*>(media.get());

    tapeUart_.disconnect();  // ...before the old tape goes out from under the stream
    d->tape = std::make_unique<TapeImage>(std::move(media));
    d->path = path;
    d->detected = detected;
    applyEncoding(d);
    retape();
    return true;
}

void SolBoard::applyEncoding(Deck* d) {
    if (d->audio)
        d->audio->setEncoding(double(d->leader), double(d->trailer), waveformByName(d->wave));
}

void SolBoard::commitTape(Deck* d) {
    if (!d || !d->tape) return;
    std::string err;
    if (!d->tape->commit(err)) log_.push_back("sol: " + err);
}

// The two speeds the CUTS UART really runs at. The guest picks between them at
// OUT 0FAh D5 -- so both are this card's hardware, and choosing by confidence at MOUNT
// is reading a switch's position, not guessing at a standard.
const std::vector<TapeFormat>& SolBoard::modem() {
    static const std::vector<TapeFormat> v = {tapeformats::cuts1200(), tapeformats::kcs300()};
    return v;
}

bool SolBoard::unmount(const std::string& unit, std::string& err) {
    int n = deckOf(unit);
    if (!n) {
        err = "sol has no cassette deck '" + unit + "' -- tape1 and tape2";
        return false;
    }
    Deck* d = deck(n);
    if (!d->tape) {
        err = "there is no cassette in deck " + std::to_string(n);
        return false;
    }

    commitTape(d);
    tapeUart_.disconnect();  // the line dies BEFORE the tape does
    d->tape.reset();
    d->audio = nullptr;  // it died with the tape -- never leave this dangling
    d->path.clear();
    d->detected.clear();  // nothing in the deck, so it is not in any format
    retape();
    return true;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------

std::vector<Property> SolBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "Base I/O port (decodes BASE+0..6). Fixed at F8 on a real Sol-PC";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0;
        x.max   = 0xF8;  // BASE+6 must clear FF (the sense switches)
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> SolBoard::unitProperties(const std::string& unit) {
    // A DECK GETS NO `connect`, AND THAT IS THE POINT OF THE UNIT KIND. What is on the
    // end of a cassette line is a cassette; offering an endpoint would advertise a
    // socket where the hardware has a transport.
    if (int n = deckOf(unit)) {
        Deck*                 d = deck(n);
        std::vector<Property> p;
        Property              x;
        x.name    = "mode";
        x.help    = "The button that is down on the recorder: play | record";
        x.kind    = Kind::Enum;
        x.choices = {"play", "record"};
        x.get     = [d] {
            return Value::ofStr(d->mode == TapeStream::Mode::Record ? "record" : "play");
        };
        x.set = [this, d](const Value& v, std::string&) {
            TapeStream::Mode m =
                (v.s() == "record") ? TapeStream::Mode::Record : TapeStream::Mode::Play;
            if (m == d->mode) return true;

            // Pressing STOP before you press the other button: whatever the guest has
            // recorded goes to the host file NOW, while we still know it was a
            // recording. And the byte still in flight came off a tape that is no
            // longer playing, so it goes too (88-ACR's `mode` says the same).
            if (d->mode == TapeStream::Mode::Record) commitTape(d);
            (void)tapeUart_.readData();

            d->mode = m;
            retape();  // the line onto the tape now runs the other way
            return true;
        };
        p.push_back(std::move(x));

        // How to READ the file in THIS deck. See the 88-ACR's identical property: the
        // choice selects a reading, and never widens what the modem can hear.
        Property f;
        f.name    = "format";
        f.help    = "How to read the mounted file: auto | raw | cuts1200 | kcs300";
        f.kind    = Kind::Enum;
        f.choices = tapeFormatChoices(modem());
        f.get     = [d] { return Value::ofStr(d->format); };
        f.set     = [d](const Value& v, std::string&) {
            d->format = v.s();
            return true;  // takes effect at the NEXT mount -- a tape decodes once
        };
        p.push_back(std::move(f));

        // Seconds of idle tone either side of a recording, when this deck writes audio.
        // The 88-ACR's `leader` carries the argument for why these exist at all (time
        // cannot survive a byte image, so a writer synthesizes it) and why they are
        // integers. What differs here is the NUMBERS: 3 and 2, measured off TRK80.WAV,
        // the one real cassette dub we hold -- not the 88-ACR's 15, which is what a
        // manual asks an operator to do rather than what a Sol tape turned out to be.
        Property lead;
        lead.name = "leader";
        lead.help = "Seconds of idle tone before recorded data, when writing audio";
        lead.kind = Kind::Int;
        lead.min  = 0;
        lead.max  = 120;
        lead.unit = "s";
        lead.get  = [d] { return Value::ofInt(d->leader); };
        lead.set  = [d](const Value& v, std::string&) {
            d->leader = v.i();
            applyEncoding(d);  // NOW, not at the next mount: SET then record must mean it
            return true;
        };
        p.push_back(std::move(lead));

        Property trail;
        trail.name = "trailer";
        trail.help = "Seconds of idle tone after recorded data, when writing audio";
        trail.kind = Kind::Int;
        trail.min  = 0;
        trail.max  = 120;
        trail.unit = "s";
        trail.get  = [d] { return Value::ofInt(d->trailer); };
        trail.set  = [d](const Value& v, std::string&) {
            d->trailer = v.i();
            applyEncoding(d);
            return true;
        };
        p.push_back(std::move(trail));

        // THE CARRIER SHAPE, when this deck writes audio. Square is the default because it
        // is what a real Sol-PC modem lays down and what a genuine dub sounds like; sine is
        // the smoother, quieter tone. It is audible only -- a re-mount decodes either the
        // same (host/tapemodem.h) -- so it changes how a tape SOUNDS, never what it holds.
        Property wav;
        wav.name    = "waveform";
        wav.help    = "Carrier shape when writing audio: square (like real hardware) | sine";
        wav.kind    = Kind::Enum;
        wav.choices = {"square", "sine"};
        wav.get     = [d] { return Value::ofStr(d->wave); };
        wav.set     = [d](const Value& v, std::string&) {
            d->wave = v.s();
            applyEncoding(d);  // NOW, not at the next mount: SET then record must mean it
            return true;
        };
        p.push_back(std::move(wav));

        // HOW FAST THIS DECK PLAYS -- on the tape's clock, not the guest's. `full`
        // (default) hands the loader bytes as fast as it reads them; `real` paces
        // playback in wall time at the baud the guest has selected (0FAh D5). clock_hz
        // stops dragging the tape either way, which is the whole reason it exists.
        Property rt;
        rt.name    = "rate";
        rt.help    = "Playback speed: full (as fast as the guest reads) | real (wall-clock baud)";
        rt.kind    = Kind::Enum;
        rt.choices = {"full", "real"};
        rt.get     = [d] { return Value::ofStr(d->rate); };
        rt.set     = [this, d](const Value& v, std::string&) {
            d->rate = v.s();
            retape();  // if this deck is the one turning, its cadence changes now
            return true;
        };
        p.push_back(std::move(rt));

        // READ-ONLY, so there is no setter at all: it is a measurement, not a switch.
        Property det;
        det.name = "detected";
        det.help = "What the cassette in this deck turned out to be (empty if none)";
        det.kind = Kind::Str;
        det.get  = [d] { return Value::ofStr(d->detected); };
        p.push_back(std::move(det));

        return p;
    }

    if (unit != "serial" && unit != "printer" && unit != "keyboard") return {};

    std::vector<Property> p;
    {
        // Every unit is a line you CONNECT. `connect` is the endpoint on the far end;
        // reading it round-trips through CONFIG SAVE.
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of this line (CONNECT sets this)";
        x.kind = Kind::Str;
        std::string u = unit;  // by value -- the lambda outlives this call
        x.get = [this, u] { return Value::ofStr(endpointOf(u)); };
        x.set = [this, u](const Value& v, std::string& err) { return connect(u, v.s(), err); };
        p.push_back(std::move(x));
    }
    if (unit == "serial") {
        {
            Property x;
            x.name = "baud";
            x.help = "Serial line speed (the strap on the Sol-PC's serial UART)";
            x.kind = Kind::Int;
            x.min  = 1;
            x.max  = 1000000;
            x.get  = [this] { return Value::ofInt(serial_.baud); };
            x.set  = [this](const Value& v, std::string&) {
                serial_.baud = v.i();
                serial_.programLine();
                return true;
            };
            p.push_back(std::move(x));
        }
        {
            Property x;
            x.name  = "data_bits";
            x.help  = "Serial word length: 8, 7, or 6 (the Sol-PC DIP)";
            x.kind  = Kind::Int;
            x.min   = 5;
            x.max   = 8;
            x.get   = [this] { return Value::ofInt(serial_.dataBits); };
            x.set   = [this](const Value& v, std::string&) {
                serial_.dataBits = (int)v.i();
                serial_.programLine();
                return true;
            };
            p.push_back(std::move(x));
        }
    }
    return p;
}

std::string SolBoard::endpointOf(const std::string& unit) const {
    if (unit == "serial")   return serial_.endpoint();
    if (unit == "printer")  return printer_->describe();
    if (unit == "keyboard") return kb_->describe();
    return "null";
}

// What SHOW says about one deck: the tape, where its head is, and whether the motor
// is turning -- because "nothing is happening" has two quite different causes here,
// and the operator who forgot to spin the transport deserves to be told which.
UnitDef SolBoard::deckUnit(int n) const {
    const Deck* d = deck(n);
    UnitDef     u{"tape" + std::to_string(n), UnitKind::Tape, "(empty)"};
    if (d->tape) {
        char buf[224];
        std::snprintf(buf, sizeof buf, "%s  at %llu of %llu bytes%s  [%s, motor %s]",
                      d->path.c_str(), (unsigned long long)d->tape->pos(),
                      (unsigned long long)d->tape->size(),
                      d->tape->atEnd() ? "  [END OF TAPE]" : "",
                      d->mode == TapeStream::Mode::Record ? "record" : "play",
                      d->motor ? "on" : "off");
        u.state          = buf;
        u.readOnly       = d->tape->readOnly();
        u.readOnlyForced = d->tape->readOnlyForced();
    }
    return u;
}

std::vector<UnitDef> SolBoard::units() const {
    return {
        {"serial",   UnitKind::Serial, serial_.endpoint()},
        {"printer",  UnitKind::Serial, printer_->describe()},
        {"keyboard", UnitKind::Serial, kb_->describe()},
        deckUnit(1),
        deckUnit(2),
    };
}

std::vector<MapEntry> SolBoard::ioMap() const {
    const uint32_t b = base_;
    return {
        {b + 0, b + 0, "read",       "Sol serial status (D6 RX-rdy, D7 TX-empty)"},
        {b + 1, b + 1, "read/write", "Sol serial data"},
        {b + 2, b + 2, "read/write", "Sol status: kbd/parallel/tape (in) / tape motor+baud (out)"},
        {b + 3, b + 3, "read/write", "Sol tape (CUTS) data"},
        {b + 4, b + 4, "read",       "Sol keyboard data"},
        {b + 5, b + 5, "read/write", "Sol parallel (printer) data"},
        {b + 6, b + 6, "write",      "Sol VDM display parameter (scroll) -> vdm1"},
    };
}

// ---------------------------------------------------------------------------
// Units: four connectable lines. Serial rides the UART; the other three are bare
// streams the card holds directly.
// ---------------------------------------------------------------------------

bool SolBoard::connect(const std::string& unit, const std::string& endpoint,
                       std::string& err) {
    // CONNECT IS NOT "UNIMPLEMENTED" ON A DECK, IT IS WRONG ON A DECK -- and it used
    // to be worse than wrong. `CONNECT sol0:tape file:tape.cuts` opened the file
    // write-only and TRUNCATING (host/endpoint.cpp), so the documented way to play a
    // tape destroyed it on connect and could never read a byte back. A cassette has a
    // position and a rewind; you put one IN.
    if (deckOf(unit)) {
        err = "a cassette goes IN the deck, it does not plug into it -- "
              "MOUNT sol0:" + lowerAscii(unit) + " \"tape.cuts\"";
        return false;
    }
    if (unit != "serial" && unit != "printer" && unit != "keyboard") {
        err = "sol has no unit '" + unit + "' -- serial, printer, keyboard, tape1, tape2";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    auto s = g_resolver(endpoint, err);
    if (!s) return false;

    if (unit == "serial")        serial_.connect(std::move(s));
    else if (unit == "printer")  printer_ = std::move(s);
    else                         kb_ = std::move(s);  // keyboard
    return true;
}

bool SolBoard::disconnect(const std::string& unit, std::string& err) {
    if (deckOf(unit)) {
        err = "nothing is connected to a cassette deck -- UNMOUNT takes the tape out.";
        return false;
    }
    if (unit == "serial")        serial_.disconnect();
    else if (unit == "printer")  printer_ = std::make_unique<NullStream>();
    else if (unit == "keyboard") kb_ = std::make_unique<NullStream>();
    else {
        err = "sol has no unit '" + unit + "' -- serial, printer, keyboard, tape1, tape2";
        return false;
    }
    return true;
}

// The DECKS ARE ABSENT HERE, and deliberately. unitStream hands an operator the
// connector behind a line (the MCP console); a cassette has no connector, and the
// stream onto one is the UART's, made and destroyed by retape() as motors turn.
ByteStream* SolBoard::unitStream(const std::string& unit) {
    if (unit == "serial")   return &serial_.stream();
    if (unit == "printer")  return printer_.get();
    if (unit == "keyboard") return kb_.get();
    return nullptr;
}

// ---------------------------------------------------------------------------
// REWIND -- your finger on the transport.
//
// The Sol can start and stop a motor, which the 88-ACR cannot, and it STILL cannot
// rewind: 0FAh has a run bit and nothing else. There is no direction, no counter and
// no way back. So this is the operator's verb here for the same reason it is there,
// and it names its deck, because there are two and neither is a sensible default.
// ---------------------------------------------------------------------------
std::vector<CommandDef> SolBoard::commands() const {
    return {{
        "REWIND",
        true,     // a card that is IN THE MACHINE has no unbuilt verbs
        nullptr,  // ...so it is waiting on nothing
        "REWIND <id>:tape1|tape2 -- wind a cassette back to the beginning",
        "The Sol-PC reads and writes a tape from wherever the head is sitting, and\n"
        "after a load the head is at the end of the program. The guest can start and\n"
        "stop the motor (OUT 0FAh D7/D6) but it cannot wind one back -- there is no\n"
        "rewind bit on the Sol-PC -- so this is your finger on the deck.\n"
        "\n"
        "Load the same tape twice:\n"
        "  MOUNT sol0:tape1 \"tapes/trk80.tap\"\n"
        "  GO 0                       (SOLOS GETs it, to the end of the tape)\n"
        "  REW sol0:tape1             (...and now the tape is back at the start)\n"
        "\n"
        "REW is the shortest spelling: RESET already answers to R, RE and RES.",
    }};
}

bool SolBoard::runCommand(const std::string& name, const std::vector<std::string>& args,
                          std::ostream& out, std::string& err) {
    if (name != "REWIND") {
        err = "the Sol has one verb, and it is REWIND";
        return false;
    }

    // args[1] is `<id>` or `<id>:<unit>` -- the monitor has already used it to find
    // THIS board (core/board.h). NAMING THE DECK IS NOT OPTIONAL: with two of them, a
    // bare `REW sol0` would have to guess, and guessing wrong rewinds the tape the
    // operator was in the middle of writing.
    std::string u;
    if (args.size() > 1) {
        size_t c = args[1].find(':');
        if (c != std::string::npos) u = args[1].substr(c + 1);
    }
    if (u.empty()) {
        err = "the Sol has two cassette decks -- say which: REW " +
              (args.size() > 1 ? args[1] : id) + ":tape1";
        return false;
    }

    int n = deckOf(u);
    if (!n) {
        err = "the Sol has no cassette deck '" + u + "' -- tape1 and tape2";
        return false;
    }
    Deck* d = deck(n);
    if (!d->tape) {
        err = "there is no cassette in deck " + std::to_string(n) + ". MOUNT one first.";
        return false;
    }

    // Everything the guest has recorded is on its way to the host file, and the head
    // is about to go somewhere else. Flush BEFORE moving it.
    commitTape(d);
    d->tape->rewind();

    // AND THROW AWAY THE BYTE THE CARD IS STILL HOLDING -- it came off a part of the
    // tape we have just wound past, and left in place the guest would read it once
    // now and again when the tape replays it. See AcrBoard::runCommand, which has the
    // long version of this argument.
    (void)tapeUart_.readData();

    retape();

    out << id << ":tape" << n << ": rewound -- " << d->path << " is at the beginning ("
        << d->tape->size() << " bytes)\n";
    return true;
}

}  // namespace altair
