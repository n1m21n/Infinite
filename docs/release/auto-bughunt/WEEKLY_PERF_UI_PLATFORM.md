# Playbook (used by daily slots 19-20): not a separate schedule

Read COMMON.md first. Quiet machine only: bench numbers from a busy machine are worthless, so skip and say so.

1. Perf (G, H): use the Release build (rebuild via scripts/ibuild.py if the commit differs). `scripts/bench/run_all.sh`, compare to bench/baselines/m2-8gb*.jsonl using the tools' own pass/fail rule (see internal/plans/perf/README.md and skill run-infinite-hygiene). Report only regressions past the tool's threshold; never your own. Also: startup time, idle CPU, frame pacing, input-to-sound latency (`tools/re-survey/latency_probe.py`, `app_bench.py`, `av_bench.py`).
2. Leaks (G): create/delete 1000 nodes and run `leaks` on the process, if scripted; else "not run".
3. UI (I): `tools/design/gallery` (shoot.py, sheet.py): per-node screenshots, then compare against ~/Infinite/test-output/node-gallery/baseline-*; check docs/release/PROTOCOL.md section 8 checklist per node (clipped text, overlapping widgets, missing hover state, dimmed controls). Panels, dark/light theme, small window. Attach the sheet path in the issue; do not paste images of the user's data.
4. Design lints: ratchet, lint_icons, motion_lint, contrast, i18n lint (P1 gate rows); any rise vs ratchet is a finding.
5. Platform (J): `gh run list` for Windows and Linux builds; `bash .claude/skills/pillar-parity-audit/static-checks.sh`; `tools/linux/xvfb-harness.sh` / `shots.sh` if Linux tooling is available on this Mac (else "not run"). Windows real-hardware checks are manual (monthly owner issue).
6. Sweeps: run the 14 skills ending in -sweep via their drivers, one at a time, only those whose static `check.py` is red or whose area changed this week (`git diff --stat origin/main@{7.days.ago}..origin/main`).
7. Sound (D): render the shipped example patches headless and check peak, DC offset, silence, clipping with the existing render check (`tools/render-check.py`, `tools/determinism-check.py`); a human listening pass is the monthly owner issue.
