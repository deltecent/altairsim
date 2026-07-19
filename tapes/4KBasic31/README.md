# 4K BASIC 3.1 — the bootstrap source

**The runnable example moved to `examples/basic/` on 2026-07-19**: the machine file, the cassette
image and the assembled bootstrap all live there, because they are what ships.

```
altairsim examples/basic/basic4k.toml
```

What is left here is **source rather than product**, and `tools/build-package.sh` strips it from
the package for that reason:

| File | What it is |
|---|---|
| `LDR4K31.ASM` | MITS's own 4K-3.1 cassette bootstrap, unmodified. Cited by `docs/boards/mits-88acr.md` as the period source the 88-ACR's leader handling was built against. |
| `LDR4K31.PRN` | Its listing. |

`LDR4K31.HEX` — the assembled bootstrap the machine actually loads — is in `examples/basic/`,
beside the tape it boots.
