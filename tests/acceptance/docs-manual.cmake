# THE USER MANUAL MAY NOT NAME ANYTHING THE READER DOES NOT HAVE.
#
# The manual ships inside the distribution: a zip with the `altairsim` binary, the manual PDF,
# and disks/ and tapes/. That is all a reader gets. There is no src/, no CMakeLists, no
# DESIGN.md, no docs/boards/, no repository.
#
# So "the manual is self-contained" is a rule, and a rule that is merely INTENDED is a rule
# that is already lost -- somebody adds one helpful "see src/core/bus.h" and it is true of a
# document nobody can follow. It erodes exactly one line at a time, in good faith, and nobody
# notices until a user does.
#
# This test is the rule, made mechanical. It greps the User Manual's sources for anything that
# points outside the package, and it fails.
#
# THE DEVELOPER GUIDE (docs/devguide/) IS DELIBERATELY NOT CHECKED. It is repo-only, it is
# where the source lives, and telling a board author to read board.h is the whole point of it.
#
# Expects: -DSRC=<source dir>

set(manual "${SRC}/docs/manual")

# What must never appear in a chapter. Each is a thing the reader cannot open.
set(forbidden
    "src/"            # the source
    "DESIGN.md"       # a document they do not have
    "docs/boards/"    # ditto
    "docs/config.md"
    "docs/cli-commands.md"
    "CMakeLists"
    "cmake --build"
    "ctest"
    "tests/"
    "github.com")

file(GLOB_RECURSE chapters "${manual}/*.md")
if(chapters STREQUAL "")
  message(FATAL_ERROR "docs-manual: no chapters found under ${manual}")
endif()

set(bad "")

foreach(f ${chapters})
  file(RELATIVE_PATH rel "${SRC}" "${f}")
  file(READ "${f}" text)

  foreach(needle ${forbidden})
    string(FIND "${text}" "${needle}" hit)
    if(hit GREATER_EQUAL 0)
      # Name the line, not just the file -- a grep the reader has to repeat by hand is a
      # test that tells you that you failed without telling you where.
      string(REPLACE "\n" ";" lines "${text}")
      set(n 0)
      foreach(line ${lines})
        math(EXPR n "${n} + 1")
        string(FIND "${line}" "${needle}" onthisline)
        if(onthisline GREATER_EQUAL 0)
          set(bad "${bad}  ${rel}:${n}: '${needle}'\n      ${line}\n")
        endif()
      endforeach()
    endif()
  endforeach()
endforeach()

# ...AND NO BARE SOURCE-HEADER REFERENCE. The forbidden list above catches "src/", but the
# leak it did NOT catch was a help string that shipped "(core/paths.h)" -- a header with no
# src/ prefix -- straight into the generated reference. A C/C++ source or header name is a
# file the reader does not have, whatever directory it claims (or none). So match name.h,
# dir/name.h, and .hpp/.hh/.cpp/.cc/.cxx -- but the trailing guard requires the extension to
# END there, which is what leaves the tape loaders (LDR4K31.HEX), a lowercase dbl.hex and an
# .html alone: a letter after the ".h" means it is not a header.
foreach(f ${chapters})
  file(RELATIVE_PATH rel "${SRC}" "${f}")
  file(READ "${f}" text)
  string(REPLACE "\n" ";" lines "${text}")
  set(n 0)
  foreach(line ${lines})
    math(EXPR n "${n} + 1")
    if(line MATCHES "[A-Za-z0-9_/]+\\.(hpp|hh|cxx|cpp|cc|h)([^A-Za-z0-9]|$)")
      set(bad "${bad}  ${rel}:${n}: source/header reference '${CMAKE_MATCH_0}'\n      ${line}\n")
    endif()
  endforeach()
endforeach()

if(NOT bad STREQUAL "")
  message(FATAL_ERROR
    "THE USER MANUAL HAS ESCAPED THE PACKAGE.\n"
    "\n"
    "  It names things the reader does not have. A person with the zip gets the binary, the\n"
    "  manual PDF, and disks/ and tapes/ -- and nothing else. Every reference below is an\n"
    "  instruction they cannot follow:\n"
    "\n${bad}"
    "\n"
    "  If this belongs in the DEVELOPER GUIDE (docs/devguide/), put it there -- that document\n"
    "  is allowed to talk about the source, and is not checked.\n")
endif()

# ---------------------------------------------------------------------------
# ...AND EVERY CHAPTER MUST BE IN THE BOOK.
#
# ORDER is the only place the chapter sequence is declared, which means a chapter that is
# never added to it is a chapter that is written, committed, and silently not in the PDF.
# That is a worse failure than a missing file, because everything looks fine.
# ---------------------------------------------------------------------------
file(READ "${manual}/ORDER" order)

foreach(f ${chapters})
  file(RELATIVE_PATH rel "${manual}" "${f}")
  if(rel STREQUAL "README.md")
    continue()  # the table of contents is the index, not a chapter
  endif()
  string(FIND "${order}" "${rel}" hit)
  if(hit LESS 0)
    message(FATAL_ERROR
      "docs/manual/${rel} is not in docs/manual/ORDER.\n"
      "  It is written and committed, and it is NOT IN THE MANUAL. ORDER is the only place\n"
      "  the chapter sequence is declared; add it there, in the place it belongs.")
  endif()
endforeach()

message(STATUS "docs-manual: self-contained, and every chapter is in the book.")
