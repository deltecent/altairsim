# THE 2.1 LINT. No OS anywhere except src/platform/posix/ and src/platform/win32/.
#
# DESIGN.md 2.1 has demanded this since the first line of the project was written:
#
#     "a CI lint greps for _WIN32, __APPLE__, __linux__, __unix__, _MSC_VER anywhere
#      outside src/platform/*/ and FAILS THE BUILD. Without the lint this rule decays
#      within a month: the first time someone needs one small thing on Windows an
#      #ifdef appears 'just this once,' and you are SIMH again."
#
# It ran for the first time on 2026-07-12, the day the terminal finally moved into
# src/platform/ and there was nothing left for it to catch.
#
# ---------------------------------------------------------------------------
# IT GREPS FOR OS HEADERS TOO, AND THAT IS NOT GOLD-PLATING.
#
# The rule as written catches conditional compilation. But when the terminal was moved,
# there were TWO offenders, and only one of them had a conditional:
#
#     src/cli/lineedit.cpp     #if defined(_WIN32)      <- what 2.1 describes
#     src/host/console.cpp     #include <termios.h>     <- and this, with no #ifdef at
#                                                          all, which a macro-only lint
#                                                          would have called CLEAN
#
# console.cpp did not branch on the OS. It just WAS POSIX, in the open, in a file called
# host/console.cpp -- and it would have compiled on Linux and macOS forever and failed on
# Windows the day someone tried. An #ifdef is the SYMPTOM. Reaching for the OS outside
# the platform layer is the disease, and this lint is aimed at the disease.
#
# Run standalone:  cmake -DROOT=<repo> -P cmake/lint_platform.cmake

set(OS_MACROS "(_WIN32|__APPLE__|__linux__|__unix__|_MSC_VER)")

# The headers that ARE an operating system. Not exhaustive, and it does not need to be:
# this is a ratchet, not a proof. Anything that gets past it and turns out to be an OS
# call belongs on this list, and adding it is the fix.
#
# <csignal> IS NOT ON IT, and that is deliberate. The lint's very first run flagged
# src/cli/monitor.cpp, which uses std::signal(SIGINT, ...) to catch ^C during a piped
# RUN -- and that is ISO C++, not POSIX. It compiles on Windows unchanged. The rule is
# "no OPERATING SYSTEM outside the platform layer", not "no header I have not seen
# before", and a lint that cries wolf about portable standard C++ is a lint people
# start passing flags to silence.
#
# (The POSIX-only signals -- SIGHUP, SIGQUIT -- are a different matter, and they are
# used in exactly one place: src/platform/posix/terminal_posix.cpp, where they belong.)
set(OS_HEADERS "#[ \t]*include[ \t]*<(termios|unistd|fcntl|dirent|poll|windows|winsock2|ws2tcpip|io)\\.h>|#[ \t]*include[ \t]*<(sys/|netinet/|arpa/)")

file(GLOB_RECURSE SOURCES
     "${ROOT}/src/*.h" "${ROOT}/src/*.cpp"
     "${ROOT}/tests/*.h" "${ROOT}/tests/*.cpp")

set(VIOLATIONS "")

foreach(f IN LISTS SOURCES)
  # The two files that are ALLOWED to know what they are running on. Note that
  # src/platform/*.h -- the contracts themselves -- are NOT exempt: a conditional in the
  # interface would defeat the entire arrangement, so the interface is linted hardest.
  if(f MATCHES "/src/platform/(posix|win32)/")
    continue()
  endif()

  file(READ "${f}" content)
  string(REPLACE ";" "\\;" content "${content}")
  string(REPLACE "\n" ";" content "${content}")

  file(RELATIVE_PATH rel "${ROOT}" "${f}")
  set(n 0)
  foreach(line IN LISTS content)
    math(EXPR n "${n}+1")
    string(STRIP "${line}" trimmed)

    # A COMMENT IS ALLOWED TO SAY THE WORDS. This file, DESIGN.md and platform/terminal.h
    # all discuss `#include <termios.h>` and `_WIN32` by name, because explaining a rule
    # means being able to quote what it forbids. Code is linted; prose is not.
    if(trimmed MATCHES "^(//|\\*|/\\*)")
      continue()
    endif()

    if(line MATCHES "${OS_MACROS}")
      list(APPEND VIOLATIONS "  ${rel}:${n}: an OS macro -- ${CMAKE_MATCH_1}")
    endif()
    if(line MATCHES "${OS_HEADERS}")
      list(APPEND VIOLATIONS "  ${rel}:${n}: an OS header -- ${trimmed}")
    endif()
  endforeach()
endforeach()

if(VIOLATIONS)
  list(JOIN VIOLATIONS "\n" report)

  # NOTICE, not FATAL_ERROR, for the body: CMake re-wraps and re-indents the text of an
  # error message, which turns a list of file:line locations into paragraph soup. The
  # error itself is one line, and it comes last.
  message(NOTICE
"
DESIGN.md 2.1: THE OPERATING SYSTEM HAS ESCAPED src/platform/.

${report}

An OS difference is AN INTERFACE WITH NO CONDITIONALS IN IT, plus one implementation
file per OS, and CMake picks the directory. It is never an #ifdef, and it is never a
bare include of a POSIX or Win32 header from a file outside src/platform/posix/ or
src/platform/win32/.

  - Need something the OS provides?  Declare it in src/platform/<thing>.h -- with NO OS
    type in the signature: no int fd, no HANDLE -- and implement it once per OS.

  - Genuinely a macOS/Linux split?   That is a posix/macos/ or posix/linux/ FILE. Never
    a conditional inside a shared one.

  - Sure you need the conditional?   Look once more for the way to STOP NEEDING IT. The
    socket layer wanted MSG_NOSIGNAL (Linux) or SO_NOSIGPIPE (macOS); plain
    signal(SIGPIPE, SIG_IGN) is both, and needed no branch at all.

This is a hard architectural rule, and this lint is the only thing keeping it true. If
you are certain an exception is right, that is a conversation, not a commit.
")
  message(FATAL_ERROR "DESIGN.md 2.1 violated -- see above.")
endif()
