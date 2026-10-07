# A LONG REFERENCE FILE IN THE SKILL MUST OPEN WITH ITS CONTENTS.
#
# docs/DRIVING-WITH-AI.md ships twice: at the package root and as the altairsim skill's
# references/driving-with-ai.md. Anthropic's skill guidance says a reference file over 100
# lines needs a table of contents at the top, because a client may preview only the first 100
# lines and must still see that every section exists. The list is typed by hand, so it can
# drift from the sections. This reads the file and fails when a `## ` heading is missing from
# the `## Contents` block, when the block names a heading that does not exist, or when the
# block does not end inside the first 100 lines.
#
# The cheatsheet needs no check: tools/gen-reference.cpp writes its list from its own headings.
#
# Expects: -DSRC=<source dir>
cmake_minimum_required(VERSION 3.20)

set(doc "${SRC}/docs/DRIVING-WITH-AI.md")
file(READ "${doc}" text)
# Split into lines WITHOUT letting CMake's list rules chop a line on its own ';' or '[' ']'
# (the file is full of code samples). Park each behind a sentinel across the newline split.
string(REPLACE ";" "@@SEMI@@" text "${text}")
string(REPLACE "[" "@@LB@@" text "${text}")
string(REPLACE "]" "@@RB@@" text "${text}")
string(REPLACE "\n" ";" lines "${text}")

set(inContents FALSE)
set(sawContents FALSE)
set(contents "")
set(headings "")
set(contentsEnd 0)
set(n 0)
foreach(line IN LISTS lines)
  string(REPLACE "@@SEMI@@" ";" line "${line}")
  string(REPLACE "@@LB@@" "[" line "${line}")
  string(REPLACE "@@RB@@" "]" line "${line}")
  math(EXPR n "${n} + 1")
  if(line STREQUAL "## Contents")
    set(inContents TRUE)
    set(sawContents TRUE)
    continue()
  endif()
  if(line MATCHES "^## (.*)$")
    set(inContents FALSE)
    list(APPEND headings "${CMAKE_MATCH_1}")
    continue()
  endif()
  if(inContents AND line MATCHES "^- (.*)$")
    list(APPEND contents "${CMAKE_MATCH_1}")
    set(contentsEnd ${n})
  endif()
endforeach()

if(NOT sawContents)
  message(FATAL_ERROR "docs-skill-contents: ${doc} has no '## Contents' section.")
endif()
if(contentsEnd GREATER 100)
  message(FATAL_ERROR
    "docs-skill-contents: the Contents list ends at line ${contentsEnd}; it must end inside the\n"
    "  first 100 lines, or a preview of the file does not show it.")
endif()

set(bad "")
foreach(h IN LISTS headings)
  if(NOT h IN_LIST contents)
    set(bad "${bad}  section '${h}' is not in the Contents list.\n")
  endif()
endforeach()
foreach(c IN LISTS contents)
  if(NOT c IN_LIST headings)
    set(bad "${bad}  Contents names '${c}', which is not a section.\n")
  endif()
endforeach()
if(NOT bad STREQUAL "")
  message(FATAL_ERROR "docs-skill-contents: Contents and sections of ${doc} disagree:\n${bad}")
endif()

message(STATUS "docs-skill-contents: the Contents list matches the sections.")
