#pragma once
//
// TwoStageTx -- the DOUBLE-BUFFERED TRANSMITTER that every UART in this tree has: a
// holding register the CPU writes, in front of a shift register that clocks the byte
// onto the line. Shared by the 1602, the 6850, the 8251, the 2651 and the TMS 5501,
// because all five data sheets describe the same two stages (issue #899).
//
// The two stages give two status answers, and they are NOT the same answer:
//
//   holdEmpty -- the holding register can take another byte. TBMT (1602), TDRE (6850),
//                TxRDY (8251, 2651), TBE (5501).
//   lineIdle  -- the shift register has finished too: the last character has left the
//                wire. TEOC (1602), TxEMPTY (8251), TxEMT (2651). The 6850 and the 5501
//                have no pin or bit for it.
//
// A byte written to an IDLE transmitter moves into the shift register after a short
// TRANSFER DELAY (xferT). Until then the holding register is full and the shift register
// has not started, so holdEmpty is false and lineIdle is still true. At the transfer the
// holding register is empty again, and lineIdle goes false for one character. A byte
// written while another is still shifting waits in the holding register until that shift
// ends, and moves at once. Output written back to back therefore runs at exactly the line
// rate -- one character time per byte, and no more.
//
// The transfer delay is the chip's, and each caller passes its own:
//
//   1602 (COM2502)  1/16 bit -- "Start bit appears within one clock period (TCP) after
//                   the trailing edge of TDS#" (reference/com2502.md); TCP is the 16x clock.
//   6850            1 bit    -- "within 1 bit-time of the write if the transmitter is
//                   idle" (reference/6850.md).
//   8251, 2651, 5501  0      -- our references give no figure.
//
// The 1602 and 6850 values are the data sheets' bounds. A real 8800c, measured on #899,
// comes close: TDRE back 0.6 to 1.5 bit times after an idle write on an MC68B50, TBMT
// within two poll loops on a COM2502. The real delay depends on the phase of the bit
// clock, which no chip here models, so the bound stands in for it.
//
// ONE CHOICE, stated so nobody mistakes it for a measurement:
//
//  - A write while the holding register is still full queues behind the byte already
//    there. The real chip would overwrite it, but the host stream has already taken that
//    byte, so the model keeps it rather than pretend it was lost. This is the same
//    tolerance the single-register model had.
//
// Both deadlines are absolute T-states, so they stay valid across SNAPSHOT/RESTORE (the
// Clock's t_ travels).

#include "core/statefile.h"

#include <algorithm>
#include <cstdint>

namespace altair {

class TwoStageTx {
public:
    // /TDS, or a write to the data register. charT is how long one character occupies
    // the line, and xferT the chip's transfer delay from an idle line, in T-states.
    void load(uint64_t now, uint64_t charT, uint64_t xferT) {
        uint64_t start;                     // when the byte enters the shift register
        if (now >= lineFreeAt_) {
            start       = now + xferT;      // idle: after the transfer delay
            idleUntil_  = start;            // ...and the shift register waits until then
        } else {
            start = lineFreeAt_;            // busy: the moment the current character ends
        }
        holdFreeAt_ = start;
        lineFreeAt_ = start + charT;
    }

    // Reset: both registers empty, the line at rest.
    void reset(uint64_t now) { holdFreeAt_ = lineFreeAt_ = idleUntil_ = now; }

    bool holdEmpty(uint64_t now) const { return now >= holdFreeAt_; }
    bool lineIdle(uint64_t now) const { return now >= lineFreeAt_ || now < idleUntil_; }

    uint64_t holdFreeAt() const { return holdFreeAt_; }
    uint64_t lineFreeAt() const { return lineFreeAt_; }

    void serialize(StateWriter& w) const {
        w.u64(holdFreeAt_);
        w.u64(lineFreeAt_);
        w.u64(idleUntil_);
    }
    void deserialize(StateReader& r) {
        holdFreeAt_ = r.u64();
        lineFreeAt_ = r.u64();
        idleUntil_  = r.u64();
    }

private:
    uint64_t holdFreeAt_ = 0;  // the holding register is empty from this T-state
    uint64_t lineFreeAt_ = 0;  // ...and the shift register, from this one
    uint64_t idleUntil_  = 0;  // an idle line's shift register starts here (the transfer)
};

}  // namespace altair
