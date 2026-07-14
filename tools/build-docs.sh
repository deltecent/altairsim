#!/bin/sh
#
# Build the two documents.
#
#   docs/manual/    -> altairsim-manual.pdf     SHIPS IN THE PACKAGE. Self-contained.
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

  # shellcheck disable=SC2086
  pandoc $chapters \
    --standalone --embed-resources \
    --toc --toc-depth=2 \
    --from=gfm --to=html5 \
    --metadata title="$title" \
    --css "$root/docs/print.css" \
    --metadata subtitle="$date · $stamp" \
    -o "$work/$name.html"

  "$chrome" --headless --disable-gpu --no-pdf-header-footer \
            --print-to-pdf="$out/$name.pdf" "$work/$name.html" 2>/dev/null

  [ -s "$out/$name.pdf" ] || { echo "build-docs: $name.pdf came out empty." >&2; exit 1; }
  echo "build-docs: $out/$name.pdf"
}

mkdir -p "$out"
build manual   altairsim-manual   "altairsim — User Manual"
build devguide altairsim-devguide "altairsim — Developer Guide"
