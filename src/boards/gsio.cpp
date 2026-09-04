#include "boards/gsio.h"

namespace altair {

// TWO channels, per-unit properties (units "a"/"b", configured as `[board.unit.a]`). The
// default 4-port block is 0-3: Serial A at 0/1, Serial B at 2/3. Both come up as MITS SIO
// Rev 0 (`sior0`) -- A at the profile's own 0/1, B the same shape with its ports overridden
// to 2/3. CONFIG SAVE writes `profile` before the port overrides, so `[board.unit.b]
// profile="sior0"` + status_port=2 / data_port=3 reloads to exactly 2/3.
GsioBoard::GsioBoard() {
    setBoardLevelProperties(false);  // per-unit props (units a/b), not board-level

    SerialStraps a{/*status*/ 0x00, /*data*/ 0x01, /*dav*/ 0, /*tbmt*/ 7, /*inverterGate*/ true};
    addChannel("a", a, "sior0");

    SerialStraps b = a;
    b.statusPort = 0x02;
    b.dataPort   = 0x03;
    addChannel("b", b, "sior0");  // report `sior0`; the 2/3 port overrides round-trip
}

} // namespace altair
