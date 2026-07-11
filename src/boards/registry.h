#pragma once
//
// Board type registry -- backs BOARD TYPES, BOARD ADD, and the MCP board_types
// tool. Adding a board type is one line here and nothing anywhere else.

#include "core/board.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

struct BoardType {
    std::string name;
    std::string description;
};

std::vector<BoardType> boardTypes();
std::unique_ptr<Board> makeBoard(const std::string& type);

} // namespace altair
