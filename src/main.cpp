#include "cli/monitor.h"
#include "config/toml.h"
#include "core/machine.h"
#include "mcp/server.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace altair;

static void usage() {
    std::cout << "altairsim -- an Altair 8800 / S-100 simulator\n"
                 "\n"
                 "  altairsim [machine.toml]        interactive monitor\n"
                 "  altairsim -c script.cmd         run a command script, exit with its status\n"
                 "  altairsim --mcp [machine.toml]  MCP server on stdio (for Claude)\n"
                 "  altairsim -h\n"
                 "\n"
                 "Milestone 1a: there is no CPU. The monitor is the bus master.\n";
}

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);

    bool mcp = false;
    std::string script, config;

    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "-h" || a[i] == "--help") {
            usage();
            return 0;
        } else if (a[i] == "--mcp") {
            mcp = true;
        } else if (a[i] == "-c") {
            if (i + 1 >= a.size()) {
                std::cerr << "-c needs a script\n";
                return 2;
            }
            script = a[++i];
        } else if (a[i].size() && a[i][0] == '-') {
            std::cerr << "unknown option " << a[i] << "\n";
            return 2;
        } else {
            config = a[i];
        }
    }

    Machine m;

    if (!config.empty()) {
        std::string err;
        if (!loadToml(config, m, err)) {
            std::cerr << err << "\n";
            return 2;
        }
    }

    // MCP and the monitor sit on the SAME Machine. Not a wrapper, not a second
    // model of the world -- the same object, reached two ways (DESIGN.md 11).
    if (mcp) return runMcp(m, std::cin, std::cout);

    Monitor mon(m);
    if (!config.empty()) mon.runStartup(std::cout);

    if (!script.empty()) {
        std::ifstream f(script);
        if (!f) {
            std::cerr << "cannot open '" << script << "'\n";
            return 2;
        }
        // The CI / regression entry point: a script that fails exits non-zero.
        return mon.repl(f, std::cout, false);
    }

    std::cout << "altairsim 0.1.0 -- no CPU yet; the monitor is the bus master.\n"
                 "HELP for commands.\n";
    // Line editing, history and BOTH backspace bytes live in cli/lineedit.cpp.
    // TODO: tab completion, driven by properties() (DESIGN.md 10.4).
    return mon.repl(std::cin, std::cout, true);
}
