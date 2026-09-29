# PPSSPP baseline build (2026-09-29)

## Baseline

| Item | Recorded value |
| --- | --- |
| PPSSPP checkout | `ppsspp/` at `b9c5b28b8f69a78a18bdf381fb4d34b703d21e97` (`v1.20.4-2207-gb9c5b28b8f`) |
| Source changes | None; checkout and initialized submodules were clean after the build |
| Host | NixOS 26.05 (Yarara), x86_64 |
| CPU/APU | AMD Ryzen 7 8745H with Radeon 780M Graphics |
| Compiler | GCC/G++ 15.2.0 |
| Build tools | CMake 4.1.6, Ninja 1.13.2 |
| Graphics hardware visible | Vulkan reports AMD Radeon 780M (RADV, Mesa 26.1.8), plus llvmpipe |
| Graphics backend exercised | PPSSPPHeadless software renderer; a synthetic homebrew scene rendered successfully. SDL window and Vulkan rendering remain untested because this shell has no X11/Wayland display. |
| Build | **Passed**: Debug Linux SDL desktop, PPSSPPHeadless, and C++ unit test executables linked |
| Unit tests | **Passed**: `PPSSPPUnitTest all` reported `60 tests passed` |
| Fixture smoke test | **Passed**: both `REM_FIXTURE` markers appeared, all four panels were visible, and two software screenshots matched byte for byte; see [Gate 0 result](results/gate0-baseline.md) |

This is the unmodified-upstream build required by [PLAN.md](../PLAN.md), Milestone 0.2. The host's default shell lacks CMake and SDL3 development files, so the build used a temporary Nix shell. No system packages were installed. The Nix package set resolved from `/nix/store/nqkh6j5xlyvlw4hlrw4ybpq1cis79szf-source` (`26.05pre-git`); to reproduce these exact dependency versions later, pin that package set in a project flake.

## Reproduce on this NixOS host

From the project root, with the PPSSPP checkout and its submodules initialized:

```bash
cd ppsspp
git submodule update --init --recursive
nix-shell -p cmake ninja sdl3 sdl3-ttf pkg-config ffmpeg libpng zlib libGL libGLU fontconfig expat libx11 curl wayland --run 'cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUNITTEST=ON -DHEADLESS=ON -DUSE_SYSTEM_FFMPEG=ON && cmake --build build --parallel 8'
build/PPSSPPUnitTest all
build/PPSSPPSDL --help
build/PPSSPPSDL --version
mkdir -p ../tests/workload/output
build/PPSSPPHeadless ../tests/workload/remaster_fixture.elf --graphics=software --timeout-wall=30 --screenshot-save=../tests/workload/output/reference.png
```

The `build/` directory is local, ignored build output. The CMake flags select a Debug build, enable the upstream C++ unit tests and headless executable, and link the system FFmpeg provided by the Nix shell. Build the fixture ELF first with the instructions in [its README](../tests/workload/README.md). PPSSPP's [building guide](../ppsspp/docs/building.md) recommends `./b.sh --debug`; the explicit CMake invocation above caps compilation at eight jobs and enables the two test executables in the same build.

The relevant resolved library versions were SDL3 3.4.10, SDL3_ttf 3.2.2, FFmpeg 8.1.2 (libavcodec 62.28.102), libpng 1.6.58, Fontconfig 2.17.1, X11 1.8.13, and Wayland 1.25.0. CMake found SDL3, SDL3_ttf, X11, Wayland, OpenGL, curl, PNG, and zlib. It used bundled GLEW, Snappy, and libzip where system development packages were absent.

## Checks and limits

- `PPSSPPSDL`, `PPSSPPHeadless`, and `PPSSPPUnitTest` linked successfully. Both emulator executables printed `v1.20.4-2207-gb9c5b28b8f`.
- `PPSSPPUnitTest all` exited 0 after `60 tests passed`, including the existing `TextureReplacer` test.
- The synthetic homebrew fixture exited 0 through PPSSPPHeadless's software renderer. It printed `REM_FIXTURE start frames=180` and `REM_FIXTURE done frames=180`. The 512×272 PNG visibly contains the expected four panels; a repeat run yielded the same SHA-256. Evidence is in [Gate 0 result](results/gate0-baseline.md).
- This TTY session has no `DISPLAY` or `WAYLAND_DISPLAY`; `glxinfo -B` fails with `unable to open display`. Vulkan device enumeration confirms the GPU is visible, but it does not prove PPSSPP's Vulkan or SDL window rendering on that GPU.
- The first configure attempt lacked X11 headers, and the first compile attempt lacked `GL/glu.h`. Adding `libx11` and `libGLU` to the temporary Nix shell resolved both; the final command above includes them. Missing GLEW, Snappy, and libzip notices are nonfatal because PPSSPP bundles those libraries.

Frame time, startup time, and peak memory measurements remain open under Milestone 0.4. A Vulkan run of the same fixture will be needed to establish the intended hardware replacement path baseline.
