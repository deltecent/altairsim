# roms/

Each folder in this directory is one built-in ROM: a monitor, a boot loader or a ROM BASIC
from the period. The build compiles the ROM images into the program. A machine file mounts one
by name and needs no file on disk:

```toml
  [[board.region]]
  type  = "rom"
  at    = 0xFF00
  mount = "builtin:dbl"
```

## What a ROM folder holds

The folder name is the ROM name. `roms/DBL/` is `builtin:dbl`.

| File | What it is |
|---|---|
| `*.HEX` or `*.BIN` | The image. This is the file that the build embeds. |
| `DESC` | One line of text. It is the description that the program shows for the ROM. |
| `*.ASM`, `*.PRN` | The source and the assembler listing, where we have them. `SYMBOLS LOAD` reads the `.PRN`. |
| `README.md` | Where the ROM came from, what it does and how to use it. |
| `*.pdf` | The period manual, where one exists. |
| A subfolder | Files that the build must not embed. The build looks only at the top of a ROM folder, so a `.HEX` in `source/` is safe. |

## How a ROM gets into the program

[`cmake/embed_roms.cmake`](../cmake/embed_roms.cmake) reads each folder and copies the image,
byte for byte, into `roms_generated.cpp`. The script looks only at folders, so a file at this
level is not a ROM.

## Add a ROM

1. Make a folder with the ROM name in upper case.
2. Add the image and a `DESC` file.
3. Add a `README.md` that gives the source of the image.
4. Add the ROM to [`docs/roms.md`](../docs/roms.md) and its source to
   [`docs/sources.md`](../docs/sources.md).
5. Build, then run `./build/altair_tests roms`.

To build an image from a period `.ASM` listing, see
[Assembling a ROM](../docs/devguide/assembling-roms.md) in the Developer Guide.

## Read more

- [`docs/roms.md`](../docs/roms.md) for the list of built-in ROMs and why they are embedded.
