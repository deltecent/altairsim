# THE OPERATOR'S OWN SCRIPT: ~/altairsim.ini runs on a plain interactive launch, and on
# NOTHING ELSE.
#
# The negative cases are the test. --mcp's stdout is the JSON-RPC transport and -x/-s
# stdout is a CI transcript; a file in $HOME that changed either would make one command
# line print two different things on two computers. So the file is allowed in exactly one
# place, and each of the other three is checked to ignore it.
#
# Expects: -DSIM=<altairsim> -DBIN=<binary dir>
set(work "${BIN}/userinit-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/home" "${work}/empty")

file(WRITE "${work}/home/altairsim.ini" "SHOW MACHINE\n")
file(WRITE "${work}/quit.txt" "QUIT\n")
file(WRITE "${work}/init.json"
     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}}\n")
file(WRITE "${work}/show.ini" "SHOW MACHINE\n")

# HOME for POSIX, USERPROFILE for Windows (platform/win32/home_win32.cpp reads HOME first).
function(launch home input out_var)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "HOME=${home}" "USERPROFILE=${home}"
            "${SIM}" -m default ${ARGN}
    INPUT_FILE        "${input}"
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    err
    TIMEOUT           30
  )
  set(${out_var} "${out}${err}" PARENT_SCOPE)
endfunction()

# 1. A plain launch runs it, echoed behind `ini> `.
launch("${work}/home" "${work}/quit.txt" out)
if(NOT out MATCHES "ini> SHOW MACHINE")
  message(FATAL_ERROR "userinit: a plain launch did not run ~/altairsim.ini\n--- output ---\n${out}")
endif()

# 2. No file in the home folder: nothing printed, nothing tripped.
launch("${work}/empty" "${work}/quit.txt" out)
if(out MATCHES "ini>")
  message(FATAL_ERROR "userinit: output mentions ini> with no file present\n${out}")
endif()

# 3. -x, -s and --mcp never run it.
launch("${work}/home" "${work}/quit.txt" out -x "SHOW MACHINE")
if(out MATCHES "ini>")
  message(FATAL_ERROR "userinit: -x ran ~/altairsim.ini\n${out}")
endif()
launch("${work}/home" "${work}/quit.txt" out -s "${work}/show.ini")
if(out MATCHES "ini>")
  message(FATAL_ERROR "userinit: -s ran ~/altairsim.ini\n${out}")
endif()
launch("${work}/home" "${work}/init.json" out --mcp)
if(out MATCHES "ini>")
  message(FATAL_ERROR "userinit: --mcp ran ~/altairsim.ini\n${out}")
endif()
if(NOT out MATCHES "protocolVersion")
  message(FATAL_ERROR "userinit: --mcp did not answer initialize (the case proves nothing)\n${out}")
endif()
