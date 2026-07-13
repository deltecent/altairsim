#include "core/paths.h"

#include <filesystem>

namespace altair {

namespace fs = std::filesystem;

std::string dirOf(const std::string& file) {
    return fs::path(file).parent_path().generic_string();
}

namespace {

// `builtin:dbl` is a SCHEME, not a path, and the one place that has ever mattered
// is the memory card's ROM mount -- where a re-based "builtin:dbl" would become
// "roms/builtin:dbl" and the ROM would vanish. Decided syntactically, like
// looksLikeFile(): we do not probe the disk to find out what a string means.
//
// TWO letters minimum before the colon, deliberately. `C:\roms\dbl.bin` on Windows
// is a path with a drive letter on it, and a one-letter rule would read it as a
// scheme called "C" and then fail to resolve it against anything.
bool hasScheme(const std::string& p) {
    size_t colon = p.find(':');
    if (colon < 2) return false;  // 0 = no scheme; 1 = a drive letter
    for (size_t i = 0; i < colon; ++i) {
        char c = p[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '+' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

} // namespace

std::string resolveFrom(const std::string& dir, const std::string& path) {
    if (dir.empty() || path.empty()) return path;
    if (hasScheme(path)) return path;
    if (fs::path(path).is_absolute()) return path;

    // lexically_normal, not absolute(): the result stays RELATIVE to the working
    // directory, exactly as `dir` was. Making it absolute here would bake this
    // machine's cwd into every path a board reports -- SHOW would print an
    // absolute path nobody typed, and CONFIG SAVE would write one out.
    return (fs::path(dir) / path).lexically_normal().generic_string();
}

} // namespace altair
