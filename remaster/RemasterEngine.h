#pragma once

#include "remaster/ArtifactCache.h"
#include "remaster/JobScheduler.h"

namespace remaster {

// Milestone 1 facade. A persistent cache implementation and lookup/publication
// path will be added in Milestone 2. Backend and callback captures must outlive
// Shutdown's return; destroy this engine only from an external thread.
class RemasterEngine {
public:
  RemasterEngine(InferenceBackend &backend, ResultCallback callback,
                 SchedulerLimits limits = {})
      : scheduler_(backend, std::move(callback), limits) {}
  SubmitStatus SubmitTexture(const EnhancementRequest &request) {
    return scheduler_.Submit(request);
  }
  SchedulerCounters Counters() const { return scheduler_.Counters(); }
  void Shutdown() { scheduler_.Shutdown(); }

private:
  JobScheduler scheduler_;
};

} // namespace remaster
