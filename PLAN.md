# PLAN.md — PPSSPP-first Universal Emulator Remaster Layer
**Execution style:** Agent-oriented, incremental, testable  
**Companion document:** `SPEC.md`

---

## 0. Working rules for agents

Before modifying code:

1. Read `SPEC.md` completely.
2. Inspect the current PPSSPP repository; do not assume old file locations or APIs.
3. Build unmodified PPSSPP once on the target machine.
4. Record the upstream commit SHA used.
5. Prefer small commits with one architectural purpose each.
6. Keep the emulator-agnostic core free of PPSSPP types.
7. Never block the render/emulation thread on inference.
8. Preserve upstream behavior when the feature is off.
9. Use legal/homebrew/synthetic assets for tests.
10. Update this plan as tasks are completed.

If reality conflicts with the spec, preserve the architectural goals and document the deviation.

---

# Milestone 0 — Repository reconnaissance and baseline

## Goal

Establish a reproducible PPSSPP development baseline and identify exact integration points.

## Tasks

### 0.1 Clone/update PPSSPP

Use the current upstream repo and recursively initialize submodules.

Record:

```text
PPSSPP commit:
OS:
compiler:
CMake version:
graphics backend tested:
GPU/APU:
```

### 0.2 Build unmodified PPSSPP

Target Linux SDL desktop build first.

Steam Deck hardware is desirable but not required for the first compile.

Produce reproducible commands in `docs/remaster-build.md`.

### 0.3 Inspect texture pipeline

Locate and understand current implementations of:

```text
TextureReplacer
ReplacedTexture
TextureCacheCommon
TextureScalerCommon
Vulkan texture cache
decoded-texture notification/dump path
replacement lookup/load path
configuration variables
developer settings UI
```

Answer in `docs/remaster-ppsspp-notes.md`:

1. Where is stable texture identity formed?
2. Where are decoded RGBA-like pixels available?
3. Which thread executes that code?
4. How does a replacement texture enter the normal cache/upload path?
5. How is texture replacement invalidated/reloaded?
6. How are framebuffer/dynamic textures distinguishable?
7. Where is game ID available?
8. Which PPSSPP hash/replacement options should be reused?
9. What is the least invasive hook point for:
   - capture/submission;
   - cache-hit replacement lookup?

### 0.4 Establish baseline tests/benchmark

Record:

- idle/main-menu frame time;
- one representative legal/homebrew 3D scene;
- memory usage;
- startup time.

## Exit criteria

- clean upstream build succeeds;
- exact proposed hook points are documented;
- no production code changed yet.

---

# Milestone 1 — Emulator-agnostic core skeleton

## Goal

Create `remaster-core` without ML.

## Tasks

### 1.1 Create module

Preferred conceptual structure:

```text
remaster/
  RemasterEngine.h/.cpp
  TextureIdentity.h/.cpp
  Image.h/.cpp
  ArtifactCache.h/.cpp
  JobScheduler.h/.cpp
  InferenceBackend.h
  IdentityBackend.h/.cpp
  tests/
```

Adapt to PPSSPP build conventions as needed.

### 1.2 Define core types

Implement:

- canonical image view/owned image;
- texture identity;
- metadata;
- request/result;
- inference-backend interface;
- cache interface;
- engine interface.

### 1.3 Implement fake backend

Use a deterministic non-ML transform such as 2x nearest-neighbor upscale plus an optional unmistakable debug marker.

Purpose:

- validate the complete pipeline;
- avoid debugging ML and emulator integration simultaneously.

### 1.4 Implement bounded scheduler

Required behavior:

- one worker by default;
- bounded queue;
- duplicate coalescing;
- clean shutdown;
- no uncaught worker exceptions;
- counters for accepted/dropped/completed/failed.

### 1.5 Unit tests

Implement tests for:

- identity equality/key formatting;
- duplicate coalescing;
- queue overflow;
- shutdown;
- fake backend deterministic output.

## Exit criteria

`remaster-core` builds/tests independently of a live PPSSPP game path and includes no PPSSPP-specific types.

---

# Milestone 2 — Persistent cache

## Goal

Make enhancement results stable across sessions.

## Tasks

### 2.1 Define cache schema

Include:

```text
schema version
emulator ID
game ID
source identity
mip
model ID/version
enhancement config version
```

### 2.2 Implement atomic file cache

Start with PNG or the easiest safe format already available in-tree.

Do not make KTX2 generation a blocker.

### 2.3 Add sidecar metadata if useful

Store dimensions/model/version/checksum.

### 2.4 Add validation

Reject:

- impossible dimensions;
- mismatched identity/model metadata;
- truncated/corrupt files;
- outputs exceeding limits.

### 2.5 Add tests

Required:

- cache miss;
- store/hit;
- corrupted artifact;
- model-version invalidation;
- interrupted/temp-file behavior;
- path sanitization.

## Exit criteria

A process can enhance a synthetic texture, exit, restart, and hit the cache without invoking the backend again.

---

# Milestone 3 — PPSSPP adapter with fake enhancement

## Goal

Prove end-to-end PPSSPP integration before ML.

## Tasks

### 3.1 Add feature flag

Developer-facing setting is sufficient:

```text
AI texture enhancement: off/on
```

Default off.

### 3.2 Capture eligible decoded textures

At the hook identified in Milestone 0:

- obtain stable PPSSPP identity;
- obtain source pixels;
- copy only what is needed before returning;
- avoid worker access to transient PPSSPP buffers;
- produce canonical metadata.

### 3.3 Add conservative eligibility gate

Initially skip aggressively:

- framebuffer textures;
- obviously dynamic textures;
- tiny textures;
- unsupported formats;
- suspected video;
- known unsafe cases.

Log the skip reason in debug mode.

### 3.4 Submit asynchronously

Important:

```text
render thread:
  construct lightweight request/copy
  enqueue
  return immediately
```

Never call backend inference synchronously.

### 3.5 Cache-hit replacement path

Prefer reuse of PPSSPP's normal replacement mechanism.

Possible approaches, ordered by preference:

1. make generated cached files visible to existing `TextureReplacer`;
2. add a narrow programmatic replacement-provider hook used by `TextureReplacer`;
3. only if necessary, create a new replacement upload path.

Avoid #3 unless source inspection proves the existing path unsuitable.

### 3.6 Activation policy

First implementation may require:

- next scene;
- texture cache invalidation;
- next session.

Immediate hot-swap is not required.

### 3.7 End-to-end fake-backend demo

Expected behavior:

```text
first encounter:
  original visible
  fake enhancement queued/stored

later reload/session:
  fake enhanced replacement visible
```

## Exit criteria

A legal test game/homebrew demonstrates cached replacement loading end-to-end with the fake backend.

---

# Milestone 4 — Real inference backend spike

## Goal

Choose one practical real enhancement backend based on evidence, not preference.

## Tasks

### 4.1 Benchmark candidate runtimes/models

At minimum evaluate a small set appropriate to Linux/Steam Deck.

Candidates may include:

```text
ONNX Runtime
NCNN
Vulkan-native inference
```

Candidate models may include compact ESRGAN-like or other SR networks.

Record for representative texture sizes:

```text
64x64
128x128
256x256
512x512
```

Measure:

- load time;
- inference latency;
- peak RAM/VRAM;
- output quality;
- alpha handling;
- dependency/build complexity;
- license/distribution constraints.

### 4.2 Select MVP backend

Decision criteria:

1. visual benefit;
2. Steam Deck viability;
3. build/distribution simplicity;
4. robust Linux support;
5. model licensing.

Document the decision in:

```text
docs/remaster-model-selection.md
```

### 4.3 Implement backend

Requirements:

- model loaded once;
- thread-safe under scheduler policy;
- deterministic preprocessing;
- alpha-safe behavior;
- useful errors;
- model version exposed through `Id()`.

### 4.4 Fallback

If model/runtime initialization fails:

```text
disable real backend
continue normal emulation
log once
```

## Exit criteria

The real backend can enhance synthetic/legal test textures through the same interface as the fake backend.

---

# Milestone 5 — PPSSPP AI texture MVP

## Goal

Run real asynchronous enhancement in normal gameplay.

## Tasks

### 5.1 Integrate selected backend

Feature flow:

```text
texture encountered
 -> eligibility
 -> cache lookup
 -> miss => queue
 -> inference
 -> validate
 -> atomic store
 -> later replacement
```

### 5.2 Add configuration

Minimum:

```text
enable
2x/4x
model
enhance UI (default off)
cache path/size behavior
debug logging
```

### 5.3 Add stats

At minimum:

```text
textures seen
eligible
cache hits
cache misses
queued
queue-dropped
enhanced
skipped
failed
mean/median inference latency
```

### 5.4 Cache budget

Implement a simple safe policy.

Acceptable MVP choices:

- user-managed/unbounded with visible size; or
- configurable cap with LRU-ish cleanup.

Do not delete artifacts currently being read/written.

### 5.5 Benchmark

Collect:

#### Feature off
- frame times
- memory

#### Feature on, warm cache
- frame times
- cache lookup cost

#### Feature on, cold cache
- frame times
- inference latency
- queue behavior
- memory

Use at least one 3D homebrew/legal test workload.

### 5.6 Visual comparison

Produce screenshots for internal evaluation:

```text
A. native/base
B. high internal resolution only
C. high internal resolution + existing PPSSPP non-AI texture scaling
D. high internal resolution + AI texture replacement
```

Do not claim superiority unless examples support it.

## Exit criteria

All MVP acceptance criteria in `SPEC.md` pass.

---

# Milestone 6 — Robustness pass

## Goal

Prevent the prototype from becoming a crash-prone renderer hack.

## Tasks

### 6.1 Stress dynamic textures

Validate:

- no runaway cache generation;
- no thousands of jobs from streaming/video content;
- no uncontrolled disk growth.

### 6.2 Stress shutdown/restart

Repeatedly:

- launch game;
- trigger queued jobs;
- quit immediately;
- relaunch.

Run with sanitizers when practical.

### 6.3 Cache corruption

Manually corrupt:

- image artifact;
- metadata;
- temp file.

Expected result: fallback and regeneration, never emulator crash.

### 6.4 Queue stress

Artificially slow inference and trigger many textures.

Verify:

- bounded memory;
- correct drop policy;
- stable frame pacing.

### 6.5 Feature-off regression

Feature off must preserve upstream behavior.

## Exit criteria

No known crash/data-race in the remaster path under tested stress scenarios.

---

# Milestone 7 — Extract/refine cross-emulator contract

## Goal

Use what was learned from PPSSPP to design the reusable adapter contract.

Do this **after** the MVP works.

## Tasks

### 7.1 Identify PPSSPP-specific leakage

List every place where `remaster-core` knows about:

```text
PSP
PPSSPP
GE
CLUT
PPSSPP filesystem conventions
PPSSPP logging/config
```

Target: zero except adapter-facing glue or generic metadata.

### 7.2 Write adapter v0 design

Create:

```text
docs/emulator-adapter-api.md
```

Include:

- capabilities;
- texture-source event;
- stable asset identity;
- replacement publication;
- lifecycle;
- game identity;
- logging;
- optional future frame/depth APIs.

### 7.3 PCSX2 or Dolphin reconnaissance

Do not implement a full port yet.

Inspect one second emulator and answer:

1. Where are decoded source textures available?
2. How are replacement textures identified?
3. Can the existing `remaster-core` concepts map cleanly?
4. What PPSSPP assumptions fail?
5. What API changes are needed?

### 7.4 Architecture gate

Classify required changes:

```text
A. trivial adapter changes
B. small generic-core extension
C. major redesign
```

If C, redesign before adding more emulators.

## Exit criteria

A second-emulator design exercise validates that the core is plausibly reusable.

---

# Milestone 8 — Optional second-emulator proof of concept

## Goal

Prove portability with the smallest meaningful integration.

Preferred order:

```text
PCSX2 or Dolphin
```

Selection should follow reconnaissance.

## Scope

Only:

```text
source texture
 -> core fake/real backend
 -> persistent cache
 -> replacement
```

Do not add temporal enhancement.

## Exit criteria

The same `remaster-core` binary/library concepts work in two emulator integrations with no forked inference/cache implementation.

---

# Milestone 9 — Research track: frame/depth/temporal enhancement

This milestone is intentionally separate from product MVP.

## Questions

1. Can PPSSPP expose useful depth consistently?
2. Can draw/transform history reconstruct screen-space motion?
3. How should UI and framebuffer effects be segmented?
4. Is temporal reconstruction visibly better than high internal resolution + MSAA + AI textures?
5. Is complexity justified on Steam Deck?

## Prototype order

```text
1. capture frame color
2. capture depth
3. visualize depth alignment
4. capture per-draw metadata
5. offline motion reconstruction experiment
6. only then attempt real-time temporal prototype
```

Do not start by embedding a full temporal neural upscaler.

---

# Recommended commit sequence

A clean sequence might look like:

```text
1. docs: record PPSSPP remaster integration notes
2. remaster: add emulator-agnostic core types
3. remaster: add bounded job scheduler
4. remaster: add deterministic fake backend
5. remaster: add persistent artifact cache
6. tests: cover scheduler and cache
7. ppsspp: add remaster feature flag
8. ppsspp: submit eligible decoded textures
9. ppsspp: consume cached generated replacements
10. tests: add PPSSPP integration coverage
11. remaster: add selected real inference backend
12. ppsspp: expose MVP settings/stats
13. perf: add benchmark results
14. docs: finalize adapter API notes
```

Do not combine all architecture and ML work into one giant commit.

---

# Agent handoff checklist

At the end of every substantial agent run, update this section or an equivalent status file.

```text
Upstream PPSSPP SHA:
Branch:
Build status:
Tests:
Current milestone:
Completed:
Blocked:
Architecture deviations:
Known bugs:
Next concrete task:
Benchmark notes:
```

---

# Decision gates

## Gate A — after Milestone 3

Question:

> Can we reliably capture a texture, cache a transformed version, and make PPSSPP consume it later without harming frame pacing?

If **no**, stop ML work and fix integration.

## Gate B — after Milestone 4

Question:

> Is at least one model/runtime combination visually useful and operationally reasonable on Steam Deck-class hardware?

If **no**, retain architecture but reconsider models/runtime before productizing.

## Gate C — after Milestone 5

Question:

> Does AI texture enhancement add enough value over PPSSPP high internal resolution + existing texture scaling to justify the feature?

Evaluate with side-by-side captures, not intuition.

## Gate D — after Milestone 7

Question:

> Can PCSX2 or Dolphin map into the core abstraction without duplicating the entire pipeline?

If **no**, revise the adapter/core boundary before porting.

---

# Immediate first task for a Codex agent

Use this exact operational objective:

> Build current upstream PPSSPP unmodified, inspect the texture decoding/replacement path, and produce `docs/remaster-ppsspp-notes.md` identifying the exact current hook points for (1) receiving stable texture identity plus decoded source pixels and (2) publishing an asynchronously generated cached replacement through the least invasive existing replacement mechanism. Do not add ML or production integration code yet. Record the PPSSPP commit SHA, relevant source files/classes/functions, thread/lifetime constraints, and any risks that invalidate assumptions in `SPEC.md`.

That reconnaissance is the highest-leverage next step and prevents the rest of the project from being built around stale assumptions.

---

# Current handoff status (2026-09-29)

```text
Upstream PPSSPP SHA: b9c5b28b8f69a78a18bdf381fb4d34b703d21e97
PPSSPP branch: ai-remaster (local)
Project branch: main
Build status: Unmodified Debug Linux SDL build and UnitTest executable pass.
Tests: PPSSPPUnitTest all passed 60 tests; no rendered fixture test yet.
Current milestone: 0, with standalone core work beginning in parallel after source reconnaissance.
Completed: Upstream checkout, reproducible baseline build, texture pipeline notes, validation protocol.
Blocked: None for source work. Rendering baseline needs a built legal fixture and display/headless run.
Architecture deviations: Same-session generated replacement requires explicit owner-thread publication; cached misses and pack scanning prevent file-only hot activation.
Known bugs: None in project code; no production integration exists yet.
Next concrete task: Build and render the synthetic fixture, record baseline screenshot/metrics, then test the standalone fake core.
Benchmark notes: No frame-time measurements yet; current shell has no display.
```

See `docs/remaster-build.md`, `docs/remaster-ppsspp-notes.md`, and `docs/remaster-validation.md` for evidence and test gates.
