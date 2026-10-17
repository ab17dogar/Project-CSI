#!/usr/bin/env bash
# Verify that this repository's FLIP integration reproduces the reference
# values published by NVIDIA. Fetches the FLIP repository (for its test
# images and its EXR/PNG loaders) at the commit vendored in src/3rdParty/flip.
#
#   tools/validate_flip.sh            # uses a temporary directory
#   FLIP_DIR=/path/to/flip tools/validate_flip.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMIT="$(sed -n 's/^- Commit: //p' "$ROOT/src/3rdParty/flip/README.md")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FLIP_DIR="${FLIP_DIR:-$WORK/flip}"

if [ ! -d "$FLIP_DIR/images" ]; then
  git init -q "$FLIP_DIR"
  git -C "$FLIP_DIR" remote add origin https://github.com/NVlabs/flip.git
  git -C "$FLIP_DIR" fetch -q --depth 1 origin "$COMMIT"
  git -C "$FLIP_DIR" checkout -q FETCH_HEAD
fi

"${CXX:-c++}" -O2 -std=c++17 -I"$ROOT/src" -I"$FLIP_DIR/src/cpp/tool" -I"$FLIP_DIR/src/cpp" \
  "$ROOT/tools/validate_flip.cpp" "$ROOT/src/eval/metrics.cpp" "$ROOT/src/util/image_io.cpp" \
  -o "$WORK/validate_flip" -w
"$WORK/validate_flip" "$FLIP_DIR/images"
