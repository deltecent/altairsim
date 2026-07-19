#include "boards/mits-2sio.h"
#include "boards/mits-88c700.h"
#include "boards/mits-88sio.h"
#include "boards/proctech-sol.h"
#include "boards/proctech-vdm1.h"
#ifdef ALTAIRSIM_ENABLE_SDL
#include "host/display_sdl.h"
#else
#include "host/display_null.h"
#endif
#include "cli/monitor.h"
#include "config/toml.h"
#include "core/machine.h"
#include "core/machines.h"
#include "host/console.h"
#include "host/endpoint.h"
#include "host/media.h"
#include "mcp/server.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

// The host video service for any graphics board in the machine (DESIGN.md 7.4).
// A real window where SDL3 was found; a no-op headless otherwise -- the boards run
// the same against either. Session-lifetime, so it outlives every Machine.
#ifdef ALTAIRSIM_ENABLE_SDL
static SdlDisplay g_display;
#else
static NullDisplay g_display;
#endif

static const char* kVersion = "altairsim 0.1.0";

// The machine you get when you name none: the working directory's, if it has one.
// See the comment on the fallback in main() for why this is the ONLY file the
// simulator ever finds rather than is given.
static const char* kCwdConfig = "./altairsim.toml";

static void usage(std::ostream& o) {
    o << kVersion << " -- an Altair 8800 / S-100 simulator\n"
         "\n"
         "usage: altairsim [options] [machine]\n"
         "\n"
         "  machine            a built-in name (altairsim 4k), or a config file if it\n"
         "                     has a '/' in it or ends in .toml. Omitted: ./altairsim.toml\n"
         "                     if the working directory has one, else `default`.\n"
         "\n"
         "  -m, --machine <n>  ALWAYS a built-in name -- never a file.\n"
         "  -f, --file <path>  ALWAYS a file -- never a built-in name.\n"
         "  -n, --none         empty backplane. No boards, no memory, nothing.\n"
         "  -l, --list         list the built-in machines and exit.\n"
         "\n"
         "  -s, --script <f>   run a command script, then exit with its status.\n"
         "  -x, --exec <cmd>   run one monitor command (repeatable), then exit.\n"
         "  -i, --interactive  after --script/--exec, stay in the monitor.\n"
         "\n"
         "      --mcp          MCP server on stdio (for Claude).\n"
         "  -v, --version      -h, --help\n";
}

static void list(std::ostream& o) {
    o << "built-in machines:\n\n";
    size_t w = 0;
    for (const auto& b : builtinMachines()) w = std::max(w, std::string(b.name).size());
    for (const auto& b : builtinMachines()) {
        o << "  " << b.name;
        for (size_t i = std::string(b.name).size(); i < w + 2; ++i) o << ' ';
        o << b.blurb << "\n";
    }
    o << "\nA built-in is a TOML file that lives in the binary -- the same format you\n"
         "would write yourself. `altairsim -x 'SHOW MACHINE' <name>` shows what is in one.\n";
}

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);

    bool mcp = false, none = false, interactive = false;
    std::string script;
    std::vector<std::string> exec;

    // Three ways in, and only one of them guesses. `positional` is the friendly
    // form, resolved by looksLikeFile(); -m and -f are the escape hatches and
    // are never resolved at all.
    std::string positional, builtin, file;

    auto need = [&](size_t& i, const char* what) -> bool {
        if (i + 1 >= a.size()) {
            std::cerr << a[i] << " needs " << what << "\n";
            return false;
        }
        ++i;
        return true;
    };

    for (size_t i = 0; i < a.size(); ++i) {
        const std::string& s = a[i];
        if (s == "-h" || s == "--help") {
            usage(std::cout);
            return 0;
        } else if (s == "-v" || s == "--version") {
            std::cout << kVersion << "\n";
            return 0;
        } else if (s == "-l" || s == "--list") {
            list(std::cout);
            return 0;
        } else if (s == "--mcp") {
            mcp = true;
        } else if (s == "-n" || s == "--none") {
            none = true;
        } else if (s == "-i" || s == "--interactive") {
            interactive = true;
        } else if (s == "-m" || s == "--machine") {
            if (!need(i, "a built-in name")) return 2;
            builtin = a[i];
        } else if (s == "-f" || s == "--file") {
            if (!need(i, "a path")) return 2;
            file = a[i];
        } else if (s == "-s" || s == "--script") {
            if (!need(i, "a script file")) return 2;
            script = a[i];
        } else if (s == "-x" || s == "--exec") {
            if (!need(i, "a command")) return 2;
            exec.push_back(a[i]);
        } else if (s.size() > 1 && s[0] == '-') {
            std::cerr << "unknown option " << s << "\nTry: altairsim --help\n";
            return 2;
        } else {
            if (!positional.empty()) {
                std::cerr << "more than one machine given ('" << positional << "' and '" << s
                          << "')\n";
                return 2;
            }
            positional = s;
        }
    }

    // Say which one you meant. Silently preferring one over another is how a
    // person spends twenty minutes editing a config file that was never read.
    int ways = (!positional.empty()) + (!builtin.empty()) + (!file.empty()) + (none ? 1 : 0);
    if (ways > 1) {
        std::cerr << "give ONE machine: a name, -m, -f, or -n -- not several.\n";
        return 2;
    }

    // THE ONE FILE THE SIMULATOR FINDS RATHER THAN IS GIVEN -- and it is found only when
    // the command line NAMES NOTHING.
    //
    // looksLikeFile() (core/machines.h) refuses to probe the disk, and the reason is
    // load-bearing: `altairsim default` must not become a different machine the day
    // somebody saves a file called `default` next to it. A command line that changes
    // meaning because of its surroundings is a trap. That argument holds for every
    // command that names a machine -- and `altairsim`, alone, names none. It is not
    // asking for `default`; it is asking for whatever machine is sensible here, and
    // letting the directory answer that is the make(1) bargain rather than the trap.
    //
    // So the rule stays exact where it matters: `altairsim basic4k` is basic4k in every
    // directory on earth, and so is -m, -f and -n. Only the empty command line looks
    // around. And it says so out loud -- see the notice below -- because the failure
    // this can cause is spending twenty minutes on a machine you did not know you were
    // running, which is the same thing the `give ONE machine` check above exists to stop.
    bool discovered = false;

    if (!positional.empty()) {
        if (looksLikeFile(positional)) file = positional;
        else builtin = positional;
    } else if (ways == 0) {
        if (std::ifstream(kCwdConfig)) {
            file       = kCwdConfig;
            discovered = true;
        } else {
            builtin = "default";  // no machine named, and none to hand: you get one anyway
        }
    }

    // THE COMPOSITION ROOT. The monitor knows the endpoint grammar; the boards do
    // not, and must not (DESIGN.md 7.7). Wiring the two together is main's job and
    // nobody else's -- a board that could reach `resolveEndpoint` itself would be
    // one `#include` away from knowing what a socket is.
    Sio2Board::setResolver(resolveEndpoint);
    SioBoard::setResolver(resolveEndpoint);
    C700Board::setResolver(resolveEndpoint);
    SolBoard::setResolver(resolveEndpoint);

    // The video service, injected the same way (DESIGN.md 7.4): a graphics board
    // draws into a Display and never learns it is SDL. The shipping binary hands it
    // an SdlDisplay (a window); a headless build (no SDL3) hands it a NullDisplay, so
    // the machine runs identically with nothing to draw on. Static so it outlives the
    // machine, and created once for the whole session.
    VdmBoard::setDisplay(&g_display);

    // Window keystrokes join the terminal's on the ONE recorded input queue: the
    // display's key sink (host/display.h) feeds the single Console, and a Sol-20's
    // keyboard board reads that Console -- so you can type in the VDM window or the
    // terminal and SOLOS sees one stream (DESIGN.md 7.4). A NullDisplay never fires it.
    g_display.setKeySink([](const uint8_t* p, size_t n) { Console::instance().inject(p, n); });

    // And the other direction, once a slice: the run loop asks the window whether the
    // operator closed it, and stops the guest if so -- the same place ATTN lands you
    // (cli/monitor.h). The board that draws into the window deliberately cannot do
    // this; only the run loop can stop a machine.
    Monitor::setDisplay(&g_display);

    // The same seam for the other kind of endpoint: a disk board asks openMedia()
    // for a path and gets a medium back, and this is the one line that decides the
    // medium is a file on the host. A test replaces it with one made of RAM.
    setMediaResolver(openHostFile);

    Machine m;
    std::string err;

    if (!builtin.empty()) {
        const BuiltinMachine* b = findMachine(builtin);
        if (!b) {
            std::cerr << "no built-in machine '" << builtin << "'.\n";
            // The likeliest mistake by a mile: they meant a file, and it did not
            // look like one. Say the exact command that works.
            if (!looksLikeFile(builtin))
                std::cerr << "(for a FILE by that name: altairsim -f " << builtin << ")\n";
            std::cerr << "\n";
            list(std::cerr);
            return 2;
        }
        if (!loadMachine(*b, m, err)) {
            std::cerr << err << "\n";
            return 2;
        }
    } else if (!file.empty()) {
        // NEVER SILENTLY. This is the only machine nobody asked for by name, so it is the
        // only one that has to introduce itself -- and BEFORE the load, so that a broken
        // file names itself too. It goes to stderr: a `-s` script's stdout is a CI
        // contract and stays exactly what the script printed.
        if (discovered)
            std::cerr << "altairsim: no machine named -- using " << kCwdConfig
                      << " (`-m default` for the built-in).\n";
        if (!loadToml(file, m, err)) {
            std::cerr << err << "\n";
            return 2;
        }
    } else {
        // -n: an empty backplane. Every read floats to FF, because nothing is
        // driving anything. That is not a broken machine, it is an empty one --
        // and it is where you start if you are building one up with BOARDS ADD.
        m.name = "none";
        m.power();
    }

    // MCP and the monitor sit on the SAME Machine. Not a wrapper, not a second
    // model of the world -- the same object, reached two ways (DESIGN.md 11).
    if (mcp) return runMcp(m, std::cin, std::cout);

    Monitor mon(m);

    // The banner reports what is ACTUALLY in the backplane, because a machine with
    // no CPU card in it is a real machine you can build -- it is the one milestone
    // 1a ran, with the monitor as bus master -- and saying so is more useful than a
    // fixed string that goes stale the moment a card lands.
    auto banner = [&] {
        std::cout << kVersion << " -- ";
        if (CpuCore* c = m.cpu()) {
            std::cout << c->isa() << ", ";
            // hz() IS A DIVISOR AND IS NEVER 0 (core/clock.h), so it cannot answer this
            // question -- it reads 2 MHz on a card with no crystal on it, which is how this
            // line came to report a paced machine while the run loop was flat out. free() is
            // the policy, and the policy is what an operator wants read back.
            if (m.clock.free()) std::cout << "full speed.\n";
            else                std::cout << m.clock.hz() / 1000000.0 << " MHz.\n";
        }
        else std::cout << "no CPU in the backplane; the monitor is the bus master.\n";
        std::cout << "machine: " << m.name << ".  HELP for commands.\n";
    };

    // The banner goes to launches that reach the interactive repl -- exactly the
    // negation of the `if (ran && !interactive) return rc;` guard below -- so -x/-s
    // CI runs keep their silent stdout. WHERE it goes depends on the launch:
    //
    //   * A plain launch (nothing on the command line) greets NOW, before runStartup.
    //     An auto-run machine (a `RUN` in `startup`) blocks in the run loop until ATTN,
    //     and the operator deserves the banner before that, not after the first stop.
    //   * A -x/-s launch defers the banner to AFTER its commands run, so it reports
    //     what they left behind -- `SET cpu0 clock_hz=...` must be reflected, and that
    //     is what proves the line reads live machine state rather than a fixed string.
    const bool willRepl = interactive || (exec.empty() && script.empty());
    const bool hasCliCmds = !exec.empty() || !script.empty();
    if (willRepl && !hasCliCmds) banner();

    mon.runStartup(std::cout);

    // -x and -s are the same thing: commands, from somewhere. -x first, then the
    // script. A failure in either is a non-zero exit, which is the CI contract.
    int rc = 0;
    bool ran = false;

    if (!exec.empty()) {
        std::stringstream ss;
        for (const auto& c : exec) ss << c << "\n";
        rc = mon.repl(ss, std::cout, false);
        ran = true;
    }

    if (rc == 0 && !script.empty()) {
        std::ifstream f(script);
        if (!f) {
            std::cerr << "cannot open '" << script << "'\n";
            return 2;
        }
        rc = mon.repl(f, std::cout, false);
        ran = true;
    }

    if (ran && !interactive) return rc;

    // The deferred banner: a -x/-s launch that stays interactive (-i) is greeted here,
    // after its commands ran, so the line reflects them. The plain-launch banner was
    // printed above, before runStartup.
    if (willRepl && hasCliCmds) banner();

    // Line editing, history and BOTH backspace bytes live in cli/lineedit.cpp.
    // TODO: tab completion, driven by properties() (DESIGN.md 10.4).
    return mon.repl(std::cin, std::cout, true);
}
