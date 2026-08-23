# THE REFERENCE CHAPTERS ARE NOT ALLOWED TO GO STALE.
#
# docs/manual/ref/*.md is printed from Board::properties() and the CommandDef table -- the
# same reflection layer the monitor, the TOML loader, CONFIG SAVE and MCP all resolve
# against. Committing that output buys a reader (and the PDF build) a reference with no
# toolchain, but it buys it at the usual price: a copy can rot.
#
# So this test regenerates into a temp directory and diffs. Change a property's default,
# add a board, reword a HELP string, and this goes red until you re-run:
#
#     cmake --build build --target docs-reference
#
# It is the same instinct as platform_lint being a build dependency rather than a nicety:
# a rule you can merge and fix later is a rule you have already lost.
#
# Expects: -DGEN=<altair_genref> -DSRC=<source dir> -DBIN=<binary dir>

set(fresh "${BIN}/docs-reference-fresh")
set(freshManual  "${fresh}/manual")
set(freshMonitor "${fresh}/monitor")
file(REMOVE_RECURSE "${fresh}")
file(MAKE_DIRECTORY "${freshManual}")
file(MAKE_DIRECTORY "${freshMonitor}")

execute_process(COMMAND "${GEN}" "${freshManual}" "${freshMonitor}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen-reference failed (${rc}):\n${out}")
endif()

# The board, machine and cheatsheet tables ship in the User Manual; the command reference
# ships in the Monitor. Each is generated into its own document's ref dir -- so this checks
# each committed file against the fresh copy in the matching directory.
set(pairs
  "docs/manual/ref/boards.md|${SRC}/docs/manual/ref/boards.md|${freshManual}/boards.md"
  "docs/manual/ref/machines.md|${SRC}/docs/manual/ref/machines.md|${freshManual}/machines.md"
  "docs/manual/ref/cheatsheet.md|${SRC}/docs/manual/ref/cheatsheet.md|${freshManual}/cheatsheet.md"
  "docs/monitor/ref/commands.md|${SRC}/docs/monitor/ref/commands.md|${freshMonitor}/commands.md")

foreach(pair IN LISTS pairs)
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 rel)
  list(GET parts 1 committed)
  list(GET parts 2 fresh_file)

  if(NOT EXISTS "${committed}")
    message(FATAL_ERROR
      "${rel} is missing.\n"
      "  It is generated and COMMITTED. Run:  cmake --build build --target docs-reference")
  endif()

  # Compare bytes, not "does it look close". A diff that tolerates whitespace is a diff
  # that will one day tolerate a wrong default.
  file(READ "${committed}" have)
  file(READ "${fresh_file}" want)

  if(NOT have STREQUAL want)
    message(FATAL_ERROR
      "${rel} IS STALE.\n"
      "\n"
      "  The reference is printed from the binary -- from Board::properties() and\n"
      "  the CommandDef table -- and what is committed no longer matches what the code says.\n"
      "  Something changed underneath it: a default, a range, a board, a HELP string.\n"
      "\n"
      "  This is not a docs chore. The committed file is what SHIPS in the PDF, so\n"
      "  right now the manual is telling users something the program does not do.\n"
      "\n"
      "  Fix it with:\n"
      "      cmake --build build --target docs-reference\n"
      "  ...and commit the result alongside the change that caused it.")
  endif()
endforeach()

file(REMOVE_RECURSE "${fresh}")
message(STATUS "docs-reference: the committed reference still matches the binary.")
