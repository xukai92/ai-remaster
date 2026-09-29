# remaster-core (Milestones 1–2)

Build dependencies: C++17 compiler, CMake, threads, and libpng (with zlib).
Build and test independently with:

```sh
nix-shell -p cmake ninja libpng --run \
  "cmake -S remaster -B build-remaster -G Ninja && cmake --build build-remaster && ctest --test-dir build-remaster --output-on-failure"
```

Outside Nix, install the same dependencies and run those three CMake commands.

`SchedulerLimits::maxJobCopyBytes` caps the visible bytes copied by one
`Submit` call (default 8 MiB). `maxSourceBytes` caps all copied source images,
including the active job (default 64 MiB); `maxPendingJobs` caps queued jobs
(default 32). Submissions exceeding a limit return `Dropped` and increment the
drop counter. These limits bound work and memory, but `Submit` still copies
accepted pixels synchronously. Its actual frame-time cost needs measurement
at the PPSSPP capture point with representative textures.

`Shutdown` may be called concurrently by external threads. The first call
stops acceptance and cancels pending jobs; external callers return after the
active job and callback finish. A call from the worker callback only requests
stop. An external caller must later join it, either through `Shutdown` or
destruction. Keep the backend and all objects captured by the callback alive
until that join returns. Destroy the engine from an external thread.

`PngArtifactCache` stores RGBA8 PNGs that a standard PNG loader can read.
`CacheKey` contains schema, emulator, game, source identity, mip, model ID,
model version, and enhancement config version. Version 1 uses hex-encoded
identity fields as path components and rejects empty or overlong components.
The `.png.meta` sidecar records the exact key, dimensions, and an FNV-1a
checksum of the PNG file. Lookup checks those fields, file size, and a full
libpng decode; corrupt artifacts become misses. Temporary files are synced,
then published by rename. A crash between PNG and metadata renames can leave
a miss, which can be regenerated. Temporary files are ignored by lookup.

The caller must use a trusted cache root. An adapter must run the full Lookup
off-thread because it performs disk I/O and PNG decode. Same-key concurrent
writers are unsupported; use one writer for a cache root. The cache has no
eviction policy.
