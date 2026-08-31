---
name: bug-blast-radius
description: The standard nine-question impact analysis to run on ANY bug reported for Infinite before proposing or writing a fix — which node owns it, which logic is actually at fault, who else calls that logic, which other nodes it silently degrades, whether it is one node or a repeated pattern across the codebase, whether it behaves the same on macOS and Windows, and whether the obvious fix opens a loophole or breaks a documented invariant, when it was introduced and what bad state it leaves behind, and why the self-test harness never caught it. Use EVERY time the user shows a bug — a screenshot, a "my FPS drops", "this looks wrong", "this crashes", "this node is broken", a pasted stack trace, or a described misbehaviour — before writing any code or any fix prompt. Also use for "investigate this bug", "how bad is this", "what else does this affect", "is this everywhere".
---

Repo root is `/Users/namansoni/infinte` (note the spelling — the directory
is `infinte`, not `infinite`). All paths below are relative to it.

## What this is for

A bug report in Infinite is never just one node. The codebase is one
12,000+ line `src/main.cpp` of node bodies over a small set of shared
engines in `src/core/` and `src/nodes/`, so a defect almost always lives in
a **shared helper** while surfacing in **one node's UI**. Fixing what the
screenshot shows, without asking who else calls it, is how a fix gets
reverted two releases later.

The output of this skill is an **analysis**, not a patch and not a prompt.
It ends in a verdict the user can act on. If they then want a prompt for a
fresh session, hand the analysis to `write-fix-brief` — that skill turns a
verified finding into an implementable prompt. This skill's job is to make
sure the finding is verified and its blast radius is known first.

Never answer any of the nine questions from reasoning alone. Every claim
below must be backed by a `grep`/`sed -n` you actually ran, cited as
`file:line`. "Probably", "likely", and "should be fine" are not answers.

## Step 0 — reproduce the claim in the code, not in your head

Before anything else, find the exact symptom in the source. For a screenshot
this means: read the values visible in the UI (parameter values, counts,
labels) and find the code path that produces exactly those. The visible
numbers are evidence — a fractional slider value means the user was
*dragging*, a point/triangle count tells you the size of the working set, a
greyed label tells you which branch the UI is in.

Then state the symptom as a mechanism, in one sentence, or stop and say you
could not reproduce it. Do not proceed to the nine questions on a symptom
you have not located.

## The nine questions

Answer all nine, in order, with a heading each. Do not merge them and do
not skip one because it "obviously doesn't apply" — write the one-line
negative answer with the evidence that made it negative.

### 1. Which node owns it

The node class and its two homes: the data/cook side (`src/nodes/*.h/.cpp`,
or `src/core/` if it is engine-level) and the UI side (the `Draw*Params` /
`Draw*Body` function in `src/main.cpp`). Give the line numbers of both.
State whether the bug is in the node or merely *exposed* by it.

### 2. Which logic is actually at fault

Name the function, the loop, the specific expression. Distinguish sharply:

- **Algorithmic** — the work itself is too expensive, wrong, or unbounded.
- **Cache/invalidation** — the work is fine but runs when it should not.
  Check the node's `RebuildIfNeeded` guard fields (`mBuilt*`) and its
  `CookIfNeeded(frameId)` frame gate; check whether an upstream
  `MeshRevision()` / `TextureRevision()` churns every frame.
- **Amplification** — the work is fine and correctly cached, but a loop
  above it (per instance, per frame, per pass, per output) multiplies it.

Most Infinite performance bugs are one of these three wearing another's
clothes. Say which, and back it with a rough cost estimate in operations
per rebuild — count the loop bounds, do not guess.

### 3. Who else calls this logic

`grep -rn "<FunctionName>" src/` — every hit, classified:

- other **production** nodes (real blast radius),
- the **self-test harness** in `src/main.cpp` (a slow or wrong test is
  itself a finding, and self-tests are the fix's regression gate — read
  what they assert before you change behaviour),
- **dead or doc-only** mentions.

If callers pass different arguments (a different mode, a different budget),
say explicitly which callers are on the broken path and which are not.

### 4. How it degrades other nodes

Two directions, both mandatory:

- **Downstream** — when this node rebuilds it usually calls
  `NextMeshRevision()` / bumps `TextureRevision`, which invalidates every
  consumer's cache. A node that rebuilds every frame therefore forces the
  whole downstream chain (VBO re-upload, dependent mesh ops, render passes)
  to redo its work every frame. Name the concrete downstream consumers.
- **Upstream / sibling** — does the bug make an upstream node's cache
  useless, or does it change what a pass-through node forwards
  (`PassthroughSource`, `GetInstanceGroupMatrix`, `InstanceSelection`,
  material and mapping forwarding)? The passthrough-forwarding trap has
  caused real bugs here; check it rather than assuming.

### 5. Is it expansive — one node or a pattern

Grep for the *shape* of the mistake, not its name. Concretely: pull the
distinguishing tokens out of the faulty code (the magic multipliers, the
budget/limit variable names, the `std::unordered_map` neighbour search, the
`sin()*43758.5453` hash, the `mBuilt*` comparison list, the missing
`frameId` gate) and search the whole of `src/` for each.

Then report one of: **isolated** (one call site, evidence given),
**duplicated** (list every site), or **systemic** (a shared helper or an
idiom copied across the node layer — this changes the fix from a patch to a
refactor, and the user needs to know that before approving).

### 6. Does it behave the same on macOS and Windows

Decide from the code, not from a build:

- Is the faulty code under `src/platform/`, or does it contain `_WIN32`?
  Then it is **platform-divergent by construction** — read both sides and
  apply the two-sided obligation from the `windows-parity` skill.
- Is it portable C++/GLSL in `src/core/` or `src/nodes/`? Then it is
  **present on both platforms**, and say so. Do not stop there: name the
  *magnitude* difference where one exists — MSVC's `std::unordered_map` and
  `std::map` are materially slower than libc++'s, MSVC's float codegen
  differs, GLSL 330 is stricter on Windows drivers, and Windows machines in
  this project's audience skew toward weaker GPUs. A shared bug is often
  **worse** on Windows even when the code is identical.

Since only macOS can be built and run here, mark each platform's status as
**verified** (you ran it) or **inferred from source** (you read it). Never
present the second as the first.

### 7. Would the obvious fix open a loophole

This is the question that most often changes the plan. Work through:

- **Which invariant does the current behaviour accidentally uphold?** Check
  the self-tests found in Q3 — they encode invariants like determinism from
  a seed, `minDistance` respected, saturation terminating, revisions only
  bumping on real change. A fix that makes the test pass by weakening what
  it asserts is not a fix.
- **Does the fix change existing saved patches?** Anything that alters point
  counts, sampling order, random draws, or default values changes how an
  already-saved `.patch` looks when reopened. Say so explicitly — silently
  changing a user's saved work is a worse bug than the one being fixed.
- **Does the fix create redundancy?** If it adds a second budget, a second
  cache, or a second guard next to one that already exists, the two will
  drift. Prefer removing the broken one to adding a correct one beside it.
- **Does it move cost rather than remove it?** Backgrounding, throttling, or
  LOD-ing expensive work is legitimate, but it introduces the async and
  stale-frame problems this codebase already has skills for. Name the new
  failure mode you would be signing up for.
- **Does it violate the real-time rules?** No allocation, locking, or
  unbounded work on the audio thread; no blocking work in the render loop.

### 8. When did it break, and what does it leave behind

Two halves, both cheap and both frequently decisive.

**History.** `git log -S "<distinguishing token>" -- <file>` for the faulty
expression, then `git tag --contains <commit>` on the introducing commit.
Report which of three cases this is:

- **Fresh regression** (introduced after the last tag) — revert is on the
  table and is usually the right first move.
- **Shipped defect** (present in one or more released tags) — every user on
  those builds has it; revert is not an option; the fix needs release notes.
- **Present since the feature was written** — the feature never worked as
  specified. This is a design question, not a patch, and Q7's
  experimentality test applies: ask whether the feature is worth keeping in
  its current shape at all.

Never assume a bug is recent because it was reported recently.

**Persistence.** Distinct from Q7's "does the fix change saved patches" —
ask whether **the bug itself writes bad state that outlives it**. Does it
put wrong values through `VisitParams` into a `.patch`, corrupt a modulation
binding, leave a dangling cable, or poison a cache that reloads? If so, the
fix needs a migration or a load-time repair, and shipping the code fix alone
leaves already-saved work broken. Check the node's `VisitParams` and the
patch serialization path before answering no.

### 9. Why didn't the harness catch it

Infinite has an extensive self-test harness (`run-infinite-hygiene`) and a
dozen sweep skills. A bug that reached the user got past all of them, and
that gap is a finding worth as much as the bug.

Determine which applies, with evidence:

- **No test covers this path** — name the sweep skill that *should* own it.
- **A test executes the path but asserts the wrong thing** — the most
  common and most valuable case. A test that checks a result is correct
  while saying nothing about what it cost, how long it took, or how many
  times it ran will sit on top of a performance bug forever without
  failing. Read what the covering test actually asserts before concluding
  it doesn't exist.
- **A test would catch it but isn't run on this platform** — cross-reference
  `pillar-parity-audit`; macOS-only coverage is the standing gap here.
- **Genuinely not machine-checkable** — say so plainly rather than inventing
  a test. Visual and feel bugs often land here.

Then state the one check that would have caught it, phrased concretely
enough to implement (an assertion, a timing bound, a call counter, an
invariant). That check belongs in the fix, not in a follow-up.

## Step 10 — verdict

Close with a short, blunt section:

- **Root cause** in one sentence.
- **Severity** — how many frames per second, how many nodes, how many
  users. Weight it by **reach**: a bug on a node's default path is not the
  same as one behind a non-default mode, an unusual parameter range, or a
  rarely-used input. Say which, from the defaults in the node's header.
- **Age** — from Q8: fresh regression, shipped in released tags, or present
  since the feature was written.
- **Scope of the correct fix** — one-line patch / one function / shared
  helper refactor / needs a design decision.
- **What the fix must not break** — the invariant list from Q7, phrased so
  a fresh session can turn each into a check.
- **The check that must ship with the fix** — from Q9, plus any migration
  for bad saved state from Q8.
- **Open questions for the user**, only where a different answer means
  genuinely different work (e.g. "is it acceptable that existing patches
  reopen with a different point count?").

Then offer — do not assume — to hand it to `write-fix-brief` for the
implementation prompt.

## Standards this analysis is held to

The same four Infinite standards `infinite-code-review` applies, used here
as analysis criteria rather than review criteria:

- **Accuracy** — is the math/algorithm right, and does the fix keep it
  right, including determinism from a seed.
- **Experimentality** — is the broken feature actually worth keeping? Some
  bugs are best answered by simplifying the feature, not by making the
  broken version fast. Say so when it is true.
- **Design** — the node must still read as an instrument. A fix that adds a
  "quality" dropdown and three budget sliders to paper over a slow
  algorithm fails this even if it restores the frame rate. Infinite's audio
  and visual nodes stay plugin-simple.
- **Quality** — performance, threading, and the caching rules above.

## Anti-patterns

- Answering any of the nine from memory of an earlier session.
- Calling a bug "isolated" after grepping only the file the screenshot
  pointed at.
- Reporting a Windows verdict as verified when only macOS was run.
- Proposing the fix inside the analysis. The user asked how bad it is; give
  them that first, then offer the prompt.
- Driving Infinite's ImGui canvas to reproduce the bug — it is slow and
  unreliable. Read the code, or ask the user to confirm a behaviour.
