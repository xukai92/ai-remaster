#include "remaster/FakeBackend.h"
#include "remaster/RemasterEngine.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <vector>

using namespace remaster;

static void Check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

static TextureIdentity Id(const char *key) { return {"emu", "game", key, 0}; }

static uint64_t Fnv1a(const std::vector<uint8_t> &bytes) {
  uint64_t hash = 14695981039346656037ull;
  for (uint8_t byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
  return hash;
}

static void TestIdentity() {
  static_assert(noexcept(TextureIdentityHash{}(std::declval<const TextureIdentity &>())),
                "identity hashing must not throw");
  const TextureIdentity a{"ab", "c", "x", 0};
  const TextureIdentity b{"a", "bc", "x", 0};
  const TextureIdentity c{"ab", "c", "x", 1};
  Check(a == a && !(a == b) && !(a == c), "identity equality");
  Check(FormatIdentity(a) == "2:ab1:c1:x0;", "identity format");
  Check(FormatIdentity(a) != FormatIdentity(b), "ambiguous fields");
  std::unordered_set<TextureIdentity, TextureIdentityHash> set;
  set.insert(a); set.insert(b); set.insert(c);
  Check(set.size() == 3 && set.count(a) == 1, "identity hashing");
  Check(TextureIdentityHash{}(a) == TextureIdentityHash{}(a), "stable hash");
}

static void TestImageAndFake() {
  const uint8_t pitched[] = {1, 2, 3, 4, 99, 99, 5, 6, 7, 8, 99, 99};
  ImageView view{pitched, 1, 2, 6, PixelFormat::RGBA8};
  OwnedImage copied;
  Check(CopyImage(view, 8, &copied), "pitched copy");
  Check(copied.strideBytes == 4 && copied.pixels ==
        std::vector<uint8_t>({1, 2, 3, 4, 5, 6, 7, 8}), "visible bytes");
  Check(!CopyImage({pitched, 1, 2, 3, PixelFormat::RGBA8}, 8, &copied), "short stride");
  Check(!CopyImage(view, 7, &copied), "byte cap");
  Check(!CopyImage({pitched, -1, 2, 6, PixelFormat::RGBA8}, 8, &copied), "bad dimensions");
  FakeBackend fake;
  const auto result = fake.Enhance({Id("x"), view, {}});
  Check(result.status == EnhancementResult::Status::Enhanced, "fake success");
  Check(result.modelId == "nearest-2x-v1" && result.image.width == 2 &&
        result.image.height == 4 && result.image.strideBytes == 8, "fake dimensions");
  Check(result.image.pixels == std::vector<uint8_t>({
    1,2,3,4,1,2,3,4, 1,2,3,4,1,2,3,4,
    5,6,7,8,5,6,7,8, 5,6,7,8,5,6,7,8}), "fake pixels and alpha");
  Check(Fnv1a(result.image.pixels) == 0x1616311c3fb35885ull, "fake golden checksum");
  const auto bad = fake.Enhance({Id("bad"), {pitched, 1, 2, 6, PixelFormat::BGRA8}, {}});
  Check(bad.status == EnhancementResult::Status::Skipped && !bad.reason.empty(),
        "unsupported format fallback");
}

class GateBackend final : public InferenceBackend {
public:
  std::string Id() const override { return "gate"; }
  EnhancementResult Enhance(const EnhancementRequest &request) override {
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++entered;
      cv.notify_all();
      cv.wait(lock, [this] { return released; });
      seenPixels.push_back(request.image.data[0]);
    }
    if (request.id.contentKey == "throw") throw std::runtime_error("backend failed");
    if (request.id.contentKey == "oom") throw std::bad_alloc();
    EnhancementResult result;
    result.status = EnhancementResult::Status::Enhanced;
    result.modelId = Id();
    return result;
  }
  void WaitEntered(int count) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this, count] { return entered >= count; });
  }
  void Release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    cv.notify_all();
  }
  int Calls() { std::lock_guard<std::mutex> lock(mutex); return entered; }
  uint8_t FirstPixel() { std::lock_guard<std::mutex> lock(mutex); return seenPixels.at(0); }
private:
  std::mutex mutex;
  std::condition_variable cv;
  int entered = 0;
  std::vector<uint8_t> seenPixels;
  bool released = false;
};

static void TestScheduler() {
  GateBackend backend;
  std::mutex mutex;
  std::vector<std::string> results;
  uint8_t pixels[] = {1, 2, 3, 4};
  const auto request = [&](const char *key) {
    return EnhancementRequest{Id(key), {pixels, 1, 1, 4, PixelFormat::RGBA8}, {}};
  };
  RemasterEngine engine(backend, [&](const TextureIdentity &id, EnhancementResult result) {
    std::lock_guard<std::mutex> lock(mutex);
    results.push_back(id.contentKey + ":" + result.reason);
  }, {1, 8});
  Check(engine.SubmitTexture(request("active")) == SubmitStatus::Accepted, "first accepted");
  backend.WaitEntered(1);
  pixels[0] = 99;
  Check(engine.SubmitTexture(request("active")) == SubmitStatus::Coalesced, "active dedup");
  Check(engine.SubmitTexture(request("throw")) == SubmitStatus::Accepted, "pending accepted");
  Check(engine.SubmitTexture(request("throw")) == SubmitStatus::Coalesced, "pending dedup");
  Check(engine.SubmitTexture(request("overflow")) == SubmitStatus::Dropped, "queue overflow");
  auto counters = engine.Counters();
  Check(counters.accepted == 2 && counters.coalesced == 2 && counters.dropped == 1 &&
        counters.pendingJobs == 1 && counters.sourceBytes == 8, "bounded counters");
  backend.Release();
  // Shutdown cancels queued work, so wait for the second job to enter first.
  backend.WaitEntered(2);
  engine.Shutdown();
  engine.Shutdown();
  counters = engine.Counters();
  Check(counters.accepted == 2 && counters.completed == 1 && counters.failed == 1 &&
        counters.cancelled == 0 && counters.sourceBytes == 0, "completion and failure");
  Check(engine.SubmitTexture(request("after")) == SubmitStatus::Stopped, "stopped submission");
  Check(backend.Calls() == 2, "dedup backend calls");
  Check(backend.FirstPixel() == 1, "source pixels copied before return");
  Check(results.size() == 2 && results[1] == "throw:backend failed", "failure reason callback");
}

static void TestShutdownCancellation() {
  GateBackend backend;
  uint8_t pixels[] = {1, 2, 3, 4};
  std::atomic<int> callbacks{0};
  RemasterEngine engine(backend, [&](const TextureIdentity &, EnhancementResult) {
    ++callbacks;
  }, {2, 12});
  auto submit = [&](const char *key) {
    return engine.SubmitTexture({Id(key), {pixels, 1, 1, 4, PixelFormat::RGBA8}, {}});
  };
  Check(submit("one") == SubmitStatus::Accepted, "active accepted");
  backend.WaitEntered(1);
  Check(submit("two") == SubmitStatus::Accepted &&
        submit("three") == SubmitStatus::Accepted, "queued accepted");
  Check(submit("four") == SubmitStatus::Dropped, "byte and queue cap");
  std::thread closer([&] { engine.Shutdown(); });
  // Wait until shutdown has cancelled the pending entries before releasing active work.
  for (;;) {
    if (engine.Counters().cancelled == 2) break;
    std::this_thread::yield();
  }
  Check(submit("late") == SubmitStatus::Stopped, "shutdown rejects work");
  backend.Release();
  closer.join();
  const auto counters = engine.Counters();
  Check(counters.cancelled == 2 && counters.completed == 1 &&
        counters.pendingJobs == 0 && counters.sourceBytes == 0 &&
        callbacks == 1 && backend.Calls() == 1, "shutdown cancellation");
}

static void TestByteBudgetAndCallbackFailure() {
  GateBackend backend;
  uint8_t pixels[] = {1, 2, 3, 4};
  RemasterEngine engine(backend, [](const TextureIdentity &, EnhancementResult) {
    throw std::runtime_error("consumer failed");
  }, {2, 4});
  auto request = [&](const char *key) {
    return EnhancementRequest{Id(key), {pixels, 1, 1, 4, PixelFormat::RGBA8}, {}};
  };
  Check(engine.SubmitTexture(request("one")) == SubmitStatus::Accepted, "budget first job");
  backend.WaitEntered(1);
  Check(engine.SubmitTexture(request("two")) == SubmitStatus::Dropped, "byte budget");
  Check(engine.SubmitTexture({Id("bad"), {pixels, 1, 1, 3, PixelFormat::RGBA8}, {}})
        == SubmitStatus::Dropped, "invalid image fallback");
  backend.Release();
  engine.Shutdown();
  const auto counters = engine.Counters();
  Check(counters.accepted == 1 && counters.dropped == 2 && counters.failed == 1 &&
        counters.sourceBytes == 0, "callback failure survives worker");
}

static void TestPerJobCap() {
  GateBackend backend;
  uint8_t pixels[] = {1, 2, 3, 4, 5, 6, 7, 8};
  RemasterEngine engine(backend, {}, {2, 16, 4});
  Check(engine.SubmitTexture({Id("large"), {pixels, 2, 1, 8, PixelFormat::RGBA8}, {}})
        == SubmitStatus::Dropped, "per-job cap rejects before copy");
  Check(engine.Counters().sourceBytes == 0 && backend.Calls() == 0,
        "oversized job has no source allocation");
  Check(engine.SubmitTexture({Id("small"), {pixels, 1, 1, 4, PixelFormat::RGBA8}, {}})
        == SubmitStatus::Accepted, "small job accepted");
  backend.WaitEntered(1);
  backend.Release();
  engine.Shutdown();
  Check(engine.Counters().completed == 1, "small job completed");
}

static void TestConcurrentAndSelfShutdown() {
  GateBackend backend;
  uint8_t pixels[] = {1, 2, 3, 4};
  std::atomic<int> callbackCalls{0};
  RemasterEngine *enginePtr = nullptr;
  RemasterEngine engine(backend, [&](const TextureIdentity &, EnhancementResult) {
    enginePtr->Shutdown(); // Worker may request stop; it must never join itself.
    ++callbackCalls;
  }, {2, 12});
  enginePtr = &engine;
  const auto submit = [&](const char *key) {
    return engine.SubmitTexture({Id(key), {pixels, 1, 1, 4, PixelFormat::RGBA8}, {}});
  };
  Check(submit("active") == SubmitStatus::Accepted, "concurrent active job");
  backend.WaitEntered(1);
  Check(submit("queued") == SubmitStatus::Accepted, "concurrent queued job");
  std::thread first([&] { engine.Shutdown(); });
  std::thread second([&] { engine.Shutdown(); });
  for (;;) {
    if (engine.Counters().cancelled == 1) break;
    std::this_thread::yield();
  }
  backend.Release();
  first.join();
  second.join();
  engine.Shutdown();
  const auto counters = engine.Counters();
  Check(callbackCalls == 1 && backend.Calls() == 1 && counters.cancelled == 1 &&
        counters.completed == 1 && counters.sourceBytes == 0, "concurrent and self shutdown");
  Check(submit("late") == SubmitStatus::Stopped, "concurrent shutdown stopped queue");
}

static void TestBackendAllocationFailure() {
  GateBackend backend;
  uint8_t pixels[] = {1, 2, 3, 4};
  std::atomic<int> failedResults{0};
  RemasterEngine engine(backend, [&](const TextureIdentity &, EnhancementResult result) {
    if (result.status == EnhancementResult::Status::Failed) ++failedResults;
  }, {2, 8});
  auto submit = [&](const char *key) {
    return engine.SubmitTexture({Id(key), {pixels, 1, 1, 4, PixelFormat::RGBA8}, {}});
  };
  Check(submit("oom") == SubmitStatus::Accepted, "OOM backend accepted");
  backend.WaitEntered(1);
  Check(submit("after") == SubmitStatus::Accepted, "following job queued");
  backend.Release();
  backend.WaitEntered(2);
  engine.Shutdown();
  const auto counters = engine.Counters();
  Check(failedResults == 1 && counters.failed == 1 && counters.completed == 1 &&
        backend.Calls() == 2, "backend OOM remains nonfatal");
}

int main() {
  try {
    TestIdentity();
    TestImageAndFake();
    TestScheduler();
    TestShutdownCancellation();
    TestByteBudgetAndCallbackFailure();
    TestPerJobCap();
    TestConcurrentAndSelfShutdown();
    TestBackendAllocationFailure();
    std::cout << "8 core test groups passed\n";
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
