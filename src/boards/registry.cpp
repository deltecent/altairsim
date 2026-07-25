#include "boards/registry.h"

#include "boards/cromemco-d7a.h"
#include "boards/cromemco-dazzler.h"
#include "boards/hostbridge.h"
#include "boards/mits-88acr.h"
#include "boards/mits-884pio.h"
#include "boards/mits-88c700.h"
#include "boards/mits-88cpu.h"
#include "boards/mits-88lpc.h"
#include "boards/mits-88pio.h"
#include "boards/mits-88uio.h"
#include "boards/mits-88dcdd.h"
#include "boards/mits-88hdsk.h"
#include "boards/mits-88mds.h"
#include "boards/mits-88virtc.h"
#include "boards/mits-frontpanel.h"
#include "boards/mits-turnkey.h"
#include "boards/mits-z80cpu.h"
#include "boards/proctech-sol.h"
#include "boards/proctech-vdm1.h"
#include "boards/s100-memory.h"
#include "boards/sd-sbc.h"
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
        {"memory", "RAM/ROM board: a list of regions, PHANTOM*, and five banking schemes"},
        {"8080", "MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus"},
        {"z80", "Generic Z80 CPU board. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core"},
        {"2sio", "MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3"},
        {"sio", "MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits"},
        {"sbc", "SD Systems SBC-100/200: Z80 SBC console. Intel 8251 USART, unit 'tty'; data at 7C, status/command at 7D. RxD->/DSR auto-baud for MSMONR21. variant=sbc100|sbc200"},
        {"dcdd", "MITS 88-DCDD: 8\" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits"},
        {"mds", "MITS 88-MDS: 5.25\" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s"},
        {"hdsk", "MITS 88-HDSK Datakeeper: Pertec hard disk, 256-byte sectors from a linear .DSK. Eight ports at BASE+0..7 (default A0). Command/handshake protocol, four page buffers"},
        {"acr", "MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the WIND/REWIND/EXTRACT verbs and a tape counter"},
        {"uio", "MITS 88-UIO: serial + cassette on one board. A 6850 (unit 'serial', default 0x10) and an 88-ACR cassette section (unit 'tape', default 0x06) with motor control and a SW-1 MITS/Kansas-City modulation switch. Defaults reproduce the standard 0x10 + 0x06 layout"},
        {"c700", "MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file"},
        {"lpc", "MITS 88-LPC: 88-LP line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Line-buffered: 6-bit codes + PRINT/LINE FEED/CLEAR. CONNECT it to a file"},
        {"pio", "MITS 88-PIO: 8-bit parallel port, units 'out'/'in'. Two ports at BASE+0..1 (default 04). CONNECT a printer, a keyboard, a socket"},
        {"4pio", "MITS 88-4PIO: up to four 6820 PIAs, sections ja/jb.. per port. 16 ports from BASE (default 20). Software-set direction; CONNECT each section"},
        {"vdm1", "Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). Needs a Display"},
        {"dazzler", "Cromemco Dazzler: color graphics from a framebuffer in main RAM. Two ports at BASE+0..1 (default 0E): control/status and format. 32x32 to 128x128, 16 colors/greys. Needs a Display"},
        {"d7a", "Cromemco D+7A: analog + parallel I/O. Eight ports from BASE (default 18): one parallel port + seven two's-complement A/D-in/D/A-out channels. Reads 1-2 JS-1 joysticks from the host"},
        {"sol", "Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one board. Seven ports F8..FE. Units serial/printer/keyboard (CONNECT) and tape1/tape2 (MOUNT). Brings the WIND/REWIND/EXTRACT verbs and a tape counter"},
        {"fp", "Altair front panel: the SENSE switches at port FF (read-only), and the lamps"},
        {"turnkey", "MITS 8800b Turnkey Module: phantom boot PROM (FC00-FFFF), integrated 6850 SIO (unit 'tty', default 0x10), sense switches at FF, and the Auto-Start JMP jam. Sockets via [[board.socket]]"},
        {"virtc", "MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE"},
        {"hostbridge", "Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN BOARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM"},
    };
}

std::unique_ptr<Board> makeBoard(const std::string& type) {
    if (type == "memory") return std::make_unique<MemoryBoard>();
    if (type == "8080") return std::make_unique<Cpu8080Board>();
    if (type == "z80") return std::make_unique<CpuZ80Board>();
    if (type == "2sio") return std::make_unique<Sio2Board>();
    if (type == "sio") return std::make_unique<SioBoard>();
    if (type == "sbc") return std::make_unique<SbcBoard>();
    if (type == "dcdd") return std::make_unique<DcddBoard>();
    if (type == "mds") return std::make_unique<MdsBoard>();
    if (type == "hdsk") return std::make_unique<HdskBoard>();
    if (type == "acr") return std::make_unique<AcrBoard>();
    if (type == "uio") return std::make_unique<UioBoard>();
    if (type == "c700") return std::make_unique<C700Board>();
    if (type == "lpc") return std::make_unique<LpcBoard>();
    if (type == "pio") return std::make_unique<PioBoard>();
    if (type == "4pio") return std::make_unique<Pio4Board>();
    if (type == "vdm1") return std::make_unique<VdmBoard>();
    if (type == "dazzler") return std::make_unique<DazzlerBoard>();
    if (type == "d7a") return std::make_unique<D7aBoard>();
    if (type == "sol") return std::make_unique<SolBoard>();
    if (type == "fp") return std::make_unique<FrontPanelBoard>();
    if (type == "turnkey") return std::make_unique<TurnkeyBoard>();
    if (type == "virtc") return std::make_unique<VirtcBoard>();
    if (type == "hostbridge") return std::make_unique<HostBridgeBoard>();
    return nullptr;
}

} // namespace altair
