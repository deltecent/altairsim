# BASIC 1.0 — the bootstrap source

**The runnable example is in `examples/basic/`**: the machine file, the cassette image and the
assembled bootstrap all live there, because they are what ships.

```
altairsim examples/basic/basic1.toml
```

What is left here is **source rather than product**, and `tools/build-package.sh` strips it from
the package for that reason:

| File | What it is |
|---|---|
| `LOAD10.ASM` | MITS's own BASIC 1.0 cassette bootstrap (the ACR / 88-SIO variant, I/O 6/7), unmodified. It loads the tape into memory from `0000` and loops forever — no length, no auto-jump — which is why the example boot is two moves (`RUN 1800`, then STOP/RESET, then `RUN 0`). |
| `LOAD10.PRN` | Its listing. |

`LOAD10.HEX` — the assembled bootstrap the machine actually loads — is in `examples/basic/`,
beside the tape it boots.

Both the tape (`BASIC Ver 1-0.tap`) and this loader are from Mike Douglas's paper-tape/cassette
archive at deramp.com (`.../altair/software/papertape_cassette/`), unmodified.
