#include "test.h"

#include "core/paths.h"
#include "platform/home.h"

#include <string>

using namespace altair;

// A leading `~` is a shell convention (core/paths.h). The monitor is the shell for
// a path the operator TYPED, so expandUser() does the one rewrite a real shell does
// before handing an argument to a program -- and nothing more.
//
// We test against platform::homeDir() rather than poking $HOME ourselves: DESIGN.md
// 2.1 keeps OS-specific env handling behind the platform seam, so the test may not
// carry an #ifdef either. The home value is whatever the host reports; the SHAPE of
// the expansion is what we pin.
void test_paths() {
    SECTION("expandUser -- leading ~ on a typed path (core/paths.h)");

    const std::string home = platform::homeDir();

    if (!home.empty()) {
        CHECK(expandUser("~/rom.bin") == home + "/rom.bin", "~/x -> HOME + /x");
        CHECK(expandUser("~") == home, "a bare ~ -> HOME");
        CHECK(expandUser("~/a/b/c") == home + "/a/b/c", "the rest of the path is kept verbatim");
    }

    // These hold no matter what HOME is -- a non-leading or non-bare ~ is never touched.
    CHECK(expandUser("~user/x") == "~user/x", "~user is NOT expanded (would need getpwnam)");
    CHECK(expandUser("~tilde.bin") == "~tilde.bin", "~ followed by a letter is a literal name");
    CHECK(expandUser("/abs/~/x") == "/abs/~/x", "a ~ that is not leading is untouched");
    CHECK(expandUser("rel/~") == "rel/~", "a ~ mid-path is untouched");
    CHECK(expandUser("rom.bin") == "rom.bin", "an ordinary path is returned unchanged");
    CHECK(expandUser("") == "", "empty stays empty");

    SECTION("resolveFrom -- ~ is a TYPED-path affordance only");

    // Empty dir == typed at the prompt: we are the shell, so ~ expands there too.
    if (!home.empty()) {
        CHECK(resolveFrom("", "~/rom.bin") == home + "/rom.bin",
              "a typed ~ expands via resolveFrom");
    }

    // A path from a machine file (dir set) is portable and must stay literal -- a
    // host-specific ~ would defeat handing the file to someone else. It is joined
    // to the config dir like any other relative path, ~ and all.
    CHECK(resolveFrom("machines", "~/rom.bin").find('~') != std::string::npos,
          "a ~ inside a machine file is NOT expanded");
    CHECK(resolveFrom("machines", "~/rom.bin") == "machines/~/rom.bin",
          "it is joined to the config dir verbatim");
}
