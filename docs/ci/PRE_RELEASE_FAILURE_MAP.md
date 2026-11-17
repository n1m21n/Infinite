# Pre-release CI failure map (v2.0.0)

Source: the last 100 `Build` runs on GitHub Actions (13 success, 42 failure,
44 cancelled, 1 action_required), read with `gh run list` / `gh run view`.
Last green Build: run 44049380418 (commit a468a406, 2026-11-17; every Linux,
Windows and macOS job green, including GENSAMPLETEST). HEAD is release/2.0-preci
after merging release/2.0 (e30eb752) plus this lane's commits.

## Failure table

| Job / step | Count | Last seen | Signature | Status on HEAD |
|---|---|---|---|---|
| Linux clang / sanitizers / gcc / AppImage, Windows x64 and ARM64 Build | 19 / 18 / 15 / 15 / 11 / 10 | Oct | `optional` not declared (AppShared.h) | Fixed (`#include <optional>`, fix 4d56047b) |
| Design ratchet: Drift checks (i18n) | 14 | Nov | 9 missing keys per locale (es, de, zh, ja, ru), 251 unused | NOT fixed. i18n lane owns it. |
| Linux clang Headless CLI smoke (Xvfb) | 11 | Nov | terminal parity | Parity static check passes on HEAD. Runtime smoke not run here (no app build). |
| Linux clang Render goldens (Xvfb) | 13 | Nov 9 | render3d golden diff | Fixed by Nov 9 |
| Linux clang / gcc GENSAMPLETEST | 4 (Nov 15-16) | Nov 16 | taste-draw threshold hard-coded at 35 of 1000 draws; 46 styles give a uniform 21; log: `landed 34 / skipped 17 ... FAIL` | Fixed by 94c156e8 (Nov 16), in HEAD and in the green run 44049380418 (gcc: `GENSAMPLETEST PASS`). |
| Windows x64 Headless self-tests | 6 (Oct 31 - Nov 15) | Nov 15 | 10-minute step timeout after `== INFINITE_RECEXPORTTEST`, no output | NOT proven fixed. Watchdog and phase markers added in this lane (see Changes); root cause needs a Windows run. |
| Linux AppImage: Package AppImage | 6 | Nov 3, Nov 8 | Nov 3: linuxdeploy SHA256 mismatch (rolling `continuous` asset republished). Nov 8: `curl: (22) ... 504` on the download. | Pin fixed (versioned tag, digest matches GitHub). Retries added in this lane for the 504. |
| One-offs | few | various | LIBJACK (gcc plugin validation); onnxruntime HTTP 504 (macOS configure); EM_SETCUEBANNER, S_ISREG (Windows plugin bundles); curl 22 (Windows plugin validation) | External or transient. Not reproduced. |

## Recurring offenders

1. **i18n drift** (Design ratchet). `tools/ci/preflight.py --fast` on the merged
   branch: i18n FAIL (9 missing, 251 unused in each of es, de, zh, ja, ru), parity
   PASS, drift/workflows/nodeshots/manuals/steps PASS. This will fail the first push
   until the i18n lane lands. Not fixed here (out of scope).
2. **Windows RECEXPORTTEST hang.** Log evidence (run 43732786970, job 132868917729,
   Nov 15): `== INFINITE_RECSYNCTEST` passed at 16:38:49.43, `== INFINITE_RECEXPORTTEST`
   printed at 16:38:49.53, and then nothing until the step was killed at 16:48:49.
   A green run (44049380418) finishes the same check in 2.5 s. The wedge is not in the
   existing bounded loop: `kMaxTotalWait` (90 s or 45 s) only guards the pending-frame
   spin in `RunRecExportTest`. Unbounded waits sit outside it: `RecorderStop` joins the
   worker (`src/platform/win/MediaWin.cpp`, around line 2456), and the worker blocks in
   `IMFSinkWriter::WriteSample` with no timeout. The output of the check was captured
   with `$(...)`, so no phase line was ever visible. The recorder code itself has not
   changed since Oct 22, so the trigger is likely the runner (an encoder stall or
   a sink-writer deadlock), not a recent edit. This is a hypothesis: the logs do not
   say which call blocked.
3. **GENSAMPLETEST** (Nov 15-16). Diagnosed from the artifact
   `test-logs/infinite_test_GENSAMPLETEST.log` (gcc, run 43793834473 and 43732786970):
   the only failing check was the taste-draw threshold. Fixed by 94c156e8. Caveat: the
   fix changes the threshold from 35 to a uniform baseline of 21 at 46 styles, so it is
   a looser absolute bar. The check still requires landed > uniform, skipped > 0 and
   skipped * 10 < landed * 6. Reviewer should confirm that loosening is intended.
4. **AppImage download.** See the table. Pins are versioned tags whose SHA256 matches
   GitHub's asset digest (linuxdeploy `c20cd71e...`, appimagetool `ed4ce84f...`), so a
   rolling republish can no longer break the Nov 3 way. The Nov 8 504 was transient and
   is now retried.

## Changes in this lane

- `tools/linux/package-appimage.sh`: both tool downloads use
  `--retry 4 --retry-delay 10 --retry-all-errors`. The SHA256 check is unchanged.
- `.github/scripts/headless-tests.sh`: `check_verdict` runs each verdict check through
  `run_bounded` with a 300 s watchdog, writes output to a file, and prints its tail on
  failure. Tested locally with a fake binary that hangs (it failed in 2 s with the
  tail shown, and a passing binary still passed). Not run on Windows, so the `kill -9`
  path under Git Bash is unproven.
- `src/app/selftest/AvRecTests.cpp`: three flushed `[phase]` markers in
  `RunRecExportTest` (recorder started, render loop done, decoding audio) so a killed
  run names its phase. Print-only change; no assertion touched. Not compiled here
  (no app builds in this lane).

## Pre-push checks (run locally, no app build)

| Check | Command | Result this pass |
|---|---|---|
| Design ratchet | `python3 tools/design/ratchet.py` | exit 0 |
| Preflight fast | `python3 tools/ci/preflight.py --fast` | FAILED 1 of 7 (i18n). Parity now PASSES. |
| Agent docs | `python3 tools/gen-agent-docs.py --check` | exit 0 (earlier pass) |
| Patch skill | `tools/gen-patch-skill.py` | not run: needs build/Infinite.app |
| Verdict watchdog | fake-binary test of `run_bounded` / `check_verdict` | pass (hang fails in 2 s, pass path passes) |
| Shell syntax | `bash -n` on the edited scripts | pass |

Also run before pushing: the `ui` group of the pre-push hook
(`scripts/git-hooks/pre-push`), which builds the base worktree. Not run here.

## Release-day workflow order

1. Push to `bugfix/<slug>` or open a PR. **Build does not run on `release/*` pushes**
   (build.yml triggers: main, feature/**, bugfix/**, PRs, dispatch).
2. Confirm a green Build for the exact HEAD (`driver.sh release` refuses otherwise).
3. Tag and create the release (`gh release create`) on main, with curated notes.
4. Build workflow artifacts: Windows x64/ARM64 zips and Linux AppImage.
5. `sign-windows.yml` (manual, SignPath).
6. `attach-release-assets.yml` (manual; run_id and tag), then `driver.sh verify-assets <tag>`.
7. `update-package-managers.yml` runs on release published (Homebrew tap, winget; skipped without secrets).
8. `publish-packages.yml` (manual; npm and PyPI).
9. `deploy-pages.yml` runs on push to main.

Nothing should be committed between the green run and the release.

## What to do if the first push fails

Run these from the worktree. Replace `<branch>`, `<run-id>` and `<job-id>`. The
owner decides re-runs. This lane never re-runs or cancels a workflow.

```bash
# 1. Find the run for the pushed HEAD
gh run list --branch <branch> --limit 5 --json databaseId,headSha,conclusion,name,createdAt
gh run view <run-id> --json jobs --jq '.jobs[] | select(.conclusion=="failure") | "\(.databaseId) \(.name)"'

# 2. Name each failure against the known signatures (no logs kept on disk)
python3 tools/ci/classify.py <run-id>

# 3. Pull the failing job's full log and find the first failing line
gh run view <run-id> --log --job <job-id> > "$TMPDIR/job-<job-id>.log"
grep -n -a -E "\[FAIL\]|\[CRASH\]|FAIL \(|##\[error\]|Process completed with exit code" "$TMPDIR/job-<job-id>.log" | head -40

# 4. Headless verdict checks: the phase that was running when a check hung or crashed
grep -n -a -E "^== INFINITE_|\[phase\]" "$TMPDIR/job-<job-id>.log" | tail -20

# 5. Test artifacts (the per-test logs the harness uploads)
gh run download <run-id> -n Infinite-linux-clang-artifacts -D "$TMPDIR/art-<run-id>"
#    (or -n Infinite-linux-gcc-artifacts; the Windows runs upload plugin-logs-windows)

# 6. Compare with the last green run before changing anything
gh run view 44049380418 --log --job 133923523900 | grep -a "\[pass\].*<TESTNAME>"

# 7. If a SHA256 or download fails, check the pin against GitHub's digest (read-only)
gh api repos/linuxdeploy/linuxdeploy/releases/tags/1-alpha-20251107-1 --jq '.assets[] | select(.name=="linuxdeploy-x86_64.AppImage") | .digest'
```

Then classify the failure: a known row in `tools/ci/signatures.json` gets the fix
listed in this doc. A new one gets a row before anyone pushes a fix (see
`ci-failure-triage`). Push all fixes for one run together, not one job per push.
