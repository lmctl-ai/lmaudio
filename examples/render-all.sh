#!/usr/bin/env bash
# Regenerate docs/images/*.{png,gif} and docs/audio/*.wav from examples/*.lms.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
export PATH="$PWD/build:$PATH"  # lmplot/lmdraw: released versions via install= (what autorun uses)
LMSCRIPT="$(scripts/select-compiler.sh)"
mkdir -p build images audio docs/images docs/audio
for src in examples/*.lms; do
  echo "==> $src"
  timeout 300 "$LMSCRIPT" "$src"
done
cp images/*.png images/*.gif docs/images/
cp audio/*.wav docs/audio/
ls -lh docs/images/ docs/audio/
