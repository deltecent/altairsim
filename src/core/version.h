// Which build this is -- the version, and the commit it came from.
//
// The values are captured by CMake at configure time (see the version block in
// CMakeLists.txt) into a generated header. This is the seam over it, so that the
// three places that answer the question -- `--version`, the startup banner and
// SHOW VERSION -- read one set of strings rather than three, and so that nothing
// in src/ has to include a header that only exists after a configure.
//
// WHERE THERE WAS NO GIT, commit() is the literal "unknown" and dirty() is false.
// A release tarball has no .git in it; saying so is the honest answer, and the
// alternative is a plausible-looking string that names the wrong revision.
#pragma once

namespace altair {

// "0.1.0" -- the project version, and nothing about the build.
const char* versionNumber();

// `git describe --tags --always`: "v0.1.0-37-gcc64cca" on a tagged history, a bare
// abbreviated sha on one with no tags, "unknown" where there was no git at all.
const char* versionCommit();

// Whether tracked files were modified when this binary was configured. Untracked
// files do not count -- see the CMake comment for why.
bool versionDirty();

// The one line `--version` prints and the banner opens with. `git describe` already
// starts with the tag, so the version number is NOT printed twice -- the describe
// string carries the release, the distance past it and the commit by itself:
//   AltairSim 0.1.0-37-gcc64cca
//   AltairSim 0.1.0-37-gcc64cca (modified)
//   AltairSim 0.1.0                          built exactly on the tag
//   AltairSim 0.1.0 (cc64cca)                a history with no tags in it
//   AltairSim 0.1.0 (commit unknown)         no git at all
const char* versionString();

}  // namespace altair
