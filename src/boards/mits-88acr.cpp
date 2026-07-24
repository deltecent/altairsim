#include "boards/mits-88acr.h"

#include "core/statefile.h"
#include "host/media.h"
#include "host/stream.h"

#include <cstdio>
#include <utility>

namespace altair {

// The one unit this card has. Case-blind, like every other name the operator types
// -- and the CLI is not the only road here: TOML's `[board.unit.TAPE]` and the tests
// call mount()/unmount() directly, with whatever case they were written in.
static bool isTape(const std::string& unit) { return lowerAscii(unit) == "tape"; }

// ---------------------------------------------------------------------------
// THE STRAPS THE KIT TELLS YOU TO SOLDER.
//
// The 88-SIO's defaults are a CHOICE -- its manual marks no standard address and no
// factory word format, and the .md says so. The 88-ACR's are a SOURCE. The assembly
// manual, in the middle of the SIO B hardwire-connections page:
//
//     "For the 88-ACR, wire address select for 006. Wire BAUD Rate for 300 (max.),
//      and wire UART options for 8 data bits, one stop bit, no parity bit."
//
// That is the whole configuration of the card, and it is not ours to pick.
//
// "300 (MAX.)" IS THE MODEM TALKING, NOT THE UART. The SIO B's own baud jumper goes
// to 25,000; the modem's FSK pair cannot carry it. Above 300 you have a card nobody
// could build -- see the .md, where it is a Limitation and an escape hatch, in that
// order.
//
// AND REV 1, WITHOUT HAVING TO ASSUME IT. The ACR manual's Bit Definition table reads
// TBMT at bit 7, DAV at bit 0, and bits 5 and 1 "NOT USED" -- which IS the post-errata
// status word (see SioRev in mits-88sio.h). The card documents itself as a Rev 1, so
// we do not have to infer it from the printing date.
// ---------------------------------------------------------------------------
AcrBoard::AcrBoard() {
    base_ = 0x06;

    u_.baud     = 300;
    u_.dataBits = 8;
    u_.stopBits = 1;
    u_.parity   = LineParity::None;

    rev_ = SioRev::Rev1;

    // The interrupt straps stay None, and THAT is sourced too: "If the 88-ACR is used
    // with MITS software, interrupts are not used. Do not make any connections to
    // interrupt lines if using MITS software." The pads exist -- inherited from the
    // SIO B, INT and VI0-VI7, independent for IN and OUT -- and the kit tells you to
    // leave them alone.
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

// The 88-SIO's properties, MINUS the ones this card does not physically have.
//
// `connect` goes because THERE IS NO CONNECTOR. The UART's serial pins are soldered
// to the modem board -- "XS" on the Modem to "STSO" on the S I/O board, "RS" on the
// Modem to "SRSI" -- and the modem's audio goes to the recorder. The line on this card
// has exactly one thing on the end of it, and you MOUNT it.
//
// THE TRANSFORM CHAIN IS NO LONGER SOMETHING THIS CARD HAS TO DEFEND AGAINST.
//
// It used to be. `upper`, `crlf`, `bsdel` and the rest rewrite CHARACTERS on a
// terminal line, and a cassette carries BINARY -- a checksummed absolute image of 4K
// BASIC. A CRLF transform on that line does not annoy you, it silently corrupts the
// program, and it corrupts it on the way onto the tape as well as off. So this card
// used to reach into its own base class and subtract every filter property by name.
//
// That argument was right, and it turned out to be right about EVERY line, not just
// this one -- a socket carrying XMODEM is no more a terminal than a cassette is. So
// the chain moved to the console, where the human is (host/console.h), and the 88-SIO
// does not offer it any more either. There is nothing left here to take away but the
// endpoint itself: the recorder is soldered to the card, so there is no `connect`.
std::vector<Property> AcrBoard::properties() {
    std::vector<Property> all = SioBoard::properties();

    std::vector<std::string> drop{"connect"};

    std::vector<Property> p;
    for (Property& x : all) {
        bool dropped = false;
        for (const std::string& d : drop) dropped = dropped || d == x.name;
        if (!dropped) p.push_back(std::move(x));
    }
    return p;
}

// THE RECORDER'S BUTTONS. Not the card's -- the card cannot reach them, and that is
// the sourced fact this property exists to keep true (see the .md: there is no motor
// control on an 88-ACR, and the "P/R" pad on the modem is an AUDIO line labelled
// "Play In", not a play/record control).
//
// It is also the thing standing between a recording and a corrupted tape. See
// host/tape.h: one head means one position, the UART reads EAGERLY, and a tape that
// could be read and written at once would have its first byte eaten before the guest
// ever ran. PLAY and RECORD are exclusive here because they are exclusive on a
// recorder.
std::vector<Property> AcrBoard::unitProperties(const std::string& unit) {
    if (!isTape(unit)) return {};

    std::vector<Property> p;
    Property x;
    x.name    = "mode";
    x.help    = "The button that is down on the recorder: play | record";
    x.kind    = Kind::Enum;
    x.choices = {"play", "record"};
    x.get     = [this] {
        return Value::ofStr(mode_ == TapeStream::Mode::Record ? "record" : "play");
    };
    x.set = [this](const Value& v, std::string&) {
        TapeStream::Mode m =
            (v.s() == "record") ? TapeStream::Mode::Record : TapeStream::Mode::Play;
        if (m == mode_) return true;

        // Pressing STOP before you press the other button: whatever the guest has
        // recorded goes to the host file NOW, while we still know it was a recording.
        if (mode_ == TapeStream::Mode::Record) commitTape();

        // ...and the byte still in flight from the old mode is gone, for the same
        // reason it is gone on REWIND: it came off a tape that is no longer playing.
        (void)u_.readData();

        mode_ = m;
        reline();  // the line onto the tape now runs the other way
        return true;
    };
    p.push_back(std::move(x));

    // WHAT TO MAKE OF THE FILE, not what the card can do. `auto` sniffs RIFF magic and
    // demodulates a WAV; `raw` reads the file's own bytes even if it IS a WAV, which is
    // how you look at a tape that decodes badly. Naming the modulation forces it -- but
    // only among the ones this card's modem can hear, because the property selects a
    // READING and never widens the hardware.
    Property f;
    f.name    = "format";
    f.help    = "How to read the mounted file: auto | raw | fsk300";
    f.kind    = Kind::Enum;
    f.choices = tapeFormatChoices(modem());
    f.get     = [this] { return Value::ofStr(format_); };
    f.set     = [this](const Value& v, std::string&) {
        format_ = v.s();
        return true;  // takes effect at the NEXT mount -- a tape is decoded once, at MOUNT
    };
    p.push_back(std::move(f));

    // HOW MUCH IDLE TONE GOES EITHER SIDE OF A RECORDING, and only a recording -- these
    // do nothing when the tape is playing, and nothing at all on a byte tape, which has
    // no audio to put them in.
    //
    // THEY EXIST BECAUSE TIME CANNOT BE RECOVERED. A byte image holds no durations, so
    // the leader a real transport needs does not survive a round trip and has to be put
    // back by whoever writes the audio. The numbers are the manual's: at least ~15
    // seconds of steady tone before data, to clear the plastic leader and let the
    // transport settle, and at least 5 seconds between batches -- which is why the
    // trailer default is 5 rather than 0, so that two of our recordings laid end to end
    // carry a gap a real machine would accept. (reference/Altair 88-ACR Cassette
    // Interface.md section 8.)
    //
    // SECONDS, AS AN INTEGER, because Kind has no floating-point member and this does
    // not want one: the quantity is "how long does the operator wait", and nobody has
    // ever needed a leader to a tenth of a second. Zero is legal and means trim it to
    // the data -- which is what the published archive files are, and why they will not
    // load on real hardware.
    Property lead;
    lead.name  = "leader";
    lead.help  = "Seconds of idle tone before recorded data, when writing audio";
    lead.kind  = Kind::Int;
    lead.min   = 0;
    lead.max   = 120;
    lead.unit  = "s";
    lead.get   = [this] { return Value::ofInt(leader_); };
    lead.set   = [this](const Value& v, std::string&) {
        leader_ = v.i();
        applyEncoding();  // NOW, not at the next mount: SET then record must mean it
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
    trail.get  = [this] { return Value::ofInt(trailer_); };
    trail.set  = [this](const Value& v, std::string&) {
        trailer_ = v.i();
        applyEncoding();
        return true;
    };
    p.push_back(std::move(trail));

    // THE CARRIER SHAPE, when this card writes audio. Square is the default -- what a real
    // 88-ACR modem lays down -- and sine is the smoother, quieter tone. Audible only: a
    // re-mount decodes either the same (host/tapemodem.h), so it is how a tape SOUNDS.
    Property wav;
    wav.name    = "waveform";
    wav.help    = "Carrier shape when writing audio: square (like real hardware) | sine";
    wav.kind    = Kind::Enum;
    wav.choices = {"square", "sine"};
    wav.get     = [this] { return Value::ofStr(wave_); };
    wav.set     = [this](const Value& v, std::string&) {
        wave_ = v.s();
        applyEncoding();
        return true;
    };
    p.push_back(std::move(wav));

    // HOW FAST THE TAPE PLAYS -- on the tape's clock, not the guest's. `full` (default)
    // hands the loader bytes as fast as it reads them; `real` paces playback in wall
    // time at the 300-baud strap, the wait a real cassette made you serve. The CPU's
    // clock_hz no longer drags it either way -- that is the whole point of the switch.
    Property rt;
    rt.name    = "rate";
    rt.help    = "Playback speed: full (as fast as the guest reads) | real (wall-clock baud)";
    rt.kind    = Kind::Enum;
    rt.choices = {"full", "real"};
    rt.get     = [this] { return Value::ofStr(rate_); };
    rt.set     = [this](const Value& v, std::string&) {
        rate_ = v.s();
        reline();  // the line's cadence changes now, not at the next mount
        return true;
    };
    p.push_back(std::move(rt));

    // ...and what the tape in the recorder actually turned out to be. READ-ONLY, which
    // means no setter at all (adding-a-board.md): it is a measurement, not a switch.
    Property d;
    d.name = "detected";
    d.help = "What the mounted tape turned out to be (empty if nothing is mounted)";
    d.kind = Kind::Str;
    d.get  = [this] { return Value::ofStr(detected_); };
    p.push_back(std::move(d));

    // WHERE THE HEAD IS, as mm:ss / total (percent). READ-ONLY -- a measurement, no setter.
    // The time is the real recording's for a WAV (leader and inter-program gaps included)
    // and an estimate from the 300-baud strap for a byte tape.
    Property pos;
    pos.name = "position";
    pos.help = "Where the tape head is now: mm:ss / total (percent) -- read-only";
    pos.kind = Kind::Str;
    pos.get  = [this] {
        return Value::ofStr(tape_ ? tapeCounterText(tapeSeconds(tape_->pos()), tapeTotalSeconds())
                                  : std::string());
    };
    p.push_back(std::move(pos));

    // The LIVE counter's switch. On by default; the console counter is off when this is,
    // and a machine whose guest talks to stdout can turn it off so the load is not
    // scribbled on. SHOW's `position` above is unaffected either way.
    Property cnt;
    cnt.name    = "counter";
    cnt.help    = "Live tape counter on the console during a load: on | off";
    cnt.kind    = Kind::Enum;
    cnt.choices = {"on", "off"};
    cnt.get     = [this] { return Value::ofStr(liveCounter_ ? "on" : "off"); };
    cnt.set     = [this](const Value& v, std::string&) {
        liveCounter_ = (v.s() != "off");
        return true;
    };
    p.push_back(std::move(cnt));

    // THE AUTO-STOP MARK -- a time at which the tape stops playing, so a multi-program tape
    // can be cued to halt at a boundary rather than running on into the next program. `off`
    // (or `end`) clears it. It is the operator's STOP button, and it gates playback only:
    // a recording writes straight through it.
    Property stp;
    stp.name = "stop";
    stp.help = "Auto-stop playback at this time: off | end | <mm:ss>";
    stp.kind = Kind::Str;
    stp.get  = [this] {
        if (!tape_ || tape_->stopAt() == TapeImage::kNoStop || tape_->stopAt() >= tape_->size())
            return Value::ofStr(std::string("off"));
        return Value::ofStr(tapeTimeMMSS(tapeSeconds(tape_->stopAt())));
    };
    stp.set = [this](const Value& v, std::string& err) {
        const std::string lo    = lowerAscii(v.s());
        const bool        clear = (lo == "off" || lo == "none" || lo == "end");
        if (!tape_) {
            // No cassette: clearing is a harmless no-op -- and `stop=off` is exactly what
            // an empty recorder round-trips through CONFIG SAVE. A real time has no tape to
            // measure against, so it says so rather than storing a number it cannot honor.
            if (clear) return true;
            err = "there is no cassette in the recorder. MOUNT one first.";
            return false;
        }
        if (clear) {
            tape_->setStop(TapeImage::kNoStop);
        } else {
            double secs;
            if (!parseTapeTime(v.s(), secs)) {
                err = "'" + v.s() + "' is not a stop -- use a time (mm:ss or seconds), or off";
                return false;
            }
            tape_->setStop(secondsToByte(secs));
        }
        reline();  // re-arm: a cleared/raised stop resumes now, a lowered one parks now
        return true;
    };
    p.push_back(std::move(stp));

    return p;
}

std::vector<MapEntry> AcrBoard::ioMap() const {
    return {
        {(uint32_t)base_, (uint32_t)base_, "read/write",
         "88-SIO B UART -- status (read) / interrupt enables (write)"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "read/write",
         "88-SIO B UART -- data, via the modem, to the cassette"},
    };
}

// ONE TAPE, and it is a TAPE and not a serial port -- which is the entire difference
// between this card and its own SIO B half, as far as the operator is concerned.
std::vector<UnitDef> AcrBoard::units() const {
    UnitDef u{"tape", UnitKind::Tape, "(empty)"};
    if (tape_) {
        char buf[288];
        // The write-protect tab is NOT spelt into this string any more -- it is a field
        // on UnitDef, so that SHOW and the mount table read the same answer from the same
        // place and a disk controller cannot forget to mention it (board.h). The counter
        // leads; the byte count stays (the codec header's promise that it is literally true);
        // an armed auto-stop says where the tape will halt.
        std::string stopNote;
        if (tape_->stopAt() != TapeImage::kNoStop && tape_->stopAt() < tape_->size())
            stopNote = "  stop @ " + tapeTimeMMSS(tapeSeconds(tape_->stopAt()));
        std::snprintf(buf, sizeof buf, "%s  %s  %llu/%llu bytes%s%s", path_.c_str(),
                      tapeCounterText(tapeSeconds(tape_->pos()), tapeTotalSeconds()).c_str(),
                      (unsigned long long)tape_->pos(), (unsigned long long)tape_->size(),
                      stopNote.c_str(),
                      tape_->atEnd() ? "  [END OF TAPE]" : "");
        u.state          = buf;
        u.readOnly       = tape_->readOnly();
        u.readOnlyForced = tape_->readOnlyForced();
    }
    return {u};
}

// ---------------------------------------------------------------------------
// The recorder
// ---------------------------------------------------------------------------

// Hand the UART a line onto the tape, running whichever way the recorder is set.
//
// ORDER MATTERS, AND IT IS A LIFETIME, NOT A STYLE. TapeStream holds a REFERENCE to
// the TapeImage (host/tape.h -- non-owning, on purpose, so the chip cannot reach
// REWIND). The old stream must die before the tape it points at does, and before the
// tape it points at is replaced.
void AcrBoard::serialize(StateWriter& w) const {
    SioBoard::serialize(w);
    w.u8(mode_ == TapeStream::Mode::Record ? 1 : 0);
    w.u64(tape_ ? tape_->pos() : 0);
    w.u64(tape_ ? tape_->stopAt() : TapeImage::kNoStop);  // the auto-stop mark travels too
}

void AcrBoard::deserialize(StateReader& r) {
    SioBoard::deserialize(r);
    mode_ = r.u8() ? TapeStream::Mode::Record : TapeStream::Mode::Play;
    uint64_t pos    = r.u64();
    uint64_t stopAt = r.u64();
    if (tape_) {
        tape_->setPos(pos);
        tape_->setStop(stopAt);
    }
    reline();  // rebuild the line onto the tape in the restored mode, from the head
               // position and stop mark just set. reline() re-arms the UART too.
}

void AcrBoard::reline() {
    attachStream(std::make_unique<NullStream>());  // the old line dies here

    // THE TAPE'S CLOCK, built from the card's strap. `full` -> 0 -> as fast as the guest
    // reads; `real` -> the 300-baud byte time in nanoseconds, a wall clock the CPU's
    // speed cannot touch (host/tape.h). The frame is the UART's to know (bitsPerChar);
    // turning it into a duration is the board's.
    uint64_t nsPerByte = 0;
    if (rate_ == "real" && u_.baud > 0)
        nsPerByte = (uint64_t)(1000000000ull * u_.bitsPerChar() / u_.baud);
    if (tape_) attachStream(std::make_unique<TapeStream>(*tape_, mode_, nsPerByte));

    // THE TAPE IS NOW MOVING. A cassette does not wait to be asked -- press PLAY and
    // the bytes come off it -- and refresh() is what tells the UART there may be
    // something on its line now.
    //
    // It cannot RUN AWAY from the guest, though: the UART pulls a byte only when it
    // has room for one, so the tape advances at the speed the guest reads it. A real
    // recorder keeps rolling and drops data on the floor; we do not, and the .md says
    // so under Limitations.
    refresh();
}

bool AcrBoard::mount(const std::string& unit, const std::string& path, bool ro, std::string& err) {
    if (!isTape(unit)) {
        err = "acr has no unit '" + unit + "' -- it has one, and it is called 'tape'";
        return false;
    }

    // Look where the machine file is; remember what the machine file said. See
    // HardSectorFdc::mount() and core/board.h -- the tape is the same bargain as
    // the disk, and for the same reason: `tapes/MitsPS2/ps2int.toml` names the tape
    // lying next to it, and must go on naming it that way when it is saved back.
    // openTapeMedia() and NOT openMedia(): a WAV is demodulated here, once, on the
    // operator's thread, and everything above this sees the bytes a .TAP would have
    // given it. A byte tape comes back unwrapped, so nothing about .TAP changes.
    std::string detected;
    std::vector<std::string> said;
    auto media = openTapeMedia(resolvePath(path), ro, modem(), format_, detected, said, err);
    if (!media) { err += pathNote(path); return false; }

    // The host would not let us write it, and the operator did not ask for that. Never
    // silent -- see MediaFile::readOnlyForced().
    if (!ro && media->readOnlyForced())
        said.push_back("acr: " + path + " is write-protected on the host -- mounted read-only");
    for (std::string& s : said) log_.push_back(std::move(s));

    // AN OBSERVING POINTER, TAKEN BEFORE THE MEDIUM IS HANDED OVER. An audio tape has
    // to be told how much leader to lay down, and it has to be told again whenever the
    // property changes -- so the board keeps a way to reach it. Non-owning: the
    // TapeImage below owns the medium, the medium is on the heap, and moving this board
    // (CONFIG LOAD) moves the unique_ptr and not the object, so the pointer stays good.
    // Null for a byte tape, which is exactly the question "is this audio?".
    audio_ = dynamic_cast<AudioTapeMedia*>(media.get());

    attachStream(std::make_unique<NullStream>());  // ...before the old tape goes
    tape_ = std::make_unique<TapeImage>(std::move(media));
    path_ = path;
    detected_ = detected;
    applyEncoding();
    reline();
    return true;
}

// What to lay down either side of the data when this tape is written back. A no-op on a
// byte tape, which has no audio to put it in.
void AcrBoard::applyEncoding() {
    if (audio_) audio_->setEncoding(double(leader_), double(trailer_), waveformByName(wave_));
}

// THE TRANSPORT STOPPED -- so an audio tape re-encodes itself and goes to the host now.
// Every caller is an operator action (UNMOUNT, REWIND, letting go of RECORD), which is
// precisely the contract MediaFile::commit() describes. A failure is REPORTED rather
// than swallowed: losing a recording quietly is the worst thing this path could do.
void AcrBoard::commitTape() {
    if (!tape_) return;
    std::string err;
    if (!tape_->commit(err)) log_.push_back("acr: " + err);
}

// The whole of what REWIND and WIND share -- see host/tape.h. Order matters: flush the
// recording BEFORE the head moves, drop the byte the UART pulled off the OLD position (or
// the guest reads it, then reads it again when the tape replays it), and reline last.
void AcrBoard::stageAt(uint64_t pos) {
    if (!tape_) return;
    commitTape();
    tape_->setPos(pos > tape_->size() ? tape_->size() : pos);
    (void)u_.readData();
    reline();
}

// SECONDS INTO THE RECORDING. A WAV kept its audio clock at decode, so the head's time is
// the real one -- leader and gaps and all. A byte tape (.TAP) never had audio, so its time
// is the honest estimate a 300-baud strap gives: bytes x frame-bits / baud.
double AcrBoard::tapeSeconds(uint64_t bytePos) const {
    if (audio_ && audio_->hasTimeline()) return audio_->secondsAt(bytePos);
    const double baud = u_.baud > 0 ? double(u_.baud) : 300.0;
    return double(bytePos) * u_.bitsPerChar() / baud;
}

double AcrBoard::tapeTotalSeconds() const {
    if (audio_ && audio_->hasTimeline()) return audio_->totalSeconds();
    if (!tape_) return 0.0;
    const double baud = u_.baud > 0 ? double(u_.baud) : 300.0;
    return double(tape_->size()) * u_.bitsPerChar() / baud;
}

uint64_t AcrBoard::secondsToByte(double secs) const {
    if (!tape_) return 0;
    if (audio_ && audio_->hasTimeline()) return audio_->byteAt(secs);
    const double baud  = u_.baud > 0 ? double(u_.baud) : 300.0;
    double       bytes = secs * baud / u_.bitsPerChar();
    if (bytes < 0.0) bytes = 0.0;
    uint64_t b = uint64_t(bytes + 0.5);
    return b > tape_->size() ? tape_->size() : b;
}

std::string AcrBoard::activityLabel() const {
    if (!tape_ || !liveCounter_ || mode_ != TapeStream::Mode::Play) return {};
    const uint64_t p = tape_->pos(), sz = tape_->size();
    // Nothing to watch before it starts, once it ends, or while it is parked at a stop mark.
    if (p == 0 || p >= sz || tape_->atStop()) return {};
    return "tape: " + tapeCounterText(tapeSeconds(p), tapeTotalSeconds());
}

// One modem, one modulation. This is the list a tape is judged against.
const std::vector<TapeFormat>& AcrBoard::modem() {
    static const std::vector<TapeFormat> v = {tapeformats::fsk300_1850()};
    return v;
}

std::vector<std::string> AcrBoard::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    for (std::string& s : SioBoard::drainLog()) out.push_back(std::move(s));
    return out;
}

bool AcrBoard::unmount(const std::string& unit, std::string& err) {
    if (!isTape(unit)) {
        err = "acr has no unit '" + unit + "' -- it has one, and it is called 'tape'";
        return false;
    }
    if (!tape_) {
        err = "there is no cassette in the recorder";
        return false;
    }

    commitTape();
    attachStream(std::make_unique<NullStream>());  // the line dies BEFORE the tape does
    tape_.reset();
    audio_ = nullptr;  // it died with the tape -- never leave this dangling
    path_.clear();
    detected_.clear();  // nothing is in the recorder, so it is not in any format

    refresh();
    return true;
}

// CONNECT is not "unimplemented" here, it is WRONG here, and the difference is worth a
// sentence. Silently inheriting SioBoard::connect() would let an operator plug a
// socket into a card whose serial pins are soldered to a modem.
bool AcrBoard::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    (void)unit;
    (void)endpoint;
    err = "the 88-ACR's line is soldered to its modem board, and the modem to a "
          "cassette -- there is no connector. Use MOUNT to put a tape in it.";
    return false;
}

bool AcrBoard::disconnect(const std::string& unit, std::string& err) {
    (void)unit;
    err = "nothing is connected to an 88-ACR -- its line goes to the modem. "
          "Use UNMOUNT to take the tape out.";
    return false;
}

// ---------------------------------------------------------------------------
// REWIND -- the verb, and the whole reason Board::commands() exists.
//
// A DISK DOES NOT NEED ONE. You can seek a disk: the DCDD's head steps to any track
// and its sector comes round every 5 ms whether you asked or not. A tape has a
// POSITION, and the only way back to the start of the program is to wind it there.
// That is the one thing a cassette has that no disk does (host/tape.h says the same),
// and it is why this verb is attached to this CARD and is not in the monitor's static
// table: pull the 88-ACR out of the machine and there is nothing left that can rewind.
// ---------------------------------------------------------------------------
std::vector<CommandDef> AcrBoard::commands() const {
    return {
        {
            "WIND",
            true,     // a card that is IN THE MACHINE has no unbuilt verbs
            nullptr,  // ...so it is waiting on nothing
            "WIND <id>:tape <mm:ss | START | END> -- move the cassette to a position",
            "The 88-ACR reads and writes a tape from wherever its head is sitting. WIND is\n"
            "your finger on the recorder's transport: it moves the head to a time on the\n"
            "tape, so a tape holding several programs one after another is reachable.\n"
            "\n"
            "The position is a time -- mm:ss, or a bare number of seconds -- or START / END.\n"
            "For a WAV the time is the recording's own, so a program indexed in a manual by\n"
            "seconds from the start of tape winds straight there. SHOW <id> reports the\n"
            "current position the same way.\n"
            "\n"
            "  MOUNT acr0:tape \"tape.wav\"\n"
            "  GO 0                 (the loader reads the first program to its end)\n"
            "  WIND acr0:tape 2:05  (...to where the second program begins)\n"
            "  GO 0\n"
            "\n"
            "WI is the shortest spelling.",
        },
        {
            "REWIND",
            true,
            nullptr,
            "REWIND <id>:tape -- wind the cassette back to the beginning (WIND START)",
            "REWIND is WIND to the start of the tape -- the common case, kept as its own\n"
            "verb. After a load the head is at the end of the program; nothing in the guest\n"
            "can move it back (the real card has NO MOTOR CONTROL), so this is your finger.\n"
            "\n"
            "Load the same tape twice:\n"
            "  MOUNT acr0:tape \"tapes/4kbas.tap\"\n"
            "  GO 0                       (the loader reads to the end of the tape)\n"
            "  REW acr0:tape              (...and now the tape is back at the start)\n"
            "  GO 0\n"
            "\n"
            "REW is the shortest spelling: RESET already answers to R, RE and RES.",
        },
    };
}

bool AcrBoard::runCommand(const std::string& name, const std::vector<std::string>& args,
                          std::ostream& out, std::string& err) {
    const bool rewind = (name == "REWIND");
    if (!rewind && name != "WIND") {
        err = "the 88-ACR's verbs are WIND and REWIND";
        return false;
    }

    // args[1] is `<id>` or `<id>:<unit>` -- the monitor has already used it to find
    // THIS board (core/board.h). All that is left is to check the unit, if one was
    // named: `WIND acr0:tty` should say what is wrong rather than move a tape the
    // operator did not mean.
    if (args.size() > 1) {
        size_t c = args[1].find(':');
        if (c != std::string::npos) {
            std::string u = args[1].substr(c + 1);
            if (!isTape(u)) {
                err = "the 88-ACR has no unit '" + u + "' -- it has one, and it is 'tape'";
                return false;
            }
        }
    }

    if (!tape_) {
        err = "there is no cassette in the recorder. MOUNT one first.";
        return false;
    }

    // Where to. REWIND is the start; WIND takes a time (mm:ss or bare seconds) or the
    // words START / END. A time past the end lands at the end, visibly (echoed below).
    uint64_t target = 0;
    if (!rewind) {
        if (args.size() < 3) {
            err = "WIND needs a position: a time (mm:ss or seconds), START, or END";
            return false;
        }
        const std::string& a  = args[2];
        const std::string  lo = lowerAscii(a);
        if (lo == "start") {
            target = 0;
        } else if (lo == "end") {
            target = tape_->size();
        } else {
            double secs;
            if (!parseTapeTime(a, secs)) {
                err = "'" + a + "' is not a position -- use a time (mm:ss or seconds), "
                      "START, or END";
                return false;
            }
            target = secondsToByte(secs);
        }
    }

    // The staging (flush the recording, seek, drop the eagerly-read byte off the OLD
    // position, reline) is shared with REWIND -- see stageAt() and its long note there
    // on why the held byte must go.
    stageAt(target);

    out << id << ":tape: " << (rewind ? "rewound" : "wound") << " to "
        << tapeCounterText(tapeSeconds(tape_->pos()), tapeTotalSeconds()) << " -- " << path_
        << " (" << tape_->pos() << " of " << tape_->size() << " bytes)\n";
    return true;
}

} // namespace altair
