# THE HISTORY FILE IS FOR A HUMAN AT A TERMINAL, AND FOR NOBODY ELSE.
#
# The monitor persists its command history to .altairsim_history in the directory it was
# launched from -- but ONLY when a real person is typing at a real terminal. A script
# (-x/-s), a pipe, the test suite, and --mcp must leave the directory exactly as they
# found it: a build that scribbled a dotfile into whatever directory CI happened to run
# in would be a surprise at best and a diff at worst.
#
# This proves the NEGATIVE, which is the whole guarantee: two non-tty launches in a fresh
# directory, and no file afterward. The guard is LineEditor::interactive() -- stdin AND
# stdout both a tty -- so a pipe fails it even on the interactive code path.
#
# pty/pipe trap (altairsim's own note, PR #85): a pipe is not a tty, so we cannot drive the
# POSITIVE case here (an interactive session actually WRITING the file) -- that needs an
# expect/pty harness like tests/acceptance/cli.exp. This file asserts the guard and nothing
# more, which is exactly what the requirement asks.
#
# Expects: -DSIM=<altairsim> -DBIN=<binary dir>

set(work "${BIN}/history-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(hist "${work}/.altairsim_history")

function(expect_no_file why)
  if(EXISTS "${hist}")
    file(READ "${hist}" got)
    message(FATAL_ERROR "history: ${why}\n"
                        "  a non-interactive run wrote ${hist}, which it must never do.\n"
                        "--- file ---\n${got}")
  endif()
endfunction()

# ---- 1. -x is a SCRIPT: interactive==false. It runs a command and leaves. ----
execute_process(
  COMMAND           "${SIM}" -n -x "QUIT"
  WORKING_DIRECTORY "${work}"
  RESULT_VARIABLE   rc
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  TIMEOUT           30
)
expect_no_file("`altairsim -n -x QUIT` (a -x script)")

# ---- 2. A PIPE on stdin: the interactive repl runs, but stdin is not a tty. ----
# This is the case the guard's second term exists for: `altairsim < script` reaches the
# interactive repl (interactive==true) yet must not touch the terminal or the disk,
# because LineEditor::interactive() is false when stdin is a pipe.
file(WRITE "${work}/in.cmds" "RESET\nQUIT\n")
execute_process(
  COMMAND           "${SIM}" -n
  WORKING_DIRECTORY "${work}"
  INPUT_FILE        "${work}/in.cmds"
  RESULT_VARIABLE   rc2
  OUTPUT_VARIABLE   out2
  ERROR_VARIABLE    err2
  TIMEOUT           30
)
expect_no_file("`altairsim -n < script` (a piped, non-tty stdin)")

file(REMOVE_RECURSE "${work}")
message(STATUS "history: no .altairsim_history is written by a script or a pipe.")
