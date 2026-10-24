# CI: the problem and solution pipeline

One page for how a CI failure is prevented, named and learned from. Owner skill: `ci-preflight`.

## Why CI kept failing (v0.6.5 evidence)

Every red job in that release was a real defect that nothing checked on the Mac. Almost all were
**drift**: a source of truth and its copy disagreeing.

| Class | v0.6.5 example | Caught locally by |
|---|---|---|
| stale-artifact | render3d golden stale since Oct 1; `BLOCKSETTEST` still in the driver after the test was deleted; a baseline line for a test that passes | `preflight.py drift`, mirror (Render goldens) |
| state-dependence | Feedback 3D tests relied on whatever clock state earlier tests left; passed on 3 jobs, failed on clang | run the failing group alone; the classifier flags "failed here, sibling jobs passed" |
| lint | 181 em dashes in the manual sources; missing translations | `preflight.py manuals`, `i18n` |
| toolchain | MSVC `CL.exe` internal compiler error | none on a Mac: `/Od` recipe, batch the siblings |
| external | SignPath app token, GitHub 5xx, a download timing out | nothing in code; steps are non-blocking |
| release | a debug build on the Desktop | `check_release_binary.py`, now enforced in deploy and release |

## The flow

```
edit -> tools/ci/preflight.py        (static drift + the CI steps that run on a Mac, minutes)
     -> push bugfix/<slug>           (CI runs the full matrix on the branch; main is never the test bed)
     -> CI red?  tools/ci/classify.py <run>   names the cause, the fix, and how to catch it locally
                                      UNKNOWN -> read the log, add a row to tools/ci/signatures.json
     -> CI green on commit X
     -> fast-forward main to X        (same SHA, so the branch run is the proof; no second matrix)
     -> ship-infinite driver.sh release   (refuses unless Build is green for HEAD; checks the app is a
                                           release build; verify-assets fails if a platform is missing)
```

Rule that makes it work: **nothing is committed between the green run and the release.** The driver
only accepts a green run whose SHA equals HEAD.

## Tools

| Tool | Does |
|---|---|
| `tools/ci/preflight.py` | Static drift checks (driver and CI test lists vs `src/`, baselines, i18n, workflows, manual PDFs) plus a **mirror** that runs the `run:` steps of the `design-ratchet` and `macos` jobs from `build.yml`, so it cannot fall behind CI. `--fast` is static only. `--self-test` proves the drift checks catch what they claim. |
| `tools/ci/classify.py` | Reads every failed job of a run and matches `tools/ci/signatures.json`. Prints cause, fix, local catch, whether it holds the release. `--record` appends to `docs/ci/failure-log.tsv`; `--summary` shows failures by class. Exit 3 means UNKNOWN. |
| `tools/ci/signatures.json` | The catalog. A row per understood failure. The first occurrence costs an investigation; later ones cost seconds. |
| `.claude/skills/ci-failure-triage/triage.sh` | Compiler-crash detail (the file CL.exe was on). Run after `classify.py` says `msvc-ice`. |
| `ship-infinite/driver.sh ci-gate`, `verify-assets <tag>` | Release gates. |

## Adding to the system

- **New failure understood** -> add a signature row (class, step/log regex, cause, fix, preflight, blocking).
- **New kind of drift** -> add a check function to `STATIC` in `preflight.py` and a case to `--self-test`.
- **New CI step that can run on a Mac** -> nothing; the mirror reads `build.yml`.
- **A class keeps recurring** -> `classify.py --summary`; that class needs a generic check, not another row.

## Not covered (needs a CI run to prove)

Windows speed: the Windows job builds serially (`/maxcpucount:1`, a leftover diagnostic from the
`CL.exe` crash) on the Visual Studio generator, which cannot use a compiler cache. Moving it to Ninja
with ccache and parallel compiles is the biggest remaining time saving and has to be proven on a
branch run before it can be trusted.
