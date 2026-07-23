# TRK80 cassette test — which of these loads on a real Sol-20?

Thanks for offering to test on real hardware — this is the one thing we can't do here.

**All four WAV files contain the *identical* TRK80 (Star Trek) program** — byte for byte the
same bytes as the genuine cassette that already loads for you. The *only* difference between
them is **how the tones are written to the audio**. So whichever ones load (and which don't)
tells us exactly what the real Sol's cassette interface needs, with the data held constant.

## What to do

Play each file into your Sol's cassette input the same way you load the working tape, then:

```
XE TRK80
```

(or `GE TRK80` / `CA` if you'd rather just see whether it catalogs and loads without running.)

Please note, for **each** file, one of:

- **loads and runs** ✅
- **reads the `TRK80` header, then fails** ⚠️ (the symptom you reported)
- **doesn't read anything / no header** ❌

## The files, and what we expect

| File | What it is | Expectation |
|---|---|---|
| `TRK80-Z-real-reference.wav` | The genuine digitized tape (positive control) | Should load — confirms the test rig |
| `TRK80-A-control.wav` | What altairsim writes **today** | Expected to **fail** the way you saw |
| `TRK80-D-square.wav` | Candidate fix — tones on a clean clock grid | Hoping it **loads** |
| `TRK80-E-rounded.wav` | Candidate fix — same, edges rounded like a real modem | **Best guess** — try this one first among the fixes |

If `A` fails and `D` and/or `E` load, we've found and fixed it. If `E` loads but `D` doesn't
(or vice-versa), that's useful too. And if the real reference itself doesn't load on the day,
something in the playback chain changed and the rest won't mean much — so please check that one.

No rush, and thank you.
