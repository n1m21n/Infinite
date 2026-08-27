---
name: release-notes-audit
description: Audit Infinite's GitHub release notes (v0.1, v0.2-preview, v0.2.1, and any future tag) against what the code actually contained at that exact tag commit — catches features credited to the wrong release, and specific technical claims in a bullet ("samples vertices and normals", "configurable depth and bipolar ranges") that don't match the real implementation. Use when asked to "vet the release", "review the release notes", "check if the changelog is accurate", "audit past releases", "sweep the commits against the releases", or before/after editing any published release's body. Not the same as verifying a release's build artifacts (DMG/zip integrity, CI status, version strings) — this skill is specifically about whether the prose is true.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`), not
this skill directory.

## Why this exists

A release's notes get written near the end of a fast-moving session, often
against "what I remember building this window" rather than the actual
diff. Two failure modes have already happened here, both confirmed by
example on real releases in this repo:

1. **Misattribution** — a bullet describes a real feature, but the commit
   that built it landed *after* the tag it's credited to (usually because
   the tag was cut a day or two before the branch merged, or notes were
   copy-pasted forward from a planning doc). The feature is real, the
   release is wrong.
2. **Inaccurate or fabricated claim** — a bullet's specific technical
   nouns don't match the code, even though something in that area exists
   ("normals" claimed for a node that only samples positions), or the
   whole capability was never built at all, at any point in history
   ("fluid simulation", "cellular automata" — searched every tag, every
   branch, never existed).

Both are invisible from reading the notes alone — you have to check them
against the commit graph and the code.

## Procedure

### 1. Enumerate releases and their windows

```bash
git tag --list
gh release list --limit 50
for t in $(git tag --list); do echo "$t -> $(git rev-list -n1 $t)  $(git log -1 --format=%ci $t)"; done
```

Sort tags chronologically by date (not by string — `v0.2-preview` sorts
after `v0.2.1` alphabetically but shipped before it). For each tag, its
**window** is `git log --oneline <prev_tag>..<tag>` — every commit whose
notes *should* credit that release. But don't stop at the window: a
misattributed feature's commit lives outside it by definition, so keyword
search has to cover `git log --all --oneline`, not just the window.

### 2. Pull the current published notes

```bash
gh release view <tag> --json body -q .body
```

Split into individual bullets. Each bullet is one claim to verify
independently — don't evaluate a whole section at once, a section with
four true bullets and one false one still needs the false one caught.

### 3. For each bullet: find the implementing commit(s)

Pull 2-4 keywords out of the bullet's bolded feature name (not the prose
description) and search the *whole* history, not just this release's
window:

```bash
git log --all --oneline | grep -iE "<keyword1>|<keyword2>"
```

Commit messages in this repo are generally specific enough that this
finds the real landing commit directly. If nothing matches, try the
prose (e.g. a distinctive noun from the description) before concluding
the feature doesn't exist in the log at all — some features land without
their release-notes name appearing in any commit subject.

### 4. Check timing: is it actually an ancestor of this tag?

```bash
git merge-base --is-ancestor <candidate_commit> <tag> && echo ancestor || echo NOT ancestor
```

If `NOT ancestor`, this bullet is either misattributed (find which tag it
*is* an ancestor of — walk forward through the release list) or simply
hasn't shipped in any tagged release yet.

### 5. Confirm the code existed at the tag, not just that a commit landed

A commit merging a branch can still leave files elsewhere, get reverted,
or the feature could live under a different name than expected. Confirm
directly against the tag's tree:

```bash
git ls-tree -r --name-only <tag> -- src | grep -i <expected_filename_fragment>
git grep -il "<expected_symbol_or_string>" <tag> -- src
```

An effect/filter implemented as a table row (see `new-effect-node` and
`new-geometry-node` skills for this pattern) won't show up as its own
file — grep the table file's *content* (`FilterDefs.cpp`, `EffectDefs.cpp`)
for the feature's display name instead of looking for a matching filename.

### 6. Spot-check every specific noun in the bullet against the real code

This is the step that's easy to skip and where the real inaccuracies
hide. Existence isn't the same as accuracy — a bullet can name a feature
that's 90% true and wrong about one detail. Once you've confirmed the
feature exists at the tag, open the actual implementation:

```bash
git show <tag>:<path/to/File.cpp> | less
git show <tag>:<path/to/File.cpp> | grep -n -i "<the specific claim>"
```

Read enough of the function to answer: does it actually do what this
bullet's specific words say, not just the feature's general vicinity?
Two real examples from this repo — a "Geometry Table" node's release
bullet claimed it samples "vertices **and normals**" when the code only
ever computed XYZ positions (grepping the file for `normal` only matched
an unrelated `Normalize()` math helper); a "Modulation Matrix Panel"
bullet claimed **"depth"** and **"bipolar ranges"** were configurable
in it, but reading the table-drawing function showed only Lo/Hi range,
invert, and an enable toggle — `depth`/`bipolar` were legacy fields
mentioned nearby in a different file, not controls this panel exposes.
Both looked plausible from the bullet text alone; both were wrong in a
specific, checkable way once the actual code was read.

### 7. Whole-history sweep for claims with no root at all

For any bullet that step 3 couldn't find a plausible commit for, don't
assume you searched wrong — confirm it was genuinely never built:

```bash
git log --all --oneline | grep -iE "<keyword>"        # already run in step 3
git grep -il "<keyword>" $(git rev-parse main) -- src   # current tree
for t in $(git tag --list); do git grep -il "<keyword>" $t -- src; done   # every tag
```

If all of these come back empty, the claim is fabricated — an aspiration
that made it into shipped-release copy but was never implemented, in any
release including the current one. This is worse than misattribution:
there's no "correct release" to move the bullet to.

## Report format

One table per release audited:

| Bullet | Verdict | Notes |
|---|---|---|
| Feature name | confirmed / misattributed → belongs in `<tag>` / inaccurate wording / fabricated — never built | commit(s), what's actually true |

For every `misattributed` or `inaccurate wording` verdict, write the
corrected bullet text next to it (same voice/format as the original —
see any current release body for the house style: bold feature name,
one-sentence description). For `fabricated`, don't invent a corrected
bullet — flag it for the user to decide (drop the claim, or leave it and
note the gap; that's a product call, not a copy-editing one).

## Applying corrections — ask first

`gh release edit <tag> --notes-file <file>` changes a live, public page.
Never run it unprompted. Show the exact before/after body text (or diff)
for every tag you're about to touch and get an explicit go-ahead — same
rule as any other public-content edit. When a feature moves from one
release's notes to another's, that's two edits (remove from the old tag,
add to the new one) — confirm both together so the user sees the whole
move, not one side of it in isolation.

```bash
gh release view <tag> --json body -q .body > /tmp/<tag>-current.md
# edit a copy, diff it, get confirmation, then:
gh release edit <tag> --notes-file /tmp/<tag>-corrected.md
```

## Scope

- A single named release: run steps 2-6 against just that tag.
- "All releases" / "since the beginning": run step 1 once, then steps
  2-7 per tag, oldest first — later tags are the likely destination for
  anything misattributed out of an earlier one, so having already audited
  the earlier tags saves re-deriving their windows.
- This skill doesn't check binary/build correctness (DMG integrity, CI
  status, version-string consistency, download-link health) — that's a
  separate concern from whether the prose is true. If asked to "vet the
  release" broadly, do both, but treat them as independent passes.
