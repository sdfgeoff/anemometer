#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="$ROOT_DIR/web"
DIST_DIR="$WEB_DIR/dist"
OUT_H="$ROOT_DIR/firmware/src/web_assets.generated.h"
OUT_CPP="$ROOT_DIR/firmware/src/web_assets.generated.cpp"

cd "$WEB_DIR"
npm run build

if [ ! -d "$DIST_DIR" ]; then
  echo "Missing web dist directory: $DIST_DIR" >&2
  exit 1
fi

mime_for() {
  case "$1" in
    *.html) echo "text/html; charset=utf-8" ;;
    *.js) echo "application/javascript" ;;
    *.css) echo "text/css; charset=utf-8" ;;
    *.json) echo "application/json" ;;
    *.svg) echo "image/svg+xml" ;;
    *.png) echo "image/png" ;;
    *.jpg|*.jpeg) echo "image/jpeg" ;;
    *.webp) echo "image/webp" ;;
    *.ico) echo "image/x-icon" ;;
    *) echo "application/octet-stream" ;;
  esac
}

symbolize() {
  echo "$1" | sed -E 's/[^a-zA-Z0-9]/_/g'
}

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mapfile -t files < <(cd "$DIST_DIR" && find . -type f | sort)

cat > "$OUT_H" << 'EOH'
#pragma once

#include "WebAssets.h"

extern const EmbeddedAsset kEmbeddedAssets[];
extern const size_t kEmbeddedAssetCount;
EOH

cat > "$OUT_CPP" << 'EOCPP'
#include "web_assets.generated.h"

#include <pgmspace.h>
EOCPP

manifest_entries=()

for rel in "${files[@]}"; do
  clean_rel="${rel#./}"
  src="$DIST_DIR/$clean_rel"
  gz="$TMP_DIR/$clean_rel.gz"
  mkdir -p "$(dirname "$gz")"
  gzip -9 -n -c "$src" > "$gz"

  symbol="asset_$(symbolize "$clean_rel")_gz"
  xxd -i -n "$symbol" "$gz" >> "$OUT_CPP"

  mime="$(mime_for "$clean_rel")"
  url_path="/$clean_rel"

  manifest_entries+=("{\"$url_path\", \"$mime\", $symbol, ${symbol}_len, true}")
done

{
  echo
  echo "const EmbeddedAsset kEmbeddedAssets[] = {"
  for entry in "${manifest_entries[@]}"; do
    echo "  $entry,"
  done
  echo "};"
  echo
  echo "const size_t kEmbeddedAssetCount = sizeof(kEmbeddedAssets) / sizeof(kEmbeddedAssets[0]);"
} >> "$OUT_CPP"

echo "Generated: $OUT_H"
echo "Generated: $OUT_CPP"
