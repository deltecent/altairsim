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
file(REMOVE_RECURSE "${fresh}")
file(MAKE_DIRECTORY "${fresh}")

execute_process(COMMAND "${GEN}" "${fresh}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen-reference failed (${rc}):\n${out}")
endif()

set(committed "${SRC}/docs/manual/ref")

foreach(f boards.md commands.md machines.md cheatsheet.md)
  if(NOT EXISTS "${committed}/${f}")
    message(FATAL_ERROR
      "docs/manual/ref/${f} is missing.\n"
      "  It is generated and COMMITTED. Run:  cmake --build build --target docs-reference")
  endif()

  # Compare bytes, not "does it look close". A diff that tolerates whitespace is a diff
  # that will one day tolerate a wrong default.
  file(READ "${committed}/${f}" have)
  file(READ "${fresh}/${f}" want)

  if(NOT have STREQUAL want)
    message(FATAL_ERROR
      "docs/manual/ref/${f} IS STALE.\n"
      "\n"
      "  The manual's reference is printed from the binary -- from Board::properties() and\n"
      "  the CommandDef table -- and what is committed no longer matches what the code says.\n"
      "  Something changed underneath it: a default, a range, a board, a HELP string.\n"
      "\n"
      "  This is not a docs chore. The committed file is what SHIPS in the manual PDF, so\n"
      "  right now the manual is telling users something the program does not do.\n"
      "\n"
      "  Fix it with:\n"
      "      cmake --build build --target docs-reference\n"
      "  ...and commit the result alongside the change that caused it.")
  endif()
endforeach()

file(REMOVE_RECURSE "${fresh}")
message(STATUS "docs-reference: the committed reference still matches the binary.")
