#pragma once

#include "remaster/ArtifactCache.h"

#include <cstddef>

namespace remaster {

struct CacheLimits {
  int maxDimension = 8192;
  size_t maxPixelBytes = 256u * 1024u * 1024u;
  size_t maxPngBytes = 256u * 1024u * 1024u;
};

class PngArtifactCache final : public ArtifactCache {
public:
  explicit PngArtifactCache(std::filesystem::path root, CacheLimits limits = {});
  bool Lookup(const CacheKey &key, std::filesystem::path *artifact) override;
  bool StoreAtomically(const CacheKey &key, const OwnedImage &image,
                       std::filesystem::path *artifact) override;

private:
  bool Paths(const CacheKey &key, std::filesystem::path *png,
             std::filesystem::path *meta) const;
  std::filesystem::path root_;
  CacheLimits limits_;
};

} // namespace remaster
