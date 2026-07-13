# THE EXAMPLES, IN THE LAYOUT A USER ACTUALLY GETS.
#
# tapes/ and disks/ are not test fixtures. They are what we SHIP: a user gets the
# `altairsim` binary and those two trees, and nothing else -- no repository, no build
# directory, no tests. So the question this file asks is not "do the boards work", which
# every other test already answers. It is:
#
#     DOES THE THING WE HAND PEOPLE WORK WHERE WE HAND IT TO THEM?
#
# Nothing asked that until now, and the coincidence that hid it is that the repo root and
# the distribution root were the same directory on this machine. Every example carried a
# repo-root-relative path, every test ran from the repo root, and the whole suite was green
# while `cd tapes/MitsPS2 && altairsim ps2int.toml` -- the documented way to run it --
# could not find its own tape.
#
# So this test COPIES an example out of the tree, leaving the repository behind, and boots
# it from there. That is the only arrangement in which the bug is visible, which is exactly
# why it is the arrangement to test in.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

set(dist "${BIN}/examples-work")
file(REMOVE_RECURSE "${dist}")

# The distribution: the binary (already built, wherever it is) plus this ONE example
# directory. Note what is NOT here -- machines/, roms/, src/, the repo. If the example
# needs any of it, this test fails, and it should.
file(COPY "${SRC}/tapes/4KBasic31" DESTINATION "${dist}/tapes")

set(example "${dist}/tapes/4KBasic31")

# What 4K BASIC has to print for the machine to have actually booted off the cassette.
# `TAPE OK` is the one that cannot be faked by a machine that merely started: it is the
# output of a BASIC program typed into a BASIC that read itself off a period .TAP.
function(expect_basic out why)
  foreach(want "ALTAIR BASIC" "OK" "42" "TAPE OK")
    string(FIND "${out}" "${want}" hit)
    if(hit LESS 0)
      message(FATAL_ERROR "examples: ${why}\n"
                          "  '${want}' never reached the terminal.\n--- output ---\n${out}")
    endif()
  endforeach()
endfunction()

# ---- 1. THE DOCUMENTED WAY: cd into the example's directory and name the file. --------
#
# This is the case the examples are FOR, and the case that was broken. `basic4k.toml` says
# MOUNT "4K BASIC Ver 3-1.tap" -- the tape lying beside it -- and here that is also the
# working directory, so this would pass even under the old cwd-relative rule. It passes
# here because the tape is where the file says it is, which is the point: the file is now
# true no matter where the directory has been moved to.
execute_process(
  COMMAND           "${SIM}" basic4k.toml
  WORKING_DIRECTORY "${example}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`cd tapes/4KBasic31 && altairsim basic4k.toml` did not boot BASIC")

# ---- 2. ...AND BY PATH, from the top of the distribution. ------------------------------
#
# The same file, the same machine, from a different directory. THIS is the half that needs
# the loader to resolve against the file rather than the process: the tape is not in the
# working directory and never will be. If a machine file meant something different
# depending on where you launched it from, it would be the very trap looksLikeFile()
# refuses to walk into (core/machines.h) -- so it must not.
execute_process(
  COMMAND           "${SIM}" tapes/4KBasic31/basic4k.toml
  WORKING_DIRECTORY "${dist}"
  INPUT_FILE        "${SRC}/tests/acceptance/basic4k.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)
expect_basic("${out}" "`altairsim tapes/4KBasic31/basic4k.toml` from the dist root did not boot BASIC")

# ---- 3. THE NEGATIVE CONTROL, and the only reason to believe either of the above. ------
#
# A path a HUMAN TYPES is relative to the SHELL, and it must stay that way. If the config's
# directory leaked out of the startup list and went on colouring commands typed afterwards,
# then `MOUNT dsk0:drive1 "scratch.dsk"` at the prompt would quietly mean a file next to
# somebody's example instead of the one the operator can see -- which is the same class of
# bug as the one this whole change fixes, only pointing the other way, and far nastier
# because it would find a file rather than fail to.
#
# So: a machine file whose startup mounts the tape BESIDE IT (which must succeed), followed
# by the identical MOUNT typed at the prompt from a directory where that tape is NOT
# (which must FAIL). The two commands are character-for-character the same. Only their
# provenance differs, and provenance is the entire feature.
file(WRITE "${example}/probe.toml"
     "[machine]\nname = \"probe\"\nbase = \"basic4k\"\n"
     "startup = [\"MOUNT acr0:tape \\\"4K BASIC Ver 3-1.tap\\\"\"]\n")

execute_process(
  COMMAND           "${SIM}" tapes/4KBasic31/probe.toml -x "MOUNT acr0:tape \"4K BASIC Ver 3-1.tap\""
  WORKING_DIRECTORY "${dist}"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           30
)

# The startup one found it -- resolved against the file, which lives beside the tape.
string(FIND "${out}" "mounted tapes/4KBasic31/4K BASIC Ver 3-1.tap" hit)
if(hit LESS 0)
  message(FATAL_ERROR
    "examples: the STARTUP mount did not resolve against the machine file's directory.\n"
    "--- output ---\n${out}")
endif()

# ...and the typed one did NOT, because the operator is standing somewhere else.
string(FIND "${out}" "no such file" hit)
if(hit LESS 0)
  message(FATAL_ERROR
    "examples: A TYPED PATH WAS RESOLVED AGAINST THE CONFIG'S DIRECTORY.\n"
    "  `MOUNT acr0:tape \"4K BASIC Ver 3-1.tap\"` was typed from ${dist}, where no such\n"
    "  tape exists -- and it found one anyway, next to the machine file. The config's\n"
    "  directory has escaped its startup list and is now colouring what humans type.\n"
    "--- output ---\n${out}")
endif()

file(REMOVE_RECURSE "${dist}")
message(STATUS "examples: the shipped examples boot from their own directory, and a typed "
               "path still means the shell's.")
