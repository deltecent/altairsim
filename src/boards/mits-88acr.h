#pragma once
//
// MITS 88-ACR -- the Audio Cassette Recorder interface (docs/boards/mits-88acr.md).
//
// THE CARD IS AN 88-SIO B WITH A MODEM BOLTED TO IT, AND THAT IS NOT AN ANALOGY.
// The manual's first sentence:
//
//     "The 88-ACR consists of two separate PC boards mated to each other to form a
//      single unit. One of these is the ACR Modem Board and the other is the 88-SIO
//      B, Serial TTL level I/O Board."
//
// -- and it then reprints the entire 88-SIO manual as the ACR's own assembly chapter.
// Same UART, same two ports, same inverted status bits, same interrupt-enable
// flip-flops. So this card DERIVES from SioBoard and inherits every one of them
// rather than growing a second, drifting copy. What is left below is only the things
// the modem board and the cassette actually change:
//
//   * THE STRAPS ARE SPECIFIED. The SIO's jumpers are whatever you soldered; the ACR
//     kit tells you exactly what to solder. "For the 88-ACR, wire address select for
//     006. Wire BAUD Rate for 300 (max.), and wire UART options for 8 data bits, one
//     stop bit, no parity bit." So: port 0x06, 300 baud, 8N1 -- defaults with a
//     source, which the 88-SIO's have never had.
//
//   * THE LINE IS SOLDERED TO A CASSETTE. There is no connector to CONNECT to. The
//     UART's serial pins go to the modem (STSO -> XS, RS -> SRSI), the modem's audio
//     goes to the recorder, and so the unit is a TAPE you MOUNT -- UnitKind::Tape
//     rather than UnitKind::Serial. DESIGN.md 7.1 called this shot exactly: "the ACR
//     hands it a TapeStream where the 88-SIO hands it a socket, and the only
//     difference left is that the unit is UnitKind::Tape (MOUNT) rather than
//     UnitKind::Serial (CONNECT)."
//
//   * A TAPE HAS A POSITION, so the card brings a verb: REWIND (Board::commands()).
//     A disk does not need one -- you can seek a disk. That is the whole reason
//     board-injected commands exist.
//
// WHAT THE MODEM CONTRIBUTES TO THE REGISTER MODEL: NOTHING. It is analog. Two tones
// -- 2400 Hz for a logic 1, 1850 Hz for a logic 0, FSK, both derived by dividing the
// 2 MHz clock -- and a phase-locked loop to get them back. The guest cannot observe a
// single bit of it, which is precisely why this file is short.
//
// AND THERE IS NO MOTOR CONTROL. Not "we did not model it" -- THE CARD HAS NONE. The
// operator pressed PLAY on the recorder with their finger. See the .md: the modem's
// "P/R" pad is a trap, and it is not Play/Record.

#include "boards/mits-88sio.h"
#include "host/tape.h"
#include "host/tapecodec.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

class AcrBoard : public SioBoard {
public:
    AcrBoard();

    std::string type() const override { return "acr"; }

    // The SIO's, minus `connect`. THE CARD HAS NO CONNECTOR: its UART's serial pins
    // are soldered to the modem board. Offering an endpoint would advertise a socket
    // where the hardware has a cassette.
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // THE BUTTONS ON THE RECORDER -- `mode = play | record`, and a UNIT property
    // rather than a board one because it is not on the card. It is not on the card in
    // the SIMULATOR because it is not on the card in REALITY: the 88-ACR has no motor
    // control, no transport register, and no way to know or care which button is
    // down. The operator pressed it. Here, the operator types it.
    std::vector<Property> unitProperties(const std::string& unit) override;

    // ONE TAPE. MOUNT puts a cassette in the recorder; UNMOUNT takes it out.
    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    // ...and CONNECT is refused, with the reason, rather than silently inherited.
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // REWIND. The one verb, and the reason Board::commands() exists at all.
    std::vector<CommandDef> commands() const override;
    bool runCommand(const std::string& name, const std::vector<std::string>& args,
                    std::ostream& out, std::string& err) override;

    // For the tests, so they can watch the head move without a filesystem.
    const TapeImage* tape() const { return tape_.get(); }

    // What a mount had to say for itself. Merged with the UART's, because the SIO's
    // drainLog() is the UART's alone and a mount that demodulated a WAV has its own
    // news (host/tapecodec.h -- report, do not hide).
    std::vector<std::string> drainLog() override;

private:
    // Hand the UART a fresh line onto the tape in whatever mode the recorder is in
    // now. Called on MOUNT and whenever a button is pressed.
    void reline();

    // THE BOARD OWNS THE TAPE; THE UART OWNS ONLY A STREAM ONTO IT (host/tape.h).
    // That split is what keeps REWIND out of the chip's reach: a UART that could
    // rewind its own line is not a UART.
    std::unique_ptr<TapeImage> tape_;
    std::string                path_;

    // WHAT THIS CARD'S MODEM CAN PHYSICALLY HEAR -- one modulation, because the card
    // has one modem: continuous FSK at 2400/1850, 300 baud. A Kansas City tape is
    // REFUSED rather than decoded, because the real PLL sits at 2125 Hz and takes
    // about +/-100 Hz, and a 1200 Hz space tone is some 925 Hz outside it. See
    // host/tapecodec.h for why decoding it anyway would be inventing hardware.
    static const std::vector<TapeFormat>& modem();

    std::string format_ = "auto";  // the `format` unit property
    std::string detected_;         // ...and what the mounted tape turned out to be

    std::vector<std::string> log_;

    // PLAY, until somebody says otherwise. It is what you do with a cassette 99 times
    // out of 100, and it is the safe default in the one way that matters: a tape that
    // is playing cannot be written over.
    TapeStream::Mode mode_ = TapeStream::Mode::Play;
};

} // namespace altair
