cmake_minimum_required(VERSION 3.20)
# A file DELETED from a folder that a build step embeds must leave the build output (issue #950).
#
# The ROM and machine embeds are custom commands that read a whole folder. A custom command runs
# again only when a DEPENDS file is newer than its output; after a delete, nothing left is newer.
# cmake/input_list.cmake writes the list of input names at configure time, and the embeds depend
# on it. This test builds a small project with the SAME wiring, using the real helper, and checks
# that a delete, an add and a no-change build each do the right thing.
#
#   -DSRC=<source dir>  -DWORK=<scratch dir>  -DGEN=<generator>  [-DPLATFORM=<generator platform>]

foreach(v SRC WORK GEN)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "embed-input-list: -D${v}= is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
set(proj "${WORK}/proj")
set(bld  "${WORK}/build")
file(MAKE_DIRECTORY "${proj}/inputs")

# The generator: write the names of every file in inputs/ to out.txt, as the embeds do.
file(WRITE "${proj}/gen.cmake" [=[
file(GLOB files RELATIVE "${IN}" "${IN}/*.txt")
list(SORT files)
string(JOIN "\n" text ${files})
file(WRITE "${OUT}" "${text}\n")
]=])

file(WRITE "${proj}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)
project(embedinputlist NONE)
include(\"${SRC}/cmake/input_list.cmake\")
file(GLOB INPUT_FILES CONFIGURE_DEPENDS \"\${CMAKE_SOURCE_DIR}/inputs/*.txt\")
set(INPUTS \"\${CMAKE_BINARY_DIR}/inputs.txt\")
write_input_list(\"\${INPUTS}\" \${INPUT_FILES})
add_custom_command(
  OUTPUT \"\${CMAKE_BINARY_DIR}/out.txt\"
  COMMAND \${CMAKE_COMMAND} -DIN=\${CMAKE_SOURCE_DIR}/inputs -DOUT=\${CMAKE_BINARY_DIR}/out.txt
          -P \${CMAKE_SOURCE_DIR}/gen.cmake
  DEPENDS \${CMAKE_SOURCE_DIR}/gen.cmake \${INPUTS} \${INPUT_FILES}
  VERBATIM)
add_custom_target(gen ALL DEPENDS \"\${CMAKE_BINARY_DIR}/out.txt\")
")

function(build)
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${bld}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "embed-input-list: build failed (${rc}):\n${o}\n${e}")
  endif()
endfunction()

# MAKE COMPARES FILE TIMES TO THE SECOND on macOS (GNU make 3.81, the one Xcode ships). A file
# changed in the same second as the build that read it looks unchanged -- for every rule, a .cpp
# as much as this one. A person edits a file seconds after a build; a test does it in
# milliseconds. So before each change, wait for the clock to reach the next second. Without the
# wait the add case fails on the macOS CI runner, and the no-change case cannot see a list file
# that is rewritten when it must not be.
function(next_second)
  string(TIMESTAMP start "%s")
  while(TRUE)
    string(TIMESTAMP now "%s")
    if(NOT now STREQUAL start)
      break()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.05)
  endwhile()
endfunction()

function(expect_out want why)
  file(READ "${bld}/out.txt" got)
  if(NOT got STREQUAL want)
    message(FATAL_ERROR "embed-input-list: ${why}\n  want: [${want}]\n  got:  [${got}]")
  endif()
endfunction()

# 1. Two inputs: both are in the output.
file(WRITE "${proj}/inputs/a.txt" "a\n")
file(WRITE "${proj}/inputs/b.txt" "b\n")
set(platform_args "")
if(DEFINED PLATFORM AND NOT PLATFORM STREQUAL "")
  set(platform_args -A "${PLATFORM}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${proj}" -B "${bld}" -G "${GEN}" ${platform_args}
                RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "embed-input-list: configure failed (${rc}):\n${o}\n${e}")
endif()
build()
expect_out("a.txt\nb.txt\n" "first build does not list both inputs")

# 2. Delete one: the generator must run again and drop it (issue #950).
next_second()
file(REMOVE "${proj}/inputs/b.txt")
build()
expect_out("a.txt\n" "a deleted input is still in the output -- the generator did not run again")

# 3. Add one: it appears.
next_second()
file(WRITE "${proj}/inputs/c.txt" "c\n")
build()
expect_out("a.txt\nc.txt\n" "an added input is not in the output")

# 4. Configure again with no change: the input list is not rewritten, so the generator does not
#    run again. out.txt is replaced by a marker; if the generator runs, the marker is gone.
file(WRITE "${bld}/out.txt" "untouched\n")
next_second()
execute_process(COMMAND "${CMAKE_COMMAND}" "${bld}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "embed-input-list: re-configure failed (${rc})")
endif()
build()
expect_out("untouched\n" "a build with no change ran the generator again")

message(STATUS "embed-input-list: delete, add and no-change builds are correct")
