#pragma once

#include "remaster/InferenceBackend.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace remaster {

enum class SubmitStatus { Accepted, Coalesced, Dropped, Stopped };

struct SchedulerLimits {
  size_t maxPendingJobs = 32;
  // Includes the active job and all pending, copied source images.
  size_t maxSourceBytes = 64u * 1024u * 1024u;
  // Maximum visible source bytes copied by one Submit call, before allocation.
  size_t maxJobCopyBytes = 8u * 1024u * 1024u;
};

struct SchedulerCounters {
  uint64_t accepted = 0;
  uint64_t coalesced = 0;
  uint64_t dropped = 0;
  uint64_t completed = 0;
  uint64_t skipped = 0;
  uint64_t failed = 0;
  uint64_t cancelled = 0;
  size_t pendingJobs = 0;
  size_t sourceBytes = 0;
};

// Callback runs on the worker; it must not touch emulator-owned objects unless
// the adapter explicitly handles cross-thread handoff. It may throw safely.
// Objects captured by the callback must outlive Shutdown's return.
using ResultCallback = std::function<void(const TextureIdentity &, EnhancementResult)>;

class JobScheduler {
public:
  JobScheduler(InferenceBackend &backend, ResultCallback callback,
               SchedulerLimits limits = {});
  ~JobScheduler();
  JobScheduler(const JobScheduler &) = delete;
  JobScheduler &operator=(const JobScheduler &) = delete;

  SubmitStatus Submit(const EnhancementRequest &request);
  SchedulerCounters Counters() const;
  // Concurrent external calls are safe and wait for the worker to finish.
  // A call from the worker callback requests stop but cannot join itself; an
  // external Shutdown or destructor must subsequently join the worker.
  // The backend and callback captures must remain alive until that join ends.
  // Destruction from the worker callback is unsupported.
  void Shutdown();

private:
  struct Job {
    TextureIdentity id;
    OwnedImage image;
    TextureMetadata metadata;
  };
  void Run();

  InferenceBackend &backend_;
  ResultCallback callback_;
  SchedulerLimits limits_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Job> pending_;
  std::unordered_set<TextureIdentity, TextureIdentityHash> inFlight_;
  SchedulerCounters counters_;
  bool stopping_ = false;
  std::thread worker_;
  const std::thread::id workerId_;
  std::mutex joinMutex_;
};

} // namespace remaster
