#pragma once
//
// Intel 8255A programmable peripheral interface -- a CHIP, NOT A CARD.
//
// Modeled from the 8255A data sheet (reference/Intel 8255.md). Three 8-bit ports and a
// control register, addressed by A1 A0. It knows nothing about S-100: the card decodes
// the ports and says what is on the pins.
//
// MODE 0 ONLY. A mode word for mode 1 or 2 is stored and sets the directions of the ports,
// but the handshake on port C and its status bits do not exist. No guest of the first
// board with this chip uses them (the Vector Graphic ZCB, whose J1 has no device here).

#include <cstdint>

namespace altair {

class StateWriter;
class StateReader;

class Intel8255 {
public:
    Intel8255() { reset(); }

    // A1 A0: 0 port A, 1 port B, 2 port C, 3 control.
    uint8_t read(uint8_t reg) const;
    void    write(uint8_t reg, uint8_t v);

    // The RESET pin: every port an input, every output register cleared.
    void reset();

    // What the outside world holds on the pins of an input port. The card sets these. An
    // output port reads its own latch, because the latch drives the same pins.
    uint8_t pinA = 0xFF;
    uint8_t pinB = 0xFF;
    uint8_t pinC = 0xFF;

    // ---- state in true sense, for the card and for tests ----
    uint8_t control() const { return control_; }
    uint8_t outA() const { return outA_; }
    uint8_t outB() const { return outB_; }
    uint8_t outC() const { return outC_; }

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    uint8_t control_ = 0x9B;   // the last mode word. Reset gives all four inputs
    uint8_t outA_    = 0;
    uint8_t outB_    = 0;
    uint8_t outC_    = 0;
};

} // namespace altair
