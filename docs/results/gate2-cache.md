# Gate 2: persistent cache

**Result: standalone cache gate passed on 2026-09-29.** The cache writes RGBA8 PNGs under a versioned key and validates dimensions, metadata, checksum, size, and PNG decoding on lookup. A cold process writes a fake-enhanced artifact; a separate warm process reads matching pixels without invoking the backend.

## Reproduce

From the project root:

```sh
nix-shell -p cmake ninja libpng --run 'cmake -S remaster -B build-remaster -G Ninja && cmake --build build-remaster && ctest --test-dir build-remaster --output-on-failure'
```

Observed: **3/3 CTest entries passed**, including core behavior, cache corruption and invalidation, and the cold/warm process pair. The dev agent also ran the three tests under ASan/UBSan with leak detection disabled for this environment.

The tests cover truncated and invalid PNGs, bad sidecar metadata, checksum and dimension mismatches, model/config/schema changes, mip separation, traversal-shaped identities, and ignored temporary files. PNG and sidecar writes use synced temporary files and rename; a crash between the two renames can yield a safe cache miss that is regenerated.

## Integration requirements

`PngArtifactCache::Lookup()` checksums and fully decodes the PNG. The PPSSPP adapter must perform this validation off the render/emulation path and publish a validated result through the existing replacement lifecycle. The cache has no eviction or concurrent same-key writer lock yet. This gate does not prove PPSSPP loads an artifact or that cache hits meet the frame-time target; those are Milestone 3 measurements.
