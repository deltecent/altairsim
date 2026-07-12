#pragma once
//
// Machine configuration (docs/config.md).
//
// THE TOML KEYS FOR A BOARD *ARE* ITS properties(). There is no separate config
// schema, anywhere, for any board -- the loader walks properties() and so does
// CONFIG SAVE, which is why they cannot drift and why a board added next year is
// configurable the day it lands with no change to this file.
//
// A LIST of things (regions, drives, serial units) is a sub-unit and gets its
// own [[board.<table>]], which the BOARD builds via addSubUnit(). The loader
// stays as ignorant of what a "region" is as the bus is.
//
// This is a small hand-written subset parser, not a general TOML implementation.
// Being explicit about that: it handles what docs/config.md documents and will
// tell you plainly when it meets something it does not.

#include "core/machine.h"

#include <string>

namespace altair {

bool loadToml(const std::string& path, Machine& m, std::string& err);

// The same parser, over text that never had a path. A BUILT-IN MACHINE IS A TOML
// FILE THAT LIVES IN .rodata (core/machines.h) -- `source` only names it in
// errors. One machine language, no second dialect for the things we ship.
bool loadTomlText(const std::string& text, const std::string& source, Machine& m,
                  std::string& err);

bool saveToml(const std::string& path, Machine& m, std::string& err);

} // namespace altair
