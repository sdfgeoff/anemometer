#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:lolin32-lite}"
PORT="${PORT:-/dev/ttyUSB0}"
SKETCH="${ROOT_DIR}/firmware/firmware.ino"
BUILD_PATH="${BUILD_PATH:-$ROOT_DIR/firmware/build}"

"$ROOT_DIR/scripts/compile_firmware.sh"

arduino-cli upload \
  --build-path "$BUILD_PATH" \
  --verify \
  -p "$PORT" \
  --fqbn "$FQBN" \
  "$SKETCH"
