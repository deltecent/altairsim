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

void test_bus();
void test_memory();
void test_hex();
void test_roms();
void test_phantom();
void test_cli();
void test_boundary();
void test_numbers();
