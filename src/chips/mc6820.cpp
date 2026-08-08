#include "chips/mc6820.h"

#include "core/statefile.h"

namespace altair {
namespace {

constexpr uint8_t kDdrSelect   = 0x04;  // control bit 2: 0 = DDR, 1 = data reg
constexpr uint8_t kIrq1Flag    = 0x80;  // status bit 7: C1/IRQ1 -- a byte has arrived
constexpr uint8_t kC1IntEnable = 0x01;  // control bit 0: C1 interrupt enable
constexpr uint8_t kCtrlStored  = 0x3F;  // bits 5..0 writable; 7,6 are read-only status

} // namespace

uint8_t Pia6820::read(int section, int reg) {
    Section& s = sec_[section & 1];
    if (reg == 0) {
        // Control/status: the stored bits plus the live IRQ1 flag (data available).
        uint8_t v = s.ctrl & kCtrlStored;
        if (s.inFull) v |= kIrq1Flag;
        return v;
    }
    // Data address.
    if (s.ctrl & kDdrSelect) {
        // Reading the DATA register hands over the input latch and clears the flag
        // (6820: bit 7 and IRQ clear on a data read).
        s.inFull = false;
        return s.inLatch;
    }
    return s.ddr;  // the DDR is what the data address reaches when control bit 2 is 0
}

void Pia6820::write(int section, int reg, uint8_t data) {
    Section& s = sec_[section & 1];
    if (reg == 0) {
        // Control: bits 5..0 are ours; 7 and 6 are read-only status flags.
        s.ctrl = data & kCtrlStored;
        return;
    }
    // Data address.
    if (s.ctrl & kDdrSelect) {
        s.outReg = data;
        s.outNew = true;   // the board picks this up with takeOutput()
    } else {
        s.ddr = data;      // program the data direction
    }
}

void Pia6820::reset() {
    for (Section& s : sec_) s = Section{};
}

void Pia6820::deliver(int section, uint8_t byte) {
    Section& s = sec_[section & 1];
    s.inLatch = byte;
    s.inFull  = true;
}

bool Pia6820::inputFull(int section) const { return sec_[section & 1].inFull; }

bool Pia6820::takeOutput(int section, uint8_t& out) {
    Section& s = sec_[section & 1];
    if (!s.outNew) return false;
    out      = s.outReg;
    s.outNew = false;
    return true;
}

bool Pia6820::irq(int section) const {
    const Section& s = sec_[section & 1];
    return s.inFull && (s.ctrl & kC1IntEnable);
}

void Pia6820::serialize(StateWriter& w) const {
    for (const Section& s : sec_) {
        w.u8(s.ctrl);
        w.u8(s.ddr);
        w.u8(s.outReg);
        w.u8(s.inLatch);
        w.boolean(s.inFull);
        w.boolean(s.outNew);
    }
}

void Pia6820::deserialize(StateReader& r) {
    for (Section& s : sec_) {
        s.ctrl    = r.u8();
        s.ddr     = r.u8();
        s.outReg  = r.u8();
        s.inLatch = r.u8();
        s.inFull  = r.boolean();
        s.outNew  = r.boolean();
    }
}

} // namespace altair
