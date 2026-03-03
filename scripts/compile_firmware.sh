#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:lolin32-lite}"
SKETCH="${ROOT_DIR}/firmware/firmware.ino"
BUILD_PATH="${BUILD_PATH:-$ROOT_DIR/firmware/build}"

"$ROOT_DIR/scripts/build_web_assets.sh"
mkdir -p "$BUILD_PATH"

arduino-cli compile \
  --build-path "$BUILD_PATH" \
  --fqbn "$FQBN" \
  "$SKETCH"
