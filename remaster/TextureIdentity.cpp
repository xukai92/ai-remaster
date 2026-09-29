#include "remaster/TextureIdentity.h"

#include <cstdint>

namespace remaster {

bool TextureIdentity::operator==(const TextureIdentity &other) const {
  return emulator == other.emulator && gameId == other.gameId &&
         contentKey == other.contentKey && mipLevel == other.mipLevel;
}

std::string FormatIdentity(const TextureIdentity &id) {
  auto field = [](const std::string &s) { return std::to_string(s.size()) + ":" + s; };
  return field(id.emulator) + field(id.gameId) + field(id.contentKey) +
         std::to_string(id.mipLevel) + ";";
}

size_t TextureIdentityHash::operator()(const TextureIdentity &id) const noexcept {
  // Hash field lengths and bytes directly so lookups and erases never allocate.
  uint64_t hash = 14695981039346656037ull;
  const auto addByte = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ull;
  };
  const auto addNumber = [&addByte](uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      addByte(static_cast<uint8_t>(value));
      value >>= 8;
    }
  };
  const auto addString = [&addByte, &addNumber](const std::string &value) {
    addNumber(value.size());
    for (unsigned char byte : value) addByte(byte);
  };
  addString(id.emulator);
  addString(id.gameId);
  addString(id.contentKey);
  addNumber(static_cast<uint32_t>(id.mipLevel));
  return static_cast<size_t>(hash);
}

} // namespace remaster
