#pragma once
//
// Cromemco 16FDC (1979-1981) -- the double-density member of the FDC family, and the board
// Joe's CDOS runs on. An FD1793 (single + double density), a 4K RDOS boot PROM at C000, and
// an OUT 40H bank-select, all on the shared CromemcoFdcBoard base. See cromemco-fdc.h and
// docs/boards/cromemco-16fdc.md.
//
// A THIN LEAF. Everything that makes it a 16FDC/64FDC -- the FD1793, the 4K ROM, OUT 40H,
// the mixed-density probe, the port-34 layout -- lives in the base, because the 64FDC is
// identical in every one of those respects (Phase 1). This leaf answers only the two things
// that genuinely differ: the board name, and which RDOS ROM it carries (2.52 here -- Joe's).

#include "boards/cromemco-fdc.h"

namespace altair {

class Fdc16Board : public CromemcoFdcBoard {
public:
    std::string type() const override { return "16fdc"; }

protected:
    std::string romName() const override { return "rdos252"; }
};

} // namespace altair
