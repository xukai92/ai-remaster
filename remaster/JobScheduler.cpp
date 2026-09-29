#include "remaster/JobScheduler.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace remaster {

JobScheduler::JobScheduler(InferenceBackend &backend, ResultCallback callback,
                           SchedulerLimits limits)
    : backend_(backend), callback_(std::move(callback)), limits_(limits),
      worker_(&JobScheduler::Run, this), workerId_(worker_.get_id()) {}

JobScheduler::~JobScheduler() { Shutdown(); }

SubmitStatus JobScheduler::Submit(const EnhancementRequest &request) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_) return SubmitStatus::Stopped;
  if (inFlight_.find(request.id) != inFlight_.end()) {
    ++counters_.coalesced;
    return SubmitStatus::Coalesced;
  }
  const size_t available = std::min(limits_.maxJobCopyBytes,
      limits_.maxSourceBytes -
      (counters_.sourceBytes <= limits_.maxSourceBytes ? counters_.sourceBytes : limits_.maxSourceBytes));
  if (pending_.size() >= limits_.maxPendingJobs) {
    ++counters_.dropped;
    return SubmitStatus::Dropped;
  }
  try {
    OwnedImage copied;
    if (!CopyImage(request.image, available, &copied)) {
      ++counters_.dropped;
      return SubmitStatus::Dropped;
    }
    const size_t bytes = copied.pixels.size();
    pending_.push_back({request.id, std::move(copied), request.metadata});
    try {
      inFlight_.insert(request.id);
    } catch (...) {
      pending_.pop_back();
      throw;
    }
    counters_.sourceBytes += bytes;
    counters_.pendingJobs = pending_.size();
    ++counters_.accepted;
    ready_.notify_one();
    return SubmitStatus::Accepted;
  } catch (...) {
    ++counters_.dropped;
    return SubmitStatus::Dropped;
  }
}

SchedulerCounters JobScheduler::Counters() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return counters_;
}

void JobScheduler::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stopping_) {
      stopping_ = true;
      for (const Job &job : pending_) {
        counters_.sourceBytes -= job.image.pixels.size();
        inFlight_.erase(job.id);
        ++counters_.cancelled;
      }
      pending_.clear();
      counters_.pendingJobs = 0;
    }
  }
  ready_.notify_one();
  if (std::this_thread::get_id() == workerId_) return;
  std::lock_guard<std::mutex> joinLock(joinMutex_);
  if (worker_.joinable()) worker_.join();
}

void JobScheduler::Run() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (stopping_) return;
      job = std::move(pending_.front());
      pending_.pop_front();
      counters_.pendingJobs = pending_.size();
    }
    EnhancementResult result;
    try {
      EnhancementRequest request{job.id, job.image.View(), job.metadata};
      result = backend_.Enhance(request);
    } catch (const std::exception &error) {
      result.status = EnhancementResult::Status::Failed;
      // Even recording an error message can allocate and fail under OOM.
      try { result.reason = error.what(); } catch (...) {}
    } catch (...) {
      result.status = EnhancementResult::Status::Failed;
      try { result.reason = "unknown backend exception"; } catch (...) {}
    }
    const auto status = result.status;
    bool callbackFailed = false;
    try {
      if (callback_) callback_(job.id, std::move(result));
    } catch (...) {
      callbackFailed = true;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (status == EnhancementResult::Status::Failed || callbackFailed) ++counters_.failed;
      else if (status == EnhancementResult::Status::Skipped) ++counters_.skipped;
      else ++counters_.completed;
      counters_.sourceBytes -= job.image.pixels.size();
      inFlight_.erase(job.id);
    }
  }
}

} // namespace remaster
