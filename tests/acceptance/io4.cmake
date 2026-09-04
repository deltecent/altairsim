# THE SSM IO-4 ACCEPTANCE TEST: boot the shipped example machine, whose console is a real
# SSM IO-4 serial channel, and prove the SSM 8080 System Monitor comes up on it.
#
# tests/test_io4.cpp already boots the monitor on a hand-built machine; this asks the other
# question -- does examples/io4/io4.toml, the file we hand people, boot in the layout they get
# it? It runs the example the documented way (cd into its directory, name the toml) and reads
# the banner the monitor prints UNPROMPTED at cold start. No keys are typed: the machine's
# startup RUN F000 cold-starts the monitor, which writes "MONITOR V1.0" through the IO-4's
# Serial A channel. We feed a few carriage returns (io4.keys) only so the console does not
# hit end-of-file mid-cold-start and stop the machine before the banner is out -- a real
# terminal never EOFs, so this is a piped-run artifact, not a keypress the monitor needs.
#
# IF THIS FAILS the shipped example is wrong (a strap, a port, the ROM window, or the console
# wiring) -- the monitor and the board are proven elsewhere.
#
# Expects: -DSIM=<altairsim> -DSRC=<source dir> -DBIN=<binary dir>

set(work "${BIN}/io4-work")
file(REMOVE_RECURSE "${work}")

# Copy ONLY the example out of the tree -- no repository, no roms/, no build dir. The SSM 8080
# monitor is a built-in ROM (builtin:ssm-8080mon), so the example needs nothing beside it.
file(COPY "${SRC}/examples/io4" DESTINATION "${work}/examples")
set(example "${work}/examples/io4")

# Run it the documented way: cd into the example directory and name the toml.
execute_process(
  COMMAND           "${SIM}" io4.toml
  WORKING_DIRECTORY "${example}"
  INPUT_FILE        "${SRC}/tests/acceptance/io4.keys"
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    out
  TIMEOUT           60
)

string(FIND "${out}" "MONITOR V1.0" hit)
if(hit LESS 0)
  message(FATAL_ERROR "io4: examples/io4/io4.toml did not boot the SSM 8080 monitor.\n"
                      "  'MONITOR V1.0' never reached the terminal.\n--- transcript ---\n${out}")
endif()

file(REMOVE_RECURSE "${work}")
message(STATUS "io4: examples/io4/io4.toml booted the SSM 8080 monitor on the IO-4 console.")
