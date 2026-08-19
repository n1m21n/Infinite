---
name: ship-infinite
description: Verify, review, commit, and push uncommitted Infinite changes, build the distributable DMG, and publish it via the GitHub Pages website (n1m21n.github.io/Infinite) — plus flag node-catalog changes that need the Node Reference Manual updated, and clean up junk/duplicate tracked files. Use when asked to "ship this", "release Infinite", "cut a release", "build and publish the DMG", "push and deploy", or "clean up the repo before release".
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
.claude/skills/ship-infinite/driver.sh release               # package.sh: builds DMG, copies into website/assets
# then commit + push again so the new DMG (and any doc/cleanup changes) deploy:
.claude/skills/ship-infinite/driver.sh commit "Release: rebuild DMG"
.claude/skills/ship-infinite/driver.sh push
```

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
