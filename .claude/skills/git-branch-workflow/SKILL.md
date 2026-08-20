---
name: git-branch-workflow
description: The standard branching workflow for this repo — every feature or bug fix request gets its own branch off main (feature/<slug> or bugfix/<slug>), work happens there, and it merges directly back into main (no PR, solo repo) once done. Replaces the old repo-privacy-split system. Use whenever the user asks for a new feature, a bug fix, or says "start work on X" — before writing any code — and when a piece of work is done and ready to land on main.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`),
not this skill directory.

## The rule

Two kinds of work, one pattern: **never commit feature/bugfix work directly
on `main`.** Branch off main, do the work, merge straight back to main when done.

- **Feature request** → `git checkout -b feature/<short-slug> main`
- **Bug fix request** → `git checkout -b bugfix/<short-slug> main`

`<short-slug>` is a few kebab-case words describing the specific thing
being built/fixed (e.g. `feature/video-in-node`, `bugfix/slider-arrows-touch`),
not a generic name. There is no need for a literal `features` or `bug-fixes`
branch — the prefix is the category, `main` is the only real integration
branch.

## Workflow

1. **Before writing code for a new ask**, check `git status` and `git branch
   --show-current`. If already mid-branch for the same task, keep using it.
   Otherwise create a new one from `main`:

       git checkout main
       git pull                     # if it makes sense to sync first
       git checkout -b feature/<slug>      # or bugfix/<slug>

2. **Work and commit on that branch** as normal until the user considers the
   task done (tests pass, they've reviewed it, etc.).

3. **Merge back into `main` directly** — no GitHub PR, this is a solo repo:

       git checkout main
       git merge feature/<slug>
       git push                      # only after explicit user confirmation

   Prefer a regular merge (or `--no-ff` if a merge commit boundary is useful)
   over rebasing published history.

4. **Delete the finished branch** once merged, to keep `git branch -vv` clean:

       git branch -d feature/<slug>

## Notes

- If the user is mid-request and there are already uncommitted changes
  sitting on `main` for what looks like a distinct feature/fix, move them to
  a properly named branch before continuing (`git checkout -b <name>` carries
  uncommitted changes with it), rather than committing them straight to `main`.
- This replaces the old `repo-privacy-split` skill/system (public/private
  repo split via `osc-feature` + `infinite-private`). That system is gone —
  don't push to the `private` remote as part of this workflow. (One
  unrelated exception still exists: the `build/vst3-latest` branch used by
  `plugin-host-hardening`, which simply hasn't been merged to `main` yet —
  unrelated to this workflow.)
- Follow the existing repo-wide git safety rules: only push when the user
  explicitly asks, never force-push/rebase published history, create NEW
  commits rather than amending.
