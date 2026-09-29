#pragma once

#include "remaster/Image.h"
#include "remaster/TextureIdentity.h"

#include <filesystem>
#include <string_view>

namespace remaster {

// Persistent implementation belongs to Milestone 2.
class ArtifactCache {
public:
  virtual ~ArtifactCache() = default;
  virtual bool Lookup(const TextureIdentity &id, std::string_view modelId,
                      std::filesystem::path *artifact) = 0;
  virtual bool StoreAtomically(const TextureIdentity &id, std::string_view modelId,
                               const OwnedImage &image,
                               std::filesystem::path *artifact) = 0;
};

} // namespace remaster
