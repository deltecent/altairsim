#include "test.h"

#include <cstdio>

int g_fail = 0;
int g_run = 0;

int main() {
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

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
