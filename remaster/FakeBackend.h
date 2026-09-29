#pragma once

#include "remaster/InferenceBackend.h"

namespace remaster {

class FakeBackend final : public InferenceBackend {
public:
  std::string Id() const override { return "nearest-2x-v1"; }
  EnhancementResult Enhance(const EnhancementRequest &request) override;
};

} // namespace remaster
