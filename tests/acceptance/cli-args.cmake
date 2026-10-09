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
