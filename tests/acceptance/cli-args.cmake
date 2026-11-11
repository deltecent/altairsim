# A FILE NAME WITH A SPACE, run unquoted, reaches altairsim as two arguments (issue #712).
# The error must say what is known -- two names, not two machines -- state the rule, and
# say how to start from a built-in and change it.
#
# Expects: -DSIM=<altairsim> -DBIN=<binary dir>
set(work "${BIN}/cli-args-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/plain" "${work}/spaced")
file(WRITE "${work}/spaced/my machine.toml" "[machine]\nname = \"spaced\"\nbase = \"default\"\n")

function(run_in dir rc_var out_var err_var)
  execute_process(
    COMMAND           "${SIM}" ${ARGN}
    WORKING_DIRECTORY "${dir}"
    RESULT_VARIABLE   rc
    OUTPUT_VARIABLE   out
    ERROR_VARIABLE    err
    TIMEOUT           30)
  set(${rc_var}  "${rc}"  PARENT_SCOPE)
  set(${out_var} "${out}" PARENT_SCOPE)
  set(${err_var} "${err}" PARENT_SCOPE)
endfunction()

function(fail msg out err)
  message(FATAL_ERROR "cli-args: ${msg}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

# ---- 1. A built-in name and a file: the rule, what each one is, and the way out. ----
run_in("${work}/plain" rc out err icecream kmart.toml)
if(NOT rc EQUAL 2)
  fail("two names must exit 2, got ${rc}" "${out}" "${err}")
endif()
if(NOT err MATCHES "Only one machine file or built-in name may be given on the command line")
  fail("the message does not state the rule" "${out}" "${err}")
endif()
if(NOT err MATCHES "'icecream' \\(a built-in name\\) and 'kmart.toml' \\(a machine file\\)")
  fail("the message does not say what kind each argument is" "${out}" "${err}")
endif()
if(NOT err MATCHES "base = \"icecream\"  under \\[machine\\]")
  fail("the message does not say how to start from a built-in" "${out}" "${err}")
endif()

# ---- 2. Two files: the rule and the kinds, but no base advice (there is no built-in). ----
run_in("${work}/plain" rc out err a.toml b.toml)
if(NOT rc EQUAL 2 OR NOT err MATCHES "'a.toml' \\(a machine file\\) and 'b.toml' \\(a machine file\\)")
  fail("two files must be named as such and exit 2" "${out}" "${err}")
endif()
if(err MATCHES "base =")
  fail("base advice appeared with no built-in name to base on" "${out}" "${err}")
endif()

# ---- 3. Quoted, a name with a space is one argument and loads. ----
run_in("${work}/spaced" rc out err "my machine.toml" -x "SHOW MACHINE")
if(NOT rc EQUAL 0 OR NOT out MATCHES "name[ \t]+spaced")
  fail("the quoted name did not load the file" "${out}" "${err}")
endif()

# ---- 4. An option name and an option's word are not case-sensitive. ----
run_in("${work}/plain" rc out err --VERSION)
if(NOT rc EQUAL 0 OR NOT out MATCHES "^AltairSim ")
  fail("--VERSION is not --version" "${out}" "${err}")
endif()
run_in("${work}/plain" rc out err -X "SHOW MACHINE" DEFAULT)
if(NOT rc EQUAL 0 OR NOT out MATCHES "name[ \t]+default")
  fail("-X is not -x, or DEFAULT is not the built-in default" "${out}" "${err}")
endif()
# --mirror PTY is the pty sink, not a port number. With an empty stdin the MCP session ends
# at once; on Windows the pty is refused by name. Either way nothing reads PTY as a port.
file(WRITE "${work}/empty.jsonl" "")
execute_process(
  COMMAND           "${SIM}" --Mirror PTY --MCP
  WORKING_DIRECTORY "${work}/plain"
  INPUT_FILE        "${work}/empty.jsonl"
  RESULT_VARIABLE   rc
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  TIMEOUT           30)
if(err MATCHES "TCP port" OR NOT err MATCHES "pty|/tmp/altairsim")
  fail("--Mirror PTY was not read as --mirror pty" "${out}" "${err}")
endif()

# ---- -x AND -s STOP AT THE FIRST COMMAND THAT FAILS (issue #916). ----
# A refused command leaves the machine as it was, so the commands after it would run on a
# machine the caller did not write, and print output that is wrong without saying why.
# So the run stops there: the error, then one line that says so, then exit 1.
run_in("${work}/plain" rc out err -n -x "SET nosuch x=1" -x "SHOW MACHINE")
if(NOT rc EQUAL 1)
  fail("-x with a failed command must exit 1, got ${rc}" "${out}" "${err}")
endif()
if(NOT out MATCHES "Stopped: that command failed")
  fail("-x did not say it stopped at the failed command" "${out}" "${err}")
endif()
if(out MATCHES "altairsim> SHOW MACHINE")
  fail("-x ran a command after the one that failed" "${out}" "${err}")
endif()

# The same for a script: line 2 fails, line 3 must not run.
file(WRITE "${work}/plain/stop.cmd" "SHOW MACHINE\nSET nosuch x=1\nSHOW CONSOLE\n")
run_in("${work}/plain" rc out err -n -s stop.cmd)
if(NOT rc EQUAL 1)
  fail("-s with a failed line must exit 1, got ${rc}" "${out}" "${err}")
endif()
if(NOT out MATCHES "altairsim> SHOW MACHINE" OR NOT out MATCHES "Stopped: that command failed")
  fail("-s did not run line 1, or did not say it stopped at line 2" "${out}" "${err}")
endif()
if(out MATCHES "altairsim> SHOW CONSOLE")
  fail("-s ran a line after the one that failed" "${out}" "${err}")
endif()
