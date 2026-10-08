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
// which RDOS ROM it carries (3.12), and the two places its port 04 differs from the 16FDC's:
// no ¬RESTORE line on the way out, and D6 always 1 on the way in.

#include "boards/cromemco-fdc.h"

namespace altair {

class Fdc64Board : public CromemcoFdcBoard {
public:
    std::string type() const override { return "64fdc"; }

protected:
    int         romBytes() const override { return 8192; }   // RDOS 3.12 is 8K: C000-DFFF
    std::string romName() const override { return "rdos312"; }

    // The 64FDC drops port 04's ¬RESTORE line (reference §5: "not assigned" -- eject/fast-seek/
    // restore go away with the simpler PerSci 299B). Its drivers home the head with the 1793's
    // own Restore command, not this register.
    bool        auxRestoreHomesHead() const override { return false; }

    // Port 04 IN D6 is ALWAYS 1 on the 64FDC (manual 023-2022 p.33; the schematic holds the
    // TMS 5501's XI6 high through a resistor) -- the board has no seek-complete input, because
    // it has no voice-coil drive lines at all. The bit is not idle: CDOS probes it each time it
    // logs in an 8-inch drive (OUT 04 with D5 low, IN 04, BIT 6). A 0 marks the drive as a
    // voice-coil PerSci, which CDOS homes ONLY with ¬RESTORE -- the line this board lacks -- so
    // the head never reaches track 0 and the boot stops on a record-not-found. A 1 makes CDOS
    // home with the FD1793's own Restore.
    uint8_t     readAux() override { return CromemcoFdcBoard::readAux() | 0x40; }
};

} // namespace altair
