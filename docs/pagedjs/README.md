# Paged.js — the paginator

This one file exists so the PDFs can have **running page numbers, a page-numbered table of
contents, and reliable page breaks** — none of which the build's PDF engine can do on its own.

`tools/build-docs.sh` prints the manual with headless Chrome's `--print-to-pdf`. That path
renders our CSS and embeds our fonts, but it does **not** implement CSS Paged Media: no
`@page` margin boxes (so no running "page N" footer) and no `target-counter()` (so a contents
entry cannot say which page its chapter is on). A TOC that lists chapters but not their pages
is the bug `TODO.md` asked to fix, and Chrome alone cannot fix it.

**Paged.js is that missing layer, and nothing more.** It is a single JavaScript polyfill that
runs *inside the same headless Chrome*, before the print: it reads the rendered DOM, chops it
into page boxes itself, and implements the Paged Media features — margin boxes,
`target-counter`, named pages, honest break control. The browser is still the PDF engine; this
just teaches it the part of the print spec it never shipped. That is the same bargain as
`docs/fonts/`: vendor one dependency so the *result* stops depending on the machine.

It is loaded as a `<script>` that `build-docs.sh` injects before the print, embedded into the
self-contained HTML like everything else. Because pagination happens asynchronously *after*
load, `chrome --print-to-pdf` — which captures at load — would print the un-paginated document;
and `--virtual-time-budget`, the usual way to make it wait, does **not** work with Paged.js (its
layout never completes under a virtual clock — see `tools/chrome-print.py` for the evidence). So
`build-docs.sh` drives Chrome over the DevTools protocol (`tools/chrome-print.py`, stdlib Python,
no Node), watches in real time for Paged.js's completion hook, and only then asks for the PDF —
and asserts afterward (with `pdftotext`) that page numbers actually reached it, so a silent
regression to the un-paginated DOM fails the build instead of shipping.

## What is here, and where it came from

`paged.polyfill.js` is the **unmodified** single-file UMD polyfill build shipped by the
project — the `dist/paged.polyfill.js` artifact, not a rebuild of ours.

- Upstream: <https://pagedmedia.org> — source at <https://gitlab.coko.foundation/pagedjs/pagedjs>
- Version 0.4.3, fetched from `https://unpkg.com/pagedjs@0.4.3/dist/paged.polyfill.js`

```
f59f361802416c770d549a647958649af2cf6601999924bc00e4f507dad5269f  paged.polyfill.js
```

Pinned by version **and** by hash: `build-docs.sh` refetches nothing, so what is checked in is
what runs, on every machine and in CI.

## License

Paged.js is **MIT** (© 2018 Adam Hyde; author Fred Chasen) — the full text is in `LICENSE`
beside this file, which is all the license asks: keep the notice with the software. altairsim
itself is MIT, so nothing here changes the terms the package ships under.
