---
name: real-altair
description: Drive a real Altair 8800 whose serial console is on another computer — run serial-mcp-server on that computer over ssh, register it here as the `real-altair` MCP server, then type at ALTMON and read what it prints. The user gives only the remote IP address and the serial port. Use when a task needs the real machine, not the simulator — for example a result to check against real hardware (issue #908), or when asked to connect to, run on or test on the real Altair.
---

# Drive a real Altair over ssh

This skill connects Claude to the console of a real Altair. The Altair's serial port is on a
remote computer. [serial-mcp-server](https://github.com/deltecent/serial-mcp-server) runs on
that computer and owns the port. Claude Code starts it over ssh, so its `serial.*` tools work
here as if the port were local.

The simulator is the `altairsim` skill. Use this skill only for the real machine.

## 1. Inputs

| Input | Example | Default |
|---|---|---|
| `HOST` | `192.168.1.40`, or `user@192.168.1.40` | none. Ask the user. |
| `PORT` | `/dev/cu.usbserial-A10KXYZ` | none. Ask the user. |
| baud | `9600` | 9600 |
| format | 8 data bits, no parity, 2 stop bits | 8N2 |

The user gives only `HOST` and `PORT`. Change the baud or the format only when the user says
so. ALTMON sets the 88-2SIO to 8N2 (`reference/ALTMON.md` §1).

Put `HOST` and `PORT` in no tracked file. The registration in step 5 is in local scope, which
is outside the repository.

## 2. Check for an earlier setup

```sh
claude mcp get real-altair
```

If the server exists, it uses the same `HOST`, and the `serial.open` tool of `real-altair` is
in your tool list, go to step 6. If the server exists for a different `HOST`, remove it
(`claude mcp remove real-altair --scope local`) and continue with step 3.

## 3. Check ssh

```sh
ssh -o BatchMode=yes -o ConnectTimeout=5 HOST true
```

If this command fails, stop. Tell the user to do these steps:

1. On the remote Mac, turn on **System Settings > General > Sharing > Remote Login**.
2. On this computer, type `! ssh-copy-id HOST` at the Claude Code prompt.

After the user has done both steps, do step 3 again.

## 4. Check the remote computer

Run the checks in one ssh call:

```sh
ssh -o BatchMode=yes HOST 'ls -l PORT; lsof PORT; ls ~/.serial-mcp/venv/bin/serial_mcp'
```

Read the output:

- `ls -l PORT` must show the device. If it does not, stop and give the user the list from
  `ssh HOST 'ls /dev/cu.*'`.
- `lsof PORT` must print nothing. If it prints a process, another program has the port open.
  Stop and tell the user which program.
- If `~/.serial-mcp/venv/bin/serial_mcp` exists, the install is done. Go to step 5, part 2.

## 5. Install and register

1. Install serial-mcp-server on the remote computer:

   ```sh
   ssh -o BatchMode=yes HOST 'set -e
     [ -x ~/.local/bin/uv ] ||
       curl -LsSf https://astral.sh/uv/install.sh | env INSTALLER_NO_MODIFY_PATH=1 sh
     export UV_PYTHON_INSTALL_DIR=~/.serial-mcp/python
     ~/.local/bin/uv venv --quiet --python 3.12 ~/.serial-mcp/venv
     ~/.local/bin/uv pip install --quiet --only-binary cryptography \
       --python ~/.serial-mcp/venv/bin/python serial-mcp-server
     ls -l ~/.serial-mcp/venv/bin/serial_mcp'
   ```

   The install needs no password and changes nothing outside the user's home folder.
   serial-mcp-server needs Python 3.11 or later. The Python that comes with macOS is 3.9.
   For this reason, `uv` installs its own Python 3.12 in `~/.serial-mcp/python`. The install
   does not use Homebrew.

   `--only-binary cryptography` is necessary on an Intel Mac. The newest `cryptography` has no
   prebuilt package for Intel Macs, and its source build fails without OpenSSL. The option
   selects the newest version that has a prebuilt package.

2. Register the server here:

   ```sh
   claude mcp add --scope local real-altair -- \
     ssh -T -o BatchMode=yes -o ServerAliveInterval=30 HOST .serial-mcp/venv/bin/serial_mcp
   ```

3. Stop. Claude Code loads a new MCP server only when it starts. Give the user these steps,
   and tell them to ask again after them:

   - **In this session:** type `/mcp`. Select **real-altair** with the arrow keys and press
     Enter. Choose **Reconnect** (or **Enable**), then press Esc.
   - **Or restart:** type `/exit`, then run `claude -c` in the same folder. The `-c` continues
     the conversation.

## 6. Open the port

Claude Code shows the server's tools with `_` in place of `.`. For example, `serial.open` is
`mcp__real-altair__serial_open`. Load them with `ToolSearch` first.

Call `serial.open`:

```json
{"port": "PORT", "baudrate": 9600, "bytesize": 8, "parity": "N", "stopbits": 2,
 "timeout_ms": 1000}
```

Keep the `connection_id` that it returns. Every other call needs it.

ALTMON prints nothing for `ESC` at the `*` prompt. To check the link, do the dump in step 7.
If no echo comes back, the Altair is off, ALTMON is not running, or the baud is wrong. Stop and
tell the user. Do not try other baud rates.

## 7. Type at ALTMON

Read `reference/ALTMON.md` §2 before you type a command. It gives the input rules.

**Write one part of a command at a time, and read its echo before you write the next part.**
The 88-2SIO holds one received byte, and it has no flow control. ALTMON does not read the port
while it prints. A character that arrives while ALTMON prints is lost. For example, a write of
`D0000000F` in one call echoed `DUMP 0000 00`, and the rest was lost.

1. Write the command letter, for example `D`. Read with `serial.read` until the name comes
   back (`DUMP`, `IN`, `OUT`). The name is the long print, so the letter goes alone.
2. Write all the values in one call, for example `00000010`. ALTMON prints only one space
   between two values, and the port holds that one byte.
3. Call `serial.read_until` with `"delimiter": "\n*"`. Use `"\n*"`, not `"*"`, because a `*`
   can be in the text column of a dump.
4. Compare the echo with what you wrote. If they are different, a character was lost. Write
   `ESC` (`"data": "1B", "as": "hex"`) to cancel the command, and start it again.

Other rules:

- Do not send a space or a `CR` inside a command. Either one cancels the command.
- Give a long command, such as a big `D` range, a longer `timeout_ms`. At 9600 baud, one dump
  line of 16 bytes takes about 75 ms.
- For a `P` deposit, write one byte (two hex digits) in each call, and read its echo. ALTMON
  prints the next address's byte and a dash after each byte. End with `ESC`.
- For an `H0` load, write one Intel HEX record (with its line end) in each call. Read until
  `"\n"` to get the address that ALTMON prints for the record. After the end record, read
  until `"\n*"`. Check the load with `Q` (checksum) over the program's range.

Example, `IN 59`: write `I`, read `IN`, write `59`, read until `"\n*"`. The result is
` 59 7F` and the prompt.

### 7.1 A long test series

Typing many cases through the `serial.*` tools takes hundreds of calls. For a long series,
write a Python script that uses `pyserial` with the same rules, and run it on the remote
computer. The install in step 5 already has `pyserial`:

1. Call `serial.close` first. Only one program can have the port open.
2. Copy the script: `scp script.py HOST:.serial-mcp/work/` (make the folder first with
   `ssh HOST 'mkdir -p ~/.serial-mcp/work'`).
3. Run it: `ssh -o BatchMode=yes HOST 'cd ~/.serial-mcp/work && ../venv/bin/python script.py'`.
   Have the script print its results, and check each echo the same as in step 7.

## 8. Close the port

When the task is done, call `serial.close`. The port is then free for a person at the remote
computer.

To remove the setup, run `claude mcp remove real-altair --scope local`. The install on the
remote computer stays in `~/.serial-mcp/` and `~/.local/bin/uv`.
