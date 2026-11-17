# Pre-release CI failure map (v2.0.0)

Source: the last 100 `Build` runs on GitHub Actions (13 success, 42 failure,
44 cancelled, 1 action_required), read with `gh run list` / `gh run view`
on 2026-11-17. Last green Build: run 44049380418 (commit a468a406), 477
commits behind release/2.0-preci HEAD. Counts are failed jobs per job name.

## Failure table

| Job / step | Count | First / last seen | Signature | Status on HEAD |
|---|---|---|---|---|
| Linux clang Build | 19 | Oct / Nov | `optional` not declared (AppShared.h) | Fixed: `#include <optional>` present (fix 4d56047b) |
| Linux sanitizers Build | 18 | Oct / Nov | same as above | Fixed |
| Linux gcc Build | 15 | Oct / Nov | same as above | Fixed |
| Linux AppImage Build | 15 | Oct / Nov | same as above | Fixed |
| Windows x64 Build | 11 | Oct / Nov | same as above | Fixed |
| Windows ARM64 Build | 10 | Oct / Nov | same as above | Fixed |
| Design ratchet: Drift checks | 14 | recent | i18n and terminal-parity drift | NOT fixed. preflight.py --fast fails i18n and parity locally. Owned by other lanes. |
| Linux clang Headless CLI smoke (Xvfb) | 11 | recent | terminal parity | NOT fixed. Owned by other lanes. |
| Linux clang Render goldens (Xvfb) | 13 | Nov 9 last | render3d golden diff | Fixed by Nov 9 |
| Linux clang / gcc Xvfb tests and screenshot smoke | 9 / 9 | Nov 15-16 | BYPASSSWEEPTEST, VIEW3DTEST, GPUPOINTSTEST; GENSAMPLETEST crash on Nov 15-16 | GENSAMPLETEST open (see below) |
| Windows x64 Headless self-tests | 6 | last Nov 15 | 10-minute timeout at INFINITE_RECEXPORTTEST | Open (see below) |
| Design ratchet: No narrow file opens | 6 | earlier | narrow-open calls | Fixed (check_utf8_paths passes) |
| Design ratchet: Literal colours | 5 | earlier | literal colour values | Fixed (ratchet.py exit 0) |
| Linux AppImage: Package AppImage | 6 | Nov 2-3, later green | linuxdeploy SHA256 mismatch | Open to verify (see below) |
| One-offs | few | various | LIBJACK (gcc plugin validation); onnxruntime HTTP 504 (macOS configure); EM_SETCUEBANNER and S_ISREG (Windows plugin bundles); curl exit 22 (Windows plugin validation) | External or transient. Not reproduced here. |

## Recurring offenders

1. **Terminal parity and i18n drift** (Design ratchet Drift, Linux clang Headless CLI smoke). These will fail the v2.0.0 push unless the other lanes land their fixes. Not touched in this lane.
2. **Windows Headless self-tests hang at RECEXPORTTEST.** `src/app/selftest/AvRecTests.cpp` (RunRecExportTest, around lines 415-580) sets `kMaxTotalWait` to 90 s for frames larger than 320x240, else 45 s (around line 524). `.github/scripts/headless-tests.sh` skips RECEXPORTTEST only on Darwin. The job hits the 10-minute `timeout-minutes` on the Windows src step. The likely cause is a wait outside the bounded loop. Not diagnosed: this needs the job log, and proof needs a Windows CI run. Proposed remedy: a per-check timeout in `headless-tests.sh`. Not committed.
3. **GENSAMPLETEST crash** (Linux clang and gcc, Nov 15-16). Needs the artifact log to diagnose. Not fetched.
4. **AppImage linuxdeploy SHA256.** Mismatch on Nov 2-3, green since. I could not locate the pin in `tools/linux/package-appimage.sh` during this pass (the grep matched only comments), so this is unverified.

## Pre-push checks (run locally, no app build)

| Check | Command | Result this pass |
|---|---|---|
| Ratchet | `python3 tools/ci/ratchet.py` | exit 0 |
| Preflight fast | `python3 tools/ci/preflight.py --fast` | FAILED 2 of 7: i18n, parity (not fixed here) |
| Keys gate, menu lints, empty lint, UTF-8 paths | per `ci-preflight` skill | all pass |
| Agent docs | `python3 tools/gen-agent-docs.py --check` | exit 0 |
| Patch skill | `tools/gen-patch-skill.py` | not run: needs build/Infinite.app (no app builds in this lane) |

Also run before pushing: the `ui` group of the pre-push hook
(`scripts/git-hooks/pre-push`), which builds the base worktree. Not run here.

## Release-day workflow order

1. Push to `bugfix/<slug>` or open a PR. **Build does not run on `release/*` pushes** (build.yml triggers: main, feature/**, bugfix/**, PRs, dispatch). Use one of these to get a green Build for the exact HEAD.
2. Confirm a green Build for the exact HEAD (`driver.sh release` refuses otherwise).
3. Tag and create the release (`gh release create`) on main, with curated notes.
4. Build workflow artifacts: Windows x64/ARM64 zips and Linux AppImage.
5. `sign-windows.yml` (manual, SignPath).
6. `attach-release-assets.yml` (manual; run_id and tag), then `driver.sh verify-assets <tag>`.
7. `update-package-managers.yml` runs on release published (Homebrew tap, winget; skipped without secrets).
8. `publish-packages.yml` (manual; npm and PyPI).
9. `deploy-pages.yml` runs on push to main.

Nothing should be committed between the green run and the release.
