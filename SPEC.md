# SPEC.md — Universal Emulator Remaster Layer
**Status:** MVP specification  
**Primary MVP target:** PPSSPP on Linux / Steam Deck-class hardware  
**Architecture goal:** Emulator-agnostic enhancement engine with a thin PPSSPP adapter  
**Primary MVP feature:** On-demand AI texture enhancement with persistent caching  
**Secondary research track:** Frame/depth/temporal enhancement, explicitly out of MVP scope unless required for interface validation

---

## 1. Purpose

Build an extensible “remaster layer” for emulators that improves old 3D games using modern image/ML techniques without requiring per-game handcrafted texture packs.

The first implementation targets PPSSPP because:

1. PSP-era 3D assets are low resolution enough for enhancement to be visually meaningful.
2. PPSSPP already has mature high-resolution rendering and texture replacement infrastructure.
3. The emulator exposes texture identity, decoded texture data, replacement loading, and backend-independent/common texture code.
4. Steam Deck-class hardware has enough headroom to perform lightweight asynchronous inference without requiring real-time per-frame neural rendering.

The MVP must prove one narrow idea:

> When PPSSPP encounters a texture that has no enhanced cached version, the system can asynchronously generate a higher-quality replacement, cache it, and use it on subsequent loads without materially disrupting emulation.

The system must be architected so that later adapters can target PCSX2, Dolphin, DuckStation, RPCS3, and a generic Vulkan/post-process mode without rewriting the enhancement engine.

---

## 2. Non-goals for the MVP

The MVP is **not**:

- a DLSS clone;
- a generic frame upscaler;
- a Steam Deck / Decky plugin;
- a cloud inference service;
- a curated per-game HD texture-pack project;
- a replacement for PPSSPP's existing internal-resolution scaling;
- a promise of temporal super-resolution, motion-vector reconstruction, or geometry modification;
- a cross-emulator release.

Those are future directions. The MVP exists to validate the reusable asset-enhancement architecture.

---

## 3. Product concept

### 3.1 User-facing behavior

A user enables **AI Remaster / Texture Enhancement** for a game.

While playing:

1. PPSSPP decodes or identifies a texture.
2. The adapter computes or obtains a stable texture identity.
3. The remaster engine checks its persistent cache.
4. If an enhanced texture exists, PPSSPP uses it.
5. If it does not exist:
   - gameplay continues using the original texture;
   - the decoded source texture is submitted to a background enhancement queue;
   - the queue runs inference;
   - the result is validated and cached.
6. The enhanced texture becomes eligible for use on a later safe reload/bind.
7. Subsequent sessions reuse the cached artifact immediately.

The system must never stall the emulation/render thread waiting for AI inference.

### 3.2 Expected visual effect

The MVP should primarily improve:

- low-resolution environmental textures;
- character diffuse/albedo-like textures;
- large UI illustrations when safe;
- pre-rendered backgrounds when safe.

It should avoid or conservatively handle:

- fonts and glyph atlases;
- dynamically updated textures;
- video frames;
- render targets / framebuffer feedback;
- procedural textures;
- tiny masks;
- palette animation;
- textures where alpha semantics could be damaged;
- content for which enhancement increases artifacts.

---

## 4. Architectural principles

### 4.1 Separate emulator integration from enhancement logic

Do not put model/runtime/cache logic directly inside PPSSPP-specific renderer code.

The implementation should have two conceptual modules:

```text
PPSSPP
  |
  | thin adapter
  v
remaster-core
  |- canonical image representation
  |- texture identity
  |- policy / classification
  |- async scheduling
  |- inference backend
  |- validation
  |- persistent cache
  |- metrics/logging
```

The adapter may live in the PPSSPP tree for the prototype, but the core APIs must not depend on PSP-specific headers or types.

### 4.2 Capability-based design

Future emulator adapters may expose different amounts of information.

Define capabilities rather than assuming every adapter supports all features.

Suggested capability model:

```text
CAP_TEXTURE_SOURCE
CAP_TEXTURE_REPLACEMENT
CAP_FRAME_COLOR
CAP_FRAME_DEPTH
CAP_DRAW_METADATA
CAP_GEOMETRY
CAP_OBJECT_MOTION
```

MVP PPSSPP requires only:

```text
CAP_TEXTURE_SOURCE
CAP_TEXTURE_REPLACEMENT
```

Future features may request additional capabilities but must gracefully degrade when unavailable.

### 4.3 Async by default

Enhancement jobs run outside the render/emulation critical path.

Required invariants:

- no synchronous inference during texture bind/upload;
- no unbounded queue growth;
- duplicate jobs are coalesced;
- cancellation works on game exit;
- cache writes are atomic;
- a failed job must not break normal rendering.

### 4.4 Preserve the emulator's source of truth

PPSSPP's existing texture identity / replacement semantics remain authoritative for PSP integration.

Do not invent a second incompatible texture-pack addressing scheme when the emulator already has one.

For the MVP, prefer compatibility with PPSSPP's existing replacement workflow and hash behavior.

Current PPSSPP documentation recommends strong hashes (notably xxh64) and commonly `ignoreAddress=true` for robust replacement packs. PPSSPP supports replacement assets including PNG and GPU-friendly KTX2/DDS formats.

---

## 5. High-level architecture

```text
                       +-----------------------------+
                       |          PPSSPP             |
                       |                             |
PSP texture ---------->| decode / identify texture   |
                       |            |                |
                       +------------|----------------+
                                    |
                                    v
                        +------------------------+
                        | PPSSPP Remaster Adapter|
                        +------------------------+
                           |              ^
              source image |              | replacement handle/path
                           v              |
                   +--------------------------------+
                   |         remaster-core          |
                   |                                |
                   | TextureIdentity                |
                   | Policy / Classifier            |
                   | Cache                          |
                   | JobScheduler                   |
                   | InferenceBackend               |
                   | Validator                      |
                   +--------------------------------+
                         |              |
                     cache miss       cache hit
                         |              |
                         v              |
                  background job       |
                         |              |
                         v              |
                  enhanced artifact ---+
```

---

## 6. Proposed module boundaries

The exact language/build layout may change during repo reconnaissance, but responsibilities should remain stable.

### 6.1 `remaster-core`

Emulator-agnostic.

Suggested interfaces:

```cpp
namespace remaster {

enum class PixelFormat {
    RGBA8,
    BGRA8,
    RGB8,
    // Add only when needed.
};

struct ImageView {
    const uint8_t *data;
    int width;
    int height;
    int strideBytes;
    PixelFormat format;
};

struct OwnedImage {
    std::vector<uint8_t> pixels;
    int width;
    int height;
    int strideBytes;
    PixelFormat format;
};

struct TextureIdentity {
    std::string emulator;      // "ppsspp"
    std::string gameId;        // e.g. disc/game identifier
    std::string contentKey;    // adapter-provided stable identity
    int mipLevel = 0;

    bool operator==(const TextureIdentity &) const = default;
};

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
    enum class Status {
        Enhanced,
        Skipped,
        Failed
    };

    Status status;
    OwnedImage image;
    std::string modelId;
    std::string reason;
};

class InferenceBackend {
public:
    virtual ~InferenceBackend() = default;
    virtual std::string Id() const = 0;
    virtual EnhancementResult Enhance(
        const EnhancementRequest &request
    ) = 0;
};

class ArtifactCache {
public:
    virtual ~ArtifactCache() = default;

    virtual bool Lookup(
        const TextureIdentity &id,
        std::string_view modelId,
        /*out*/ std::filesystem::path *artifact
    ) = 0;

    virtual bool StoreAtomically(
        const TextureIdentity &id,
        std::string_view modelId,
        const OwnedImage &image,
        /*out*/ std::filesystem::path *artifact
    ) = 0;
};

class RemasterEngine {
public:
    void SubmitTexture(const EnhancementRequest &request);
    std::optional<std::filesystem::path> LookupEnhancedTexture(
        const TextureIdentity &id
    );
    void Shutdown();
};

} // namespace remaster
```

The API above is illustrative. Agents may adapt names/types to PPSSPP conventions, but must preserve:

- emulator-independent core;
- stable asset identity;
- async submission;
- nonblocking lookup;
- pluggable inference backend;
- persistent versioned cache.

### 6.2 PPSSPP adapter

Responsibilities:

- determine game ID;
- receive decoded source texture data at a point where pixels are valid;
- map PPSSPP texture identity to `TextureIdentity`;
- identify mip level;
- provide useful metadata;
- ask `remaster-core` for an already cached replacement;
- safely cause PPSSPP to use the enhanced result;
- avoid submitting unsupported/dynamic/framebuffer textures;
- expose configuration and debug metrics.

The adapter should reuse the current texture replacement path where practical rather than creating a second GPU upload pipeline.

Upstream code paths to inspect first:

- `GPU/Common/TextureReplacer.*`
- `GPU/Common/ReplacedTexture.*`
- `GPU/Common/TextureCacheCommon.*`
- backend texture caches such as Vulkan
- current texture scaler integration (`GPU/Common/TextureScalerCommon.*`)
- config/UI locations controlling texture replacement/upscaling

Do not assume exact hook locations until current source is read.

---

## 7. Texture identity and cache semantics

### 7.1 Requirements

Texture identity must be:

- deterministic;
- stable across sessions;
- robust against irrelevant memory-address changes where PPSSPP already supports that;
- separated by game;
- separated by mip level when necessary;
- separated by enhancement model/version/settings.

### 7.2 Cache key

Recommended conceptual key:

```text
cache-key =
    emulator
  + game-id
  + source-content-identity
  + mip-level
  + model-id
  + model-version
  + enhancement-config-version
```

Never use only a PSP memory address as persistent identity.

### 7.3 Directory layout

Illustrative:

```text
REMMASTER_CACHE/
  ppsspp/
    <game-id>/
      <model-id>/
        <prefix>/
          <content-key>_m0.png
          <content-key>_m0.json
```

Metadata sidecar may include:

```json
{
  "schema_version": 1,
  "source_width": 128,
  "source_height": 128,
  "output_width": 512,
  "output_height": 512,
  "model_id": "example-4x-v1",
  "model_sha256": "...",
  "created_at": "...",
  "input_content_hash": "...",
  "status": "enhanced"
}
```

Negative/skipped decisions may also be cached to avoid repeatedly reclassifying bad candidates.

### 7.4 Atomicity

Write:

1. temporary artifact;
2. fsync/close as appropriate;
3. atomic rename into the final cache location.

A crash must not leave a file that appears valid but is partially written.

---

## 8. Candidate filtering policy

The MVP should be conservative.

### 8.1 Hard skip candidates

Skip initially if any is true:

- width or height below configurable minimum;
- texture changes too frequently;
- framebuffer/render-target originated;
- source pixels cannot be safely captured;
- unsupported pixel format;
- texture exceeds configured maximum dimensions/memory budget;
- alpha or palette semantics cannot be represented safely;
- known video/streaming texture;
- repeated hash churn indicates dynamic content.

### 8.2 Heuristic skip candidates

May skip:

- font atlases;
- UI text;
- monochrome masks;
- particle atlases;
- textures with extremely high transparency;
- flat-color textures;
- already high-resolution textures;
- obvious noise.

Heuristics must produce an explainable reason string for debug logs.

### 8.3 Model confidence / quality gate

The first MVP can use deterministic heuristics instead of a learned classifier.

Do not make a texture classifier a prerequisite for proving the architecture.

---

## 9. Inference backend

### 9.1 Backend abstraction

The core must not hardcode one inference technology.

Expected future backends may include:

- ONNX Runtime;
- NCNN;
- Vulkan compute;
- platform-specific GPU runtimes;
- a mock/identity backend for tests.

The MVP must include at minimum:

1. `IdentityBackend` or deterministic test backend;
2. one real super-resolution backend.

### 9.2 Model requirements

The initial model should be:

- permissively redistributable or cleanly user-supplied;
- small enough for Steam Deck-class hardware;
- capable of 2x or 4x enhancement;
- deterministic for the same input/config;
- runnable off the main thread;
- able to preserve alpha or process RGB and restore alpha safely.

The specification intentionally does **not** mandate Real-ESRGAN, ESRGAN, SwinIR, or another specific model. Benchmark before locking the choice.

### 9.3 Model versioning

A change to model weights or preprocessing that may change output must change the cache-visible model version.

Never silently reuse artifacts generated by incompatible model versions.

---

## 10. Scheduling

### 10.1 Queue behavior

Use a bounded queue.

Suggested defaults:

```text
worker count:          1
max pending jobs:      32
duplicate coalescing:  yes
priority:              larger/reused textures first if data available
```

Do not spawn one worker per texture.

### 10.2 Backpressure

When the queue is full:

- gameplay continues normally;
- low-priority submissions are dropped or deferred;
- a counter increments;
- no render-thread blocking.

### 10.3 Shutdown

On game close / emulator shutdown:

- stop accepting work;
- cancel queued jobs where safe;
- allow in-progress atomic writes to complete or abandon their temporary files;
- join worker threads;
- never access destroyed emulator objects from worker threads.

---

## 11. Applying enhanced textures safely

The MVP must define a safe policy for when a newly generated texture becomes active.

Preferred order:

1. **Next natural texture reload** — safest.
2. Explicit invalidation/reload using an existing PPSSPP mechanism, if confirmed safe.
3. Immediate hot-swap only if the relevant PPSSPP texture-cache lifecycle clearly supports it.

Do not introduce use-after-free or cross-thread GPU resource mutation to make live replacement appear faster.

A texture becoming enhanced on the next scene/load is acceptable for MVP.

---

## 12. User configuration

Minimal settings:

```text
AI texture enhancement: Off / On
Scale:                  2x / 4x
Model:                  Auto / <available models>
Enhance UI:             Off by default
Cache size limit:       configurable
Debug logging:          Off / On
```

Optional diagnostics:

```text
seen textures
cache hits
cache misses
queued
completed
skipped
failed
average inference time
cache disk usage
```

The MVP can expose these through a developer/debug UI before polishing a consumer UI.

---

## 13. Performance and resource requirements

The core success condition is **zero material impact on frame pacing when a new texture is queued**.

Initial target budgets on Steam Deck-class hardware:

- render-thread submission overhead: ideally < 0.2 ms per newly observed texture;
- cache-hit lookup: ideally < 0.1 ms amortized;
- no synchronous disk decode/inference on render thread;
- inference can take hundreds of milliseconds because it is asynchronous;
- bounded RAM used by queued source images;
- configurable cache disk budget.

These are engineering targets, not guaranteed product claims.

Benchmark and report actual numbers.

---

## 14. Failure behavior

All enhancement failures are nonfatal.

On any error:

```text
enhancement unavailable
        ->
use original PPSSPP texture
        ->
log/debug counter
```

Failures include:

- inference backend unavailable;
- model load failure;
- unsupported format;
- OOM;
- corrupt cache;
- failed cache write;
- invalid enhanced dimensions;
- exception in worker.

The remaster feature must be disable-able without affecting normal PPSSPP behavior.

---

## 15. Security and robustness

Treat model files and cache artifacts as untrusted inputs.

Requirements:

- validate dimensions before allocation;
- enforce maximum pixel counts;
- reject path traversal in generated cache paths;
- do not derive filesystem paths directly from unsanitized game strings;
- validate decoded replacement files;
- cap decompression/output sizes;
- never execute model/cache-provided scripts;
- avoid loading arbitrary shared libraries from game directories.

---

## 16. Testing

### 16.1 Core unit tests

Required:

- deterministic cache-key generation;
- identity equality/hash;
- atomic cache store;
- corrupted cache handling;
- duplicate job coalescing;
- queue saturation;
- shutdown while jobs exist;
- skipped-result caching if implemented;
- model-version cache invalidation;
- unsupported image format fallback.

### 16.2 Adapter tests

Where feasible:

- same texture produces same identity across repeated runs;
- address relocation does not unexpectedly duplicate identity when PPSSPP replacement semantics say it should not;
- mip levels do not collide;
- dynamic/framebuffer textures are skipped;
- cache hit causes replacement to be discoverable;
- feature-off path behaves exactly as upstream.

### 16.3 Integration test backend

Provide a deterministic fake backend, for example:

```text
2x nearest-neighbor upscale + visible debug watermark/pattern
```

This is not a production enhancer. It allows end-to-end testing without ML runtime/model dependencies.

### 16.4 Golden tests

Store a small legal/homebrew test asset set.

For each:

- source hash;
- expected transformed dimensions;
- deterministic fake-backend checksum.

Do not commit copyrighted commercial game assets.

---

## 17. Logging and observability

Use PPSSPP's logging conventions on the adapter side.

Core events should expose structured enough data to answer:

- Why was this texture skipped?
- Was this a cache hit?
- Which model/version generated it?
- How long did inference take?
- Did output validation pass?
- Why was a cached artifact rejected?

Avoid per-frame log spam.

---

## 18. MVP acceptance criteria

The MVP is complete when all of the following are true:

1. PPSSPP builds and runs normally with enhancement disabled.
2. A legal test/homebrew PSP workload can be launched with enhancement enabled.
3. At least one eligible texture is captured without blocking gameplay.
4. A background backend generates a higher-resolution replacement.
5. The result is persisted under a stable cache key.
6. On a later load/session, the enhanced texture is used automatically.
7. A second run does not repeat inference for the same model/config/source.
8. Unsupported/skipped textures fall back cleanly.
9. Queue saturation and inference failure do not affect emulator stability.
10. The core enhancement/cache/scheduler module contains no PPSSPP-specific types.
11. Automated tests cover cache identity, async scheduling, failure fallback, and a fake end-to-end backend.
12. A short benchmark reports frame-time impact, inference latency, and cache-hit behavior.
13. The architecture documentation identifies what a second emulator adapter would need to implement.

---

## 19. Stretch acceptance criteria

Not required for MVP:

- hot-swapping newly completed textures during the same scene;
- KTX2 output generation;
- texture classification;
- per-game configuration overrides;
- Decky UI;
- Steam Deck package/install flow;
- multiple concurrent inference backends;
- Dolphin/PCSX2 proof-of-concept adapter;
- depth capture;
- final-frame enhancement;
- temporal reconstruction.

---

## 20. Cross-emulator adapter contract

The architecture should converge toward a small adapter boundary similar to:

```cpp
struct EmulatorCapabilities {
    bool textureSource = false;
    bool textureReplacement = false;
    bool frameColor = false;
    bool frameDepth = false;
    bool drawMetadata = false;
    bool geometry = false;
    bool objectMotion = false;
};

class EmulatorAdapter {
public:
    virtual ~EmulatorAdapter() = default;

    virtual std::string EmulatorId() const = 0;
    virtual std::string GameId() const = 0;
    virtual EmulatorCapabilities Capabilities() const = 0;

    // Asset-level path.
    virtual void OnTextureAvailable(/* adapter-specific source */) = 0;
    virtual void OnEnhancedArtifactReady(
        const TextureIdentity &id,
        const std::filesystem::path &artifact
    ) = 0;
};
```

Do not prematurely freeze this ABI. The PPSSPP MVP should produce enough evidence to design it properly.

---

## 21. Future architecture

### Phase A — additional emulator adapters

Likely next candidates:

1. PCSX2
2. Dolphin
3. DuckStation

Selection should be based on:

- accessible decoded texture path;
- stable identity/replacement mechanism;
- renderer architecture;
- active upstream compatibility.

### Phase B — generic Vulkan fallback

A system-level Vulkan layer may provide:

- final color frame;
- generic spatial restoration/upscaling;
- sharpening;
- possibly depth in limited cases.

It will not match deep emulator integration because source textures and object semantics are lost.

### Phase C — temporal / geometry-aware reconstruction

Potential future inputs:

```text
current color
previous color
depth
jitter
draw metadata
geometry transforms
reconstructed motion
```

This is a separate research program and must not distort the MVP architecture around unsupported assumptions.

---

## 22. PPSSPP upstream facts to verify during implementation

Agents must inspect current upstream source before editing because exact symbols can change.

As of the spec's preparation, useful current references include:

- PPSSPP texture replacement documentation:
  https://www.ppsspp.org/docs/reference/texture-replacement/
- PPSSPP graphics settings:
  https://www.ppsspp.org/docs/settings/graphics/
- PPSSPP source:
  https://github.com/hrydgard/ppsspp
- Current code includes common components named around:
  `TextureReplacer`, `ReplacedTexture`, `TextureCacheCommon`,
  `TextureScalerCommon`, plus backend-specific texture caches.

The documentation currently describes:
- strong content hashes including xxh64;
- `ignoreAddress=true` as a common robust texture-pack choice;
- PNG replacement/dump workflow;
- GPU-oriented KTX2 and DDS replacement support.

Treat these as integration clues, not immutable APIs.

---

## 23. Definition of done for a Codex agent

An agent claiming completion must leave:

```text
- buildable source
- tests
- reproducible build/run commands
- a legal test scenario
- benchmark notes
- architecture notes
- known limitations
- no committed copyrighted game assets
```

It must also update `PLAN.md` with completed tasks, deviations, and follow-up work.
