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
        const std::string commit = versionCommit();

        // ONE VERSION, NOT TWO. This line used to read
        //
        //     altairsim 0.1.0 (v0.1.0-51-g1e58347)
        //
        // which says 0.1.0 twice: `git describe` ALREADY BEGINS WITH THE TAG, so the
        // number in front of the parenthesis is the same number again, and the reader
        // has to notice that before they can tell which part is the new information.
        // The describe string on its own carries all of it -- the release it is built
        // on, how far past it, and which commit.
        //
        // So the normal line is the describe, with its leading `v` dropped because
        // this is prose and not a tag:
        //
        //     AltairSim 0.1.0-51-g1e58347
        //     AltairSim 0.1.0                    (built exactly on the tag)
        std::string v = "AltairSim ";
        const std::string tag = "v" + std::string(ALTAIRSIM_VERSION);
        if (commit.rfind(tag, 0) == 0) v += commit.substr(1);

        // ...AND THE CASES WHERE IT DOES NOT. A history with no tags describes to a
        // bare sha, which names the commit but not the release; a source tarball has no
        // git in it at all. Both have to say the version themselves, so the parenthesis
        // comes back -- and it is now carrying something the line does not already say.
        //
        // The mismatch case lands here too, deliberately: if the nearest tag is not this
        // build's version the two really do differ, and printing both is the point.
        //
        // "commit unknown" rather than a bare "unknown", so it still reads as a sentence
        // when there is nothing to put in it.
        else if (commit == "unknown") v += std::string(ALTAIRSIM_VERSION) + " (commit unknown)";
        else v += std::string(ALTAIRSIM_VERSION) + " (" + commit + ")";

        // MODIFIED, not "dirty": the operator reading this line wants to know the binary
        // does not match the commit named next to it, and "dirty" is a word that only
        // means that to someone who already knew. It is its own parenthesis now rather
        // than a clause inside the commit's, because in the normal case above there is
        // no longer a parenthesis for it to sit in.
        if (versionDirty()) v += " (modified)";
        return v;
    }();
    return s.c_str();
}

}  // namespace altair
