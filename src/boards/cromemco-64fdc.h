#pragma once
//
// Cromemco 64FDC (1983) -- the 16FDC's successor. Same FD1793, same OUT 40H bank-select; it
// differs from the 16FDC only in details Phase 1 does not model (its front-panel switches set
// baud/boot-drive/self-test instead of the 16FDC's RDOS-defeat functions, and it drops the
// 16FDC's RTC/Mode-2 jumpers -- reference §1) -- plus one that it does: the RDOS PROM is 8K
// (C000-DFFF), not the 16FDC's 4K. RDOS 3.12 grew into the second 4K, which is why romBytes()
// is overridden here (rdos0312.lst / rdos0312.bin span C000-DFFF). See cromemco-fdc.h and
// docs/boards/cromemco-64fdc.md.
//
// A THIN LEAF, for the same reason as the 16FDC: it answers only the board name, its ROM size,
// and which RDOS ROM it carries (3.12). The port-04 subset (the 64FDC drops the 16FDC's
// eject/fast-seek/restore bits) is a Phase-4 follow-up, when port 04 stops being an inert stub.

#include "boards/cromemco-fdc.h"

namespace altair {

class Fdc64Board : public CromemcoFdcBoard {
public:
    std::string type() const override { return "64fdc"; }

protected:
    int         romBytes() const override { return 8192; }   // RDOS 3.12 is 8K: C000-DFFF
    std::string romName() const override { return "rdos312"; }
};

} // namespace altair
