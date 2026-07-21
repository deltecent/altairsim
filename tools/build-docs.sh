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
# One document.
# ---------------------------------------------------------------------------
build() {  # build <docdir> <output-name> <title>
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
  # Know what this costs, because it is not nothing: --print-to-pdf emits no bookmark tree
  # (verified -- zero /Outlines in the PDF, before and after), so the contents page is the
  # ONLY navigation this document has. Cutting it to depth 1 means a subsection is reached by
  # its chapter, or by the reader's own text search. That is the right trade for a manual whose
  # chapters are short and whose reference lives at the back -- but if a chapter ever grows big
  # enough to need finding-by-subsection, the answer is to SPLIT IT, not to reopen this.
  #
  # shellcheck disable=SC2086
  pandoc $chapters \
    --standalone --embed-resources \
    --toc --toc-depth=1 \
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
  "$chrome" --headless --disable-gpu --no-pdf-header-footer \
            --print-to-pdf="$work/$name.pdf" "$work/$name.html" 2>/dev/null

  [ -s "$work/$name.pdf" ] || { echo "build-docs: $name.pdf came out empty." >&2; exit 1; }

  # NOW CHECK WHAT ACTUALLY CAME OUT. Embedding the fonts (above) only guarantees they were
  # OFFERED to the browser; it says nothing about a character neither of them has. Chrome
  # answers that silently, by going shopping on the local machine -- and the very first
  # build with embedded fonts did exactly that, pulling one arrow out of macOS's Lucida
  # Grande because XCharter-Bold has no U+2192. One glyph, no warning, unshippable font,
  # and a different result on a machine without it.
  #
  # So: every face in the finished PDF must be one we shipped. This is the check that
  # catches the NEXT character somebody types that our fonts do not have.
  if have pdffonts; then
    faces=$(pdffonts "$work/$name.pdf" | awk 'NR > 2 { print $1 }' | sed 's/^[A-Z]*+//' |
            sort -u | grep -v '^$' || true)
    strangers=$(echo "$faces" | grep -vxE 'XCharter-(Roman|Bold|Italic|BoldItalic)|DejaVuSansMono(-Bold)?' || true)
    if [ -n "$strangers" ]; then
      echo "build-docs: $name.pdf is set in fonts WE DID NOT SHIP:" >&2
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

  # It passed. NOW it is the document.
  mv "$work/$name.pdf" "$out/$name.pdf"
  echo "build-docs: $out/$name.pdf"
}

mkdir -p "$out"
build manual    altairsim-manual    "altairsim — User Manual"
build changelog altairsim-changelog "altairsim — Changelog"
build devguide  altairsim-devguide  "altairsim — Developer Guide"
