# write_input_list(<out> <files...>)
#
# Writes the sorted names of <files...> (relative to CMAKE_SOURCE_DIR) to <out>, one per line,
# and writes it ONLY WHEN THE LIST CHANGED. Put <out> in the DEPENDS of a generator that reads a
# whole folder (the ROM and machine embeds).
#
# WHY: an add_custom_command runs again only when one of its DEPENDS is newer than its OUTPUT.
# Add a file to the folder and the new file is newer; DELETE one and nothing left is newer, so
# the old generated table -- with the deleted entry still in it -- stays in the binary (issue
# #950). CONFIGURE_DEPENDS re-runs configure when a glob changes, and configure calls this, so a
# deleted, added or renamed input rewrites <out>, and <out> is then newer than the output.
#
# An unchanged list leaves <out> untouched, so a plain re-configure does not re-run the embed.
function(write_input_list out)
  set(names "")
  foreach(f IN LISTS ARGN)
    file(RELATIVE_PATH r "${CMAKE_SOURCE_DIR}" "${f}")
    list(APPEND names "${r}")
  endforeach()
  list(SORT names)
  string(JOIN "\n" text ${names})
  string(APPEND text "\n")

  if(EXISTS "${out}")
    file(READ "${out}" old)
    if(old STREQUAL text)
      return()
    endif()
  endif()
  file(WRITE "${out}" "${text}")
endfunction()
