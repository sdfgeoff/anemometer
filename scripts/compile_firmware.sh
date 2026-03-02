#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:lolin32-lite}"
SKETCH="${ROOT_DIR}/firmware/firmware.ino"

"$ROOT_DIR/scripts/build_web_assets.sh"

arduino-cli compile \
  --fqbn "$FQBN" \
  "$SKETCH"
