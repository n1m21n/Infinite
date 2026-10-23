---
name: build-discipline
description: "When, why and how to build Infinite on the 8 GB M2 without hanging it: build gate, -j sizing, dev/ASan/release/Linux (OrbStack) modes, semi-builds, concurrent sessions. Use before ANY cmake/make/xcodebuild, sanitizer run, container build, or when the Mac is slow."
---

## When to use (full scope)

Before running any build: cmake, make, `cmake --build`, driver.sh without `--skip-build`, a sanitizer (ASan/UBSan/TSan) build, a Debug build, a Linux/OrbStack container build, a release build. Also when the Mac hangs, swaps or fans spin; when more than one session is working; when deciding "do I need to rebuild?"; when a new worktree needs a first build.

## Why this exists

The machine: **MacBook M2, 8 cores, 8 GB RAM**, 10 GB swap. Incident 2026-10-23: another session ran `make -j10` (about 10 clang -O3 at 100-170 MB each, plus a universal arm64+x86_64 build doubling the work) -> load 32, 9 GB swap used, UI frozen. A build is the heaviest thing this Mac does; 8 GB cannot host two.

## The one entry point

```bash
scripts/ibuild.py status            # RAM, swap, load, lock holder, recommended -j
scripts/ibuild.py plan  [--mode M]  # N files stale? "up to date" means SKIP
scripts/ibuild.py build [--mode M] [--target T]
scripts/ibuild.py obj src/x/Y.cpp   # syntax-check ONE file (semi-build)
scripts/ibuild.py linux build|test  # OrbStack on demand, stopped after
```

It takes a machine-wide lock (`/tmp/infinite-build.lock`, shared by every worktree and session, so builds queue), sizes `-j` from free RAM / swap / load, runs at `nice 10`, skips when nothing is stale, uses ccache if installed, and keeps the log at `/tmp/ibuild_<mode>.log`. **Never type a raw `make -jN` or `cmake --build -jN`.** If you must (CI parity), `-j2` max and take the lock first.

## Rule 1 - Do I need to build at all? (cheapest first)

| Question | Answer | Cost |
|---|---|---|
| Docs, skills, scripts, tools/*.py, JSON, website changed only | No build | 0 |
| Python tool or a roadmap/doc change | Run the tool | 0 |
| Did my C++ edit even compile? | `ibuild obj <file>` (syntax only) | seconds |
| Header edit | `plan` first: a widely-included header means hundreds of files; prefer moving code to the .cpp | check |
| Need to see/hear it, or run a self-test | `build --mode dev` then `driver.sh --skip-build --fast` | minutes |
| Hunting memory/UB bug | `--mode asan` (see Rule 3) | slow, rare |
| Shipping, CI parity, packaging | `--mode release` (universal) | slowest, only on release asks |
| Linux behaviour | `ibuild linux` | VM + build |

If `plan` says up to date, do not build. Do not build "to be safe" after a no-C++ change. Do not rebuild after only running tests.

## Rule 2 - What to build (the semi-build ladder)

1. `obj` one file -> 2. `build --target <one target>` (a test or tool target, not Infinite) -> 3. `build` the app target -> 4. release.
Stop at the lowest rung that answers the question. Dev mode is **Debug + arm64 only** (`build-dev/`): half the compile work of the universal `build/` and no -O3 cost. `build/` (universal Release) exists for shipping, not for iteration.

## Rule 3 - Mode table

| Mode | Dir | Flags | -j cap | Use for |
|---|---|---|---|---|
| dev | build-dev | Debug, arm64 | 4 | daily iteration, driver.sh |
| asan | build-asan | Debug, arm64, `INFINITE_SANITIZE=address,undefined`, HARDEN on | 3 | crashes, heap/UB bugs; ~2x RAM per compile, run alone |
| tsan | (not in ibuild) | `INFINITE_SANITIZE=thread` - not combinable with asan | 2 | audio-thread races; ask first |
| release | build | Release, universal | 3 | packaging / release only |
| linux | container | tools/linux/local.sh | 2 | Platform:: work, CI parity (arm64 container; CI is x86_64) |

ASan/UBSan: build in `build-asan`, run the failing self-test only (not the full matrix), then **delete the sanitizer dir when the bug is closed** (it is the biggest build dir). Never ship it.

## Rule 4 - Concurrent sessions

- Exactly **one build at a time on the whole Mac**. The lock enforces it; if `ibuild` says it is waiting, wait - do not bypass.
- Before starting a long build run `ibuild status`. Swap > 4 GB or load > 12 = do not start; wait or ask the owner to stop something.
- Do not run a build while another session benches, renders a film or runs the semi-brain nightly (see bench-quiet rule).
- A session that only reads/edits code must not build. Let the building session finish, then pull its result.
- Run builds in the background (the tool call has a 10 minute ceiling) and check progress soon after - a build that has not printed in 3 minutes is probably swapping.
- Worktrees: every new worktree is a full cold build. Prefer one worktree per concurrent edit lane, build only in one, and install ccache (below).

## Rule 5 - OrbStack / Linux

OrbStack is a VM and holds RAM while up. `ibuild linux` starts it only for the job and stops it after if it was down. Do not leave it running across sessions. The ccache volume (`infinite-ccache`) is inside the container and survives; keep it.

## Rule 6 - Make builds cheaper (one-time, ask first)

- **ccache** (installed 2026-10-23; `brew install` is broken on this Mac - its extract sandbox is killed - so it was unpacked from the cached bottle and relocated by hand; if `ccache` ever stops launching, re-run `brew fetch ccache` and redo that relocation, do not use `brew link openssl@4`, it unlinks openssl@3). ibuild uses it automatically on a fresh configure (existing dirs: re-run cmake with `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`). Biggest win for worktrees and clean rebuilds. Cap it: `ccache -M 5G`.
- **ninja** (`brew install ninja`) schedules better than Makefiles, but needs a fresh configure; do it only with the owner's yes.
- Disk: remove `build-dev`/`build-asan`/worktree builds once merged (keep-disk-footprint-low).

## Rule 7 - Hang recovery

`ibuild status` -> find the build (`ps`), stop the parent `make` (kill the make, its clang children die with it), delete a stale `/tmp/infinite-build.lock` only if its pid is dead (the tool does this itself). Then rebuild with a lower `--jobs`.
