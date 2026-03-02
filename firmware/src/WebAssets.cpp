#include "WebAssets.h"

#include <string.h>

#if __has_include("web_assets.generated.h")
#include "web_assets.generated.h"
#endif

namespace {
#if !__has_include("web_assets.generated.h")
const uint8_t kFallbackIndexHtml[] PROGMEM =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' "
    "content='width=device-width,initial-scale=1'><title>Anemometer</title></head><body>"
    "<h1>Anemometer firmware running</h1><p>Run `./scripts/build_web_assets.sh` to embed "
    "the React app.</p></body></html>";

const EmbeddedAsset kFallbackAssets[] = {
    {"/index.html", "text/html; charset=utf-8", kFallbackIndexHtml,
     sizeof(kFallbackIndexHtml) - 1, false},
};
#endif
}  // namespace

const EmbeddedAsset* getEmbeddedAssets(size_t& count) {
#if __has_include("web_assets.generated.h")
  count = kEmbeddedAssetCount;
  return kEmbeddedAssets;
#else
  count = sizeof(kFallbackAssets) / sizeof(kFallbackAssets[0]);
  return kFallbackAssets;
#endif
}

const EmbeddedAsset* findEmbeddedAsset(const String& path) {
  size_t count = 0;
  const EmbeddedAsset* assets = getEmbeddedAssets(count);

  for (size_t i = 0; i < count; i++) {
    if (path.equals(assets[i].path)) {
      return &assets[i];
    }
  }

  return nullptr;
}
