# Release hunt tools: what exists, what was built, what is not built

Checked on disk 2026-10-15 against `RELEASE_BUGHUNT_PLAN.md` (which marked tools `[exists]` from an index, not from the tree). Category letters: `PROTOCOL.md`.

## Built for the protocol (this folder and `tools/release/`)

| Tool | What |
|---|---|
| `tools/release/findings.py` | Findings tracker: `new`, `list`, `counts`, `exit` (exit criteria 1, 2, 8 from the tracker), `--self-test`. Findings in `docs/release/findings/<version>/`. |
| `tools/release/run_gate.sh` | Serial P1 then P2 runner over existing tools, one pass/fail table per category, `--list` dry run, never builds, refuses P2 while a build or an Infinite app runs. `--self-test` checks the step table. |

## Exists and was verified (paths corrected)

All 44 `INFINITE_*TEST`/`*AUDIT`/`AUDIOSPIKE` fixtures the plan names exist in `src/`. Tool paths, where the plan was wrong:

| Plan said | Actual |
|---|---|
| `driver.sh` | `.claude/skills/run-infinite-hygiene/driver.sh` (`--fast`, `--full`, `--group`, `--auto`, `--skip-build`; honours `INFINITE_BIN`) |
| `known-test-failures*.txt`, `audio-param-sweep-expected.txt` | same folder as the driver |
| `census_baseline.json` | `tests/headless/census_baseline.json` (and `describe_join_baseline.json` beside it) |
| `latency_probe.py`, `load_bench.py`, `project_bench.py`, `app_bench.py`, `av_bench.py`, `render_bench.py`, `blender_bench.py`, `host_audit.py`, `synth_probe.py`, `plugin_probe.py` | `tools/re-survey/` (not `scripts/bench/`) |
| `run_all.sh`, `ab.sh`, `base.sh` | `scripts/bench/` (correct); baselines in `bench/baselines/m2-8gb*.jsonl` |
| `node_census.py` | `scripts/node_census.py` |
| `ratchet.py`, `lint_icons.py`, `motion_lint.py`, `contrast.py`, gallery (`shoot.py`, `sheet.py`, `sizes.sh`, `widen.py`) | `tools/design/` and `tools/design/gallery/` |
| gallery baseline `baseline-2026-10-09` | `~/Infinite/test-output/node-gallery/` (outside the repo) |
| `USER_TEST_CHECKLIST.md` | **not in the repo** (decision in `v0.6.md`) |

Present as the plan said: `tools/render-check.py`, `tools/determinism-check.py`, `tools/i18n/lint.py`, `tools/brand/brand_lint.py`, `tools/quality/quality.py`, `tools/re-survey/data_ledger.py`, `scripts/{headless_smoke.sh,release_version_gate.py,selfupdate_swap_test.sh,audit_param_truth.py,audit_node_params.py}`, `tools/linux/{xvfb-harness,shots,distro-smoke}.sh`, `docs/WINDOWS_VERIFICATION.md`, `tools/spatial`, the 13 sweep drivers and the three `check.py` static checks, `pillar-parity-audit/static-checks.sh`.

Known wart: the sweep drivers hard-code `build/Infinite.app` (`scripts/sweep_runner.sh`, lines 33-34) and do not read `INFINITE_BIN`. A two-line change (`BIN="${INFINITE_BIN:-...}"`) would let the runner point every P2 step at one candidate binary. Not done: it edits shared code outside this task.

## Not built (effort in focused working days; Mac build and run time extra)

| Tool | Closes | Effort | Cost note |
|---|---|---|---|
| Settings migration test (start from the previous release's settings file) | B settings survive upgrade | 1 | needs the settings file layout and a headless relaunch path |
| `legacy_diff.py` (render a corpus of old patches in the old and new build, diff with `render-check` tolerances) | C old-patch fidelity: the top-yield gap for v0.6 | 1.5-2 | needs an old-version patch corpus; base build already scripted (`base.sh`); two builds, serial |
| `fuzz_inf.py` (mutate the corpus, load headless, expect error not crash) | C hostile files | 0.5-1 | uses `--validate`; 10k mutations is minutes of CPU |
| Extreme-param fuzz at the master (min/max/random every param while audio runs, assert finite and bounded) | D NaN and full-scale | 1.5 | new C++ fixture in the app, so one build slot |
| `sanitize.sh` (ASan/UBSan, TSan for audio/UI) | A | 2 | separate build dir, a full extra build on the slow Mac, third-party suppressions |
| Autosave kill -9 and atomic-save fault injection | B | 1 | scripted, no build |
| OSC malformed-packet fuzz | K | 0.5 | python UDP to 127.0.0.1:9100 |
| Forced-crash report check | A | 0.5 | |
| Leak check (create/delete 1000 nodes, `leaks`) | G | 0.5 | quiet machine |
| Golden `.inf` patches for new 3D/visual nodes in `tests/render` | F | 0.5 per 2 nodes | the owner approves each reference image |
| Node-screenshot judge sheet (pre-filled per-node checklist, dimming re-shoot per selector value) | I | 1-1.5 | extends `tools/design/gallery` |
| Node manual vs `--describe` join check | L | 0.5 | |
| Unicode-path and format-fixture lists | C | 0.5 | |

Recommended order for the owner to approve: `legacy_diff.py`, extreme-param fuzz, `fuzz_inf.py`, then the scripted B/K items, `sanitize.sh` last.
