#pragma once
#include <cstdio>
#include <string>

extern int g_fail;
extern int g_run;

#define CHECK(cond, what)                                                                \
    do {                                                                                 \
        ++g_run;                                                                         \
        if (!(cond)) {                                                                   \
            ++g_fail;                                                                    \
            std::printf("  FAIL  %s\n        at %s:%d\n", (what), __FILE__, __LINE__);   \
        }                                                                                \
    } while (0)

#define SECTION(name) std::printf("\n%s\n", name)

void test_clock();
void test_bus();
void test_memory();
void test_readonly_props();
void test_save_is_a_read();
void test_hex();
void test_media();
void test_roms();
void test_phantom();
void test_cli();
void test_idle_judgement();
void test_should_pace();
void test_achieved_hz();
void test_boundary();
void test_numbers();
void test_units();
void test_machines();
void test_subunit_schema();
void test_isa();
void test_z80_isa();
void test_cpu();
void test_z80_cpu();
void test_expr();
void test_debug();
void test_dma();
void test_sio2();
void test_88sio();
void test_lines();
void test_wd17xx();
void test_spindle();
void test_dcdd();
void test_mds();
void test_88acr();
void test_frontpanel();
void test_virtc();
void test_hostdir();
void test_hostbridge();
void test_mcp();

// The Developer Guide's worked example (examples/boards/lamp/). Not a shipping
// board -- it is compiled into the test binary only, so the tutorial's code
// cannot rot, and the reader still performs the registry step themselves.
void test_lamp();
