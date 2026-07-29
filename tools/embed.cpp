// Bootstrap code generator for the plain-Makefile build (docs/building-*.md).
//
// This reproduces, byte for byte, what the CMake build generates with
// cmake/embed_roms.cmake, cmake/embed_machines.cmake and configure_file() of
// cmake/version.h.in. It exists so a build with nothing but make + a C++
// compiler -- MinGW on Windows, in particular -- can produce the same three
// generated sources CMake does, WITHOUT needing CMake or a POSIX shell.
//
// It is the SIMH sim_BuildROMs.c pattern: a tiny tool the build compiles first
// and then runs. The .cmake scripts remain authoritative; if the two ever drift
// the Makefile's codegen-parity check (a diff against build/generated/) catches
// it. Do not add cleverness here that the .cmake scripts do not have -- the
// point is that the outputs are identical.
//
// Usage:
//   embed roms     <roms-dir>     <out.cpp>
//   embed machines <machines-dir> <out.cpp>
//   embed version  <version.h.in> <out.h>   [<CMakeLists.txt>]

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// popen/pclose are spelled with a leading underscore in the Windows CRT (MinGW
// and MSVC). This tool is not under src/, so the platform_lint #ifdef ban does
// not reach it; one guard here keeps `git describe` working on every host.
#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#define DEVNULL "NUL"
#else
#define DEVNULL "/dev/null"
#endif

// --- helpers ---------------------------------------------------------------

static std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Write only when the content changed, so the Makefile can regenerate on every
// build (avoiding fragile file-list prerequisites -- ROM dirs have filenames
// with spaces) without needlessly re-touching mtimes and forcing recompiles.
static bool writeFile(const fs::path& p, const std::string& s) {
    if (fs::exists(p) && readFile(p) == s)
        return false;
    fs::create_directories(p.parent_path().empty() ? fs::path(".") : p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << s;
    return true;
}

// Report only a real write, so a no-op `make` stays quiet.
static void report(const char* who, const fs::path& out, bool wrote) {
    if (wrote)
        std::fprintf(stderr, "%s: wrote %s\n", who, out.string().c_str());
}

// Mirror of CMake's string(MAKE_C_IDENTIFIER): every character that is not a
// letter, digit or underscore becomes '_', and a leading digit gets a '_'
// prepended (so machine "4k" -> "_4k", matching mach__4k).
static std::string cIdentifier(const std::string& in) {
    std::string out;
    for (char c : in) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
        out += ok ? c : '_';
    }
    if (!out.empty() && out[0] >= '0' && out[0] <= '9')
        out = "_" + out;
    return out;
}

static std::string toLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}

// Escape for a C string literal, exactly as the .cmake scripts do: backslash,
// then double-quote.
static std::string escapeC(std::string s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

// The bytes of a file as {0xNN,0xNN,...,} -- lowercase hex, trailing comma,
// matching CMake's file(READ ... HEX) + regex.
static std::string byteArray(const std::string& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 5 + 2);
    out += '{';
    for (unsigned char c : bytes) {
        out += "0x";
        out += hex[c >> 4];
        out += hex[c & 0xf];
        out += ',';
    }
    out += '}';
    return out;
}

// --- roms ------------------------------------------------------------------

static int genRoms(const fs::path& romsDir, const fs::path& out) {
    // Directory entries, sorted -- CMake file(GLOB) returns them sorted.
    std::vector<std::string> dirs;
    for (const auto& e : fs::directory_iterator(romsDir))
        if (e.is_directory())
            dirs.push_back(e.path().filename().string());
    std::sort(dirs.begin(), dirs.end());

    std::string body, table;
    for (const std::string& d : dirs) {
        // First image file in the dir, by sorted name (CMake GLOB order).
        std::vector<fs::path> imgs;
        for (const auto& e : fs::directory_iterator(romsDir / d)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::string el = toLower(ext);
            if (el == ".hex" || el == ".bin")
                imgs.push_back(e.path());
        }
        if (imgs.empty()) continue;
        std::sort(imgs.begin(), imgs.end());
        const fs::path& img = imgs.front();

        std::string name = toLower(d);
        std::string sym = cIdentifier(name);
        std::string el = toLower(img.extension().string());
        std::string fmt = (el == ".hex") ? "Format::Hex" : "Format::Bin";
        std::string base = img.filename().string();

        std::string bytes = readFile(img);
        std::string arr = byteArray(bytes);

        // Description: first line of roms/<NAME>/DESC, trimmed.
        std::string desc;
        fs::path descPath = romsDir / d / "DESC";
        if (fs::exists(descPath)) {
            std::string raw = readFile(descPath);
            size_t nl = raw.find_first_of("\r\n");
            desc = (nl == std::string::npos) ? raw : raw.substr(0, nl);
            size_t a = desc.find_first_not_of(" \t");
            size_t b = desc.find_last_not_of(" \t");
            desc = (a == std::string::npos) ? "" : desc.substr(a, b - a + 1);
        } else {
            std::fprintf(stderr,
                "embed_roms: roms/%s has no DESC file -- SHOW ROMS description will be blank\n",
                d.c_str());
        }
        desc = escapeC(desc);

        body += "static const unsigned char rom_" + sym + "[] = " + arr + ";\n";
        table += "  { \"" + name + "\", \"" + base + "\", " + fmt + ", rom_" + sym +
                 ", " + std::to_string(bytes.size()) + ", \"" + desc + "\" },\n";
    }

    std::string s;
    s += "// GENERATED by cmake/embed_roms.cmake -- do not edit.\n";
    s += "#include \"core/roms.h\"\n\n";
    s += "namespace altair {\n\n";
    s += body;
    s += "\nstatic const BuiltinRom kRoms[] = {\n";
    s += table;
    s += "};\n\n";
    s += "std::span<const BuiltinRom> builtinRoms() {\n";
    s += "  return std::span<const BuiltinRom>(kRoms, sizeof(kRoms) / sizeof(kRoms[0]));\n";
    s += "}\n\n";
    s += "} // namespace altair\n";
    report("embed_roms", out, writeFile(out, s));
    return 0;
}

// --- machines --------------------------------------------------------------

static int genMachines(const fs::path& machinesDir, const fs::path& out) {
    std::vector<fs::path> tomls;
    for (const auto& e : fs::directory_iterator(machinesDir))
        if (e.is_regular_file() && toLower(e.path().extension().string()) == ".toml")
            tomls.push_back(e.path());
    std::sort(tomls.begin(), tomls.end());

    std::string body, table;
    for (const fs::path& f : tomls) {
        std::string name = f.stem().string();
        std::string sym = cIdentifier(name);
        std::string text = readFile(f);

        // Blurb: the first `# ...` line, minus the hash and leading blanks.
        std::string blurb;
        {
            size_t nl = text.find('\n');
            std::string first = (nl == std::string::npos) ? text : text.substr(0, nl);
            if (!first.empty() && first.back() == '\r') first.pop_back();
            if (!first.empty() && first[0] == '#') {
                size_t a = first.find_first_not_of(" \t", 1);
                blurb = (a == std::string::npos) ? "" : first.substr(a);
            }
        }
        blurb = escapeC(blurb);

        std::string arr = byteArray(text);
        body += "static const unsigned char mach_" + sym + "[] = " + arr + ";\n";
        table += "  { \"" + name + "\", \"" + blurb + "\", (const char*)mach_" + sym +
                 ", " + std::to_string(text.size()) + " },\n";
    }

    std::string s;
    s += "// GENERATED by cmake/embed_machines.cmake -- do not edit.\n";
    s += "#include \"core/machines.h\"\n\n";
    s += "namespace altair {\n\n";
    s += body;
    s += "\nstatic const BuiltinMachine kMachines[] = {\n";
    s += table;
    s += "};\n\n";
    s += "std::span<const BuiltinMachine> builtinMachines() {\n";
    s += "  return std::span<const BuiltinMachine>(kMachines,\n";
    s += "                                         sizeof(kMachines) / sizeof(kMachines[0]));\n";
    s += "}\n\n";
    s += "} // namespace altair\n";
    report("embed_machines", out, writeFile(out, s));
    return 0;
}

// --- version ---------------------------------------------------------------

// Capture stdout of a command. Empty string on any failure -- the caller then
// keeps the "unknown"/0 defaults, which is the documented behavior.
static std::string runCapture(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
        out.append(buf, n);
    int rc = pclose(p);
    if (rc != 0) return "";
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

static std::string projectVersion(const fs::path& cmakeLists) {
    std::string text = readFile(cmakeLists);
    // project(altairsim VERSION X.Y.Z LANGUAGES CXX) -- find the VERSION that
    // belongs to project(), not cmake_minimum_required(VERSION 3.20).
    size_t proj = text.find("project(");
    if (proj == std::string::npos) return "unknown";
    size_t v = text.find("VERSION", proj);
    if (v == std::string::npos) return "unknown";
    v += 7;
    while (v < text.size() && (text[v] == ' ' || text[v] == '\t')) v++;
    std::string ver;
    while (v < text.size() && (isdigit((unsigned char)text[v]) || text[v] == '.'))
        ver += text[v++];
    return ver.empty() ? "unknown" : ver;
}

static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t i = 0; (i = s.find(from, i)) != std::string::npos; i += to.size())
        s.replace(i, from.size(), to);
}

static int genVersion(const fs::path& templ, const fs::path& out, const fs::path& cmakeLists) {
    std::string version = projectVersion(cmakeLists);
    std::string commit = "unknown";
    std::string dirty = "0";

    // Only trust git inside a real work tree.
    if (fs::exists(".git")) {
        std::string describe = runCapture("git describe --tags --always 2>" DEVNULL);
        if (!describe.empty()) {
            commit = describe;
            std::string status = runCapture("git status --porcelain --untracked-files=no 2>" DEVNULL);
            if (!status.empty()) dirty = "1";
        }
    }

    std::string s = readFile(templ);
    replaceAll(s, "@PROJECT_VERSION@", version);
    replaceAll(s, "@ALTAIRSIM_GIT_COMMIT@", commit);
    replaceAll(s, "@ALTAIRSIM_GIT_DIRTY@", dirty);
    report("embed_version", out, writeFile(out, s));
    return 0;
}

// --- main ------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "roms")
        return genRoms(argv[2], argv[3]);
    if (argc >= 4 && std::string(argv[1]) == "machines")
        return genMachines(argv[2], argv[3]);
    if (argc >= 4 && std::string(argv[1]) == "version")
        return genVersion(argv[2], argv[3], argc >= 5 ? argv[4] : "CMakeLists.txt");

    std::fprintf(stderr,
        "usage:\n"
        "  embed roms     <roms-dir>     <out.cpp>\n"
        "  embed machines <machines-dir> <out.cpp>\n"
        "  embed version  <version.h.in> <out.h> [<CMakeLists.txt>]\n");
    return 2;
}
