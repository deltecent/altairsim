#include "test.h"

#include "boards/sio2.h"
#include "host/endpoint.h"

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

    test_hex();
    test_roms();
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

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
