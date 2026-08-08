#pragma once
//
// TerminalEmulator -- the DIALECT half of a terminal: a state machine that turns an
// incoming byte stream into operations on a TerminalScreen (host/terminal/screen.h).
//
// This is where terminals differ. The SD Systems VDB-8024 answers its v1.6 firmware's
// control codes (SdVdb16Emulator, src/boards/sd-vdb8024.cpp); a VT100, an ADM-3A, a
// VT52 and a Heath/Zenith H19 each answer their own -- and each will be a subclass here
// (issue #244). The screen and the renderer are shared; only this changes.
//
// feed() takes ONE display byte and applies its effect to the screen. reset() returns
// the parser to its ground state (a mid-sequence ESC is abandoned), as an S-100 RESET
// or a power cycle does. Key ENCODING (a host arrow key -> the guest's byte sequence)
// is the emulator's too and joins this interface with the multi-window work (Task 2).

#include <cstdint>

namespace altair {

class TerminalScreen;

class TerminalEmulator {
public:
    virtual ~TerminalEmulator() = default;

    // Apply one display byte to the screen, advancing any multi-byte sequence.
    virtual void feed(uint8_t b, TerminalScreen& scr) = 0;

    // Abandon any partial sequence and return to the ground state.
    virtual void reset() = 0;
};

} // namespace altair
