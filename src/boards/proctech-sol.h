#pragma once
//
// Sol I/O -- the Processor Technology Sol-PC's onboard I/O, modeled as ONE composite
// S-100 card. See reference/Sol-20.md and docs/boards/proctech-sol.md.
//
// WHY ONE CARD AND NOT FOUR. Everything else in altairsim is a bag of single-purpose
// cards. The Sol-20 is an integrated machine: the Sol-PC motherboard carries the
// serial port, the keyboard, the parallel/printer port and the CUTS cassette all at
// once -- and, decisively, it multiplexes the READINESS of the keyboard, the parallel
// port and the tape into ONE physical status register at 0xFA. Our bus is
// single-driver (one board answers a read, or it floats 0xFF), so three cards could
// not each OR a bit into `IN 0FAH`. The hardware is one register, so the model is one
// board -- and the functions are named UNITS you drive independently:
//
//     CONNECT sol0:serial   socket:2323
//     CONNECT sol0:printer  file:out.txt
//     CONNECT sol0:keyboard console
//     MOUNT   sol0:tape1    "tapes/trk80.tap"
//
// THE TAPES ARE MOUNTED, NOT CONNECTED, AND THERE ARE TWO OF THEM. A cassette has a
// POSITION -- the head is where it is, and the only way back to the start of the
// program is to REWIND (host/tape.h) -- which a ByteStream has nowhere to keep. And
// the Sol-PC really does drive two transports: OUT 0FAh D7 spins deck 1 and D6 spins
// deck 2 (reference/Sol-20.md). `tape` is kept as a case-blind alias for `tape1` so
// that configs written against the old single line still resolve.
//
// PORTS (fixed on the Sol-PC; see reference/Sol-20.md for the bit tables):
//
//     F8  in   serial status   (active HIGH: D6 RX-ready, D7 TX-empty)
//     F9  i/o  serial data
//     FA  in   general status  (MIXED polarity: D0 kbd / D1,2 parallel active LOW,
//                               D3,4,6,7 tape active HIGH)
//     FA  out  tape motor (D7/D6) + cassette 300-baud select (D5)
//     FB  i/o  tape (CUTS) data
//     FC  in   keyboard data   (ready = FA D0, active low)
//     FD  i/o  parallel (printer) data
//     FE  out  VDM display parameter (scroll) -- forwarded to the vdm1 board
//
//     FF (sense switches) is the `fp` board; the VDM screen RAM is the `vdm1` board.
//
// SCOPE. Serial, keyboard and both cassette decks are modeled; the parallel/printer
// port works if you connect it. SOLOS is polled, so this card raises no interrupts.
//
// WHAT THE TAPE IS AND IS NOT, HERE. The bytes on `sol0:tape1` are the bytes the CUTS
// UART sent or received -- the same bargain the 88-ACR's .TAP has. The Kansas City
// AUDIO is a separate seam (host/tapemodem.h) that decodes a .WAV into exactly these
// bytes before the card ever sees them, so nothing below has to know a tone exists.
//
// ONE UART, TWO TRANSPORTS -- which is the hardware, not a saving. There is a single
// CUTS modem on the Sol-PC and a single data register at 0FBh; the motor bits pick
// which deck is turning in front of it. So the card attaches its UART to the deck
// whose motor is on, and to NOTHING when both are off: a stopped transport is not a
// slow line, it is no line at all.

#include "chips/uart1602.h"  // the serial UART -- A CHIP IS NOT A CARD (DESIGN.md 7.8)
#include "core/board.h"
#include "host/stream.h"
#include "host/tape.h"
#include "host/tapecodec.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class VdmBoard;  // the scroll target for OUT 0xFE, located on the bus at run time

class SolBoard : public Board {
public:
    SolBoard();

    std::string type() const override { return "sol"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset) override;
    void power() override;
    void pump() override;

    // A keystroke arriving is live traffic too, so the idle policy stands the machine
    // back up for it -- not just serial bytes (Board::rxBytes, [[idle policy]]).
    uint64_t rxBytes() const override { return serial_.rxBytes() + kbRx_; }

    std::vector<std::string> drainLog() override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // ---- The two cassette decks. MOUNT puts a tape in one. ----
    bool mount(const std::string& unit, const std::string& path, bool ro,
               std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    // REWIND, for the same reason the 88-ACR has it: after a load the head is at the
    // end of the program, and nothing in the guest can wind it back. The Sol needs
    // the unit spelled out, because it has two decks and no sensible default.
    std::vector<CommandDef> commands() const override;
    bool runCommand(const std::string& name, const std::vector<std::string>& args,
                    std::ostream& out, std::string& err) override;

    // For the tests, so they can watch a head move without a filesystem. 1 or 2.
    const TapeImage* tape(int deck) const;

    // The monitor resolves an endpoint string to a stream; the BOARD may not know what
    // a socket is (DESIGN.md 7.7). Wired once in main.cpp / tests/main.cpp.
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The connector behind a unit, for an operator that owns the endpoint (the MCP
    // console). Non-owning. See Board::unitStream.
    ByteStream* unitStream(const std::string& unit) override;

    // ---- The pins, so a test can look at them without going through the bus. ----
    uint8_t serialStatus() const;   // what IN 0xF8 returns
    uint8_t generalStatus() const;  // what IN 0xFA returns
    bool    keyboardReady() const { return kbHave_; }

private:
    // Take one character off the keyboard line into the holding register, if one is
    // waiting and the register is free. Called from pump() ONLY -- reading the host
    // from inside a bus cycle is the thing the chip/card split exists to prevent.
    void latchKeyboard();

    bool     serialTxEmpty() const;
    bool     tapeTxEmpty() const;
    bool     printerWritable() const { return printer_ && printer_->writable(); }
    std::string endpointOf(const std::string& unit) const;

    // ---- The cassette decks ------------------------------------------------
    struct Deck {
        std::unique_ptr<TapeImage> tape;
        std::string                path;
        // PLAY, until somebody says otherwise -- and a tape that is playing cannot be
        // written over. Same reasoning as the 88-ACR's, in host/tape.h.
        TapeStream::Mode mode = TapeStream::Mode::Play;
        bool             motor = false;  // OUT 0FAh D7 (deck 1) / D6 (deck 2)

        // How to READ the file, and what it turned out to be. Per deck, because the two
        // transports hold two different cassettes (host/tapecodec.h).
        std::string format = "auto";
        std::string detected;

        // THE TAPE, IF IT IS AUDIO -- non-owning, null for a byte tape or an empty
        // deck. `tape` owns the medium; this reaches the one thing only an audio tape
        // has, which is an encoding to write itself back with.
        AudioTapeMedia* audio = nullptr;

        // Seconds of idle tone either side of a recording, when writing audio. MEASURED
        // off the one genuine cassette dub we hold -- TRK80.WAV carries 3.05 s of
        // leading mark and 1.93 s of trailing, and you can see the operator's finger in
        // it. NOT the 88-ACR's 15 s: that is what the MITS manual asks an operator to
        // do, and this is what a Sol tape actually turned out to be. Per deck, because
        // the two transports hold two different cassettes.
        long long leader  = 3;
        long long trailer = 2;

        // The CARRIER SHAPE this deck writes audio with: "square" (default -- what a real
        // Sol modem lays down and what a genuine dub sounds like) or "sine". Audible only;
        // a re-mount decodes either identically (host/tapemodem.h).
        std::string wave = "square";

        // HOW FAST THIS DECK PLAYS BACK, on the tape's clock and not the CPU's. "full"
        // (default) empties the cassette as fast as the loader reads it, at any clock_hz;
        // "real" paces playback in wall time at the baud the guest has selected (0FAh D5:
        // 300 or 1200), the wait a real Sol made you serve. See host/tape.h.
        std::string rate = "full";
    };

    // WHAT THIS CARD'S CUTS MODEM CAN HEAR -- two formats, HONESTLY: the UART really
    // does run at 300 or 1200 baud and the GUEST picks which at OUT 0FAh D5. That is a
    // switch on the hardware, not the host guessing at a standard. An 88-ACR tape
    // (2400/1850 FSK) is refused: 1850 Hz is nowhere near the 1200 Hz space this
    // demodulator is built around, and a real Sol fed one would read nothing.
    static const std::vector<TapeFormat>& modem();

    // Which deck, for a unit name. 1, 2, or 0 for "that is not a deck". Case-blind,
    // and `tape` means `tape1`.
    static int deckOf(const std::string& unit);
    UnitDef    deckUnit(int n) const;  // what SHOW says about one transport
    Deck*       deck(int n);
    const Deck* deck(int n) const;

    // Put the CUTS UART's line on whichever deck is turning -- or on nothing, if
    // neither is. Called whenever the motor bits, the mounted tape, the mode or the
    // head position changes, i.e. whenever the answer could differ.
    //
    // DECK 1 WINS IF BOTH MOTORS ARE ON. On the real machine both transports move and
    // both preamps drive one audio bus, which is a collision the manual does not
    // describe and no program does on purpose. Picking one beats inventing what a
    // shorted line sounds like.
    void retape();

    // Push a deck's `leader`/`trailer` onto its tape. A no-op unless that tape is audio.
    static void applyEncoding(Deck* d);

    // THE TRANSPORT STOPPED -- an audio tape re-encodes itself and goes to the host.
    // Called wherever something ENDS a recording: UNMOUNT, REWIND, releasing RECORD,
    // and -- uniquely to this card -- the guest dropping the motor line, which the
    // 88-ACR has no way to do. See MediaFile::commit() for why this is not sync().
    void commitTape(Deck* d);

    // The baud strap that is not a strap: OUT 0FAh D5 picks 300 or 1200, at run time,
    // from the guest. See the .cpp -- it is the one place the Sol's cassette differs
    // structurally from the 88-ACR's soldered 300.
    void programTapeBaud();

    // The vdm1 board on the same bus, for OUT 0xFE (scroll). Located each time rather
    // than cached, so a machine rebuilt under us (CONFIG LOAD) is never stale; OUT
    // 0xFE is rare (only on scroll), so the walk is free. Null if there is no VDM.
    VdmBoard* vdm() const;

    static Clock& deadCard();  // a stopped clock, for when no crystal is attached

    uint8_t base_ = 0xF8;  // F8..FE on the Sol-PC; a property for tests, fixed in fact

    // ---- The serial port: a real UART on F8/F9 (src/chips/uart1602.h). ----
    Uart1602 serial_{"serial"};

    // ---- The CUTS cassette: one UART on FB, two transports in front of it. ----
    Uart1602 tapeUart_{"tape"};
    Deck     deck1_;
    Deck     deck2_;

    // ---- The other two lines: connectable byte streams. Never null (NullStream
    //      stands in) so the read/write path has no pointer to check. ----
    std::unique_ptr<ByteStream> kb_;       // keyboard  (FC data, FA D0)
    std::unique_ptr<ByteStream> printer_;  // parallel  (FD data, FA D1/D2)

    // The keyboard holding register + its strobe (FA D0, active low = a key waiting).
    uint8_t  kbData_ = 0;
    bool     kbHave_ = false;
    uint64_t kbRx_   = 0;  // keystrokes handed to the guest -- the idle-traffic signal

    uint8_t  tapeCtl_ = 0;  // last OUT 0xFA -- motors (D7/D6) + baud select (D5)

    // A motor fell since the last pump(), so that deck's recording wants writing back.
    // Set in the bus cycle, acted on in pump() -- the codec must not run inside a bus
    // cycle, and OUT 0FAh is one.
    bool stop1_ = false;
    bool stop2_ = false;

    // What a mount had to say for itself, until drainLog() carries it off.
    std::vector<std::string> log_;
};

} // namespace altair
