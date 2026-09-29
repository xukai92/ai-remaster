#include "remaster/Image.h"

#include <cstring>
#include <limits>

namespace remaster {

bool CopyImage(ImageView source, size_t maxBytes, OwnedImage *output) {
  if (!output || !source.data || source.width <= 0 || source.height <= 0 ||
      source.strideBytes <= 0) return false;
  size_t channels = 0;
  switch (source.format) {
  case PixelFormat::RGBA8:
  case PixelFormat::BGRA8: channels = 4; break;
  case PixelFormat::RGB8: channels = 3; break;
  default: return false;
  }
  const size_t width = static_cast<size_t>(source.width);
  const size_t height = static_cast<size_t>(source.height);
  if (width > static_cast<size_t>(source.strideBytes) / channels ||
      width > static_cast<size_t>(std::numeric_limits<int>::max()) / channels ||
      height > maxBytes / (width * channels)) return false;
  const size_t rowBytes = width * channels;
  const size_t total = height * rowBytes;
  OwnedImage copy;
  copy.width = source.width;
  copy.height = source.height;
  copy.strideBytes = static_cast<int>(rowBytes);
  copy.format = source.format;
  copy.pixels.resize(total);
  for (size_t row = 0; row < height; ++row)
    std::memcpy(copy.pixels.data() + row * rowBytes,
                source.data + row * static_cast<size_t>(source.strideBytes), rowBytes);
  *output = std::move(copy);
  return true;
}

} // namespace remaster
