#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/build/tests"
mkdir -p "$OUT_DIR"

c++ -std=c++17 -Wall -Wextra -Werror \
  "$ROOT_DIR/tests/test_wind_history.cpp" \
  "$ROOT_DIR/firmware/src/WindHistory.cpp" \
  -o "$OUT_DIR/test_wind_history"

"$OUT_DIR/test_wind_history"
