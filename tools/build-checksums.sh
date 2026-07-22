#!/bin/sh
#
# SHA256SUMS for a release -- the one file that lets altairsim.com and the GitHub
# Release be checked against each other, and lets anyone who downloads an archive
# check what they got. See DISTRIBUTION.md 6 and 8.
#
# COORDINATOR ONLY, and AFTER all four archives have collected in dist/. A checksum
# file is worthless until every file it vouches for is present -- three of four is
# WORSE than none, because someone runs `-c`, sees three OK lines, and ships a release
# whose fourth archive nobody checked. So this REFUSES unless all four packages of one
# version are here (DISTRIBUTION.md 1):
#
#     altairsim-X.Y.Z-macos-arm64.tar.gz
#     altairsim-X.Y.Z-macos-x86_64.tar.gz
#     altairsim-X.Y.Z-linux-x86_64.tar.gz
#     altairsim-X.Y.Z-windows-x86_64.zip
#
# It writes dist/SHA256SUMS in the standard `shasum -a 256` / `sha256sum` format --
# "<hash>  <name>", two spaces, BARE names -- so `shasum -a 256 -c SHA256SUMS`
# verifies it from inside dist/ and from inside an unpacked download alike. Then it
# verifies its OWN output before claiming success, because a checksum file nobody has
# checked is a draft, same as everything else in this pipeline.
#
# It does NOT upload. Like build-package.sh, it produces an artifact and stops; the
# coordinator uploads SHA256SUMS beside the four archives (DISTRIBUTION.md 5 step 6).
#
#   usage: tools/build-checksums.sh [--version X.Y.Z] [outdir]
#
#     --version  The release to checksum. Default: auto-detected from the archives in
#                outdir, which must all be ONE version or you are asked to name it.
#     outdir     Where the archives are and where SHA256SUMS is written. Default: dist/

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)

ver=""
out=""

usage() {
  cat <<'USAGE'
usage: tools/build-checksums.sh [--version X.Y.Z] [outdir]

  --version  The release to checksum. Default: auto-detected from the archives
             in outdir, which must all be one version or you are asked to name it.
  outdir     Where the archives are and where SHA256SUMS is written. Default: dist/
USAGE
  exit "${1:-0}"
}

while [ $# -gt 0 ]; do
  case $1 in
    --version) [ $# -ge 2 ] || { echo "build-checksums: --version needs a value" >&2; exit 1; }
               ver=$2; shift 2 ;;
    -h|--help) usage 0 ;;
    -*)        echo "build-checksums: unknown option $1" >&2; usage 1 ;;
    *)         [ -z "$out" ] || { echo "build-checksums: only one outdir, got '$out' and '$1'" >&2; exit 1; }
               out=$1; shift ;;
  esac
done

out=${out:-$root/dist}
[ -d "$out" ] || { echo "build-checksums: $out does not exist" >&2; exit 1; }

# The four packages, in the canonical target order (DISTRIBUTION.md 1). The order is
# fixed so a re-run produces a byte-identical SHA256SUMS -- a file that can be diffed.
# No target name is a suffix of another, so the globs below cannot cross platforms
# (the same property build-package.sh's scoped cleanup rests on).
targets="macos-arm64:tar.gz macos-x86_64:tar.gz linux-x86_64:tar.gz windows-x86_64:zip"

# WHICH VERSION. If not named, read it off the archives -- but they must agree. Two
# versions' archives sitting in one dist/ is ambiguous (which release is this?), so
# stop and ask rather than guess. Auto-detect keeps the release's common case a
# zero-argument command; --version is the escape hatch for a cluttered dist/.
if [ -z "$ver" ]; then
  found=""
  for te in $targets; do
    t=${te%%:*}; e=${te##*:}
    for f in "$out"/altairsim-*-"$t"."$e"; do
      [ -f "$f" ] || continue
      b=$(basename "$f"); b=${b#altairsim-}; b=${b%-"$t"."$e"}
      found="$found $b"
    done
  done
  detected=$(printf '%s\n' $found | sort -u | sed '/^$/d')
  if [ -z "$detected" ]; then
    echo "build-checksums: no altairsim-*-<target>.<ext> archives in $out." >&2
    echo "  Nothing to checksum -- build and collect the four packages first" >&2
    echo "  (DISTRIBUTION.md 4-5)." >&2
    exit 1
  fi
  if [ "$(printf '%s\n' "$detected" | wc -l | tr -d ' ')" -gt 1 ]; then
    echo "build-checksums: archives for more than one version are in $out:" >&2
    printf '%s\n' "$detected" | sed 's/^/    /' >&2
    echo "  Name the one to checksum: tools/build-checksums.sh --version X.Y.Z" >&2
    exit 1
  fi
  ver=$detected
fi

echo "build-checksums: version $ver"

# The four filenames this release must have, in order.
files=""
for te in $targets; do
  t=${te%%:*}; e=${te##*:}
  files="$files altairsim-$ver-$t.$e"
done

# ALL FOUR OR NONE. A partial SHA256SUMS is the trap this file exists to avoid.
missing=""
for f in $files; do
  [ -f "$out/$f" ] || missing="$missing $f"
done
if [ -n "$missing" ]; then
  echo "build-checksums: $out has no archive for these -- REFUSING a partial SHA256SUMS:" >&2
  for f in $missing; do echo "    $f" >&2; done
  echo >&2
  echo "A checksum file that vouches for three of four is worse than none: the three verify" >&2
  echo "OK and the fourth ships unchecked. Collect all four first (DISTRIBUTION.md 5-6)." >&2
  exit 1
fi

# The hasher. The coordinator is macOS, which has `shasum` but not `sha256sum`; Linux
# has both. Their output is byte-identical -- "<hash>  <name>", two spaces -- and each
# reads the other's file, so which one wrote SHA256SUMS never matters to who verifies
# it (a downloader on any platform, altairsim.com, the GitHub Release).
if command -v shasum > /dev/null 2>&1; then
  sha()  { shasum -a 256 "$@"; }
  shac() { shasum -a 256 -c "$@"; }
elif command -v sha256sum > /dev/null 2>&1; then
  sha()  { sha256sum "$@"; }
  shac() { sha256sum -c "$@"; }
else
  echo "build-checksums: no shasum and no sha256sum on this host -- cannot hash." >&2
  exit 1
fi

# Hash BARE NAMES from inside out/, so SHA256SUMS carries "altairsim-...tar.gz" and not
# "dist/altairsim-...": it is published beside the archives and verified from whatever
# directory holds them -- dist/ here, an unpacked download there. Write to a temp and
# move, so a failure never leaves a half-written SHA256SUMS that reads as complete.
tmp=$out/SHA256SUMS.tmp
trap 'rm -f "$tmp"' EXIT
( cd "$out" && sha $files ) > "$tmp"
mv "$tmp" "$out/SHA256SUMS"

echo "build-checksums: wrote $out/SHA256SUMS"

# PROVE IT against the very archives it names, before saying it is done.
( cd "$out" && shac SHA256SUMS ) || {
  echo "build-checksums: SHA256SUMS failed to verify against its own archives -- STOP." >&2
  exit 1
}

echo
echo "build-checksums: OK -- all four archives verify. Upload it beside them:"
echo "    gh release upload v$ver $out/SHA256SUMS"
