---
name: ci-preflight
description: "Run the checks CI will run before pushing, name any CI failure from a catalog, and gate releases on a green run for the exact commit. Use before every push to bugfix/ or feature/, whenever a Build job is red, before `driver.sh release`, and when adding a test, a driver entry, a baseline line or a CI step."
---

## When to use (full scope)

Every push that CI will build, every red CI job, every release. The point is to find on the Mac in
minutes what CI would otherwise find in 30 to 60 minutes. Read `docs/ci/README.md` for the model.

## Before pushing

```bash
python3 tools/ci/preflight.py            # static drift + the Mac-runnable CI steps (needs build/ for tests)
python3 tools/ci/preflight.py --fast     # static only, seconds
python3 tools/ci/preflight.py --release-app build-dist/Infinite.app   # before shipping an app
```

Fix every FAIL before pushing. Static checks are generic: they compare the driver and CI test lists
with the tests in `src/`, the baseline with the driver, translations with strings, and parse every
workflow. The mirror runs the real `run:` steps of the `design-ratchet` and `macos` jobs.

## When a CI job is red

```bash
python3 tools/ci/classify.py <run-id | branch> --record
```

It prints the cause, the fix and the local check for each failed job. Then:

- **Known signature** -> apply the fix in one push, fixing siblings too.
- **UNKNOWN (exit 3)** -> read the log once, understand it, add a row to `tools/ci/signatures.json`
  and, if a local check could have caught it, add that check to `preflight.py` (and a `--self-test` case).
- **"failed here, sibling jobs passed"** -> a test depending on global state, order or UB. Run the
  group alone (`INFINITE_<NAME>=1 <binary>`), pin the state in the test.
- **toolchain (MSVC ICE)** -> `ci-failure-triage` for the `/Od` recipe.

Never fix one job per push when several are red: classify the whole run first.

## Releasing

1. Push the branch; wait for every Build job green.
2. Fast-forward `main` to that same commit and push. Commit nothing else.
3. `.claude/skills/ship-infinite/driver.sh release`. It refuses without a green Build run for HEAD,
   refuses a debug app, then `release-windows <tag>` / `release-linux <tag>`.
4. `.claude/skills/ship-infinite/driver.sh verify-assets <tag>` must pass (DMG, both Windows zips, AppImage).
5. Deploy to the Desktop only with `scripts/deploy_desktop.sh <release .app>`; it rejects debug builds.

## Anti-patterns

- Using `main` as the test bed.
- Committing (even docs) after the green run.
- Treating a red job as flaky without comparing sibling jobs and previous runs.
- Adding a check to catch one failure instead of a generic check for its class.
- Building with a bare `-j` (use `ibuild.py`, max 4 jobs on the 8 GB Mac).
