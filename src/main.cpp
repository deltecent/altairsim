#include "boards/mits-2sio.h"
#include "boards/mits-88sio.h"
#include "cli/monitor.h"
#include "config/toml.h"
#include "core/machine.h"
#include "core/machines.h"
#include "host/endpoint.h"
#include "mcp/server.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

static const char* kVersion = "altairsim 0.1.0";

static void usage(std::ostream& o) {
    o << kVersion << " -- an Altair 8800 / S-100 simulator\n"
         "\n"
         "usage: altairsim [options] [machine]\n"
         "\n"
         "  machine            a built-in name (altairsim 4k), or a config file if it\n"
         "                     has a '/' in it or ends in .toml. Omitted: `default`.\n"
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
         "  -v, --version      -h, --help\n"
         "\n"
         "Milestone 1a: there is no CPU. The monitor is the bus master.\n";
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

    if (!positional.empty()) {
        if (looksLikeFile(positional)) file = positional;
        else builtin = positional;
    } else if (ways == 0) {
        builtin = "default";  // no machine named: you get one anyway, and it is honest
    }

    // THE COMPOSITION ROOT. The monitor knows the endpoint grammar; the boards do
    // not, and must not (DESIGN.md 7.7). Wiring the two together is main's job and
    // nobody else's -- a board that could reach `resolveEndpoint` itself would be
    // one `#include` away from knowing what a socket is.
    Sio2Board::setResolver(resolveEndpoint);
    SioBoard::setResolver(resolveEndpoint);

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

    // The banner reports what is ACTUALLY in the backplane, because a machine with
    // no CPU card in it is a real machine you can build -- it is the one milestone
    // 1a ran, with the monitor as bus master -- and saying so is more useful than a
    // fixed string that goes stale the moment a card lands.
    std::cout << kVersion << " -- ";
    if (CpuCore* c = m.cpu()) std::cout << c->isa() << ", " << m.clock.hz() / 1000000.0 << " MHz.\n";
    else std::cout << "no CPU in the backplane; the monitor is the bus master.\n";
    std::cout << "machine: " << m.name << ".  HELP for commands.\n";
    // Line editing, history and BOTH backspace bytes live in cli/lineedit.cpp.
    // TODO: tab completion, driven by properties() (DESIGN.md 10.4).
    return mon.repl(std::cin, std::cout, true);
}
