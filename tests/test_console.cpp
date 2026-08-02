#include "test.h"

#include "core/value.h"
#include "host/console.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace altair;

namespace {

// Find the named property on the one Console. All of log/attn/base/history and the
// transform chain come through here, so this is exactly the seam SET CONSOLE and MCP
// use -- driving it is driving the real thing.
Property prop(const std::string& name) {
    for (Property& p : Console::instance().properties())
        if (p.name == name) return p;
    // A missing property is a hard failure of the test, not a silent empty.
    CHECK(false, "console property exists");
    return Property{};
}

std::string setLog(const std::string& path, std::string& err) {
    Property p = prop("log");
    return p.set(Value::ofStr(path), err) ? std::string("ok") : std::string("fail");
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::in | std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Drive bytes to the screen the way the guest does -- through Console::write(), so they
// pass the whole filter chain (all off by default but bell) down to writeRaw(), which is
// where the log tap lives. Plain ASCII so no default filter touches it.
void screen(const std::string& s) {
    Console::instance().write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    Console::instance().flush();
}

} // namespace

void test_console() {
    SECTION("console log tees screen output to a file, and off stops it");

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path();
    const fs::path logp = dir / "altair_console_log_test.log";
    std::error_code ec;
    fs::remove(logp, ec);  // start clean, ignore "not there"

    std::string err;

    // OPEN. A fresh path, opened for append on an absent file, is just a create.
    CHECK(setLog(logp.string(), err) == "ok", "SET log=<path> succeeds");
    CHECK(err.empty(), "and reports no error");
    CHECK(prop("log").get().s() == logp.string(), "SHOW CONSOLE would read the path back");

    // TEE. Exactly the bytes that reached the screen land in the file.
    const std::string first = "MEMORY SIZE? 4096\r\n";
    screen(first);
    CHECK(readFile(logp) == first, "the file holds exactly what went to the screen");

    // OFF. `off` closes the file; nothing further is recorded.
    CHECK(setLog("off", err) == "ok", "SET log=off succeeds");
    CHECK(prop("log").get().s().empty(), "and clears the path");
    const std::string after = "THIS MUST NOT BE LOGGED\r\n";
    screen(after);
    CHECK(readFile(logp) == first, "the file did not grow after log=off");

    SECTION("empty path also closes, and re-opening appends");

    // Re-open the same path: APPEND, so the earlier transcript is preserved.
    CHECK(setLog(logp.string(), err) == "ok", "re-open the same log");
    const std::string more = "SECOND SESSION\r\n";
    screen(more);
    CHECK(readFile(logp) == first + more, "append kept the first session and added the second");

    // The empty string is the other way to say 'off'.
    err.clear();
    CHECK(setLog("", err) == "ok", "SET log= (empty) closes");
    CHECK(prop("log").get().s().empty(), "empty path leaves logging off");

    SECTION("an unopenable path is reported and leaves logging off");

    err.clear();
    // A file inside a directory that does not exist cannot be opened for append.
    const std::string bad = (dir / "no_such_dir_altairsim" / "x.log").string();
    CHECK(setLog(bad, err) == "fail", "SET log=<bad path> fails");
    CHECK(!err.empty(), "and returns an error message");
    CHECK(prop("log").get().s().empty(), "and logging stays off");
    screen("STILL NOT LOGGED\r\n");  // must not throw and must go nowhere

    fs::remove(logp, ec);
}
