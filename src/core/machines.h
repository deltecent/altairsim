#pragma once
//
// Built-in machines (DESIGN.md 10.0).
//
// A BUILT-IN MACHINE IS A TOML FILE THAT LIVES IN .rodata. machines/*.toml is
// embedded verbatim and parsed at runtime by loadTomlText() -- the SAME parser
// that reads a config off disk. There is exactly one machine language, and the
// machines we ship are written in it, which means they are also worked examples
// of it and they cannot rot: if the config format changes under them, they stop
// loading and a test goes red.
//
// The alternative -- building the default machine in C++ with m.add("memory",
// "mem0") -- would have been fewer lines and a quiet second dialect that nobody
// could copy, edit, or diff.

#include "core/machine.h"

#include <span>
#include <string>

namespace altair {

struct BuiltinMachine {
    const char* name;   // "default" -- what `altairsim default` names
    const char* blurb;  // the first comment line of the .toml, for --list
    const char* toml;   // the file, verbatim. NOT null-terminated: use `size`.
    size_t size;
};

std::span<const BuiltinMachine> builtinMachines();  // defined by the generated TU

const BuiltinMachine* findMachine(const std::string& name);

// Build `m` from a built-in. Errors read "builtin:turnkey: ..." -- exactly as a
// path would, because to everything downstream it is one.
bool loadMachine(const BuiltinMachine& b, Machine& m, std::string& err);

// IS THIS ARGUMENT A FILE OR A BUILT-IN NAME?
//
// Decided SYNTACTICALLY -- it contains a '/' (or '\'), or it ends in ".toml".
// Deliberately NOT by probing the filesystem: if the answer depended on what
// happens to be in the current directory, then `altairsim default` would mean
// one thing normally and something else entirely the day somebody saves a file
// called `default` next to it. A command line that changes meaning because of
// its surroundings is a trap, and it is the kind that gets found at 2am.
//
// The escape hatches are explicit and never guess: `-f ./default` is always a
// file, `-m default` is always a built-in.
bool looksLikeFile(const std::string& arg);

} // namespace altair
