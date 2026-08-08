#pragma once
//
// mc6820.h -- a Motorola 6820/6821 PIA (Peripheral Interface Adapter), the
// register model only.
//
// ONE 6820 = TWO independent sections, A and B. Each section presents three
// registers behind two addresses (a register-select bit) plus Control bit 2:
//
//     reg 0 (control)  -> Control/Status Register     (read: + live IRQ flags)
//     reg 1 (data)     -> Data Register       if Control bit 2 = 1
//                         Data Direction (DDR) if Control bit 2 = 0
//
// A CHIP IS NOT A CARD (see chips/mc6850.h): this has no addresses of its own.
// The owning board decodes its F0xx window, turns an address into a
// (section, reg) pair, and calls read()/write(). The board also owns the
// OUTSIDE WORLD -- it feeds a received byte in with deliver() (which sets the
// input flag, status bit 7) and drains a guest-driven output byte with
// takeOutput(). The 88-4PIO models the very same part INLINE
// (src/boards/mits-884pio.cpp); this is that proven model lifted into a chip so
// the Altair 680b UI/O reuses it rather than copying the register logic a second
// time (the 4pio may migrate onto this later). See [[altairsim-pio-boards]].
//
// Register bits acted on (reference/Altair 680b Universal IO Board.md §3 and
// reference/MITS 88-4PIO.md): Control bit 2 = DDR/Data select; bit 0 = C1
// interrupt enable; bits 5..0 are stored, bits 7,6 are read-only status. Status
// bit 7 is the C1/IRQ1 flag (a byte has arrived) -- set by an input strobe
// (deliver) and cleared by reading the Data Register. DDR bit 1 = output line,
// 0 = input. Power-on reset clears every register, so all lines are inputs and
// all flags clear.

#include <cstdint>

namespace altair {

class StateWriter;
class StateReader;

class Pia6820 {
public:
    static constexpr int kSectionA = 0;
    static constexpr int kSectionB = 1;

    // section: 0 = A, 1 = B. reg: 0 = control/status, 1 = data/DDR.
    uint8_t read(int section, int reg);
    void    write(int section, int reg, uint8_t data);

    void reset();  // power-on / RESET: every register 0

    // The board's door to the outside world -- IT owns the stream, not the chip:
    //  - deliver(): an input strobe. Latch a received byte and raise status bit 7.
    //    Overwriting an unread latch is a silent overrun, so gate on inputFull()
    //    first (the 4pio does the same in its pump()).
    //  - inputFull(): is the one-byte input latch occupied?
    //  - takeOutput(): did the guest just drive a data byte onto the lines?
    //    Consume it once (returns false when nothing is pending).
    void deliver(int section, uint8_t byte);
    bool inputFull(int section) const;
    bool takeOutput(int section, uint8_t& out);

    // The section requests an interrupt when a byte is latched AND C1
    // interrupt-enable (Control bit 0) is set (reference §6).
    bool irq(int section) const;

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    struct Section {
        uint8_t ctrl    = 0;   // control/status: bits 5..0 stored; 7,6 live status
        uint8_t ddr     = 0;   // data-direction: 1 = output line, 0 = input
        uint8_t outReg  = 0;   // last byte the guest drove onto the data lines
        uint8_t inLatch = 0;   // last byte received on the input side
        bool    inFull  = false;
        bool    outNew  = false;  // a guest data write is waiting for takeOutput()
    };
    Section sec_[2];
};

} // namespace altair
