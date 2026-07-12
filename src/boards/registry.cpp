#include "boards/registry.h"

#include "boards/mits-88cpu.h"
#include "boards/s100-memory.h"
#include "boards/mits-2sio.h"

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
        {"2sio", "MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3"},
    };
}

std::unique_ptr<Board> makeBoard(const std::string& type) {
    if (type == "memory") return std::make_unique<MemoryBoard>();
    if (type == "8080") return std::make_unique<Cpu8080Board>();
    if (type == "2sio") return std::make_unique<Sio2Board>();
    return nullptr;
}

} // namespace altair
