#include "test.h"

#include "boards/mits-2sio.h"
#include "boards/mits-88c700.h"
#include "boards/mits-88sio.h"
#include "boards/proctech-sol.h"
#include "boards/proctech-vdm1.h"
#include "host/display_null.h"
#include "host/endpoint.h"
#include "host/media.h"

#include <cstdio>

int g_fail = 0;
int g_run = 0;

int main() {
    // THE SAME WIRING main() DOES (DESIGN.md 7.7). The monitor knows the endpoint
    // grammar; boards do not. If the tests installed a DIFFERENT resolver -- a
    // convenient one that quietly turned `console` into a NullStream, say -- then
    // machines/default.toml would be exercised here in a configuration that no
    // user will ever run, and the first thing to break would be the real one.
    altair::Sio2Board::setResolver(altair::resolveEndpoint);
    altair::SioBoard::setResolver(altair::resolveEndpoint);
    altair::C700Board::setResolver(altair::resolveEndpoint);
    altair::SolBoard::setResolver(altair::resolveEndpoint);

    // A graphics board draws into an injected Display; headless tests give it a
    // NullDisplay, so a VDM-1 renders into memory and a test reads the pixels back
    // with no window. The SAME injection main() does, one backend down.
    static altair::NullDisplay g_display;
    altair::VdmBoard::setDisplay(&g_display);

    // The REAL media resolver, for the same reason. A test that wants a disk
    // without a filesystem installs a MemoryMedia resolver for the length of the
    // test and puts this one back -- see test_media.cpp.
    altair::setMediaResolver(altair::openHostFile);

    test_hex();
    test_symbols();
    test_media();
    test_tapecodec();
    test_roms();
    test_clock();
    test_bus();
    test_memory();
    test_readonly_props();
    test_save_is_a_read();
    test_phantom();
    test_cli();
    test_idle_judgement();
    test_should_pace();
    test_achieved_hz();
    test_boundary();
    test_numbers();
    test_units();
    test_machines();
    test_load_is_atomic();
    test_clock_survives_load();
    test_subunit_schema();
    test_isa();
    test_z80_isa();
    test_cpu();
    test_z80_cpu();
    test_expr();
    test_debug();
    test_dma();
    test_sio2();
    test_88sio();
    test_lines();
    test_wd17xx();
    test_spindle();
    test_dcdd();
    test_mds();
    test_88acr();
    test_c700();
    test_vdm1();
    test_sol();
    test_tapemount();
    test_frontpanel();
    test_virtc();
    test_hostdir();
    test_hostbridge();
    test_mcp();
    test_lamp();

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
