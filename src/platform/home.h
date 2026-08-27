#pragma once

// The current user's HOME directory (DESIGN.md 2.1: no OS type in the signature, one
// implementation file per OS).
//
// This exists for exactly one reason: expanding a leading `~` in a path the operator
// TYPED at the monitor prompt (core/paths.h, expandUser()). A real shell rewrites `~`
// before a program is ever handed the argument; the monitor has no shell in that loop,
// so it must find the home directory itself -- and WHERE that lives is the one thing
// that differs between hosts. POSIX keeps it in $HOME; Windows has no HOME by
// convention and uses %USERPROFILE%. That single difference is what this seam hides.

#include <string>

namespace altair::platform {

// The user's home directory, or "" if the host does not tell us one. "" is a real
// answer, not an error: expandUser() leaves the `~` in place so the operator's own
// text still shows in the "cannot open" message.
std::string homeDir();

}  // namespace altair::platform
