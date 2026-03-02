#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:lolin32-lite}"
PORT="${PORT:-/dev/ttyUSB0}"
SKETCH="${ROOT_DIR}/firmware/firmware.ino"

"$ROOT_DIR/scripts/compile_firmware.sh"

arduino-cli upload \
  -p "$PORT" \
  --fqbn "$FQBN" \
  "$SKETCH"
