#include "test.h"

#include "boards/cromemco-d7a.h"
#include "boards/cromemco-dazzler.h"
#include "boards/mits-2sio.h"
#include "boards/mits-884pio.h"
#include "boards/mits-88c700.h"
#include "boards/mits-88lpc.h"
#include "boards/mits-88pio.h"
#include "boards/mits-88sio.h"
#include "boards/mits-88uio.h"
#include "boards/mits-turnkey.h"
#include "boards/pmmi-mm103.h"
#include "boards/usio.h"
#include "boards/proctech-sol.h"
#include "boards/proctech-vdm1.h"
#include "boards/sd-sbc.h"
#include "boards/sd-vdb8024.h"
#include "host/display_null.h"
#include "host/endpoint.h"
#include "host/joystick_null.h"
#include "host/media.h"

#include <cstdio>

int g_fail = 0;
int g_run = 0;

namespace {

// The whole suite as an ordered name->function table, keyed by each test
// function's name minus its `test_` prefix. No args runs all of it in this
// order (identical to the historic flat call list, so the `unit` ctest test and
// CI are unaffected); named args run only the matching rows. See the plan and
// CLAUDE.md's Testing section -- this is a local iteration speed-up, not a
// substitute for the pre-commit full run.
const struct {
    const char* name;
    void (*fn)();
} kTests[] = {
    {"hex", test_hex},
    {"symbols", test_symbols},
    {"media", test_media},
    {"tapecodec", test_tapecodec},
    {"roms", test_roms},
    {"clock", test_clock},
    {"statefile", test_statefile},
    {"snapshot", test_snapshot},
    {"bus", test_bus},
    {"memory", test_memory},
    {"readonly_props", test_readonly_props},
    {"save_is_a_read", test_save_is_a_read},
    {"phantom", test_phantom},
    {"cli", test_cli},
    {"idle_judgement", test_idle_judgement},
    {"should_pace", test_should_pace},
    {"achieved_hz", test_achieved_hz},
    {"boundary", test_boundary},
    {"numbers", test_numbers},
    {"units", test_units},
    {"machines", test_machines},
    {"load_is_atomic", test_load_is_atomic},
    {"clock_survives_load", test_clock_survives_load},
    {"subunit_schema", test_subunit_schema},
    {"isa", test_isa},
    {"z80_isa", test_z80_isa},
    {"cpu", test_cpu},
    {"z80_cpu", test_z80_cpu},
    {"expr", test_expr},
    {"debug", test_debug},
    {"ddt", test_ddt},
    {"dma", test_dma},
    {"sio2", test_sio2},
    {"usio", test_usio},
    {"88sio", test_88sio},
    {"sbc", test_sbc},
    {"lines", test_lines},
    {"modemline", test_modemline},
    {"wd17xx", test_wd17xx},
    {"versafloppy", test_versafloppy},
    {"tarbell", test_tarbell},
    {"spindle", test_spindle},
    {"dcdd", test_dcdd},
    {"mds", test_mds},
    {"hdsk", test_hdsk},
    {"88acr", test_88acr},
    {"88uio", test_88uio},
    {"c700", test_c700},
    {"papertape", test_papertape},
    {"pmmi", test_pmmi},
    {"lpc", test_lpc},
    {"printer", test_printer},
    {"tee", test_tee},
    {"pio", test_pio},
    {"4pio", test_4pio},
    {"vdm1", test_vdm1},
    {"vdb8024", test_vdb8024},
    {"dazzler", test_dazzler},
    {"d7a", test_d7a},
    {"sol", test_sol},
    {"tapemount", test_tapemount},
    {"frontpanel", test_frontpanel},
    {"turnkey", test_turnkey},
    {"virtc", test_virtc},
    {"hostdir", test_hostdir},
    {"hostbridge", test_hostbridge},
    {"mcp", test_mcp},
    {"lamp", test_lamp},
};

bool nameEq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return false;
    }
    return *a == *b;
}

} // namespace

int main(int argc, char** argv) {
    // THE SAME WIRING main() DOES (DESIGN.md 7.7). The monitor knows the endpoint
    // grammar; boards do not. If the tests installed a DIFFERENT resolver -- a
    // convenient one that quietly turned `console` into a NullStream, say -- then
    // machines/default.toml would be exercised here in a configuration that no
    // user will ever run, and the first thing to break would be the real one.
    altair::Sio2Board::setResolver(altair::resolveEndpoint);
    altair::TurnkeyBoard::setResolver(altair::resolveEndpoint);
    altair::SioBoard::setResolver(altair::resolveEndpoint);
    altair::SbcBoard::setResolver(altair::resolveEndpoint);
    altair::UioBoard::setResolver(altair::resolveEndpoint);
    altair::C700Board::setResolver(altair::resolveEndpoint);
    altair::LpcBoard::setResolver(altair::resolveEndpoint);
    altair::PioBoard::setResolver(altair::resolveEndpoint);
    altair::Pio4Board::setResolver(altair::resolveEndpoint);
    altair::SolBoard::setResolver(altair::resolveEndpoint);
    altair::Vdb8024Board::setResolver(altair::resolveEndpoint);
    altair::PmmiBoard::setResolver(altair::resolveEndpoint);
    altair::UsioBoard::setResolver(altair::resolveEndpoint);

    // A graphics board draws into an injected Display; headless tests give it a
    // NullDisplay, so a VDM-1 renders into memory and a test reads the pixels back
    // with no window. The SAME injection main() does, one backend down.
    static altair::NullDisplay g_display;
    altair::VdmBoard::setDisplay(&g_display);
    altair::DazzlerBoard::setDisplay(&g_display);
    altair::Vdb8024Board::setDisplay(&g_display);

    // The game-controller service, injected the same way: a D+7A reads its JS-1 sticks
    // from here. Headless tests give it a NullJoystick (every stick centered, no
    // buttons -- a board with no console plugged in); a test that cares installs its own
    // stub for its length (see test_d7a.cpp), exactly as test_media swaps resolvers.
    static altair::NullJoystick g_joystick;
    altair::D7aBoard::setJoystick(&g_joystick);

    // The REAL media resolver, for the same reason. A test that wants a disk
    // without a filesystem installs a MemoryMedia resolver for the length of the
    // test and puts this one back -- see test_media.cpp.
    altair::setMediaResolver(altair::openHostFile);

    const int kCount = static_cast<int>(sizeof(kTests) / sizeof(kTests[0]));

    // Collect selectors, and handle --list up front.
    bool haveSelectors = false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (nameEq(arg, "--list") || nameEq(arg, "-l")) {
            for (int t = 0; t < kCount; ++t)
                std::printf("%s\n", kTests[t].name);
            return 0;
        }
        haveSelectors = true;
    }

    if (!haveSelectors) {
        // No args: run the whole table in order -- identical to the historic behavior.
        for (int t = 0; t < kCount; ++t)
            kTests[t].fn();
    } else {
        // Named selectors: an unknown name is a hard error, so a typo can't run
        // zero tests and print "0 failed" (which would read as a pass).
        for (int i = 1; i < argc; ++i) {
            bool matched = false;
            for (int t = 0; t < kCount; ++t) {
                if (nameEq(argv[i], kTests[t].name)) { matched = true; break; }
            }
            if (!matched) {
                std::fprintf(stderr,
                    "error: unknown test '%s' (try --list)\n", argv[i]);
                return 2;
            }
        }
        // Run matching rows in table order, each under a header.
        for (int t = 0; t < kCount; ++t) {
            bool selected = false;
            for (int i = 1; i < argc; ++i) {
                if (nameEq(argv[i], kTests[t].name)) { selected = true; break; }
            }
            if (!selected) continue;
            std::printf("== %s ==\n", kTests[t].name);
            kTests[t].fn();
        }
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
