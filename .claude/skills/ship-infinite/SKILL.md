---
name: ship-infinite
description: Verify, review, commit, and push uncommitted Infinite changes, cut a versioned GitHub Release tag with curated "What's new" notes, build the distributable macOS DMG, pull the CI-built Windows x64/ARM64 zips, and publish all of it via the GitHub Pages website (n1m21n.github.io/Infinite) and that Release — plus flag node-catalog changes that need the Node Reference Manual updated, and clean up junk/duplicate tracked files. Use when asked to "ship this", "release Infinite", "cut a release", "build and publish the DMG", "push and deploy", or "clean up the repo before release".
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`), not
this skill directory.

## What this does, and why it's not a single command

`driver.sh` exposes separate subcommands rather than one `all` button,
because two steps need judgment calls a script can't make on its own:
the commit message, and the base ref to diff nodes against. Run the steps
in order:

```bash
.claude/skills/ship-infinite/driver.sh verify              # build + self-test suite (run-infinite-hygiene)
.claude/skills/ship-infinite/driver.sh review               # prints git status + full diff — READ IT
.claude/skills/ship-infinite/driver.sh commit "<message>"   # git add -A && commit
.claude/skills/ship-infinite/driver.sh push                 # git push
.claude/skills/ship-infinite/driver.sh nodediff <base-ref>   # e.g. origin/main~1 — reports node-table changes
# bump INFINITE_VERSION/INFINITE_VERSION_RC in CMakeLists.txt, commit, push (see gotchas)
.claude/skills/ship-infinite/driver.sh whatsnew v0.2.7      # candidate release-notes bullets — curate by hand
gh release create v0.2.7 --title v0.2.7 --notes-file notes.md --target main
.claude/skills/ship-infinite/driver.sh release               # DMG (package.sh) + Windows zips (from CI) + Release upload
# then commit + push again so the new DMG (and any doc/cleanup changes) deploy:
.claude/skills/ship-infinite/driver.sh commit "Release: rebuild DMG"
.claude/skills/ship-infinite/driver.sh push
```

`release` needs the `push` above to have already landed on `main`, since
the Windows half pulls binaries CI built for that exact commit — see
[Release (Windows)](#release-windows) below.

Pushing to `main` is what deploys the site — `.github/workflows/deploy-pages.yml`
already runs on every push to `main` and republishes the whole `website/`
directory (including `website/assets/Infinite.dmg`) to
https://n1m21n.github.io/Infinite/. There is no separate "upload" step:
`release` regenerates the DMG in place, and the next push ships it.

## Verify

Delegates to the existing [run-infinite-hygiene](../run-infinite-hygiene/SKILL.md)
skill — builds the app and runs its self-test suite (undo/redo, patch
save/load, audio param/teardown sweeps, the 167-node-type round trip, etc.).
Don't skip this before committing. See that skill for what it actually
checks and how to read a failure.

## Review

Prints `git status --short` and the full `git diff HEAD` (staged + unstaged).
Read it before committing — this is the one place a human judgment call
belongs even in an otherwise-unattended pipeline: does this diff actually
look like what you meant to change?

## Node Reference Manual — flag, don't edit

`Infinite_Node_Reference_Manual.pdf` (root and `website/assets/`) is a
hand-maintained PDF, not generated from source — there is no automated way
to rewrite its content, so this skill never touches it. Instead:

```bash
.claude/skills/ship-infinite/driver.sh nodediff origin/main
```

diffs `src/main.cpp` between two refs and greps the result for lines
matching `REGISTER_NODE(...)` (the actual node-type registry, ~120 entries
around line 2344) and the two `{ "Name", "description" }` help-text tables
(`SpecificNodeHelpText`'s `kText` map starting ~line 15954, and the
categorized `groups` table further down) — these are where a node's name
and documented behavior live in source. If any such line was added,
removed, or changed, it prints them and tells you to review the PDF; it
exits 0 with nothing to report if the manual is still accurate.

This only catches nodes documented in those two tables (the convention new
nodes are expected to follow — see the `new-audio-node`/`new-effect-node`/
`new-geometry-node` skills). A node type registered without a matching
help-text entry won't be flagged by name, only by its `REGISTER_NODE` line.

## One release per version

Every shipped version gets its own GitHub Release tag (`v0.2.6`, `v0.2.7`,
...) — `release` uploads onto whichever tag GitHub currently marks
"latest", so **cut the tag first**, by hand, before running `release`:

```bash
.claude/skills/ship-infinite/driver.sh whatsnew v0.2.7   # prints candidate bullets, see below
# curate the bullets, then:
gh release create v0.2.7 --title v0.2.7 --notes-file notes.md --target main
.claude/skills/ship-infinite/driver.sh release             # DMG + Windows zips upload onto that tag
```

Bump `INFINITE_VERSION`/`INFINITE_VERSION_RC` in `CMakeLists.txt` and
commit that *before* `gh release create` — see the version-check gotcha
below. `release`'s DMG/Windows-zip uploads use `--clobber`, so re-running
`release` (e.g. after Windows CI finishes) safely re-uploads onto the same
tag without creating a duplicate.

Older tags predate this policy and are left alone as history (`v0.1` was
never re-cut, and `v0.2.1`–`v0.2.6` briefly shared one clobbered "latest"
release before this policy). Going forward, every version is its own tag
with its own notes page — see [What's new](#whats-new) below for the
release-notes format.

## Release (DMG)

`release` runs `package.sh` unmodified — see that file for what it does
(universal Release build, ad-hoc codesign, DMG, copies
`Infinite_Node_Reference_Manual.pdf` into the DMG and the built `.dmg` into
`website/assets/Infinite.dmg`). It's a from-scratch two-architecture
rebuild and takes minutes; don't run it as part of routine iteration, only
when actually cutting a release. Commit and push afterward — that's what
publishes the new DMG to the website.

It then also uploads that same `website/assets/Infinite.dmg` onto the
latest GitHub Release (`gh release upload <tag> ... --clobber`). The
website and the Releases page are two independent distribution points —
pushing `main` redeploys the site but never touches a release's attached
files, so without this step the Releases page silently drifts out of date
every time the DMG is rebuilt. Needs `gh` authenticated; if it isn't (or
there's no release yet), this step prints a warning and continues rather
than failing the pipeline — the website deploy from `commit`/`push` still
goes through either way.

## Release (Windows)

There's no Windows machine here to build on, so this doesn't build
anything — it fetches the `Infinite-windows-x64` and `Infinite-windows-ARM64`
binaries that `.github/workflows/build.yml`'s `windows` job already built
for the exact commit at `HEAD` (`gh run list --workflow=build.yml --branch
main` filtered to that SHA), repacks each into the same `Infinite/` folder
+ zip layout the existing Release assets use (exe + README + LICENSE), and
uploads them onto the release with `gh release upload --clobber`. Runs
automatically as part of `release`, right after the DMG upload; can also
be run standalone once CI has finished for a commit already on `main`:

```bash
.claude/skills/ship-infinite/driver.sh release-windows <tag>   # e.g. v0.2-preview
```

Best-effort, matching how the macOS DMG upload behaves: no `gh`, no
successful CI run yet for `HEAD`, or a run where a Windows job failed (the
ARM64 job is `continue-on-error`, so the workflow can be green with only
the x64 artifact present) all just print a warning and return — they never
fail the overall `release` step, since the DMG and website deploy already
went through by the time this runs.

## What's new

**Standard release-notes format**, effective v0.2.7: a single flat section,
no dates, one bullet per user-facing change —

```
## What's new in v0.2.7

- Add a unified Settings panel (Cmd+0) with Appearance, Canvas & Workspace, ...
- Fix cross-voice glide/portamento retriggering across Oscillator, Wavetable, ...
- ...
```

No `## Changelog` section, no `### YYYY-MM-DD` sub-headings, no
accumulating history in one release's body — each version is its own tag
(see [One release per version](#one-release-per-version) above), so each
tag's notes only need to cover that version.

`whatsnew` prints candidate bullets to stdout — it never writes to a
release. It diffs the given tag against the most recent tag reachable from
`HEAD` (or all of `HEAD`'s history if there is no previous tag), lists
`git log --no-merges` commit subjects, and filters out `Release: ` and
`Bump version to ` commits (mechanics, not user-facing):

```bash
.claude/skills/ship-infinite/driver.sh whatsnew v0.2.7
```

Curate the output before publishing — this is a human/Claude judgment call
like `review` and `nodediff`, not something to publish verbatim:

- Merge near-duplicate commits (an early fix and its same-day refinement
  are usually one bullet, not two — e.g. two commits fixing the same glide
  bug became one bullet for v0.2.7).
- Drop internal/dev-tooling commits (a new Claude Code skill, CI config) —
  they're real commits but not something a user of the app cares about.
- Check large "save progress"-style commits by hand: `git show --stat` can
  reveal a real fix or feature bundled into an otherwise-mechanical commit
  message (this happened for v0.2.7 — a Distribute Points on Faces cache
  bug fix was buried inside a commit titled "Save progress on core
  codebase, nodes, and documentation").
- Verify technical claims against the actual diff before publishing — see
  the `release-notes-audit` skill.

Then write the curated list to a file and publish/update the release:

```bash
gh release create v0.2.7 --title v0.2.7 --notes-file notes.md --target main   # new tag
gh release edit v0.2.7 --notes-file notes.md                                   # revise an existing tag's notes
```

## Cleanup

```bash
.claude/skills/ship-infinite/driver.sh cleanup
```

Auto-deletes exactly one category, reports another, and touches nothing
else:

1. **Junk-pattern files — auto-deleted.** `*.orig`, `*.bak`, `*.swp`,
   `*~`, `Thumbs.db`, `__MACOSX/*`, `*.pyc`, `*.class`. Anything tracked
   matching these is junk by construction; `git rm`'d and staged (not
   committed — review with `git status --short` before running `commit`).
2. **Byte-identical duplicate tracked files — reported only, never
   deleted.** An earlier version of this auto-deleted duplicates that were
   "unreferenced by basename" anywhere in the tracked tree, on the theory
   that a duplicate nobody links to is dead weight. Tested live against a
   real clone of this repo, it deleted every `assets/*.iconset/*.png`
   (consumed by `iconutil` via directory, not filename — never
   grep-referenceable) and both copies of `Infinite_Overview_Deck.pdf` and
   `The_Node_Field_Guide.pdf` (linked from GitHub's own file browser, not
   from any tracked file's text). Neither of those is junk. "Not
   grep-referenced" turned out not to mean "unused," so duplicate
   detection now only prints candidate pairs — deciding whether a
   duplicate like `Infinite_Node_Reference_Manual.pdf` (root, for the DMG
   stage, vs. `website/assets/`, for the download link — both genuinely
   needed) is intentional stays a human call.

**Repo description / GitHub metadata**: this skill deliberately does
*not* auto-edit the GitHub repo description or topics (`gh repo edit`).
Editing public repo metadata is treated as publishing content, not
housekeeping — check `gh repo view n1m21n/Infinite --json description` against
the tagline in `README.md`'s first paragraph and `website/index.html`'s
`<meta name="description">` tag by hand, and run `gh repo edit --description
"..."` yourself if they've drifted.

## Gotchas

- `driver.sh`'s `cleanup` uses `git rm`, which stages the deletion but
  doesn't commit — it composes with the `commit` subcommand rather than
  committing on its own, so a bad cleanup run is still just `git reset` away
  before you commit it.
- `nodediff`'s pattern match is line-based against `src/main.cpp`; a
  description that wraps or was reformatted without changing content will
  still show as a diff line (false positive is safe — it just means an
  extra look at the PDF that turns out to be unnecessary). A node whose
  behavior changed in its own `src/nodes/*.cpp` file without touching
  either table in `main.cpp` won't be caught at all — this is a proxy
  signal, not a guarantee.
- `release` overwrites `website/assets/Infinite.dmg` in place; `git diff`
  on a binary file won't show anything useful, so `review` after a
  `release` run will just show the file as changed, not why.
- Pushing to `main` deploys the public site immediately (no staging
  environment, no manual approval step in the GitHub Actions workflow) —
  `push` after a `release` run is a live deploy, not a dry run.
- Running `release` immediately after `push` races CI: the Windows job
  takes a few minutes, so if you run `release` right away it may not find
  a successful run for `HEAD` yet and will skip the Windows upload with a
  warning rather than wait for one. Re-run `release-windows <tag>` once
  the `windows` job on `main` is green — see [Release (Windows)](#release-windows).
- **`CMakeLists.txt`'s `INFINITE_VERSION` is the single source of truth for
  the version baked into the built app** (macOS bundle version, Windows
  `.rc` VERSIONINFO, and the string the in-app update checker compares
  itself against). The GitHub Release *tag* is a separate, independently-set
  string — nothing keeps them in sync automatically. If you cut a new
  versioned release (e.g. `v0.2.2`) without also bumping `INFINITE_VERSION`
  (and `INFINITE_VERSION_RC` right below it) first, the shipped build still
  self-identifies as the old version, and every user who installs it
  immediately sees a false "update available" badge pointing at the release
  they just downloaded. `release` now checks this and refuses to build
  (`step_release`'s version-check guard) if the tag and `INFINITE_VERSION`
  disagree — but that check only fires if the target tag already exists
  *before* you run `release`. When cutting a genuinely new tag by hand (the
  "One release, not one per version" exception above), bump
  `INFINITE_VERSION`/`INFINITE_VERSION_RC` and commit that **before**
  running `gh release create`, not after.
