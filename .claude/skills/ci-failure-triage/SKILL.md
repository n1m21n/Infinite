---
name: ci-failure-triage
description: "Diagnose a failed GitHub Actions run (Windows MSVC crash, Linux clang/sanitizer, link errors) from the logs in one pass, and fix every candidate at once instead of one CI round-trip at a time. Use when any Build job is red, before editing for a CI-only failure, or before cutting a release when `main` CI is not green. Owns the CL.exe internal-compiler-error recipe."
---

## When to use (full scope)

A CI job is red and the failure does not reproduce on this Mac. We cannot run MSVC
here, and one CI round-trip costs 20 to 35 minutes, so the goal is: read the log once,
name the cause with evidence, fix **every** plausible candidate in the same push, and
only then spend a CI run. Use before editing anything for a CI-only failure, when `main`
is red before a release, and when a fix attempt just failed the same way.

Paths are relative to the repo root.

## Step 1: read, do not guess (2 minutes)

```bash
.claude/skills/ci-failure-triage/triage.sh <run-id | branch>
```

Prints each failed job, the failed step, the first real errors (configure noise
filtered out), and for a `CL.exe` crash the exact file it was compiling and whether
that file is already `/Od`-scoped in `CMakeLists.txt`. Read its output before opening
any source file.

Compare with the previous failing runs of the same job
(`gh run list --workflow=build.yml -L 6`, then `triage.sh` on each). Same job, same
file, several runs in a row = deterministic, not flaky. A different failure each run =
a different bug each time, not one stuck bug.

## Step 2: log-reading rules that were learned the hard way

- **`CL.exe` prints a file's name when it STARTS compiling it.** The last `Foo.cpp` line
  before the crash is the file that crashed, not the one after it. (v0.4.7: guessed the
  next file, `/Od` went on the wrong file, one CI run wasted.)
- **Exit code `-529706956` (0xE06D7363) plus `CL!RaiseException` frames** = the compiler
  threw (internal compiler error or out of heap), not a diagnostic about our code.
  Fix is scoping `/Od` to that file, not editing the code.
- **A job log has the whole story only for jobs that ran.** A skipped step after the
  failure (tests, upload) says nothing about those steps.
- **x64 and ARM64 are separate compilers.** One passing does not clear the other; the
  v0.4.7 crash hit x64 only.
- **Linux sanitizer / clang-tidy failures name the file and line.** Fix those directly;
  the `/Od` recipe is for compiler crashes only.
- **Link errors (`LNK1120` unresolved externals)** name the missing symbols: a target is
  missing a library or a source in `CMakeLists.txt` (see the `infinite-vst3-scanner`
  opengl32 fix), not a code bug.
- **GitHub-side failures** (`Failed to create deployment (status: 500)`, `Multiple
  artifacts named github-pages`) are not our code. Check githubstatus.com; a plain
  re-run duplicates the artifact, so start a fresh `workflow_dispatch` instead.

## Step 3: the CL.exe crash recipe

1. `triage.sh` gives the file. Confirm it is the last-started name in the log, and that
   the gap before the error matches one file's normal compile time (about 10 to 17 s).
2. Scope the override to **that file only**, Windows only:

   ```cmake
   if(MSVC)
       set_source_files_properties(src/app/<dir>/<File>.cpp PROPERTIES COMPILE_OPTIONS "/Od")
   endif()
   ```

   Existing example and rationale: the comment block in `CMakeLists.txt` above the
   `field-preset-check` target. `/Od` is fine for self-test and fixture code; it is not
   acceptable for audio-thread hot paths, so if the crashing file is DSP that ships, tell
   the owner and split the file instead.
3. **Batch the siblings.** Files that came out of the same split, are within ~25% of the
   crashing file's line count, and compile in the same pass are the next candidates.
   Check them: `wc -l` the directory, look at the files with the same shape (huge
   functions, giant `if/else` ladders, big initializer lists). If the owner agrees,
   `/Od` the whole set in this push, so the next run does not just crash one file later.
   Say which siblings were scoped and why in the commit message.
4. Do not touch Linux/macOS flags for a MSVC-only crash.

## Step 4: spend one CI run well

- Push to `bugfix/<slug>`; `build.yml` runs on `bugfix/**` and `feature/**`. Never use
  `main` as the test bed.
- Windows x64 fails (or passes) at about the 15 to 20 minute mark, long before the run
  ends. Watch that one job, not the whole run:
  `until gh run view <id> --json jobs -q '.jobs[]|select(.name=="Windows (x64)")|.conclusion' | grep -q "failure\|success"; do sleep 30; done`
- When a run fails, run `triage.sh` on **the new log** and compare file names with the
  last attempt. Same file = the fix did not apply (check the `COMPILE_OPTIONS` actually
  reached the vcxproj; `build.yml` has a step that prints main.cpp's flags, copy that
  pattern for the new file). Different file = progress, fix that one and its siblings.
- Only after the branch is green: merge to `main`, confirm `main` CI, then release.

## Step 5: before the push, edge cases to check

- Does the change compile on all three platforms? A `CMakeLists.txt` edit must be
  guarded (`if(MSVC)`, `if(WIN32)`, `if(APPLE)`). See `windows-parity`, `linux-parity`.
- Did the previous attempt leave a wrong override behind (like `DspFixtures3.cpp` in
  v0.4.7)? Remove it; a stale `/Od` hides nothing but rots the comment.
- Does the commit message state the evidence (job id, last-started file, exit code)?
  The next person will not have this conversation.
- Add the finding to `docs/` or the roadmap board only if the owner asks.

## Anti-patterns

- Editing source code to "fix" a compiler crash before checking whether `/Od` on that one
  file clears it.
- Fixing one file per CI run when four siblings share the shape.
- Re-running a failed `deploy-pages` run instead of dispatching a new one.
- Calling a release ready while any job in the `main` run is red or still running.
