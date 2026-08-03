#pragma once
//
// Cromemco D+7A -- an S-100 analog + parallel I/O card (1976). See reference/D+7A.md,
// reference/JS-1.md and docs/boards/cromemco-d7a.md.
//
// A BLOCK OF EIGHT CONSECUTIVE I/O PORTS, and no memory.
//
//   BASE+0 (default 0x18) -- a PARALLEL port. IN reads eight parallel input lines,
//                            OUT latches eight parallel output lines. Independent.
//   BASE+1..BASE+7        -- SEVEN ANALOG channels. IN <ch> hands back the A/D of that
//                            channel's analog input; OUT <ch> latches that channel's D/A
//                            output. Read and write are INDEPENDENT -- in one direction a
//                            port is an A/D input pin, in the other a D/A output pin
//                            (which is why a JS-1's X-axis A/D-in and its speaker D/A-out
//                            share one port number). Values are 8-bit TWO'S-COMPLEMENT,
//                            20 mV/LSB: 0x00 = 0 V, 0x7F = +2.54 V, 0x80 = -2.56 V.
//
// THE JS-1 JOYSTICK LIVES HERE, NOT ON ITS OWN CARD. A JS-1 console is a peripheral
// that plugs into the D+7A over a cable (reference/JS-1.md); one D+7A carries one or
// two. Cromemco's recommended wiring, which this card follows: console 1's X/Y pots ->
// analog inputs 0x19/0x1A, its four buttons -> parallel-input bits D0-D3; console 2 ->
// 0x1B/0x1C and D4-D7. The stick itself comes from the host Joystick service
// (host/joystick.h), injected at the composition root like a Display -- a USB gamepad
// in the shipping binary, the keyboard as a fallback, a NullJoystick (centered, no
// buttons) headless. The card never touches SDL.
//
// SOUND is deliberately NOT produced yet. A JS-1 makes sound by the CPU writing a
// waveform to a speaker's analog-OUTPUT port (0x19 / 0x1B); reproducing it needs a host
// audio service and a reconciliation of emulated time with wall-clock audio, which is a
// separable follow-up (docs/boards/cromemco-d7a.md). The D/A writes are latched here so
// that path has somewhere to read them; today nothing plays them.
//
// POLLED, and no interrupts. The parallel port's STB handshake and the analog wait
// states (reference/D+7A.md 4) are not modeled -- a polling driver is complete, and the
// wait states matter only for bit-exact sound timing.

#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class Joystick;    // host/joystick.h -- injected; the board never learns it is SDL
struct StickState;

class D7aBoard : public Board {
public:
    D7aBoard() = default;

    std::string type() const override { return "d7a"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset r) override;
    void power() override;
    void pump() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The software-visible latches: the seven A/D input
    // shadows, the seven D/A output latches, and the two parallel bytes. The port strap,
    // the joystick assignments and the host Joystick* do not travel (config/host).
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<std::string> statusLines() const override;
    std::vector<MapEntry> ioMap() const override;

    // The host game-controller service, wired once in main.cpp / tests/main.cpp -- an
    // SdlJoystick in the shipping binary, a NullJoystick headless, a stub in a test.
    // Borrowed; the composition root owns it (like DazzlerBoard::setDisplay).
    static void setJoystick(Joystick* j);

    // ---- For tests, without a controller: the card's software-visible latches. ----
    uint8_t analogIn(int ch) const { return ch >= 0 && ch < 7 ? analogIn_[ch] : 0; }
    uint8_t analogOut(int ch) const { return ch >= 0 && ch < 7 ? analogOut_[ch] : 0; }
    uint8_t parallelIn() const { return parIn_; }
    uint8_t parallelOut() const { return parOut_; }

private:
    // SDL axis (-32768..32767) -> the two's-complement byte an A/D returns. An
    // arithmetic >>8 maps center 0 -> 0x00, +full -> 0x7F, -full -> 0x80.
    static uint8_t axis8(int16_t a);

    // Resolve one console's `joystick*` strap ("none"/"auto"/"keyboard"/<index>) to a
    // stick reading from the injected service. Absent when nothing is behind it.
    // `autoIndex` is the gamepad `auto` prefers for THIS console -- 0 for console 1, 1
    // for console 2 -- so two consoles on `auto` claim two different sticks and a
    // two-player setup works unconfigured; each falls back to the keyboard.
    StickState resolveStick(const std::string& spec, int autoIndex) const;

    // Apply a console's stick to the A/D input shadows and the parallel-input nibble.
    // `xCh`/`yCh` are analog channel indices (0-based: channel 1 -> 0); `buttonShift`
    // is 0 for console 1 (bits D0-D3) or 4 for console 2 (bits D4-D7); `autoIndex` is
    // the gamepad this console's `auto` prefers (0 or 1).
    void applyConsole(const std::string& spec, int xCh, int yCh,
                      int buttonShift, int autoIndex);

    // ---- Straps ----
    uint8_t base_ = 0x18;   // the 8-port block: BASE+0 parallel, BASE+1..7 analog

    // Which host stick drives each JS-1 console. Strings so "none"/"auto"/"keyboard"
    // sit alongside a numeric index (see properties()).
    std::string js1_ = "auto";   // console 1: gamepad 0 if present, else the keyboard
    std::string js2_ = "auto";   // console 2: gamepad 1 if present, else the keyboard

    // ---- Software-visible state ----
    uint8_t analogIn_[7]  = {};  // channel 1..7 A/D shadow (index 0..6), refreshed in pump()
    uint8_t analogOut_[7] = {};  // channel 1..7 D/A latch
    uint8_t parIn_  = 0xFF;      // parallel input byte (JS-1 buttons, ACTIVE-LOW: idle = 1s)
    uint8_t parOut_ = 0;         // parallel output latch
};

} // namespace altair
