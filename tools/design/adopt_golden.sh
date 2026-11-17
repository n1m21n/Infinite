#!/usr/bin/env bash
# Adopt a UI golden from a green CI run as the committed baseline for that platform.
#
#   tools/design/adopt_golden.sh <build-run-id> <windows|linux>
#
# Downloads the ui-golden-<platform> artifact (rendered by the build.yml job on a hosted runner), copies
# gallery-light.png and gallery-dark.png into tests/ui-golden/<platform>/, and runs the same compare the
# release uses. Nothing is committed: review the PNGs, then commit them yourself. See docs/ci/HOSTED_TESTS.md.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  sed -n '2,9p' "$0" >&2
  exit 2
fi
RUN="$1"
PLAT="$2"
case "$PLAT" in
  windows|linux) ;;
  *) echo "platform must be windows or linux (macos goldens come from the owner's Mac)" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

gh run download "$RUN" -n "ui-golden-$PLAT" -D "$TMP/cand"
for f in gallery-light.png gallery-dark.png; do
  if [ ! -s "$TMP/cand/$f" ]; then
    echo "the artifact has no $f: the golden step did not render on this run (see its log)" >&2
    exit 1
  fi
done

mkdir -p "tests/ui-golden/$PLAT"
cp "$TMP/cand/gallery-light.png" "$TMP/cand/gallery-dark.png" "tests/ui-golden/$PLAT/"
echo "copied into tests/ui-golden/$PLAT/ (not committed): review the PNGs, then commit them"
echo "from now on attach-release-assets.yml compares this platform's render against them"
