# remaster-core (Milestone 1)

Build and test independently with `cmake -S remaster -B build-remaster`,
`cmake --build build-remaster`, and `ctest --test-dir build-remaster`.

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

The core does not provide persistent storage yet. `ArtifactCache` is an
interface for Milestone 2.
