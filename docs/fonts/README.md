# The fonts the PDFs are printed in

These six files exist so that **the machine that builds the PDF stops mattering**.

`docs/print.css` used to name a wish list — `"Charter", "Georgia", "Palatino", serif` —
and a wish list resolves to whatever fonts the builder happens to own. That was invisible
while exactly one Mac ever built the documents. It became a real problem the moment CI
started building them: `ubuntu-latest` has none of those three, so it would have re-typeset
the manual in fallback DejaVu Serif — a different face, different pagination and a
different page count from every copy shipped so far — without a word of complaint.

So the document carries its own faces. `pandoc --embed-resources` inlines them as `data:`
URIs, and **`tools/build-docs.sh` fails the build if any one of them does not inline**.
That check is not ceremony: pandoc only *warns* about a font it cannot fetch and still
exits 0, so the cost of trusting it is a silently wrong PDF — which is the exact failure
this directory was created to end.

## What is here, and where it came from

Every file is an **unmodified upstream release file**. Nothing is subsetted. A subset would
be a quarter of the size, but it would also be a modified font — harder to check against
its source, and a font missing a glyph fails by printing a blank box rather than by saying
so. Unmodified files can be verified against upstream by hash, and both licenses below are
satisfied simply by keeping this notice next to them.

### XCharter — the body text

An extension of Bitstream Charter by Michael Sharpe. Charter is what the manual has always
been set in (macOS ships Bitstream Charter as `Charter.ttc`); XCharter is the same typeface
from a source we are allowed to redistribute, so switching to it changed the look of the
document by essentially nothing.

- Upstream: <https://ctan.org/pkg/xcharter> — `https://mirrors.ctan.org/fonts/xcharter.zip`
- Version 1.26 (2024-06-18); zip `sha256:d8e3ab99355a8cae11c2559dfa9a458530ce3d24dd4f4cbd88f0140e8f31a87e`
- Taken from `xcharter/opentype/` in that archive:

```
e5d22b0f417a0a3e22ee2d7ef10d2e8bba1397066ac3de44e3e3b34e6e56c601  XCharter-Roman.otf
baf595556bbd65749a83b41034c900a8d7f8f5b2a1aa24314319501fdcacce9b  XCharter-Bold.otf
46100192810480a54dd287e8b3966d5bccf4603ba008d9c8f6bdbe562c6a134b  XCharter-Italic.otf
255d912eb1d70aac000a27ad3ee6c8082e7dbc004083b1a86526bcff472b50a9  XCharter-BoldItalic.otf
```

### DejaVu Sans Mono — the code

Every listing and terminal transcript in the manual. **The mono matters more than the serif
here, not less**: the listings are written to a hard 79 columns and mono is the unit a code
block's width is measured in. DejaVu Sans Mono was already the Linux end of the old wish
list, and its advance width (0.602em) is Menlo's — so pinning it moved almost nothing and
guarantees all of it.

- Upstream: <https://github.com/dejavu-fonts/dejavu-fonts>
- Version 2.37 (`dejavu-fonts-ttf-2.37.zip`); zip `sha256:7576310b219e04159d35ff61dd4a4ec4cdba4f35c00e002a136f00e96a908b0a`
- Taken from `dejavu-fonts-ttf-2.37/ttf/`:

```
b4a6c3e4faab8773f4ff761d56451646409f29abedd68f05d38c2df667d3c582  DejaVuSansMono.ttf
bce60f1b4421acd9ea51ba6623d7024ecbe6817a953e3654df62a5e6bdf8f769  DejaVuSansMono-Bold.ttf
```

## Licenses

altairsim is MIT. **These fonts are not**, and neither of them needs to be: both licenses
below permit redistribution outright. Both also require that the notice travel with the
font, which is what this file is for. The full texts are in `LICENSE-XCharter.txt` and
`LICENSE-DejaVu.txt` beside them.

Note what is *not* here: macOS's `/System/Library/Fonts/Supplemental/Charter.ttc` and
`Menlo.ttc` — which is what the old wish list actually resolved to on the machine that
built every shipped PDF. Those are Apple's builds, covered by the macOS license, and
copying them into a public repository is not something the underlying Bitstream grant makes
alright. That is why XCharter and DejaVu are here instead of the fonts you already have.

- **XCharter** — Bitstream Charter's original free license (© 1989–1992 Bitstream Inc.):
  permission "to use, copy, modify, sublicense, sell, and redistribute … for any purpose
  and without restriction", provided the notice is kept intact and the trademark
  acknowledged. BITSTREAM CHARTER is a registered trademark of Bitstream Inc. Sharpe's and
  Panov's extensions are released under the same terms.
- **DejaVu Sans Mono** — the Bitstream Vera license (© 2003 Bitstream Inc.): an MIT-shaped
  grant to reproduce and distribute, with the condition that modified versions be renamed
  away from "Bitstream" and "Vera". DejaVu's own changes are public domain. Bitstream Vera
  is a trademark of Bitstream Inc.
