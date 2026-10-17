#!/usr/bin/env bash
# Regenerate the small references used by the statistical regression test
# (tests/test_eval.cpp). Do this only when the expected image changes on
# purpose, e.g. after a deliberate material-model change, and say so in the
# commit message.
#
#   tests/data/make_regression_refs.sh [path/to/Raytracer]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${1:-$ROOT/build/Raytracer}"
SPP=65536
SEED=1000003   # never used by tests (they use small seeds)
for scene in cornell_box veach_mis; do
  "$BIN" --scene "$ROOT/benchmarks/scenes/$scene.xml" --width 48 --progressive \
    --samples "$SPP" --seed "$SEED" --no-denoise --quiet \
    --out "$ROOT/tests/data/regression_$scene.png" \
    --pfm "$ROOT/tests/data/regression_$scene.pfm" \
    --variance-out "$ROOT/tests/data/regression_${scene}_var.pfm"
  rm -f "$ROOT/tests/data/regression_$scene.png"
  printf 'spp %s\nseed %s\n' "$SPP" "$SEED" > "$ROOT/tests/data/regression_$scene.txt"
  echo "wrote tests/data/regression_$scene.pfm ($SPP spp)"
done
