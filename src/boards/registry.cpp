#include "boards/registry.h"

#include "boards/hostbridge.h"
#include "boards/mits-88acr.h"
#include "boards/mits-88c700.h"
#include "boards/mits-88cpu.h"
#include "boards/mits-88dcdd.h"
#include "boards/mits-88mds.h"
#include "boards/mits-88virtc.h"
#include "boards/mits-frontpanel.h"
#include "boards/mits-z80cpu.h"
#include "boards/s100-memory.h"
#include "boards/mits-2sio.h"
#include "boards/mits-88sio.h"

namespace altair {

// Milestone 1a is CLI + bus + memory, and NO CPU: the monitor is the bus master.
// That is not a limitation to apologize for -- it is the point. Every claim the
// bus design makes (a ROM that never answers a write, an empty socket that
// floats, a PHANTOM* overlay that is not contention, five incompatible banking
// cards) is testable with two boards, a hex file, and no processor. And it is
// worth testing BEFORE a CPU exists, because those behaviors differ SILENTLY:
// get one wrong and the symptom is a guest misbehaving ten thousand
// instructions later.
// The type name is the CHIP, because that is the word an operator reaches for --
// `BOARDS ADD 8080 cpu0`. Nobody asks for an 88-CPU by its catalog number, and
// when the Z80 cards land they will be `z80`, which is what people called those
// too. The card's identity lives in its .md, where it belongs.
std::vector<BoardType> boardTypes() {
    return {
        {"memory", "RAM/ROM card: a list of regions, PHANTOM*, and five banking schemes"},
        {"8080", "MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus"},
        {"z80", "Generic Z80 CPU card. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core"},
        {"2sio", "MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3"},
        {"sio", "MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits"},
        {"dcdd", "MITS 88-DCDD: 8\" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits"},
        {"mds", "MITS 88-MDS: 5.25\" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s"},
        {"acr", "MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the REWIND verb"},
        {"c700", "MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file"},
        {"fp", "Altair front panel: the SENSE switches at port FF (read-only), and the lamps"},
        {"virtc", "MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE"},
        {"hostbridge", "Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN CARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM"},
    };
}

std::unique_ptr<Board> makeBoard(const std::string& type) {
    if (type == "memory") return std::make_unique<MemoryBoard>();
    if (type == "8080") return std::make_unique<Cpu8080Board>();
    if (type == "z80") return std::make_unique<CpuZ80Board>();
    if (type == "2sio") return std::make_unique<Sio2Board>();
    if (type == "sio") return std::make_unique<SioBoard>();
    if (type == "dcdd") return std::make_unique<DcddBoard>();
    if (type == "mds") return std::make_unique<MdsBoard>();
    if (type == "acr") return std::make_unique<AcrBoard>();
    if (type == "c700") return std::make_unique<C700Board>();
    if (type == "fp") return std::make_unique<FrontPanelBoard>();
    if (type == "virtc") return std::make_unique<VirtcBoard>();
    if (type == "hostbridge") return std::make_unique<HostBridgeBoard>();
    return nullptr;
}

} // namespace altair
