#include "boards/registry.h"

#include "boards/memory.h"

namespace altair {

// Milestone 1a is CLI + bus + memory, and NO CPU: the monitor is the bus master.
// That is not a limitation to apologize for -- it is the point. Every claim the
// bus design makes (a ROM that never answers a write, an empty socket that
// floats, a PHANTOM* overlay that is not contention, five incompatible banking
// cards) is testable with two boards, a hex file, and no processor. And it is
// worth testing BEFORE a CPU exists, because those behaviors differ SILENTLY:
// get one wrong and the symptom is a guest misbehaving ten thousand
// instructions later.
std::vector<BoardType> boardTypes() {
    return {
        {"memory", "RAM/ROM card: a list of regions, PHANTOM*, and five banking schemes"},
    };
}

std::unique_ptr<Board> makeBoard(const std::string& type) {
    if (type == "memory") return std::make_unique<MemoryBoard>();
    return nullptr;
}

} // namespace altair
