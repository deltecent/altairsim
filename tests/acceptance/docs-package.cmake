# THE PRODUCER'S MANIFEST AND THE READER'S MANIFEST MUST AGREE.
#
# Two files describe what ships. `docs/package.map` is the producer's view -- the single
# source of truth tools/build-package.sh assembles the zip from. `docs/manual/package.md` is
# the reader's view -- the "What is in the package" chapter, the first thing a person opening
# the archive reads. DISTRIBUTION.md says it in one line: "docs/manual/package.md must then
# name it too, because the manual may only name paths that actually ship." Add LICENSE-SDL3 to
# the map and not the manual, or the other way round, and the two drift -- silently, because
# nothing reads both.
#
# This is the CHEAP HALF of "check the manual against the package". It is a text comparison of
# two files in the tree: no build, no packaging, no binary. It does NOT assemble the zip and
# run the manual's own commands against it -- that expensive half still needs build-package.sh
# under a workflow, and package.map's own header says so. What this catches is the drift the
# blacklist in docs-manual.cmake cannot see: that grep proves the manual names nothing OUTSIDE
# the package; this proves the manual and the map name the SAME set inside it.
#
# It is not hypothetical. The exact bug it guards against sat in the tree: the manual promised
# `examples/debugger` while the map did not ship it, found only by reading the two files side
# by side. This is that read, automated.
#
# Expects: -DSRC=<source dir>

# This runs as a standalone `cmake -P` script, so it carries no policies from the project's
# CMakeLists. Without this line CMP0057 is unset and the `name IN_LIST ship` test below is an
# ERROR under OLD behaviour -- which is why this passed on one runner's CMake and failed the
# others. Match the project's floor (top-level CMakeLists) so IN_LIST is the operator it reads as.
cmake_minimum_required(VERSION 3.20)

set(map "${SRC}/docs/package.map")
set(chapter "${SRC}/docs/manual/package.md")

foreach(needed "${map}" "${chapter}")
  if(NOT EXISTS "${needed}")
    message(FATAL_ERROR "docs-package: ${needed} does not exist")
  endif()
endforeach()

file(READ "${map}" maptext)
file(READ "${chapter}" chaptertext)

# ---------------------------------------------------------------------------
# The producer's manifest: every FILE and DIR line in the map.
#
# A line is  `DIR  <package path>  <=  <repo path>`  or the FILE equivalent. The package path
# is the first token after the keyword -- what the reader sees in the zip. A DIR ships a whole
# tree (examples/cpm, examples/basic, ...); the reader's manual lists the PARENT (`examples/`)
# once, not every child, so a DIR contributes its top-level component. A FILE ships as itself.
# ---------------------------------------------------------------------------
string(REPLACE "\n" ";" maplines "${maptext}")

set(ship "")          # what the map says ships, as top-level names the reader would see
foreach(line ${maplines})
  if(line MATCHES "^(DIR|FILE)[ \t]+([^ \t]+)")
    set(kind "${CMAKE_MATCH_1}")
    set(path "${CMAKE_MATCH_2}")
    if(kind STREQUAL "DIR")
      # examples/cpm -> examples   (the parent the manual names once)
      string(REGEX REPLACE "/.*$" "" path "${path}")
      set(path "${path}/")
    endif()
    list(APPEND ship "${path}")
  endif()
endforeach()
list(REMOVE_DUPLICATES ship)

if(ship STREQUAL "")
  message(FATAL_ERROR
    "docs-package: parsed no FILE/DIR lines out of ${map}.\n"
    "  Either the map is empty or its line format changed and this parser did not. Either way\n"
    "  the check below would pass vacuously, which is the failure this project keeps naming:\n"
    "  a test that proves nothing while looking green.")
endif()

# ---------------------------------------------------------------------------
# MAP -> MANUAL: everything the map ships must be named in the chapter.
#
# This is the direction DISTRIBUTION.md spells out and the direction the LICENSE-SDL3 mistake
# lived in. A plain substring test: the chapter's manifest block writes each path literally
# (`LICENSE-SDL3`, `examples/`), so if the token is not in the file's text, the manual has
# stopped naming something the reader is holding.
# ---------------------------------------------------------------------------
set(bad "")
foreach(item ${ship})
  string(FIND "${chaptertext}" "${item}" hit)
  if(hit LESS 0)
    set(bad "${bad}  package.map ships '${item}', and docs/manual/package.md never names it.\n")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# MANUAL -> MAP: nothing the chapter's manifest block lists may be absent from the map.
#
# The chapter opens with a fenced code block -- the archive listing, one ship-item per line in
# the first column. Read the FIRST fence only: later fences are `$ altairsim ...` command
# examples, not manifests. Each first-column name must be either something the map ships or one
# of the BUILD ARTIFACTS the map does not carry as a line because nothing copies them from the
# repo -- the binary and the rendered PDFs are produced, per tools/build-package.sh.
# ---------------------------------------------------------------------------
set(artifacts
    "altairsim"                  # the binary, built not copied
    "altairsim-manual.pdf"       # rendered from docs/manual/ by the doc build
    "altairsim-changelog.pdf"    # rendered from docs/changelog/ by docs.yml
    "altairsim-cheatsheet.pdf"   # rendered from docs/manual/ref/cheatsheet.md by docs.yml
    "altairsim-monitor.pdf"      # rendered from docs/monitor/ by docs.yml
    "altairsim-debugger.pdf")    # rendered from docs/debugger/ by docs.yml

# Split into lines WITHOUT letting CMake's list semantics chop a line on its own semicolons
# (the manifest's "see below." would otherwise arrive as a phantom line beginning "see"). Park
# every literal ';' behind a sentinel across the newline split, then restore it per line.
string(REPLACE ";" "@@SEMI@@" chapterguarded "${chaptertext}")
string(REPLACE "\n" ";" chapterlines "${chapterguarded}")
set(infence FALSE)
set(sawfence FALSE)
foreach(rawline ${chapterlines})
  string(REPLACE "@@SEMI@@" ";" line "${rawline}")
  if(line MATCHES "^```")
    if(infence)
      break()                   # end of the first fence -- the manifest is the first one
    endif()
    set(infence TRUE)
    set(sawfence TRUE)
    continue()
  endif()
  if(NOT infence)
    continue()
  endif()
  # First column: the ship-item. Skip blank lines inside the fence.
  if(line MATCHES "^[ \t]*([^ \t]+)")
    set(name "${CMAKE_MATCH_1}")
    if(name IN_LIST ship OR name IN_LIST artifacts)
      continue()
    endif()
    set(bad "${bad}  docs/manual/package.md lists '${name}', which package.map does not ship.\n")
  endif()
endforeach()

if(NOT sawfence)
  message(FATAL_ERROR
    "docs-package: found no fenced manifest block in ${chapter}.\n"
    "  The chapter is supposed to open with the archive listing in a code fence; without it\n"
    "  the manual->map half of this check reads nothing and passes on an empty set.")
endif()

if(NOT bad STREQUAL "")
  message(FATAL_ERROR
    "THE PACKAGE AND ITS MANUAL DISAGREE ON WHAT SHIPS.\n"
    "\n"
    "  docs/package.map is what the archive actually contains; docs/manual/package.md is what\n"
    "  the manual tells the reader it contains. They must name the same set. Below, they do\n"
    "  not:\n"
    "\n${bad}"
    "\n"
    "  Fix whichever is wrong: add the FILE/DIR line to docs/package.map, or name the path in\n"
    "  docs/manual/package.md. If the map wins (it is the source of truth), the manual follows.\n")
endif()

message(STATUS "docs-package: the map and the manual name the same package.")
