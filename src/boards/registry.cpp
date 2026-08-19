#include "boards/registry.h"

#include "boards/cromemco-16fdc.h"
#include "boards/cromemco-64fdc.h"
#include "boards/cromemco-d7a.h"
#include "boards/cromemco-dazzler.h"
#include "boards/dualide.h"
#include "boards/dualsd.h"
#include "boards/bankmem.h"
#include "boards/hostbridge.h"
#include "boards/icom-fd3712.h"
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
#include "boards/mits-680cpu.h"
#include "boards/mits-680io.h"
#include "boards/mits-680kcacr.h"
#include "boards/mits-680uio.h"
#include "boards/mits-frontpanel.h"
#include "boards/mits-turnkey.h"
#include "boards/mits-z80cpu.h"
#include "boards/mits-8085cpu.h"
#include "boards/pmmi-mm103.h"
#include "boards/proctech-sol.h"
#include "boards/proctech-vdm1.h"
#include "boards/s100-memory.h"
#include "boards/sd-sbc.h"
#include "boards/sd-vdb8024.h"
#include "boards/sd-versafloppy.h"
#include "boards/tarbell.h"
#include "boards/usio.h"
#include "boards/propio.h"
#include "boards/v2z80rom.h"
#include "boards/mits-2sio.h"
#include "boards/mits-88sio.h"

namespace altair {

// Milestone 1a is CLI + bus + memory, and NO CPU: the monitor is the bus master.
// That is not a limitation to apologize for -- it is the point. Every claim the
// bus design makes (a ROM that never answers a write, an empty socket that
// floats, a PHANTOM* overlay that is not contention, banked cards that each own
// their decode) is testable with two boards, a hex file, and no processor. And it is
// worth testing BEFORE a CPU exists, because those behaviors differ SILENTLY:
// get one wrong and the symptom is a guest misbehaving ten thousand
// instructions later.
// The type name is the CHIP, because that is the word an operator reaches for --
// `BOARDS ADD 8080 cpu0`. Nobody asks for an 88-CPU by its catalog number, and
// when the Z80 cards land they will be `z80`, which is what people called those
// too. The card's identity lives in its .md, where it belongs.
std::vector<BoardType> boardTypes() {
    return {
        {"memory", "RAM/ROM board: a list of regions and PHANTOM* -- plain, unbanked memory (bank switching is its own board, `bankmem`)"},
        {"bankmem", "S-100 bank-switched RAM. One card, four decoders (card=vector|cromemco64kz|northstar|expandoram2): a write-only select port swaps which RAM plane(s) drive the bus. Each card owns its own decode -- one-hot select (Vector 40), 8-bit bank mask (Cromemco 40), on/off+one-hot toggle (North Star C0), or PROM page-select (ExpandoRAM II FF, approximated)"},
        {"8080", "MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus"},
        {"z80", "Generic Z80 CPU board. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core"},
        {"8085", "Generic 8085 CPU board. Decodes nothing -- it drives the bus. The 88-CPU's twin, with an 8085 core (RIM/SIM + TRAP/RST 5.5/6.5/7.5)"},
        {"6800", "Altair 680b CPU board: a Motorola 6800 at 500 KHz. Decodes nothing -- it drives the bus. The 88-CPU's twin, one core down, with memory-mapped I/O"},
        {"680io", "Altair 680b onboard I/O: a 6850 ACIA console ('tty') at F000/F001 and the config-strap read port at F002. Memory-mapped"},
        {"680uio", "Altair 680b Universal I/O: a second 6850 ACIA serial port ('serial') and a 6820 PIA parallel port (sections 'p1a/p1b', 'p2a/p2b' with pias=2) in an S9-relocatable window (default base F000: serial F006/F007, PIA F008-F00F), plus fixed switch inputs at F003 and a non-latched output at F010-F013. Memory-mapped, active-high"},
        {"680kcacr", "Altair 680b KCACR audio-cassette interface: a 1602-family UART recording Kansas City Standard FSK, memory-mapped at F010 (status/control) and F011 (data), active-LOW. Adds software motor control (control D7=on, D6=off) and interrupt-driven transfer (D0/D1 enables pull the 6800 IRQ). Reuses the 88-ACR tape machinery -- MOUNT a tape, WIND/REWIND it"},
        {"2sio", "MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3"},
        {"sio", "MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits"},
        {"sbc", "SD Systems SBC-100/200: Z80 single-board computer. One 8-port block (78-7F): Intel 8251 console (unit 'tty', data 7C / status 7D, RxD->/DSR auto-baud for MSMONR21), Z80-CTC (78-7B) whose ch1 raises a mode-2 keyboard interrupt (vector 0x82) off the 8251 RxRDY, and a parallel port (7E/7F) whose OUT 7F bit 1 switches the onboard PROM out. Optional onboard boot PROM via [[board.socket]] (at+mount). variant=sbc100|sbc200"},
        {"dcdd", "MITS 88-DCDD: 8\" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits"},
        {"mds", "MITS 88-MDS: 5.25\" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s"},
        {"hdsk", "MITS 88-HDSK Datakeeper: Pertec hard disk, 256-byte sectors from a linear .DSK. Eight ports at BASE+0..7 (default A0). Command/handshake protocol, four page buffers"},
        {"dualsd", "S100Computers Dual SD: two microSD sockets (drives 0/1) presented as raw 512-byte-sector CF/SD cards, for CP/M 3. Two ports at BASE+0..1 (default 80): status/command + data. Programmed-I/O command/handshake engine (33H-lead + 8 commands). No boot PROM -- the CPU board's monitor loads CP/M from track 0. Mount a card image (a .img with a .geo geometry sidecar)"},
        {"dualide", "S100Computers IDE-AB (CF): the IDE/CompactFlash half of the IDE+ESP32 combination board -- two CF sockets (drives 0/1 = A:/B:) for CP/M 3. Five 8255 ports at BASE+0..4 (default 30): A/B data, C control lines, mode config, drive select. Programmed-I/O ATA register engine (LBA read/write, 512-byte sectors). No boot PROM -- the CPU board's monitor boots CP/M from the CF. Mounts the SAME card image as dualsd (a .img with a .geo geometry sidecar); pair with dualsd for the full A:/B:+C:/D: system"},
        {"icom", "iCOM FD3712/FD3812 8\" floppy: a programmed-I/O command/handshake controller on the S-100 Interface board. Two ports at BASE+0..1 (default C0) plus a boot PROM and 6810 scratch RAM in high memory (rom=builtin:icom-fd3712-cpm | icom-fd3712-fdos | icom-fd3812-cpm). Boots CP/M 2.2 (single and double density) and FDOS. Up to 4 drives"},
        {"versafloppy", "SD Systems VersaFloppy I/II: WD FD177x soft-sector floppy, up to 4 drives. Eight ports at BASE+0..7 (default 60). variant=vfi (FD1771, single density) | vfii (FD1791, single+double). Boots SDOS with the SBC-200 + DDBIOS"},
        {"tarbell", "Tarbell #1011: single-density WD FD1771 floppy, up to 4 drives. Eight ports at BASE+0..7 (default F8). Carries a 32-byte boot PROM that shadows 0000 over PHANTOM* -- boots CP/M automatically at reset (bootstrap=on)"},
        {"tarbelldd", "Tarbell #2022: double-density WD FD1791 floppy (mixed-density media, SD track 0), up to 4 drives. The single-density card's twin with a bitmap OUT-FC latch and a port-FD DMA/ext-addr register. Same 32-byte boot PROM"},
        {"16fdc", "Cromemco 16FDC: WD FD1793 soft-sector floppy (single + double density), up to 4 drives. Disk registers at 30-34, a TMS 5501 console UART at 00-09 (unit 'tty'), and a 4K RDOS 2.52 boot PROM at C000 (OUT 40H banks it out, RESET restores it). Boots CDOS"},
        {"64fdc", "Cromemco 64FDC: the 16FDC's 1983 successor -- same FD1793 + TMS 5501, carrying an 8K RDOS 3.12 boot PROM at C000-DFFF (OUT 40H banks it out, RESET restores it). Boots CDOS"},
        {"acr", "MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the WIND/REWIND/EXTRACT verbs and a tape counter"},
        {"uio", "MITS 88-UIO: serial + cassette on one board. A 6850 (unit 'serial', default 0x10) and an 88-ACR cassette section (unit 'tape', default 0x06) with motor control and a SW-1 MITS/Kansas-City modulation switch. Defaults reproduce the standard 0x10 + 0x06 layout"},
        // "CONNECT it to a file" was true when a file was the only place printing could go.
        // It is not the offer any more -- printer:, socket: and the rest are on the same
        // list HELP CONNECT prints -- and a one-liner that names one endpoint reads as if
        // it named the only one.
        {"c700", "MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file, a socket, or a real printer queue"},
        {"lpc", "MITS 88-LPC: 88-LP line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Line-buffered: 6-bit codes + PRINT/LINE FEED/CLEAR. CONNECT it to a file, a socket, or a real printer queue"},
        {"pio", "MITS 88-PIO: 8-bit parallel port, units 'out'/'in'. Two ports at BASE+0..1 (default 04). CONNECT a printer, a keyboard, a socket"},
        {"4pio", "MITS 88-4PIO: up to four 6820 PIAs, sections ja/jb.. per port. 16 ports from BASE (default 20). Software-set direction; CONNECT each section"},
        {"vdm1", "Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). Needs a Display"},
        {"dazzler", "Cromemco Dazzler: color graphics from a framebuffer in main RAM. Two ports at BASE+0..1 (default 0E): control/status and format. 32x32 to 128x128, 16 colors/greys. Needs a Display"},
        {"vdb8024", "SD Systems VDB-8024: an 80x24 video terminal on one board -- the video console for an SBC-100/200 (the alternative to the 8251). Two I/O ports at BASE+0..1 (default 00): status/keyboard/display. Unit 'keyboard' (CONNECT). Optional keyboard-strobe interrupt strap (interrupt=vi0..vi7) for the SBC-200's CTC to vector -- what the SD video CBIOS needs; polled by default. Boots sdmonv21. Needs a Display"},
        {"d7a", "Cromemco D+7A: analog + parallel I/O. Eight ports from BASE (default 18): one parallel port + seven two's-complement A/D-in/D/A-out channels. Reads 1-2 JS-1 joysticks from the host"},
        {"sol", "Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one board. Seven ports F8..FE. Units serial/printer/keyboard (CONNECT) and tape1/tape2 (MOUNT). Brings the WIND/REWIND/EXTRACT verbs and a tape counter"},
        // Not a toggle: the SENSE switches a guest reads at IN 0FFH are a CONFIGURED byte
        // (SET fp0 sense=, or TOML) -- there is no switch on this board to flip. No OUT.
        {"fp", "Altair front panel: the SENSE switches a guest reads at IN 0FFH -- a configured byte (SET fp0 sense= or TOML), not toggled here. No OUT"},
        {"turnkey", "MITS 8800b Turnkey Module: phantom boot PROM (FC00-FFFF), integrated 6850 SIO (unit 'tty', default 0x10), sense switches at FF, and the Auto-Start JMP jam. Sockets via [[board.socket]]"},
        {"virtc", "MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE"},
        {"hostbridge", "Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN BOARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM"},
        {"pmmi", "PMMI MM-103: Bell 103 modem on an S-100 card, unit 'line'. Four ports at BASE+0..3 (default C0), read/write different registers. Transmit/receive over a ByteStream; CONNECT it to in:/out: files. No dialer; modem status is a fixed stub"},
        {"usio", "Universal Serial board: a UART-agnostic serial card, unit 'serial'. Two ports you strap: a status/control port (status_port -- read synthesizes RDR/TDRE at bit positions you pick, write is discarded) and a data port (data_port). Built-in profiles preset the straps: profile=tuart (Cromemco TU-ART) | imsai-sio2 | compupro-if2 (CompuPro Interfacer II) | compupro-ss1 (CompuPro System Support 1). Polled, no interrupts. CONNECT it to a file, socket, serial port, in:/out:"},
        {"propio", "S100Computers Console IO Board (Parallax-Propeller console), unit 'serial'. A usio subtype preset to the board's documented convention: status/data at 00/01, RX-ready = status bit 1, TX-ready = status bit 2, both active high. Every strap (status_port/data_port/rdr_bit/tdre_bit/polarity) is still overridable -- the real board is jumpered. Polled, no interrupts. CONNECT it to a file, socket, serial port, in:/out:"},
        {"v2z80rom", "S100Computers V2 Z80 CPU board -- its onboard paged monitor EEPROM (the Z80 itself is board 'z80cpu'). An 8K 28C64 at F000-FFFF holding two 4K pages, builtin:master0 (low) / master1 (high), selected by OUT D3H bit1 (bit0=1 inactivates the EEPROM so RAM shows through). Shadows RAM in its window while enabled. Cold-start the MASTER monitor with startup=[\"RUN F000\"]; the 'I' command boots CP/M 3 off a dualsd card"},
    };
}

std::unique_ptr<Board> makeBoard(const std::string& type) {
    if (type == "memory") return std::make_unique<MemoryBoard>();
    if (type == "bankmem") return std::make_unique<MemBankBoard>();
    if (type == "8080") return std::make_unique<Cpu8080Board>();
    if (type == "z80") return std::make_unique<CpuZ80Board>();
    if (type == "8085") return std::make_unique<Cpu8085Board>();
    if (type == "6800") return std::make_unique<Cpu6800Board>();
    if (type == "680io") return std::make_unique<Io680Board>();
    if (type == "680uio") return std::make_unique<Uio680Board>();
    if (type == "680kcacr") return std::make_unique<KcacrBoard>();
    if (type == "2sio") return std::make_unique<Sio2Board>();
    if (type == "sio") return std::make_unique<SioBoard>();
    if (type == "sbc") return std::make_unique<SbcBoard>();
    if (type == "dcdd") return std::make_unique<DcddBoard>();
    if (type == "mds") return std::make_unique<MdsBoard>();
    if (type == "hdsk") return std::make_unique<HdskBoard>();
    if (type == "dualide") return std::make_unique<DualIdeBoard>();
    if (type == "dualsd") return std::make_unique<DualSdBoard>();
    if (type == "icom") return std::make_unique<IcomFdBoard>();
    if (type == "versafloppy") return std::make_unique<VersaFloppyBoard>();
    if (type == "tarbell") return std::make_unique<TarbellBoard>();
    if (type == "tarbelldd") return std::make_unique<TarbellDdBoard>();
    if (type == "16fdc") return std::make_unique<Fdc16Board>();
    if (type == "64fdc") return std::make_unique<Fdc64Board>();
    if (type == "acr") return std::make_unique<AcrBoard>();
    if (type == "uio") return std::make_unique<UioBoard>();
    if (type == "c700") return std::make_unique<C700Board>();
    if (type == "lpc") return std::make_unique<LpcBoard>();
    if (type == "pio") return std::make_unique<PioBoard>();
    if (type == "4pio") return std::make_unique<Pio4Board>();
    if (type == "vdm1") return std::make_unique<VdmBoard>();
    if (type == "dazzler") return std::make_unique<DazzlerBoard>();
    if (type == "vdb8024") return std::make_unique<Vdb8024Board>();
    if (type == "d7a") return std::make_unique<D7aBoard>();
    if (type == "sol") return std::make_unique<SolBoard>();
    if (type == "fp") return std::make_unique<FrontPanelBoard>();
    if (type == "turnkey") return std::make_unique<TurnkeyBoard>();
    if (type == "virtc") return std::make_unique<VirtcBoard>();
    if (type == "hostbridge") return std::make_unique<HostBridgeBoard>();
    if (type == "pmmi") return std::make_unique<PmmiBoard>();
    if (type == "usio") return std::make_unique<UsioBoard>();
    if (type == "propio") return std::make_unique<PropIoBoard>();
    if (type == "v2z80rom") return std::make_unique<V2Z80RomBoard>();
    return nullptr;
}

} // namespace altair
