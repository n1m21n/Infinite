# Playbook (used by daily slots 14-18): not a separate schedule

Read COMMON.md first. Run only when nothing else runs (see Machine safety). Each step has a cap; record each as ran / skipped / not run.

0. Binary: use the worktree build dir /Users/namansoni/Infinite/worktrees/bughunt/build-hunt. If its commit differs from origin/main, rebuild via `python3 scripts/ibuild.py` (ASan+UBSan: `-DINFINITE_SANITIZE=address,undefined`). If the build fails, file ONE issue "[build] main fails to build" with the error and stop heavy steps.
1. `INFINITE_PARAMFUZZ=1` against the binary (every audio node, in process, minutes) under `perl -e 'alarm 1500; exec @ARGV'`. NAN/HOT/SLOW rows are findings. (A, D)
2. `python3 tools/bughunt/frame_fuzz.py BIN OUT.log --budget 30 --home <scratch dir>`: every picture/3D node. NANMESH/BIGMESH/BIGTEX/GLERR/HANG/OOM/CRASH rows. (E, F)
3. `python3 tools/bughunt/param_fuzz.py BIN OUT --kind all --budget 20`; resume, don't restart. Then `python3 tools/bughunt/triage.py OUT --stubs DIR` to group BAD rows into one finding per distinct bug.
4. Decoder fuzz: `tools/bughunt/fuzz/build.sh`, then 10 min per target. (C)
5. `python3 tools/release/patch_variants.py` (mutates community and example patches through the headless engine; needs idle machine). (B, C)
6. Headless selftests under the sanitizer build: `INFINITE_DSPTEST=1`, `INFINITE_NOTEAUDIT=1` and the other INFINITE_*TEST/*AUDIT fixtures listed in docs/release/TOOLS.md; use `.claude/skills/run-infinite-hygiene/driver.sh --fast` for the routing. Script under `script -q` so output survives a timeout. (A, B, D)
7. `tools/release/run_gate.sh --phase P2` (serial sweeps) only if steps 1-6 were clean enough and time remains. (A-F)
8. TSan (`-DINFINITE_SANITIZE=thread`) build and `tools/bughunt/tsan_selftests.sh`, only every 4th week (track in rotation.json "tsan_week"). (A)
9. Persistence faults: autosave under `kill -9`, atomic-save interruption, load of truncated/corrupt .inf via `--validate`, non-ASCII paths. Use existing tools; if none exists for a case say "not run". (B, C)
10. Known flake: PLUGINDRAGTEST may fail intermittently with an empty id in driver.sh; report once, don't re-file.

For each BAD row apply bug-blast-radius, then file one issue per root cause.
