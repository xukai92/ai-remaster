#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace remaster {

enum class PixelFormat { RGBA8, BGRA8, RGB8, Unknown };

struct ImageView {
  const uint8_t *data = nullptr;
  int width = 0;
  int height = 0;
  int strideBytes = 0;
  PixelFormat format = PixelFormat::Unknown;
};

struct OwnedImage {
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  int strideBytes = 0;
  PixelFormat format = PixelFormat::Unknown;

  ImageView View() const { return {pixels.data(), width, height, strideBytes, format}; }
};

// Rejects unsupported formats, malformed pitch, and allocations above maxBytes.
// Copies only visible pixels; the result is tightly packed.
bool CopyImage(ImageView source, size_t maxBytes, OwnedImage *output);

} // namespace remaster
