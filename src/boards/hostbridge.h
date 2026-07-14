#pragma once
//
// The Host Bridge -- guest <-> host file transfer (DESIGN.md 12.1,
// docs/boards/hostbridge.md).
//
// THIS IS OUR OWN CARD. It is not a period board and it does not pretend to be
// one; it is documented to the same standard as one because that is the only way
// to keep ourselves honest, and because it is the FIRST genuinely new piece of
// hardware built against the board API -- which makes it a real test of that API
// rather than a rehash of a card whose shape we already knew.
//
// WHY NOT AltairZ80's PORT-0xFE PSEUDO DEVICE. We do not implement it (DESIGN.md
// 0.1, 12): it is another simulator's invention, reimplementing its protocol means
// deriving from that simulator's source, and -- as it happens -- every port it
// touches belongs to a real card here. 0xFE is the 88-VI/RTC's control register and
// 0x12/0x13 are the 88-2SIO's channel B. AltairZ80 has to STEAL those ports at
// transfer time (it saves the handlers, installs its own, and puts them back). We
// author both sides of this protocol, so we have no legacy to honor and we simply
// take a free address instead. Two owners for one wire is a bug, not a technique.
//
// WHY 0xB0. It is a hole, and finding one took a census of both catalogs. Taken
// elsewhere: 00-01 88-SIO, 03-05 (88-PIO), 06-07 88-ACR, 08-0A 88-DCDD/MDS, 0E-0F
// Dazzler, 10-13 88-2SIO, 20-23 (88-4PIO), 30-34 WD179X + Cromemco 64FDC, 40/C0
// bank select, A0-A7 88-HDSK, C0 PMMI, F8-FC Tarbell, FE 88-VI/RTC, FF front panel.
// 0x30 was the first choice and it was WRONG -- the WD179X floppy controller
// defaults there, and putting a host-transfer card on top of a disk controller is
// precisely the mistake we refused to inherit. B0-BF and D0-DF are the only empty
// 16-port holes in either catalog. (Patrick called it before the grep did.)
//
// THE GUEST UTILITIES ARE R.COM, W.COM AND HDIR.COM (cpm/hostbridge/*.ASM). The
// names are AltairZ80's, because the muscle memory is worth keeping; the code and
// the protocol are ours. AltairZ80's own R.COM and W.COM will not run here, and
// that has not changed.

#include "core/board.h"
#include "host/hostdir.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

class HostBridgeBoard : public Board {
public:
    std::string type() const override { return "hostbridge"; }

    // ---- the bus ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // ---- the seam the tests come through ----
    //
    // A board's protocol tests are about the state machine at BA+0 and BA+1, and
    // they must not be able to fail because of a temp directory or a read-only
    // checkout -- the same argument MemoryMedia makes for disks (host/media.h). So a
    // test hands the card a MemHostDir and asserts on it afterwards.
    //
    // An INJECTED directory outranks the `hostdir` property and survives a SET of
    // it, deliberately: a test that set up a fake filesystem should not have it
    // yanked out from under by a property write it did not make.
    void setDir(std::unique_ptr<HostDir> d) {
        dir_      = std::move(d);
        injected_ = true;
    }

    // ---- the wire, as the guest sees it. Public because the tests speak it. ----

    enum Status : uint8_t {
        RDY = 0x01,  // ready for a command
        DAV = 0x02,  // a byte is waiting at BA+1
        TBE = 0x04,  // BA+1 will accept a byte
        EOFF = 0x08, // the current stream is exhausted ("EOF" is a macro on some hosts)
        ERR = 0x10,  // the last operation failed -- read the code with ERROR
                     // bits 5-7 read ZERO, always. See status() in the .cpp.
    };

    enum class Cmd : uint8_t {
        Ident     = 0x00,
        OpenRead  = 0x01,
        OpenWrite = 0x02,
        Close     = 0x03,
        DirFirst  = 0x04,
        DirNext   = 0x05,
        Delete    = 0x06,
        Error     = 0x07,
        Reset     = 0x08,
    };

    // The signature IDENT hands back. A guest that does not see this is not talking
    // to a host bridge, and must say so rather than hang -- an IN from a port no card
    // decodes floats to 0xFF, which is indistinguishable from a card with every flag
    // set. This is the only reliable probe, and it is why the command exists.
    static constexpr const char* kIdent = "ALTAIRSIM HOSTBRIDGE 1";

    // A whole CP/M volume, and then some. The cap exists so a guest that names the
    // wrong host file cannot talk us into a multi-gigabyte allocation -- it is asked
    // BEFORE anything is read, so the refusal costs nothing.
    static constexpr size_t kMaxFile = 8u * 1024u * 1024u;

private:
    // WHERE THE CARD IS IN A TRANSFER. Not "what the guest asked for" -- what BA+1
    // will do with the next byte.
    enum class Mode {
        Idle,     // nothing in flight
        WantName, // taking a NUL-terminated name (or a glob) from the guest
        Reading,  // handing a file to the guest
        Writing,  // taking a file from the guest, uncommitted
        DirList,  // handing out directory names
        TextOut,  // handing out IDENT or an error message
    };

    uint8_t status() const;
    uint8_t dataIn();                 // IN  BA+1 -- may CONSUME
    void    dataOut(uint8_t v);       // OUT BA+1
    void    command(uint8_t c);       // OUT BA+0

    void nameComplete();              // the NUL landed; do the thing
    void emit(const std::string& s);  // hand `s` + NUL to the guest as a TextOut
    void presentName();               // names_[idx_] onto the data port, or EOF
    void clearStreams();              // the "a command abandons the stream" rule
    void fail(const HbFail& f);       // latch an error and stop whatever was going on

    HostDir& dir();                   // build a RealHostDir on demand, or the injected one

    uint8_t base_ = 0xB0;             // two ports: BA+0 command/status, BA+1 data

    std::string hostdir_;             // "" = the shell's working directory
    bool        readOnly_ = false;

    std::unique_ptr<HostDir> dir_;
    bool                     injected_ = false;

    Mode   mode_    = Mode::Idle;
    Cmd    pending_ = Cmd::Ident;     // which command the name being typed belongs to
    HbFail err_;

    // The guest's side of a transfer, whole. There is no file handle open across a
    // bus cycle and no host I/O in the hot path: OPEN slurps once, CLOSE spills once,
    // and every IN/OUT on BA+1 in between is a memcpy against these.
    //
    // That is also what makes "an unclosed write is not committed" STRUCTURAL rather
    // than a rule someone has to remember: an abandoned `in_` is simply never handed
    // to the host, and a reset that drops it has done the whole job.
    std::vector<uint8_t> out_;        // card -> guest
    size_t               pos_ = 0;
    std::vector<uint8_t> in_;         // guest -> card, uncommitted
    std::string          writeName_;  // where in_ goes on CLOSE

    // THE DIRECTORY ENUMERATOR, which is NOT a stream and outlives one. DIR_FIRST
    // builds it, DIR_NEXT walks it, and an OPEN_READ in between -- which is exactly
    // what `R *.ASM` does on every iteration -- leaves it alone. Only DIR_FIRST and
    // RESET clear it. See the long note in command().
    std::vector<std::string> names_;
    size_t                   idx_     = 0;
    bool                     dirOpen_ = false;  // what DIR_NEXT tests, since mode_ has moved on
};

} // namespace altair
