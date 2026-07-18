#include "boards/mits-88acr.h"

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

    // ...and what the tape in the recorder actually turned out to be. READ-ONLY, which
    // means no setter at all (adding-a-board.md): it is a measurement, not a switch.
    Property d;
    d.name = "detected";
    d.help = "What the mounted tape turned out to be (empty if nothing is mounted)";
    d.kind = Kind::Str;
    d.get  = [this] { return Value::ofStr(detected_); };
    p.push_back(std::move(d));

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
        char buf[192];
        // The write-protect tab is NOT spelt into this string any more -- it is a field
        // on UnitDef, so that SHOW and the mount table read the same answer from the same
        // place and a disk controller cannot forget to mention it (board.h).
        std::snprintf(buf, sizeof buf, "%s  at %llu of %llu bytes%s", path_.c_str(),
                      (unsigned long long)tape_->pos(), (unsigned long long)tape_->size(),
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
void AcrBoard::reline() {
    attachStream(std::make_unique<NullStream>());  // the old line dies here
    if (tape_) attachStream(std::make_unique<TapeStream>(*tape_, mode_));

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
    if (audio_) audio_->setEncoding(double(leader_), double(trailer_));
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
    return {{
        "REWIND",
        true,     // a card that is IN THE MACHINE has no unbuilt verbs
        nullptr,  // ...so it is waiting on nothing
        "REWIND <id>:tape -- wind the cassette back to the beginning",
        "The 88-ACR reads and writes a tape from wherever its head is sitting, and\n"
        "after a load the head is at the end of the program. Nothing in the guest can\n"
        "move it back: the real card has NO MOTOR CONTROL -- the operator pressed the\n"
        "buttons -- so REWIND is your finger on the recorder.\n"
        "\n"
        "Load the same tape twice:\n"
        "  MOUNT acr0:tape \"tapes/4kbas.tap\"\n"
        "  GO 0                       (the loader reads to the end of the tape)\n"
        "  REW acr0:tape              (...and now the tape is back at the start)\n"
        "  GO 0\n"
        "\n"
        "REW is the shortest spelling: RESET already answers to R, RE and RES.",
    }};
}

bool AcrBoard::runCommand(const std::string& name, const std::vector<std::string>& args,
                          std::ostream& out, std::string& err) {
    if (name != "REWIND") {
        err = "the 88-ACR has one verb, and it is REWIND";
        return false;
    }

    // args[1] is `<id>` or `<id>:<unit>` -- the monitor has already used it to find
    // THIS board (core/board.h). All that is left is to check the unit, if one was
    // named: `REWIND acr0:tty` should say what is wrong rather than rewind a tape the
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

    // Everything the guest has recorded is on its way to the host file, and the head
    // is about to go somewhere else. Flush BEFORE moving it, for the same reason the
    // DCDD flushes a partial sector before it invalidates its buffer.
    commitTape();
    tape_->rewind();

    // AND THROW AWAY THE BYTE THE CARD IS STILL HOLDING.
    //
    // The UART receives eagerly, so at the moment you rewind there is typically a
    // character sitting in its receive register -- one that came off a part of the
    // tape you have just wound PAST. Leave it there and the guest's next read gets
    // that byte, and then gets it AGAIN when the tape replays it: a duplicated byte,
    // manufactured by us, in a program image.
    //
    // A DELIBERATE DEPARTURE, and small. On the real card the receiver is fed by a
    // phase-locked loop behind a CARRIER DETECT, so a stopped tape delivers it
    // nothing and the question never arises. We have no carrier to lose, so we drop
    // the byte by hand. Reading the data register is exactly how the guest would have
    // dropped it (it is the chip's /RDAR strobe), so nothing here reaches past the
    // pins. See the .md, Limitations.
    (void)u_.readData();

    reline();  // and the head is at the top of the tape again

    out << id << ":tape: rewound -- " << path_ << " is at the beginning ("
        << tape_->size() << " bytes)\n";
    return true;
}

} // namespace altair
