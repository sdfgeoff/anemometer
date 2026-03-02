#pragma once

#include <Arduino.h>

struct EmbeddedAsset {
  const char* path;
  const char* mimeType;
  const uint8_t* data;
  size_t length;
  bool gzip;
};

const EmbeddedAsset* getEmbeddedAssets(size_t& count);
const EmbeddedAsset* findEmbeddedAsset(const String& path);
