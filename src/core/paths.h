#pragma once
//
// WHERE A RELATIVE PATH IS RELATIVE TO (docs/config.md).
//
// There are exactly two answers, and the whole of this file exists to keep them
// from being confused with each other:
//
//   A path WRITTEN INSIDE a machine file is relative to THAT FILE.
//   A path TYPED at the prompt is relative to THE SHELL.
//
// The first is what makes a machine file portable. `tapes/MitsPS2/ps2int.toml`
// says `MOUNT acr0:tape "PS2-MON.TAP"` and means the tape lying next to it -- so
// the directory can be copied anywhere, handed to anyone, and it still boots. A
// user gets the binary and the tapes/ and disks/ trees; they do not get this
// repository, and a machine file that only works from the repository root is a
// machine file that only works for us.
//
// The second is not a compromise, it is the other half of the rule. When the
// operator types `MOUNT dsk0:drive1 "scratch.dsk"` they mean the scratch.dsk they
// can see in the shell they are standing in. Resolving THAT against some config
// file's directory would be the trap: a command whose meaning depends on which
// machine happens to be loaded.
//
// So: the loader tells a board which window it is in (Board::setConfigDir), and
// the board asks this. Nobody guesses, and there is no search path -- a file is
// looked for in ONE place, and if it is not there you are told which place.

#include <string>

namespace altair {

// The directory `file` lives in, or "" if it names no directory at all.
//
// "" IS THE ANSWER FOR A BARE FILENAME, and it is the answer that matters most:
// `altairsim ps2int.toml`, run from the directory the file is in, resolves every
// path in it against "" -- which is to say against the working directory, which
// is that same directory. The common case costs nothing and changes nothing.
std::string dirOf(const std::string& file);

// `path`, as seen from `dir`. Returns `path` untouched when there is nothing to do:
//
//   - `dir` is empty          -- no config directory; the shell's cwd stands.
//   - `path` is absolute      -- it already says where it is.
//   - `path` has a SCHEME     -- `builtin:dbl` is not a file and must never become
//                                one. Two or more letters before the ':', so a
//                                Windows `C:\rom.bin` is a path and not a scheme.
std::string resolveFrom(const std::string& dir, const std::string& path);

} // namespace altair
