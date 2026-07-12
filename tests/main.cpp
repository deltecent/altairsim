#include "test.h"

#include "boards/mits-2sio.h"
#include "boards/mits-88sio.h"
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

    // The REAL media resolver, for the same reason. A test that wants a disk
    // without a filesystem installs a MemoryMedia resolver for the length of the
    // test and puts this one back -- see test_media.cpp.
    altair::setMediaResolver(altair::openHostFile);

    test_hex();
    test_media();
    test_roms();
    test_clock();
    test_bus();
    test_memory();
    test_phantom();
    test_cli();
    test_boundary();
    test_numbers();
    test_units();
    test_machines();
    test_isa();
    test_cpu();
    test_debug();
    test_sio2();
    test_88sio();
    test_lines();
    test_wd17xx();
    test_spindle();

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
