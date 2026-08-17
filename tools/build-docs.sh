#!/bin/sh
#
# Build the documents.
#
#   docs/manual/    -> altairsim-manual.pdf     SHIPS IN THE PACKAGE. Self-contained.
#   docs/changelog/ -> altairsim-changelog.pdf  SHIPS IN THE PACKAGE. What changed, per release.
#   docs/devguide/  -> altairsim-devguide.pdf   Repo only. May talk about the source.
#
# THIS IS NOT PART OF THE BUILD, and it must never become part of it. altairsim's loudest
# claim is that it builds with a C++20 compiler and CMake and nothing else -- no
# downloads, no package manager. A PDF needs pandoc and a browser, and neither of them is
# allowed anywhere near the thing that produces the simulator. So: a separate script, and
# a CMake target that is SKIPPED with a message when the tools are absent.
#
# There is no LaTeX here on purpose. pandoc's PDF writer wants a TeX engine (a gigabyte of
# it), and we do not need one: pandoc emits a self-contained HTML page and a browser prints
# it. Chrome's --print-to-pdf is a real, paginating print path -- it is what the browser
# does when you hit Cmd-P -- and it renders the CSS we already have to write for the
# HTML anyway.
#
#   usage: tools/build-docs.sh [outdir]

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/docs}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# ---------------------------------------------------------------------------
# The tools. Say what is missing and what it is for -- a build that just says
# "command not found: pandoc" makes the reader guess whether they broke something.
# ---------------------------------------------------------------------------
have() { command -v "$1" > /dev/null 2>&1; }

if ! have pandoc; then
  echo "build-docs: pandoc is not installed -- it is what turns the Markdown into a page." >&2
  echo "            The Markdown manual is complete and readable at docs/manual/." >&2
  echo "            To get the PDF:  brew install pandoc   (or your platform's equivalent)" >&2
  exit 1
fi

# The browser is the PDF engine. Any Chromium will do; name the ones we know.
chrome=""
for c in \
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  "/Applications/Chromium.app/Contents/MacOS/Chromium" \
  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge" \
  google-chrome chromium chromium-browser microsoft-edge; do
  if [ -x "$c" ] || have "$c"; then chrome=$c; break; fi
done

if [ -z "$chrome" ]; then
  echo "build-docs: no Chromium-based browser found -- one is used as the PDF engine." >&2
  echo "            (Not LaTeX: a browser already paginates and already renders our CSS.)" >&2
  exit 1
fi

# Paged.js (docs/pagedjs/) gives the PDFs their running page numbers, their page-numbered
# contents and their honest page breaks -- none of which the browser's own print path can do.
# It runs INSIDE the browser, and it finishes asynchronously, so we cannot use --print-to-pdf
# (it captures too early). tools/chrome-print.py drives the browser over the DevTools protocol
# instead, waits for Paged.js, then prints -- and it is stdlib-only Python, so the one new tool
# the PDFs now need is python3, which the CI runners and every dev machine already have.
if ! have python3; then
  echo "build-docs: python3 is not installed -- it drives the browser and waits for Paged.js" >&2
  echo "            (tools/chrome-print.py, stdlib only). The Markdown manual is at docs/manual/." >&2
  exit 1
fi
pagedjs="$root/docs/pagedjs/paged.polyfill.js"
[ -f "$pagedjs" ] || { echo "build-docs: docs/pagedjs/paged.polyfill.js is missing -- it is the paginator." >&2; exit 1; }

# ---------------------------------------------------------------------------
# TOKENS. The manual writes {{MACHINE_CPM}}; docs/package.map says what that is.
#
# THE SUBSTITUTION IS ALLOWED TO FAIL, AND THAT IS THE POINT. A path that reached the PDF
# as a literal {{...}} would be a broken instruction shipped to a user; a path someone
# typed by hand instead of using a token would silently escape the package contract. So an
# unexpanded token is a hard error, checked after substitution.
# ---------------------------------------------------------------------------
map=$root/docs/package.map

expand() {  # expand <src.md> <dst.md>
  cp "$1" "$2"
  # Read TOKEN = value lines; skip comments and the DIR table.
  sed -n 's/^\([A-Z_][A-Z0-9_]*\)[ \t]*=[ \t]*\(.*\)$/\1\t\2/p' "$map" |
  while IFS="$(printf '\t')" read -r key val; do
    # `|` as the delimiter: package paths contain `/`, and none of them contain `|`.
    sed "s|{{$key}}|$val|g" "$2" > "$2.tmp" && mv "$2.tmp" "$2"
  done
}

# ---------------------------------------------------------------------------
# CHECK WHAT ACTUALLY CAME OUT. Embedding the fonts only guarantees they were OFFERED to the
# browser; it says nothing about a character neither of them has. Chrome answers that silently,
# by going shopping on the local machine -- and the very first build with embedded fonts did
# exactly that, pulling one arrow out of macOS's Lucida Grande because XCharter-Bold has no
# U+2192. One glyph, no warning, unshippable font, and a different result on a machine without
# it.
#
# So: every face in the finished PDF must be one we shipped. This is the check that catches the
# NEXT character somebody types that our fonts do not have. Both build() and build_readme() run
# it, on the temp PDF, before it is allowed to become the shipped file.
# ---------------------------------------------------------------------------
check_fonts() {  # check_fonts <pdf> <display-name>
  pdf=$1; label=$2
  if have pdffonts; then
    faces=$(pdffonts "$pdf" | awk 'NR > 2 { print $1 }' | sed 's/^[A-Z]*+//' |
            sort -u | grep -v '^$' || true)
    strangers=$(echo "$faces" | grep -vxE 'XCharter-(Roman|Bold|Italic|BoldItalic)|DejaVuSansMono(-Bold)?' || true)
    if [ -n "$strangers" ]; then
      echo "build-docs: $label is set in fonts WE DID NOT SHIP:" >&2
      echo "$strangers" | sed 's/^/              /' >&2
      echo "            That means some character is not in XCharter or DejaVu Sans Mono, so the" >&2
      echo "            browser quietly borrowed a face from THIS machine -- which another machine" >&2
      echo "            will not have. Find the character (the usual suspect is a symbol or arrow" >&2
      echo "            in bold or italic) and either write it differently or extend the fallback" >&2
      echo "            chain in docs/print.css. Do not ignore this: it renders differently in CI." >&2
      exit 1
    fi
  else
    # Not fatal locally -- but CI installs poppler-utils precisely so this always runs
    # somewhere. See .github/workflows/docs.yml.
    echo "build-docs: (no pdffonts -- skipping the font check; CI runs it)" >&2
  fi
}

# ---------------------------------------------------------------------------
# TURN A PANDOC HTML PAGE INTO A PAGINATED PDF.
#
# This is the step the browser cannot do alone. It splices Paged.js into the page (embedded, so
# the intermediate HTML stays self-contained, exactly like the fonts), then hands it to
# chrome-print.py, which waits for Paged.js to finish before printing. The Paged.js completion
# hook is wired here: window.PagedConfig.after stamps the page count into document.title, and
# chrome-print.py watches for it.
#
# Then it CHECKS THE RESULT. If Paged.js had silently not run, the browser would have printed
# the un-paginated document -- no footer page numbers, an un-numbered contents -- and it would
# look plausible. So we read the finished PDF back and fail the build if the running page
# number is not there, in the same spirit as the font check.
# ---------------------------------------------------------------------------
paginate() {  # paginate <html> <pdf> <label> [cover]
  html=$1; pdf=$2; label=$3
  # The cover flag tags the TITLE BLOCK, not the body. It must be a class on the header element
  # itself: Paged.js relocates the body into its own page containers before it assigns named
  # pages, so a `body.something header` selector no longer matches the header when `page: cover`
  # is read -- and the cover would keep the running page number. A class ON the header has no
  # body ancestor to lose. (Verified: body-scoped selector -> cover numbered; header class -> clean.)
  addcover=""
  [ "${4:-}" = cover ] && addcover="var h=document.getElementById('title-block-header'); if(h){h.classList.add('cover');}"

  # The injected tail: the completion hook and the cover flag FIRST (they must be in place
  # before the polyfill initializes), then the polyfill itself inline.
  {
    printf '<script>window.PagedConfig={after:function(f){document.title="PAGES_"+f.total;}};%s</script>\n' "$addcover"
    printf '<script>\n'
    cat "$pagedjs"
    printf '\n</script>\n'
  } > "$work/inject.html"

  # Splice it in right before the closing </body>. pandoc emits that tag alone on its own line;
  # any </body> in prose is escaped to &lt;/body&gt;, so an exact-line match hits only the real one.
  awk -v injf="$work/inject.html" '
    $0=="</body>" { while((getline l < injf) > 0) print l }
    { print }
  ' "$html" > "$html.paged" && mv "$html.paged" "$html"

  python3 "$root/tools/chrome-print.py" "$chrome" "$html" "$pdf" || {
    echo "build-docs: $label -- the browser/Paged.js print failed (see above)." >&2; exit 1; }

  [ -s "$pdf" ] || { echo "build-docs: $label came out empty." >&2; exit 1; }

  # Proof that Paged.js actually ran: its @bottom-center margin box prints the page number as a
  # bare line of its own. The browser's native print ignores @bottom-center, so no such line
  # means the pagination silently did not happen.
  if have pdftotext; then
    if ! pdftotext "$pdf" - 2>/dev/null | grep -qE '^[[:space:]]*[0-9]+[[:space:]]*$'; then
      echo "build-docs: $label has NO running page numbers -- Paged.js did not paginate it." >&2
      echo "            The browser printed the un-paginated document. Check docs/pagedjs/ and" >&2
      echo "            tools/chrome-print.py; do not ship this." >&2
      exit 1
    fi
  fi
}

# ---------------------------------------------------------------------------
# One document.
# ---------------------------------------------------------------------------
build() {  # build <docdir> <output-name> <title> [notoc]
  dir=$1; name=$2; title=$3
  src=$root/docs/$dir

  [ -f "$src/ORDER" ] || { echo "build-docs: $dir/ORDER is missing -- it declares the chapters, in order." >&2; exit 1; }

  # THE ORDER IS DECLARED ONCE. Chapter files are not numbered (renaming eleven files to
  # insert one is how cross-links rot), so ORDER is the only place that knows the sequence.
  chapters=""
  while read -r f; do
    case "$f" in ''|'#'*) continue ;; esac
    [ -f "$src/$f" ] || { echo "build-docs: $dir/ORDER names '$f', which does not exist." >&2; exit 1; }
    expand "$src/$f" "$work/$(echo "$f" | tr '/' '_')"
    chapters="$chapters $work/$(echo "$f" | tr '/' '_')"
  done < "$src/ORDER"

  # An unexpanded token is a broken instruction. Refuse to ship one.
  if grep -l '{{[A-Z_]*}}' $chapters 2>/dev/null | head -1 | grep -q .; then
    echo "build-docs: $dir has UNEXPANDED TOKENS -- every one of these is a path a reader" >&2
    echo "            would be told to type, and it is not a path:" >&2
    grep -Hn '{{[A-Z_]*}}' $chapters >&2
    echo "            Add it to docs/package.map, or stop inventing paths in prose." >&2
    exit 1
  fi

  stamp="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo '?')"
  date="$(date -u '+%Y-%m-%d')"

  # CHAPTERS ONLY -- --toc-depth=1. At depth 2 the contents listed all 146 subsection
  # headings as well as the 21 chapters and ran to five pages: 109pp -> 105pp when it went.
  # A contents page you have to page THROUGH is not a contents page. The reader opens it to
  # learn the SHAPE of the document, and an index of every `## Getting back out -- ^E` in it
  # tells them the shape of a chapter they have not read yet.
  #
  # Know what this costs, because it is not nothing: the print path emits no bookmark tree
  # (verified -- zero /Outlines in the PDF, before and after, with Paged.js as with plain
  # --print-to-pdf), so the contents page is the ONLY navigation this document has -- which is
  # exactly why its page numbers earn their keep. Cutting it to depth 1 means a subsection is reached by
  # its chapter, or by the reader's own text search. That is the right trade for a manual whose
  # chapters are short and whose reference lives at the back -- but if a chapter ever grows big
  # enough to need finding-by-subsection, the answer is to SPLIT IT, not to reopen this.
  #
  # The changelog opts out of the contents page entirely (build ... notoc): it is read
  # newest-first, top to bottom, so a list of the release headings is just the first pages
  # over again. Every other document keeps the chapter-level contents described above.
  toc_args="--toc --toc-depth=1"
  [ "${4:-}" = notoc ] && toc_args=""

  # shellcheck disable=SC2086
  pandoc $chapters \
    --standalone --embed-resources \
    $toc_args \
    --from=gfm --to=html5 \
    --metadata title="$title" \
    --css "$root/docs/print.css" \
    --metadata subtitle="$date · $stamp" \
    -o "$work/$name.html"

  # A missing or unreadable font needs no check of ours: because --css is an absolute path,
  # pandoc resolves the url()s in it to absolute paths too and HARD-ERRORS on one it cannot
  # read ("withBinaryFile: does not exist"), which set -e turns into a failed build. Both
  # cases verified. Do not add a grep for un-inlined url("fonts/...") here -- one was
  # written, and it could not be made to fire under any input. (Pandoc does have a
  # warn-and-continue path for unfetchable resources, but only for a RELATIVE --css, which
  # this script never passes. If that ever changes, the font check after the PDF is built
  # catches it anyway, on any machine that does not have XCharter installed -- which
  # includes the CI runner that owns these files.)
  # PRINT INTO THE TEMP DIRECTORY, NOT OVER THE SHIPPED FILE. The checks below can reject
  # this PDF, and a rejected PDF must not be left lying in docs/ where the next person --
  # or CI, which commits what it finds there -- picks it up as the real one. It only lands
  # once it has passed. (This is not hypothetical: the font check first fired on a build
  # that had already overwritten docs/altairsim-manual.pdf with the bad copy.)
  paginate "$work/$name.html" "$work/$name.pdf" "$name.pdf" cover

  check_fonts "$work/$name.pdf" "$name.pdf"

  # It passed. NOW it is the document.
  mv "$work/$name.pdf" "$out/$name.pdf"
  echo "build-docs: $out/$name.pdf"
}

# ---------------------------------------------------------------------------
# One example README, rendered beside its source.
#
# The per-directory READMEs under examples/ are notes a reader opens in a file manager -- and a
# file manager has no Markdown viewer, so each one gets a PDF sibling built the SAME way as the
# manual (same faces, same paginator, same font check), so it looks like the rest of the docs
# and travels in the package. Unlike the manual these are single files: no ORDER, no {{TOKENS}}
# (verified -- the READMEs have neither), no images, and NO --toc (a 40-line note does not get
# a contents page). The output lands beside the source at examples/<dir>/README.pdf, not in
# $out; .gitignore allowlists examples/**/README.pdf and CI (docs.yml) commits them.
# ---------------------------------------------------------------------------
build_readme() {  # build_readme <src.md relative to root>
  rel=$1
  src=$root/$rel
  dst=$root/${rel%.md}.pdf
  [ -f "$src" ] || { echo "build-docs: $rel does not exist." >&2; exit 1; }

  # The H1 IS the title. Promote it out of the body so it renders once -- as the title block,
  # the way the manual's title does -- instead of a second time as a heading underneath it.
  # awk, not `sed '0,/re/'`: that address is a GNU extension BSD/macOS sed silently ignores,
  # which left the heading in the body (and printed it twice). This drops the FIRST H1 only.
  title="altairsim — $(sed -n 's/^# *//p' "$src" | head -1)"
  awk 'dropped || !/^# /{print; next} {dropped=1}' "$src" > "$work/readme.md"

  stamp="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo '?')"
  date="$(date -u '+%Y-%m-%d')"

  pandoc "$work/readme.md" \
    --standalone --embed-resources \
    --from=gfm --to=html5 \
    --metadata title="$title" \
    --css "$root/docs/print.css" \
    --metadata subtitle="$date · $stamp" \
    -o "$work/readme.html"

  paginate "$work/readme.html" "$work/readme.pdf" "$rel -> pdf"

  check_fonts "$work/readme.pdf" "$rel -> pdf"

  mv "$work/readme.pdf" "$dst"
  echo "build-docs: $dst"
}

# ---------------------------------------------------------------------------
# One loose single-file document, rendered into $out.
#
# Like build_readme (single file, no ORDER, no {{TOKENS}}, no --toc, same faces and font
# check) but with an EXPLICIT title and an $out destination rather than a sibling of the
# source. The cheatsheet needs this: its source is docs/manual/ref/cheatsheet.md, a GENERATED
# file that docs-reference.cmake byte-diffs against the binary -- so its PDF may NOT land in
# ref/ beside it, the way a README's does. It ships from $out (docs/), tracked and rebuilt by
# docs.yml exactly like the manual and the changelog.
# ---------------------------------------------------------------------------
build_single() {  # build_single <src.md relative to root> <output-name> <title>
  rel=$1; name=$2; title=$3
  src=$root/$rel
  [ -f "$src" ] || { echo "build-docs: $rel does not exist." >&2; exit 1; }

  # Drop the FIRST H1 (see build_readme) -- it becomes the title block, not a body heading.
  # The cheatsheet's leading HTML comment is not a `# ` line, so it survives; the first real
  # heading (`# Quick reference`) is the one promoted out.
  awk 'dropped || !/^# /{print; next} {dropped=1}' "$src" > "$work/$name.md"

  stamp="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo '?')"
  date="$(date -u '+%Y-%m-%d')"

  pandoc "$work/$name.md" \
    --standalone --embed-resources \
    --from=gfm --to=html5 \
    --metadata title="$title" \
    --css "$root/docs/print.css" \
    --metadata subtitle="$date · $stamp" \
    -o "$work/$name.html"

  paginate "$work/$name.html" "$work/$name.pdf" "$name.pdf"

  check_fonts "$work/$name.pdf" "$name.pdf"

  mv "$work/$name.pdf" "$out/$name.pdf"
  echo "build-docs: $out/$name.pdf"
}

mkdir -p "$out"
build manual    altairsim-manual    "altairsim — User Manual"
build changelog altairsim-changelog "altairsim — Changelog" notoc
build devguide  altairsim-devguide  "altairsim — Developer Guide"

# The quick reference, rendered for a human to read. The Markdown still ships too (it is the
# AI's plain-text crib); this is the same content a file manager can open.
build_single docs/manual/ref/cheatsheet.md altairsim-cheatsheet "altairsim — Quick Reference"

# The examples' READMEs, each to a sibling PDF. The index first, then one per directory.
# Glob against $root, not the caller's cwd -- this script may be run from anywhere.
for abs in "$root"/examples/README.md "$root"/examples/*/README.md; do
  [ -f "$abs" ] || continue
  build_readme "${abs#"$root"/}"
done
