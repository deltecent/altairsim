# THE reference/ INDEX AND MANIFEST MAY NOT GO STALE.
#
# reference/*.md are the distilled, text-only hardware references -- one per period scan,
# each citing that scan in its own header. Two hand-maintained files catalogue them, and
# both drift SILENTLY when a distillation is added and only one (or neither) is updated:
#
#   reference/README.md  -- links every distillation (the index a developer browses)
#   docs/sources.md      -- names the period scan each distillation was made from (the manifest)
#
# A distillation committed but left out of either is invisible: no index row to find it by,
# or no recorded provenance for the number a reader is about to trust. This has already
# happened four times at once and was caught only by a hand-run `comm`. So it is a test.
#
# This globs reference/*.md and fails if any file is absent from either catalogue. It is a
# plain text check -- no binary, no toolchain -- in the spirit of docs-manual.cmake.
#
# Expects: -DSRC=<source dir>

cmake_minimum_required(VERSION 3.16)

set(refdir "${SRC}/reference")
set(readme "${refdir}/README.md")
set(manifest "${SRC}/docs/sources.md")

foreach(f "${readme}" "${manifest}")
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "reference-index: expected file not found: ${f}")
  endif()
endforeach()

file(GLOB refs RELATIVE "${refdir}" "${refdir}/*.md")
list(REMOVE_ITEM refs "README.md")
list(LENGTH refs count)
if(count EQUAL 0)
  message(FATAL_ERROR "reference-index: no reference/*.md distillations found under ${refdir}")
endif()

file(READ "${readme}" readme_text)
file(READ "${manifest}" manifest_text)

# README links are relative and URL-encoded. Rather than parse them (CMake's greedy regex
# makes that fragile), encode each on-disk name the way README does -- a closed set of six
# escapes is all these filenames use -- and look for its "](name)" link. None of the
# replacement outputs reintroduces a source character, so the order is immaterial.
set(missing_readme "")
set(missing_manifest "")
foreach(f IN LISTS refs)
  set(enc "${f}")
  string(REPLACE "+" "%2B" enc "${enc}")
  string(REPLACE " " "%20" enc "${enc}")
  string(REPLACE "&" "%26" enc "${enc}")
  string(REPLACE "'" "%27" enc "${enc}")
  string(REPLACE "(" "%28" enc "${enc}")
  string(REPLACE ")" "%29" enc "${enc}")
  string(FIND "${readme_text}" "](${enc})" hit)
  if(hit EQUAL -1)
    list(APPEND missing_readme "${f}")
  endif()
  # The manifest keys rows on the source SCAN, and different rows cite a distillation by a
  # reference/, src/ or docs/boards/ path (or not at all) -- so match on the distillation's
  # bare name (without .md), which every row that documents it carries somewhere.
  string(REGEX REPLACE "\\.md$" "" base "${f}")
  string(FIND "${manifest_text}" "${base}" pos)
  if(pos EQUAL -1)
    list(APPEND missing_manifest "${f}")
  endif()
endforeach()

if(missing_readme OR missing_manifest)
  set(msg "reference-index: a distillation is committed but not catalogued.\n")
  if(missing_readme)
    string(APPEND msg "\n  NOT linked in reference/README.md:\n")
    foreach(f IN LISTS missing_readme)
      string(APPEND msg "      ${f}\n")
    endforeach()
    string(APPEND msg "    -> add a row to the matching section of reference/README.md.\n")
  endif()
  if(missing_manifest)
    string(APPEND msg "\n  NOT named in the docs/sources.md manifest:\n")
    foreach(f IN LISTS missing_manifest)
      string(APPEND msg "      ${f}\n")
    endforeach()
    string(APPEND msg "    -> add a manifest row naming the period scan it was distilled from.\n")
  endif()
  message(FATAL_ERROR "${msg}")
endif()

message(STATUS "reference-index: all ${count} distillations are linked in README.md and named in sources.md.")
