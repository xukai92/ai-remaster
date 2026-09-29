#pragma once

#include "remaster/Image.h"
#include "remaster/TextureIdentity.h"

#include <string>

namespace remaster {

struct TextureMetadata {
  bool hasAlpha = false;
  bool dynamic = false;
  bool framebufferBacked = false;
  bool paletted = false;
  bool likelyUI = false;
  bool likelyFont = false;
  int sourceWidth = 0;
  int sourceHeight = 0;
};

struct EnhancementRequest {
  TextureIdentity id;
  ImageView image;
  TextureMetadata metadata;
};

struct EnhancementResult {
  enum class Status { Enhanced, Skipped, Failed };
  Status status = Status::Failed;
  OwnedImage image;
  std::string modelId;
  std::string reason;
};

class InferenceBackend {
public:
  virtual ~InferenceBackend() = default;
  virtual std::string Id() const = 0;
  virtual EnhancementResult Enhance(const EnhancementRequest &request) = 0;
};

} // namespace remaster
