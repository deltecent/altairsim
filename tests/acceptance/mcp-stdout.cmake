# UNDER --mcp, STDOUT CARRIES JSON-RPC AND NOTHING ELSE (issue #459).
#
# `altairsim <file> --mcp` speaks MCP over stdio, and the stdio contract is that every line on
# stdout is one JSON-RPC message. A strict client parses stdout line by line, so a single
# stray line -- a machine file's `#>` notes, printed for a human at load time -- breaks the
# session before `initialize` is answered.
#
# So this loads a machine file that HAS notes, pipes the MCP handshake in, and checks:
#   * every non-empty stdout line is a JSON object, and the first answers `initialize`;
#   * the notes still appear, on stderr -- moved out of the way, not dropped;
#   * a `!` shell escape sent through the `monitor` tool is refused (issue #915). The child
#     would inherit this stdout, so its output would land between the JSON-RPC messages.
#
# A pipe is enough here: nothing types at a guest, the server exits at EOF on stdin.
#
# Expects: -DSIM=<altairsim> -DBIN=<binary dir>

cmake_minimum_required(VERSION 3.16)

set(work "${BIN}/mcp-stdout-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

file(WRITE "${work}/noted.toml"
  "#> NOTE-ONE for whoever loads this machine.\n"
  "#> NOTE-TWO, a second line.\n"
  "\n"
  "[machine]\n"
  "name = \"noted\"\n"
  "base = \"default\"\n")

file(WRITE "${work}/in.jsonl"
  "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n"
  "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
  "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":"
    "{\"name\":\"monitor\",\"arguments\":{\"command\":\"!echo LEAKED\"}}}\n")

execute_process(
  COMMAND           "${SIM}" "${work}/noted.toml" --mcp
  WORKING_DIRECTORY "${work}"
  INPUT_FILE        "${work}/in.jsonl"
  RESULT_VARIABLE   rc
  OUTPUT_VARIABLE   out
  ERROR_VARIABLE    err
  TIMEOUT           30
)

function(fail why)
  message(FATAL_ERROR "mcp-stdout: ${why}\n"
                      "--- stdout ---\n${out}\n--- stderr ---\n${err}")
endfunction()

if(NOT rc EQUAL 0)
  fail("`altairsim noted.toml --mcp` exited ${rc}, expected 0")
endif()

string(REPLACE "\r" "" out_lf "${out}")
string(REPLACE "\n" ";" lines "${out_lf}")
set(first "")
foreach(line IN LISTS lines)
  if(line STREQUAL "")
    continue()
  endif()
  if(NOT line MATCHES "^{")
    fail("a stdout line is not a JSON-RPC message: '${line}'")
  endif()
  if(first STREQUAL "")
    set(first "${line}")
  endif()
endforeach()

if(first STREQUAL "")
  fail("nothing on stdout -- `initialize` was never answered")
endif()
if(NOT first MATCHES "\"id\":1[,}]")
  fail("the first stdout line is not the `initialize` reply: '${first}'")
endif()

# The `!` reply: an error that says why, and no trace of the child's output anywhere on stdout.
if(out MATCHES "LEAKED")
  fail("`!echo LEAKED` ran under --mcp: its output reached stdout (issue #915)")
endif()
set(reply2 "")
foreach(line IN LISTS lines)
  if(line MATCHES "\"id\":2[,}]")
    set(reply2 "${line}")
  endif()
endforeach()
if(reply2 STREQUAL "")
  fail("no reply to the `monitor` call with `!echo LEAKED`")
endif()
if(NOT reply2 MATCHES "\"isError\":true")
  fail("the `!` call under --mcp did not return isError: '${reply2}'")
endif()
if(NOT reply2 MATCHES "host shell is not available under --mcp")
  fail("the `!` call under --mcp did not say why it was refused: '${reply2}'")
endif()

if(NOT err MATCHES "NOTE-ONE" OR NOT err MATCHES "NOTE-TWO")
  fail("the machine file's `#>` notes are missing from stderr -- moved, not dropped")
endif()

file(REMOVE_RECURSE "${work}")
message(STATUS "mcp-stdout: under --mcp, stdout is JSON-RPC only; the notes go to stderr; `!` is refused.")
