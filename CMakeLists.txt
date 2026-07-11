cmake_minimum_required(VERSION 3.20)
project(altairsim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Built-in ROMs (DESIGN.md 10.3.1)
#
# Each subdirectory of roms/ is one ROM. Its .HEX or .BIN is embedded VERBATIM
# as a byte array; the C++ side runs the ordinary Intel HEX loader over those
# bytes at startup. One parser, two sources (a host file, or .rodata) -- so a
# built-in cannot drift from a mounted file, and CMake never learns what Intel
# HEX is. Provenance and CRC32 for every ROM live in docs/roms.md.
# ---------------------------------------------------------------------------
set(ROMS_GENERATED "${CMAKE_BINARY_DIR}/generated/roms_generated.cpp")
file(GLOB ROM_DIRS RELATIVE "${CMAKE_SOURCE_DIR}/roms" "${CMAKE_SOURCE_DIR}/roms/*")

set(ROM_IMAGE_FILES "")
foreach(d ${ROM_DIRS})
  if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/roms/${d}")
    file(GLOB img "${CMAKE_SOURCE_DIR}/roms/${d}/*.HEX" "${CMAKE_SOURCE_DIR}/roms/${d}/*.hex"
                  "${CMAKE_SOURCE_DIR}/roms/${d}/*.BIN" "${CMAKE_SOURCE_DIR}/roms/${d}/*.bin")
    list(APPEND ROM_IMAGE_FILES ${img})
  endif()
endforeach()

add_custom_command(
  OUTPUT "${ROMS_GENERATED}"
  COMMAND ${CMAKE_COMMAND}
          -DROMS_DIR=${CMAKE_SOURCE_DIR}/roms
          -DOUT=${ROMS_GENERATED}
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_roms.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_roms.cmake ${ROM_IMAGE_FILES}
  COMMENT "Embedding ROMs from roms/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Core library -- everything except main(). Tests link this.
# ---------------------------------------------------------------------------
add_library(altair_core STATIC
  src/core/crc32.cpp
  src/core/hex.cpp
  src/core/bus.cpp
  src/core/board.cpp
  src/core/machine.cpp
  src/core/roms.cpp
  src/core/value.cpp
  src/boards/memory.cpp
  src/boards/registry.cpp
  src/config/toml.cpp
  src/cli/monitor.cpp
  src/cli/commands.cpp
  src/cli/lineedit.cpp
  src/util/json.cpp
  src/mcp/server.cpp
  "${ROMS_GENERATED}")

target_include_directories(altair_core PUBLIC src)

add_executable(altairsim src/main.cpp)
target_link_libraries(altairsim PRIVATE altair_core)

# ---------------------------------------------------------------------------
# Tests -- no framework; a test is an executable that returns non-zero.
# ---------------------------------------------------------------------------
enable_testing()
add_executable(altair_tests
  tests/main.cpp
  tests/test_bus.cpp
  tests/test_memory.cpp
  tests/test_hex.cpp
  tests/test_roms.cpp
  tests/test_phantom.cpp
  tests/test_cli.cpp
  tests/test_boundary.cpp
  tests/test_numbers.cpp)
target_link_libraries(altair_tests PRIVATE altair_core)
add_test(NAME unit COMMAND altair_tests)
