#pragma once

#include <cstddef>
#include <string>

namespace remaster {

struct TextureIdentity {
  std::string emulator;
  std::string gameId;
  std::string contentKey;
  int mipLevel = 0;

  bool operator==(const TextureIdentity &other) const;
};

// Length-prefixed fields make the serialization unambiguous. It is an in-memory
// identity encoding, not a filesystem path or a cache artifact key.
std::string FormatIdentity(const TextureIdentity &id);

struct TextureIdentityHash {
  size_t operator()(const TextureIdentity &id) const noexcept;
};

} // namespace remaster
