---
name: write-fix-brief
description: Turn a raw bug report, a pasted list of code-review findings, a screenshot of broken UI, or a vague "we should add a node for X" idea into a verified, technically-precise implementation prompt for a fresh Claude Code session on Infinite. Investigates the actual codebase first (grep/read main.cpp, ARCHITECTURE.md, src/nodes/, src/core/) to confirm every claim is real and pin down exact files/lines/functions before writing anything — never restates the input at face value. Use whenever asked "is this a real bug", "are these fixes needed", "write me a prompt for this", "make a prompt for a new session", "should I fix this", "critically evaluate this", or when the user describes a UI glitch, pastes review findings, or floats a new node idea for Infinite and wants it turned into something actionable, even if they don't use the word "prompt" or "skill" explicitly.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`), not
this skill directory.

## What this is for

The output of this skill is always a **prompt**, never a code change. The
user hands you something raw — a screenshot, a paragraph of frustration, a
review tool's output, a "wouldn't it be cool if" — and you hand back
something a completely fresh Claude Code session (no memory of this
conversation) could pick up and execute correctly on the first try, because
it already contains the file paths, line numbers, function names, and root
cause that would otherwise take that fresh session several rounds of
exploration to rediscover.

The value you're adding is the **verification step in between**. Anyone can
reformat a bug report into a prompt. What makes it worth handing to a fresh
session is that you've already confirmed the claim is true against the
actual code, found exactly where, and figured out what's a real fix versus
what's speculative — so the implementing session spends its effort
implementing, not re-deriving what you already know.

## Step 1 — classify what you were handed

- **A single concrete bug**, often with a screenshot. Go to Step 3.
- **A list of several findings** (a code-review tool's output, an audit,
  someone else's "top issues" list). Go to Step 2 first, then Step 3.
- **A vague feature / new-node idea** ("we should have a way to do X", "add
  a node for Y"). Go to Step 4.

Don't skip straight to writing a prompt from any of these. All three need
grounding first — the difference is only in what kind of grounding.

## Step 2 — for a list of findings: evaluate before you relay

A pasted list of "top fixes" or review findings is not automatically correct
or automatically worth doing in the order given. Before touching it:

1. **Verify each item is real**, not just plausible-sounding. Grep for the
   symbol/file/pattern it names. If it cites a line number, open that line
   and check it still says what's claimed — reviews and docs drift out of
   sync with the code constantly (e.g. `ARCHITECTURE.md` can claim
   `main.cpp` is ~9,000 lines when it's actually grown to 12,000+; always
   `wc -l` the file rather than trusting a doc's stated size).
2. **When a claim is about duplication ("these two things should be one
   shared struct/function"), verify it by grepping the actual repeated
   tokens** — field names, function bodies, literal blocks — **across the
   whole relevant directory, not just the first plausible-looking spot.**
   Duplication often hides in sibling header files (e.g. the same
   `metallic`/`roughness`/`emissionColor` fields redeclared in half a dozen
   node headers under `src/nodes/`) rather than in the UI code that exposes
   them. Searching only where the *symptom* shows up (a params panel, an
   error message) and not finding the duplication there is not the same as
   confirming the duplication doesn't exist — widen the search before
   concluding a claim is false.
3. **Re-rank by actual risk/effort/payoff for this codebase**, not the order
   handed to you. `main.cpp` is one ~12k-line file with no UI-layer test
   coverage (see `ARCHITECTURE.md`'s "Dev/Test Harness" section for what
   *is* covered) — a large refactor there is a different risk class than a
   one-line doc fix or a struct dedup that doesn't touch `main.cpp` at all.
   Say so explicitly and propose a reordering, don't just accept the given
   priority.
4. **Say plainly which items are and aren't worth doing now.** "All N are
   real, but #3 is a large speculative refactor — I'd defer it unless you're
   about to add a wave of new node types" is a more useful answer than
   silently agreeing with everything on the list.

Report this evaluation back to the user in chat before or alongside the
prompt — they're asking "are these needed", not just "turn this into a
prompt", and deserve the actual judgment call.

## Step 3 — for a bug: confirm root cause with evidence, not a guess

- **If there's a screenshot, read it literally, character by character**,
  not just for the general gist. A UI glitch is often a concrete, findable
  bug hiding in plain sight — e.g. a badge rendering as `?x` instead of the
  intended `ƒx` is not "a rendering glitch", it's a missing-glyph fallback,
  and grepping the source for that exact byte sequence plus checking how the
  font was loaded (`AddFontFromFileTTF(path, size)` with no glyph-range
  argument → default Basic-Latin-only range → U+0192 has no glyph → ImGui
  substitutes `?`) turns a vague "something looks off" into a one-line,
  provably-correct fix. Values, exact numbers, and label text in a
  screenshot are evidence, not decoration.
- **Trace the actual code path**, don't reason about what "should" happen.
  Find the function, read what it actually does line by line, and check
  whether the user's described behavior matches. If a mechanism looks like
  it *should* already work from reading the code (e.g. a double-click
  handler that mirrors a pattern used successfully elsewhere in the same
  file), say that explicitly rather than assuming it's broken just because
  the user says something doesn't work — ask the implementing session to
  verify it rather than asserting a root cause you haven't actually confirmed.
- **Use `git log`/`git show <hash>` on the relevant file** when a feature
  looks recent — knowing "this shipped in the same commit as the thing
  you're describing" often explains exactly why a UI affordance exists but
  isn't discoverable yet, versus being an old, well-worn bug.
- If you genuinely cannot pin down a root cause from reading the code, say
  so in the prompt — ask the fresh session to reproduce and diagnose as an
  explicit first step, rather than inventing a plausible-sounding cause.

## Step 4 — for a new node idea: ground it in the existing pattern, not a blank page

Infinite's node system has real structure that a vague "add a node for X"
idea needs to be mapped onto before it's implementable. Before drafting the
prompt:

1. **Read `ARCHITECTURE.md`'s "Node Library" section** and the node table in
   it — figure out which existing node is the closest sibling to what's
   being asked for (e.g. a new per-pixel effect belongs as a `FilterNode`
   table entry via `src/core/FilterDefs.h`, not a new C++ class; a new
   geometry op is one more case in `GeometryOpNodes`; a genuinely new kind
   of source/behavior is a new `.h`/`.cpp` pair under `src/nodes/`).
2. **Name every main.cpp touch point** a new node type needs, from the
   "Node Library" line-range table: `RegisterNodes()`, `DisplayName`,
   `InputCountFor`, `CableFor`, `ConnectGeometrySlot` (if it has geometry
   pins), the `DrawXxxParams` function for its param panel, the per-frame
   dispatch that calls it, and `QueryNewLink` if it needs custom
   connection-validation rules. Missing any of these is why "I added the
   class but it doesn't show up / doesn't connect / has no UI" happens.
3. **If it's an `IGeometrySource`-consuming node** (has a geometry input),
   read `ARCHITECTURE.md`'s "Invariants for IGeometrySource-consuming
   nodes" and make sure the prompt calls out: forward every side-channel
   accessor it doesn't explicitly change (`GetMesh()`, `GetModelMatrix()`,
   `GetMaterial()`, `GetSurfaceTexture()`, `GetMaterialTexture()`,
   `GetMappingTransform()`), and only bump its revision/generation stamp
   when its actual output changed. These two rules have each caused a real,
   shipped bug before (see the `geometry-transform-sweep` skill) — call
   them out explicitly in the prompt rather than assuming the implementer
   already knows the convention.
4. Point the prompt at running `.claude/skills/geometry-transform-sweep/driver.sh`
   afterward if the new node has a geometry input — it catches exactly the
   two bug classes above automatically instead of relying on manual review.

## Step 5 — write the prompt

The prompt is the deliverable. It must stand completely on its own — assume
zero memory of this conversation. Structure:

- **One numbered item per distinct change.** Each item names the exact
  file(s), and where you found something concrete (a line number, a
  function name, an existing pattern to mirror), include it — don't make
  the fresh session re-grep for something you already located.
- **State what's confirmed versus what's a judgment call left open.** If
  you resolved every design decision yourself (which struct, which file,
  what the new field is called), say so — that's what makes a prompt
  "exact" rather than just "pointed in the right direction." If something
  genuinely has more than one reasonable answer, say that explicitly and
  give the fresh session your recommendation plus why, rather than silently
  picking one without flagging it as a choice.
- **Always end with a build step.** This is a compiled macOS app, not
  something you can smoke-test by reading source. Include:
  ```
  cmake --build build -j"$(sysctl -n hw.ncpu)"
  ```
  (reuses the existing configured `build/` tree — see `run-infinite-hygiene`
  for the fuller build/test story) and tell the implementer to confirm it
  compiles clean, not just that the edit "looks right." If the change
  touches a geometry-consuming node, also tell it to run
  `.claude/skills/geometry-transform-sweep/driver.sh` before considering it
  done.
- **Say explicitly what's out of scope**, if the surrounding conversation
  turned up adjacent things worth fixing that this prompt deliberately
  isn't covering (e.g. "don't touch X, that's a separate larger change
  being scoped independently") — this keeps a fresh session from
  scope-creeping into something you evaluated and chose to defer in Step 2.

## A note on rigor

Don't pad a prompt with process ("investigate the codebase", "look for the
root cause") in place of actually doing that investigation yourself first.
A prompt that tells the next session to go find the file is strictly worse
than one that names the file, because you had the chance to check and
didn't. If you're not sure something is exactly right, say so in the prompt
rather than asserting it confidently — an honest "verify this before
proceeding, I found X but didn't confirm Y" is more useful to the
implementer than a wrong confident claim.
