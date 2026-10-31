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
#include "core/value.h"

#include <string>
#include <vector>

namespace altair {

// `notes`, if given, is filled with the `#>` comment lines from the OUTERMOST file --
// display-only text the author wrote for the operator to read on load (one entry per
// `#>` line, in file order). They are never stored on the machine and never written back
// by CONFIG SAVE; an ordinary `#` comment is discarded as always. A `base` machine's own
// notes are not collected -- these belong to the file the operator actually loaded.
bool loadToml(const std::string& path, Machine& m, std::string& err,
              std::vector<std::string>* notes = nullptr);

// The same parser, over text that never had a path. A BUILT-IN MACHINE IS A TOML
// FILE THAT LIVES IN .rodata (core/machines.h) -- `source` only names it in
// errors. One machine language, no second dialect for the things we ship.
bool loadTomlText(const std::string& text, const std::string& source, Machine& m,
                  std::string& err, std::vector<std::string>* notes = nullptr);

// THE HOST SETTINGS ARE PROCESS-WIDE, and the scratch machine that makes a load all or
// nothing does not hold them: [console], [display] and [terminal] are not boards. So a load
// saves them, resets them, and puts them back if the file fails. A load that succeeds gives
// the same settings at the monitor as it does at startup.
//
// resetHostSettings() resets the keys that CONFIG SAVE writes. stop, base, history and log
// stay, because they are the operator's. saveHostSettings() walks the property lists, so a
// new key is covered with no second list, and it holds every key so a failed load puts
// those four back too.
struct SavedHostSetting {
    Property property;
    Value    value;
};
using HostSettings = std::vector<SavedHostSetting>;

HostSettings saveHostSettings();
void         restoreHostSettings(const HostSettings& saved);
void         resetHostSettings();

bool saveToml(const std::string& path, Machine& m, std::string& err);

// ...and the same text, without a file in the way. The round trip is then something a
// test can just DO -- saveTomlText() into loadTomlText() and compare -- which is how
// this stays honest: the writer is generic over properties()/unitProperties(), and the
// reader has to be generic over exactly the same pair or a saved machine will not load.
std::string saveTomlText(Machine& m);

// ...and the same, reporting a value it could not write. A text value holding BOTH ' and "
// has no form the reader takes back (issue #538); the first such value is named in `*err`,
// and saveToml() refuses rather than write a file that will not load.
std::string saveTomlText(Machine& m, std::string* err);

} // namespace altair
