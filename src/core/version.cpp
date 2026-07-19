#include "core/version.h"

#include "version_generated.h"  // configure_file'd from cmake/version.h.in

#include <string>

namespace altair {

const char* versionNumber() { return ALTAIRSIM_VERSION; }
const char* versionCommit() { return ALTAIRSIM_COMMIT; }

bool versionDirty() { return ALTAIRSIM_DIRTY != 0; }

const char* versionString() {
    // Built once and kept, because callers hand it straight to a stream and the
    // three of them together would otherwise rebuild the same string three times.
    static const std::string s = [] {
        std::string v = "altairsim ";
        v += ALTAIRSIM_VERSION;
        // "commit unknown" rather than a bare "unknown", so the parenthetical still
        // reads as a sentence when there is nothing to put in it.
        if (versionCommit() == std::string("unknown")) return v + " (commit unknown)";
        v += " (";
        v += ALTAIRSIM_COMMIT;
        // MODIFIED, not "dirty": the operator reading this line wants to know the
        // binary does not match the commit named next to it, and "dirty" is a word
        // that only means that to someone who already knew.
        if (versionDirty()) v += ", modified";
        return v + ")";
    }();
    return s.c_str();
}

}  // namespace altair
