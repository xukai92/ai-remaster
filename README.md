# AI Remaster

An emulator-agnostic texture enhancement engine, with PPSSPP as the first integration target. See [SPEC.md](SPEC.md) for the MVP and [PLAN.md](PLAN.md) for the work sequence.

## Repository layout

This repository holds the project plan, research notes, and reusable `remaster-core` code. The local `ppsspp/` directory is a separate Git checkout of [upstream PPSSPP](https://github.com/hrydgard/ppsspp), excluded from this repository. PPSSPP adapter changes will be committed in that checkout. A GitHub fork can be added as its `origin` when there is a branch ready to publish; the official repository should remain available as `upstream`.

This keeps the core's history independent of PPSSPP while allowing the adapter to follow PPSSPP's own source tree and build system. The build integration between the two repositories will be decided after inspecting the current texture pipeline, as Milestone 0 requires.

## First checkout

```sh
git clone --recurse-submodules https://github.com/hrydgard/ppsspp.git ppsspp
```

If `ppsspp/` already exists, update its submodules with:

```sh
git -C ppsspp submodule update --init --recursive
```

The first implementation task is the PPSSPP baseline build and texture pipeline reconnaissance described at the end of [PLAN.md](PLAN.md). Do not commit game assets or model weights without checking their redistribution terms.
