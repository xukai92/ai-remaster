#include "remaster/FakeBackend.h"

#include <cstring>
#include <limits>

namespace remaster {

EnhancementResult FakeBackend::Enhance(const EnhancementRequest &request) {
  EnhancementResult result;
  result.modelId = Id();
  const ImageView source = request.image;
  // A small explicit output cap keeps the test backend safe for arbitrary input.
  constexpr size_t kMaxOutputBytes = 256u * 1024u * 1024u;
  if (source.format != PixelFormat::RGBA8 || source.width <= 0 || source.height <= 0 ||
      source.width > std::numeric_limits<int>::max() / 8 ||
      source.height > std::numeric_limits<int>::max() / 2 ||
      static_cast<size_t>(source.width) * static_cast<size_t>(source.height) >
          kMaxOutputBytes / 16) {
    result.status = EnhancementResult::Status::Skipped;
    result.reason = "unsupported format or dimensions";
    return result;
  }
  OwnedImage input;
  if (!CopyImage(source, kMaxOutputBytes / 4, &input)) {
    result.reason = "invalid source image";
    return result;
  }
  result.image.width = source.width * 2;
  result.image.height = source.height * 2;
  result.image.strideBytes = result.image.width * 4;
  result.image.format = PixelFormat::RGBA8;
  result.image.pixels.resize(static_cast<size_t>(result.image.strideBytes) * result.image.height);
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      const uint8_t *pixel = input.pixels.data() +
          static_cast<size_t>(y) * input.strideBytes + static_cast<size_t>(x) * 4;
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
          std::memcpy(result.image.pixels.data() +
                          static_cast<size_t>(y * 2 + dy) * result.image.strideBytes +
                          static_cast<size_t>(x * 2 + dx) * 4, pixel, 4);
    }
  }
  result.status = EnhancementResult::Status::Enhanced;
  return result;
}

} // namespace remaster
