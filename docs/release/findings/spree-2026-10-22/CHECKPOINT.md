# Spree checkpoint, 2026-10-22

Resume from here. All 32 spree fix branches were merged into local main on 2026-10-22 with no conflicts. Nothing is pushed. Not merged:

- `bugfix/slider-stack-pointer`: a duplicate fix.
- `bugfix/mpc-dotted-switched-links`: its commits are website/library work only.
- `bugfix/text-node-move`: older than the spree, and not part of it.

The raw run logs are in `docs/release/runs/spree-2026-10-22` (185 MB), which is not committed.

## Where things stand

- **Findings:** 37 filed (B-A-001 … B-K-019). 34 are fixed on branches. 3 are open: B-G-001 and B-H-001 (P3, need a quiet-Mac re-run) and B-J-001 (the Windows headless hang, P5).
- **Int-param fuzz** (1025 cases, every node type, all four extremes): complete. Every real BAD is fixed.
- **Last verification:** the batch B-K-014..019 was re-fuzzed on an ASan+UBSan build of the merged fixes, with zero reports. The throwaway `verify/spree-batch-2` branch is deleted.
- **Rows still showing BAD in the re-run, all explained:**
  - Sketch and Sketch 3D: fixed by B-K-013 on its own branch, which wasn't in that merge.
  - Difference, Intersect, Union and Join Geometry: timeouts. These are slow booleans under ASan, not hangs.
  - Mixer: a negative channel count now clamps to 0, so the output is silent instead of hot.

## Decisions waiting on the owner

1. **Push:** whether to push main.
2. **Duplicate:** done. param-widget-dangling-local is merged, and B-K-018 is closed as a duplicate of B-A-004.
3. **Go or no-go on P3 and P5:** B-G-001 and B-H-001 (benches) and B-J-001 (Windows).
4. **The old plan file:** delete claude-context/v06-release/RELEASE_BUGHUNT_PLAN.md or mark it superseded.
5. **Tooling proposals.** All are test infrastructure and go into the repo, so they need an OK:
   - a. libFuzzer harnesses for the file decoders (AIFF/FLAC/WAV, HAP, glTF, PLY, Patch::Load). B-K-003..007 are exactly the class of bug these find.
   - b. A local ThreadSanitizer build, plus headless audio runs, for audio/GUI thread races. Then later a TSan CI job.
   - c. stress_kit `random_patch`: emit random `mod` lines and feedback. B-K-018 was only reachable through modulation.
   - d. An audio-chain invariant driver: no NaN, peak at most the limiter ceiling when the limiter is last, decay after note-off with each node's tail allowed for.
   - e. Leak detection in CI with a suppressions file instead of `detect_leaks=0`. Add macOS to the sanitizer job.

## Next steps when resuming (in order)

1. `--kind float` fuzz:
   `python3 $SP/intfuzz.py worktrees/spree-fixes/build-asan/Infinite.app/Contents/MacOS/Infinite <newdir> --kind float`
   build-asan currently holds the batch-2 merge. Rebuild it from a branch that has the fixes you want covered. The script lives in the session scratchpad; copy it somewhere permanent if the scratchpad is gone.
2. `--kind text` fuzz.
3. Re-time Join Geometry, Revolve 100000 and Remove Background alone on a release build, to confirm they are slow and not hangs.
4. B-H-001: three quiet FPSTEST runs.
5. Final report: branch → finding table, merge order.

## Rules in force

- One `bugfix/` branch per fix. Run a blast-radius scan before each fix.
- One build at a time. App-launching tests run serially. Kill specific PIDs only.
- No push or merge without the owner's word. No new nodes, modes or panels without approval.
