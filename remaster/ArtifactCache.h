#pragma once

#include "remaster/Image.h"
#include "remaster/TextureIdentity.h"

#include <filesystem>
#include <cstdint>
#include <string>

namespace remaster {

inline constexpr uint32_t kCacheSchemaVersion = 1;

struct CacheKey {
  uint32_t schemaVersion = kCacheSchemaVersion;
  TextureIdentity texture;
  std::string modelId;
  std::string modelVersion;
  std::string configVersion;
};

// Unambiguous, deterministic metadata identity; not a filesystem path.
std::string FormatCacheKey(const CacheKey &key);

class ArtifactCache {
public:
  virtual ~ArtifactCache() = default;
  // Lookup validates metadata, size, checksum, and PNG decode before returning.
  virtual bool Lookup(const CacheKey &key, std::filesystem::path *artifact) = 0;
  virtual bool StoreAtomically(const CacheKey &key, const OwnedImage &image,
                               std::filesystem::path *artifact) = 0;
};

} // namespace remaster
