#include "chips/intel8259a.h"

#include "core/statefile.h"

#include <cstdio>

namespace altair {

// ---------------------------------------------------------------------------
// A PORT WRITE. A0 = 0 is the "lower" port (ICW1, then OCW2/OCW3 once initialized);
// A0 = 1 is the "upper" port (ICW2-4 during init, else OCW1 the mask). Which one a byte
// is depends on the initialization state machine -- there is no separate address.
// ---------------------------------------------------------------------------
void Intel8259a::write(bool a0, uint8_t v) {
    if (!a0) {
        // ICW1 is the byte with D4 = 1 on the lower port; it (re)starts init.
        if (v & 0x10) {
            icw1_ = v;
            ltim_ = (v & 0x08) != 0;
            adi4_ = (v & 0x04) != 0;
            sngl_ = (v & 0x02) != 0;
            ic4_  = (v & 0x01) != 0;
            // ICW1 clears the mask and the in-service register, resets priority to
            // fully-nested, drops special mask mode and points reads at the IRR, and
            // resets the edge-sense latch (a currently-high line must fall and rise
            // again before it is seen). Same effects a real 8259A applies on ICW1.
            imr_     = 0;
            isr_     = 0;
            edge_    = 0;
            pins_    = 0xFF;
            lowPri_  = 7;
            readIsr_ = false;
            smm_     = false;
            if (!ic4_) icw4_ = 0;  // no ICW4 => 8080/85 mode, normal EOI, non-buffered
            initStep_ = 2;         // ICW2 comes next
            icw1Seen_ = true;
            return;
        }
        // Not ICW1: an OCW on the lower port. D3 picks OCW3 (1) from OCW2 (0).
        if (v & 0x08) {
            // OCW3. We honor the read-register select (RR=D1, RIS=D0) and the special
            // mask mode enable (ESMM=D6, SMM=D5). The poll command (P=D2) is not modeled.
            if (v & 0x02) readIsr_ = (v & 0x01) != 0;
            if (v & 0x40) smm_     = (v & 0x20) != 0;
            return;
        }
        ocw2(v);  // OCW2: the EOI / rotate / set-priority family
        return;
    }

    // The upper port: the rest of the init sequence, or OCW1 (the mask) once done.
    switch (initStep_) {
    case 2:
        icw2_     = v;
        initStep_ = sngl_ ? (ic4_ ? 4 : 0) : 3;  // ICW3 only in cascade mode
        return;
    case 3:
        icw3_     = v;
        initStep_ = ic4_ ? 4 : 0;
        return;
    case 4:
        icw4_     = v;
        initStep_ = 0;
        return;
    default:
        imr_ = v;  // OCW1
        return;
    }
}

// A0 = 1 reads the mask; A0 = 0 reads the ISR or the IRR, per the last OCW3. Pure --
// reading an 8259A register never changes its state.
uint8_t Intel8259a::read(bool a0, uint8_t live) const {
    if (a0) return imr_;
    return readIsr_ ? isr_ : effectiveIrr(live);
}

void Intel8259a::powerOn() {
    icw1_ = icw2_ = icw3_ = icw4_ = 0;
    ltim_ = adi4_ = sngl_ = ic4_ = false;
    initStep_ = 0;
    icw1Seen_ = false;
    imr_  = 0xFF;  // all masked: an unprogrammed controller drives no interrupt
    isr_  = 0;
    edge_ = 0;
    pins_ = 0xFF;
    lowPri_  = 7;
    readIsr_ = false;
    smm_     = false;
}

// ---------------------------------------------------------------------------
// PRIORITY. IR0 is highest and IR7 lowest by default; rotation moves the bottom of the
// order to lowPri_, so the priority order from highest is IR(lowPri_+1) .. IR(lowPri_).
//
// A request wins if it is unmasked and NOTHING OF EQUAL-OR-HIGHER PRIORITY IS IN
// SERVICE (fully-nested). We walk the eight levels highest-priority first: the first
// in-service bit we meet blocks everything at or below it, and the first request bit we
// meet before any block is the winner. In special mask mode, an in-service level whose
// mask bit is set does NOT block -- that is the whole point of the mode.
// ---------------------------------------------------------------------------
int Intel8259a::winner(uint8_t live) const {
    uint8_t req = (uint8_t)(effectiveIrr(live) & ~imr_);
    if (!req) return -1;
    for (int r = 0; r < 8; ++r) {
        int     lvl = (lowPri_ + 1 + r) & 7;
        uint8_t bit = (uint8_t)(1u << lvl);
        if ((isr_ & bit) && !(smm_ && (imr_ & bit))) return -1;  // blocked from here down
        if (req & bit) return lvl;
    }
    return -1;
}

void Intel8259a::senseEdges(uint8_t live) {
    edge_ |= (uint8_t)(live & ~pins_);  // rising edges latch; masked in level mode's math
    pins_ = live;
}

int Intel8259a::acknowledge(uint8_t live) {
    int w = winner(live);
    if (w < 0) return -1;
    uint8_t bit = (uint8_t)(1u << w);
    isr_ |= bit;
    if (!ltim_) edge_ &= (uint8_t)~bit;  // edge mode: the request latch clears on ack
    return w;
}

int Intel8259a::highestIsr() const {
    for (int r = 0; r < 8; ++r) {
        int lvl = (lowPri_ + 1 + r) & 7;
        if (isr_ & (uint8_t)(1u << lvl)) return lvl;
    }
    return -1;
}

// OCW2: bits 7-5 are R (rotate), SL (specific level), EOI; bits 2-0 the level.
void Intel8259a::ocw2(uint8_t v) {
    int level = v & 0x07;
    switch (v & 0xE0) {
    case 0x20: {  // non-specific EOI: clear the highest-priority in-service bit
        int h = highestIsr();
        if (h >= 0) isr_ &= (uint8_t)~(1u << h);
        break;
    }
    case 0x60:  // specific EOI
        isr_ &= (uint8_t)~(1u << level);
        break;
    case 0xA0: {  // rotate on non-specific EOI: clear highest, make it lowest priority
        int h = highestIsr();
        if (h >= 0) {
            isr_ &= (uint8_t)~(1u << h);
            lowPri_ = h;
        }
        break;
    }
    case 0xE0:  // rotate on specific EOI
        isr_ &= (uint8_t)~(1u << level);
        lowPri_ = level;
        break;
    case 0xC0:  // set priority: no EOI, just move the bottom of the rotation
        lowPri_ = level;
        break;
    default:
        // 0x80 / 0x00 (rotate in automatic-EOI set/clear) and 0x40 (no-op) are not
        // modeled -- automatic EOI itself is not modeled (see the header).
        break;
    }
}

uint16_t Intel8259a::callAddress(int level) const {
    uint16_t hi = (uint16_t)((uint16_t)icw2_ << 8);  // A15-A8 = ICW2
    // A7-A5 (interval 4) or A7-A6 (interval 8) come from ICW1; the level fills the
    // bits below, scaled by the interval.
    uint16_t lo = adi4_ ? (uint16_t)((icw1_ & 0xE0) | ((level & 7) << 2))
                        : (uint16_t)((icw1_ & 0xC0) | ((level & 7) << 3));
    return (uint16_t)(hi | lo);
}

uint8_t Intel8259a::vector8086(int level) const {
    return (uint8_t)((icw2_ & 0xF8) | (level & 7));
}

std::string Intel8259a::describe(uint8_t live) const {
    char buf[96];
    std::snprintf(buf, sizeof buf, "IRR=%02X ISR=%02X IMR=%02X INT=%d%s",
                  (unsigned)effectiveIrr(live), (unsigned)isr_, (unsigned)imr_,
                  intOut(live) ? 1 : 0, icw1Seen_ ? "" : " (unprogrammed)");
    return buf;
}

void Intel8259a::serialize(StateWriter& w) const {
    w.u8(icw1_);
    w.u8(icw2_);
    w.u8(icw3_);
    w.u8(icw4_);
    w.boolean(ltim_);
    w.boolean(adi4_);
    w.boolean(sngl_);
    w.boolean(ic4_);
    w.u8((uint8_t)initStep_);
    w.boolean(icw1Seen_);
    w.u8(imr_);
    w.u8(isr_);
    w.u8(edge_);
    w.u8(pins_);
    w.u8((uint8_t)lowPri_);
    w.boolean(readIsr_);
    w.boolean(smm_);
}

void Intel8259a::deserialize(StateReader& r) {
    icw1_     = r.u8();
    icw2_     = r.u8();
    icw3_     = r.u8();
    icw4_     = r.u8();
    ltim_     = r.boolean();
    adi4_     = r.boolean();
    sngl_     = r.boolean();
    ic4_      = r.boolean();
    initStep_ = r.u8();
    icw1Seen_ = r.boolean();
    imr_      = r.u8();
    isr_      = r.u8();
    edge_     = r.u8();
    pins_     = r.u8();
    lowPri_   = r.u8();
    readIsr_  = r.boolean();
    smm_      = r.boolean();
}

}  // namespace altair
