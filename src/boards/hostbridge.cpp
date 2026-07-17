#include "boards/hostbridge.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace altair {

// ---------------------------------------------------------------------------
// The bus. Two ports, and no memory decode at all.
// ---------------------------------------------------------------------------
bool HostBridgeBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    uint8_t p = c.port();
    return p == base_ || p == (uint8_t)(base_ + 1);
}

uint8_t HostBridgeBoard::read(const BusCycle& c) {
    if (c.port() == base_) return status();
    return dataIn();
}

void HostBridgeBoard::write(const BusCycle& c) {
    if (c.port() == base_) command(c.data);
    else dataOut(c.data);
}

// ---------------------------------------------------------------------------
// Status. IN BA+0, and it is ALWAYS this -- never a result code, never the second
// byte of something.
//
// That is the difference between this card and the pseudo-device it replaces. In
// AltairZ80's device an IN means whatever the last OUT decided it means, which is
// why its own documentation has to warn that failing to drain a result leaves the
// device "in an undefined state". A status register that is sometimes not the
// status register is not a status register.
//
// BITS 5-7 READ ZERO, ALWAYS, and that is load-bearing: an IN from a port no card
// decodes floats to 0xFF (DESIGN.md 4.6.1), so a guest that reads 0xFF here has
// found an empty slot, not a card with every flag lit. IDENT is still the probe the
// utilities use -- this just means a program that forgets to probe fails loudly.
// ---------------------------------------------------------------------------
uint8_t HostBridgeBoard::status() const {
    uint8_t s = RDY;  // nothing this card does takes long enough to be busy. See below.

    if (pos_ < out_.size()) s |= DAV;
    if (mode_ == Mode::WantName || mode_ == Mode::Writing) s |= TBE;

    // EOF means "the stream you are draining has run dry", and it is only meaningful
    // for the two streams that can. A file's is byte position; a directory's is name
    // position -- an exhausted enumerator, not an exhausted name.
    if (mode_ == Mode::Reading && pos_ >= out_.size()) s |= EOFF;
    if (mode_ == Mode::DirList && idx_ >= names_.size()) s |= EOFF;

    if (err_.code != HbError::None) s |= ERR;
    return s;
}

// RDY IS ALWAYS SET, AND THAT IS AN HONEST ANSWER, NOT A STUB.
//
// Every host operation this card performs -- the slurp at OPEN, the spill at CLOSE,
// the directory scan -- completes inside the OUT that requested it. There is never a
// moment when the guest could observe the card working, so there is never a moment
// when RDY could be clear.
//
// It is in the interface anyway because the guest polls it, and a guest that polls
// it is a guest that keeps working if this card ever grows a real EventQueue-backed
// open (`docs/boards/hostbridge.md` principle 4 asks for one). Publishing a flag the
// guest already honors costs one bit; retrofitting the poll into three .COM files
// after the fact costs a reassembly of all of them.

// ---------------------------------------------------------------------------
// The data port.
// ---------------------------------------------------------------------------
uint8_t HostBridgeBoard::dataIn() {
    if (pos_ < out_.size()) return out_[pos_++];

    // Drained. ZERO, not 0xFF -- no board may ever manufacture 0xFF, because that is
    // the bus's word for "nobody answered" and tests/test_boundary.cpp enforces it.
    // A guest reading past EOF gets a NUL, which is also the terminator it was
    // already looking for.
    return 0x00;
}

void HostBridgeBoard::dataOut(uint8_t v) {
    switch (mode_) {
    case Mode::WantName:
        // The NUL COMMITS. Until it lands, nothing has been opened, nothing has been
        // looked up, and the guest can still change its mind by sending a command.
        if (v == 0) {
            nameComplete();
        } else if (writeName_.size() < 255) {
            writeName_.push_back((char)v);
        }
        break;

    case Mode::Writing:
        // Straight into the buffer. NOT to the host: this file does not exist until
        // CLOSE, which is the promise the card makes and the reason a bus reset in
        // the middle of a transfer leaves no half-written file behind.
        in_.push_back(v);
        break;

    default:
        // A byte with nowhere to go. Dropping it is right: the guest is out of step
        // with the card, and the way back is a command -- which resets everything.
        break;
    }
}

// ---------------------------------------------------------------------------
// THE COMMAND PORT, AND THE ONE RULE THAT MAKES THIS CARD DIFFERENT.
//
//     ANY OUT TO THE COMMAND PORT ABANDONS THE STREAM IN FLIGHT.
//
// That single invariant deletes the entire family of caveats the SIMH pseudo-device
// carries -- "the calling program must request all bytes of the result, otherwise
// the pseudo device is left in an undefined state" -- and with it the reason its
// utilities send a reset command 128 times in a row before they dare do anything.
// There is no state here to climb out of. A guest can abandon a transfer at any
// point, for any reason, and the next command simply works.
//
// DIR_NEXT is the one command that continues rather than abandons, and it is not an
// exception to the rule: it abandons the current NAME's remaining bytes and moves to
// the next one, which is exactly what the rule says. The enumerator is not a stream.
// ---------------------------------------------------------------------------
void HostBridgeBoard::command(uint8_t c) {
    // CLOSE is the only command that consumes what was in flight, so take it before
    // the rule above throws it away.
    std::vector<uint8_t> pendingBytes = std::move(in_);
    std::string          pendingName  = std::move(writeName_);
    const Mode           was          = mode_;

    clearStreams();

    const Cmd cmd = (Cmd)c;

    // THE ENUMERATOR IS NOT A STREAM, AND IT OUTLIVES ONE.
    //
    // The rule above abandons the bytes in flight. It does NOT abandon the directory
    // listing, and that distinction is what makes `R *.ASM` possible at all: the guest
    // has to interleave the enumeration with the transfers --
    //
    //     DIR_FIRST "*.ASM" -> "A.ASM"
    //       OPEN_READ "A.ASM" ... read it ... write it to CP/M
    //     DIR_NEXT          -> "B.ASM"
    //       OPEN_READ "B.ASM" ...
    //
    // -- and if OPEN_READ threw the listing away, the only way to walk a wildcard would
    // be for the guest to buffer every matching name up front. In 8080, with 128 bytes
    // of DMA buffer and a CP/M TPA to leave room in, that is a real cost for nothing.
    //
    // So the listing is cleared by DIR_FIRST (which rebuilds it) and by RESET, and by
    // nothing else. `dirOpen_` is what DIR_NEXT tests, because `mode_` has by then been
    // legitimately moved on to Reading by the transfer in between.
    if (cmd == Cmd::DirFirst || cmd == Cmd::Reset) {
        names_.clear();
        idx_     = 0;
        dirOpen_ = false;
    }

    // AN OPERATION CLEARS THE ERROR LATCH; A REPORT DOES NOT. If ERROR cleared it, a
    // guest could never read it -- and if IDENT cleared it, a guest could not probe
    // the card and then ask what went wrong.
    switch (cmd) {
    case Cmd::OpenRead:
    case Cmd::OpenWrite:
    case Cmd::Close:
    case Cmd::DirFirst:
    case Cmd::DirNext:
    case Cmd::Delete:
    case Cmd::Reset:
        err_ = {};
        break;
    case Cmd::Ident:
    case Cmd::Error:
        break;
    }

    switch (cmd) {
    case Cmd::Ident:
        emit(kIdent);
        break;

    case Cmd::OpenRead:
    case Cmd::Delete:
    case Cmd::DirFirst:
        // All three want a NUL-terminated string next. DIR_FIRST's may be empty --
        // that is a glob meaning "everything" -- but the NUL is still required, so
        // the guest never has to know which commands take a name and which do not.
        pending_ = cmd;
        mode_    = Mode::WantName;
        break;

    case Cmd::OpenWrite:
        if (readOnly_) {
            fail({HbError::Permission, "the host bridge is read-only"});
            break;
        }
        pending_ = cmd;
        mode_    = Mode::WantName;
        break;

    case Cmd::Close: {
        if (was == Mode::Reading) break;  // closing a read is a no-op, and is not an error
        if (was != Mode::Writing) {
            fail({HbError::NoFile, "no file open"});
            break;
        }
        HbFail f;
        if (!dir().write(pendingName, pendingBytes, f)) fail(f);
        break;
    }

    case Cmd::DirNext:
        if (!dirOpen_) {
            fail({HbError::NoFile, "no directory enumeration in progress"});
            break;
        }
        ++idx_;
        presentName();
        break;

    case Cmd::Error: {
        // The code, then the text. A guest that only wants the byte takes one and
        // issues its next command -- safe, because of the rule at the top of this
        // function. A guest that wants to TELL THE USER something drains the rest and
        // prints the host's own words.
        out_.clear();
        out_.push_back((uint8_t)err_.code);
        const std::string& m = err_.msg.empty() ? std::string(hbErrorText(err_.code)) : err_.msg;
        for (char ch : m) out_.push_back((uint8_t)ch);
        out_.push_back(0);
        pos_  = 0;
        mode_ = Mode::TextOut;
        break;
    }

    case Cmd::Reset:
        // Everything is already gone -- clearStreams() did it and the latch was
        // cleared above. RESET exists so a guest that has lost track can SAY so in one
        // byte, and so that a utility's first act can be to know where it stands.
        mode_ = Mode::Idle;
        break;

    default:
        // An unknown command. Say so, rather than silently doing nothing: a guest
        // built against a later version of this card deserves to find out.
        fail({HbError::BadName, "unknown host bridge command"});
        break;
    }
}

// The NUL landed. Whatever the guest was naming, do it now.
void HostBridgeBoard::nameComplete() {
    const std::string name = std::move(writeName_);
    writeName_.clear();

    switch (pending_) {
    case Cmd::OpenRead: {
        HbFail f;
        std::vector<uint8_t> bytes;
        if (!dir().read(name, kMaxFile, bytes, f)) {
            fail(f);
            return;
        }
        out_  = std::move(bytes);
        pos_  = 0;
        mode_ = Mode::Reading;  // an EMPTY file lands here too, with EOF set at once
        break;
    }

    case Cmd::OpenWrite:
        // Nothing has touched the host yet, and nothing will until CLOSE. We do not
        // even check that the name is writable: a card that pre-created the file
        // would leave an empty one behind on an aborted transfer, which is the exact
        // failure the buffer exists to prevent.
        writeName_ = name;
        in_.clear();
        mode_ = Mode::Writing;
        break;

    case Cmd::Delete: {
        if (readOnly_) {
            fail({HbError::Permission, "the host bridge is read-only"});
            return;
        }
        HbFail f;
        if (!dir().remove(name, f)) fail(f);
        else mode_ = Mode::Idle;
        break;
    }

    case Cmd::DirFirst: {
        HbFail f;
        if (!dir().list(name, names_, f)) {  // an empty `name` is a glob meaning everything
            fail(f);
            return;
        }
        idx_     = 0;
        dirOpen_ = true;  // ...and it stays open across the transfers that follow
        presentName();
        break;
    }

    default:
        fail({HbError::BadName, "no operation is expecting a name"});
        break;
    }
}

void HostBridgeBoard::emit(const std::string& s) {
    out_.clear();
    for (char ch : s) out_.push_back((uint8_t)ch);
    out_.push_back(0);  // every string this card hands out is NUL-terminated
    pos_  = 0;
    mode_ = Mode::TextOut;
}

// Put names_[idx_] on the data port -- or, if the enumerator has run out, put
// nothing there and let status() raise EOF.
//
// Both DIR_FIRST and DIR_NEXT end here, which is what makes "no matches at all" and
// "no matches left" the same state to the guest: an empty stream with EOF up. A
// utility therefore needs exactly one loop and no special case for an empty
// directory -- which is the difference between HDIR being ten lines of 8080 and
// twenty.
void HostBridgeBoard::presentName() {
    if (idx_ < names_.size()) emit(names_[idx_]);
    else clearStreams();
    mode_ = Mode::DirList;  // emit()/clearStreams() had no way to know; we do
}

void HostBridgeBoard::clearStreams() {
    out_.clear();
    pos_ = 0;
    in_.clear();
    writeName_.clear();
    mode_ = Mode::Idle;
}

void HostBridgeBoard::fail(const HbFail& f) {
    err_ = f;
    clearStreams();  // a failed operation leaves NOTHING half-open
}

// ---------------------------------------------------------------------------
// The sandbox, built on demand.
//
// Constructing a RealHostDir opens nothing -- it stores a root -- so this is not
// host I/O in a bus cycle. The board never sees a file handle, a path grammar or a
// symlink; that is all behind HostDir (host/hostdir.h), which is where the escape
// tests live too.
// ---------------------------------------------------------------------------
// A path written INSIDE a machine file is relative to that file; one TYPED at the
// prompt is relative to the shell (core/paths.h). An EMPTY hostdir is the shell's
// working directory, and that is the same rule, not a special case -- RealHostDir
// reads an empty root as the cwd.
//
// Against hostdirBase_, NOT configDir() -- the base is the one that was standing when
// the value was written, and dir() runs long after. See hostbridge.h.
std::string HostBridgeBoard::configuredRoot() const {
    return hostdir_.empty() ? std::string() : resolveFrom(hostdirBase_, hostdir_);
}

HostDir& HostBridgeBoard::dir() {
    if (!dir_) dir_ = std::make_unique<RealHostDir>(configuredRoot());
    return *dir_;
}

// The resolved sandbox root, WITHOUT building the sandbox -- SHOW must be able to ask
// this of a card the guest has never touched, and asking must not be what decides where
// the fence lands. That is the bug this pair of members exists to prevent (hostbridge.h).
std::string HostBridgeBoard::sandboxRoot() const {
    if (injected_ && dir_) return dir_->root();  // a test's MemHostDir outranks the property

    // ONCE THE FENCE EXISTS, THE FENCE IS THE ANSWER. Re-deriving the root here while a
    // RealHostDir stands on a different one is precisely how a display comes to disagree
    // with the thing it claims to describe -- and of the two, the operator needs the one
    // the guest is actually inside. Asking it also means SHOW PATHS reports a wrong
    // dir(), which is what makes the guard below able to see one.
    std::error_code ec;
    std::string p = dir_ ? dir_->root() : configuredRoot();

    // An empty root is the cwd, and it asks for it BY NAME. `absolute("")` is not
    // spelled "the cwd" in the standard -- it is unspecified enough to hand back a
    // trailing-slash oddity or nothing at all, and this string is the fence.
    if (p.empty()) {
        auto cwd = std::filesystem::current_path(ec);
        return ec ? std::string("(unknown -- the host will not say what directory we are in)")
                  : cwd.lexically_normal().string();
    }

    auto abs = std::filesystem::absolute(p, ec);
    if (ec) return p;  // no cwd to resolve against: say what we have rather than nothing
    return abs.lexically_normal().string();
}

// ---------------------------------------------------------------------------
// Lifecycle. Both resets do the same thing, and it is the honest thing.
// ---------------------------------------------------------------------------
void HostBridgeBoard::reset(Reset) {
    // ABORT, AND DISCARD ANY UNCOMMITTED WRITE. On a real card the guest hit STOP and
    // RESET in the middle of a transfer, and the half of a file that had been shifted
    // across is not a file. Because the write buffer is the only place those bytes
    // ever lived, dropping it IS the whole job -- there is no partial file on the host
    // to clean up, because one was never created.
    clearStreams();
    names_.clear();
    idx_     = 0;
    dirOpen_ = false;
    err_     = {};

    // `hostdir`, `port` and `readonly` survive. They are CONFIGURATION -- jumpers and
    // a cable -- and a reset does not move a jumper (DESIGN.md 6).
}

void HostBridgeBoard::power() { reset(Reset::PowerOn); }

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> HostBridgeBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base port. Two ports: BASE+0 command/status, BASE+1 data";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFE;  // BASE+1 has to fit too
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string&) {
            base_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "hostdir";
        x.help = "The sandbox root. Guest names resolve here and CANNOT escape it. "
                 "Empty = the shell's working directory";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(hostdir_); };
        x.set  = [this](const Value& v, std::string&) {
            hostdir_ = v.s();
            // PIN WHAT IT IS RELATIVE TO, HERE, WHILE WE STILL KNOW. See hostbridge.h --
            // dir() is built lazily and configDir() is empty by then.
            hostdirBase_ = configDir();
            // Drop the cached sandbox so the next command builds one at the new root.
            // An INJECTED directory (a test's MemHostDir) is not dropped -- see
            // setDir().
            if (!injected_) dir_.reset();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        // WHERE THE FENCE ACTUALLY IS -- which `hostdir` alone cannot tell you.
        //
        // `hostdir` reads back what was WRITTEN, because that is what SHOW and CONFIG
        // SAVE need (board.h). But a bare `xfer` is two different directories depending
        // on who wrote it, and the one question worth asking about a sandbox is which
        // directory it is actually fencing. That is this. Read-only, so CONFIG SAVE
        // skips it (toml.cpp) -- a resolved path is a FACT, not configuration, and
        // writing it back into a machine file would nail that file to this host.
        Property x;
        x.name = "hostdir_root";
        x.help = "LIVE: the sandbox root as RESOLVED -- the actual directory the guest is "
                 "fenced into. Read-only; `hostdir` is what was written.";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(sandboxRoot()); };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "readonly";
        x.help = "Refuse OPEN_WRITE and DELETE -- the guest may read the host, not change it";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(readOnly_); };
        x.set  = [this](const Value& v, std::string&) {
            readOnly_ = v.b();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;

    // THERE IS NO `interrupt` PROPERTY, and its absence is a decision.
    //
    // docs/boards/hostbridge.md listed one as optional. But every operation this card
    // performs completes inside the OUT that asked for it, so an interrupt could only
    // ever fire immediately -- there is nothing to wait for. A strap that always means
    // "already done" tells the guest nothing it could not learn by reading the status
    // port it is about to read anyway, and it would be a lie about what the card does.
    // If the open ever goes async, the strap comes back with something to say.
}

std::vector<MapEntry> HostBridgeBoard::ioMap() const {
    return {
        {base_, base_, "r/w", "Host Bridge: OUT = command, IN = status"},
        {(uint8_t)(base_ + 1), (uint8_t)(base_ + 1), "r/w",
         "Host Bridge: data -- file names, file bytes, directory names"},
    };
}

} // namespace altair
