# cmake/

These are the helper scripts that the root [`CMakeLists.txt`](../CMakeLists.txt) runs during a
build. The build itself is described in `CMakeLists.txt`. This directory holds only the parts
that are easier to read as separate files.

## What is here

| File | What it does |
|---|---|
| `embed_machines.cmake` | Reads each `.toml` in [`machines/`](../machines/) and writes `machines_generated.cpp`. The built-in machines are compiled into the program from that file. |
| `embed_roms.cmake` | Reads each folder in [`roms/`](../roms/) and writes `roms_generated.cpp`. The built-in ROMs are compiled into the program from that file. |
| `input_list.cmake` | Writes the list of input file names for each embed step, and writes it only when the list changes. Each embed step depends on that list. When you delete a file from `machines/` or a folder from `roms/`, the list changes, and the next build runs the embed step again. |
| `lint_platform.cmake` | Searches the source for operating-system macros and headers outside `src/platform/`. The build fails if it finds one. |
| `version.h.in` | The template for the generated `version.h`. The version number is set in `CMakeLists.txt`. |

The generated files go into `build/generated/`. They are not tracked.

## Rules

- The two embed scripts copy each file byte for byte. They do not parse TOML or Intel HEX. The
  C++ code parses the embedded text with the same loader that reads a file from disk.
- A script that runs with `cmake -P` needs its own `cmake_minimum_required` line. It does not
  get the policies of the root `CMakeLists.txt`.
- [`tools/embed.cpp`](../tools/embed.cpp) produces the same three generated files for the
  plain `Makefile` build. If you change an embed script, change `embed.cpp` to match.

## Read more

- [Building it](../docs/devguide/building.md) in the Developer Guide.
- [`DESIGN.md`](../DESIGN.md) §2.1 for the rule that `lint_platform.cmake` enforces.
