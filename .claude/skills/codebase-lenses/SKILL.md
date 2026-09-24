---
name: codebase-lenses
description: "Split any request into seven concern lenses (Structure, Execution, Data & State, UI/UX, Platform, Performance, Correctness) to see which skills a change needs; Change mode and Deep-dive mode. Use before any non-trivial change or plan, for \"deep dive into X\", \"what does this touch\", or when triage/planner/cartographer picks skills."
---

## When to use (full scope)

Splits any Infinite request into seven concern lenses — Structure, Execution, Data & State, UI/UX, Platform, Performance, Correctness — each with its own numbered sub-lenses, anchors, trigger questions and owning skills, so a change or investigation is examined along every axis it actually touches instead of only the obvious one. Two modes - Change mode (classify a request into a lens map of primary / touched / clear before planning or editing) and Deep-dive mode (walk a system lens by lens at C4-style zoom levels L1 system → L4 code). Use when asked to make any non-trivial change, "deep dive into X", "how does X work", "what does this touch", "break this down", "look at this from every angle", when planning a feature, and whenever triage, infinite-planner or cartographer needs to decide which skills to load. Complements codebase-navigation (how to search) and ARCHITECTURE.md (where code lives); this skill says which concerns a change carries.

Paths are relative to the repo root (`/Users/namansoni/infinte`).

## Why this exists

`ARCHITECTURE.md` sorts code by **where it lives** (Node Library, Engine Core,
Editor UI, App Features, Platform, Harness). A single change almost never
stays inside one of those. For example, "add a knob to Delay" lives in the Node
Library, but it also carries:

- a **Data** concern: does it save/load, undo, and copy/paste?
- an **Execution** concern: does it reach the audio thread through `ParamMailbox`?
- a **UI** concern: does the knob row stay symmetric?
- a **Correctness** concern: can the new range undo an existing clamp?

Lenses sort by **what a change is about**, so the second, third and fourth
concerns get named before code is written, not found after merge.

```
                 WHERE it lives (ARCHITECTURE.md)
                 Nodes  Engine  EditorUI  AppFeat  Platform
 WHAT it's  1 Structure    ·      ·        ·         ·        ·
 about      2 Execution    ·      ·        ·         ·        ·
 (lenses)   3 Data/State   ·      ·        ·         ·        ·
            4 UI/UX        ·      ·        ·         ·        ·
            5 Platform     ·      ·        ·         ·        ·
            6 Performance  ·      ·        ·         ·        ·
            7 Correctness  ·      ·        ·         ·        ·
```

## The seven lenses

Each lens has a file under `lenses/` with its full sub-structure: numbered
sub-lenses, symbol anchors, trigger questions, known traps from this repo's
history, owning skills, and what each zoom level looks like for that lens.
**Read the file for every lens you mark Primary. For Touched lenses, reading its
trigger-question block is enough.**

| # | Lens | One-line question | Sub-lenses | File |
|---|---|---|---|---|
| 1 | Structure | What exists, how is it wired together, what is reachable? | 1a Object model · 1b Registration & wiring · 1c Module boundaries · 1d Graph topology · 1e Dependencies & licensing | [lenses/1-structure.md](lenses/1-structure.md) |
| 2 | Execution | What actually runs, on which thread, in what order, and what does it do? | 2a Threads & ownership · 2b The frame tick · 2c Time & clock · 2d Behaviour logic · 2e Lifecycle | [lenses/2-execution.md](lenses/2-execution.md) |
| 3 | Data & State | What is remembered, where, and does it survive every round trip? | 3a Params · 3b Persistence · 3c Undo & clipboard · 3d Caches & revisions · 3e Runtime-only state | [lenses/3-data-state.md](lenses/3-data-state.md) |
| 4 | UI/UX | What does the user see, touch and understand? | 4a Node body · 4b Canvas & graph interaction · 4c Panels & windows · 4d Input & shortcuts · 4e Theme & visual language · 4f Wording & discoverability | [lenses/4-ui-ux.md](lenses/4-ui-ux.md) |
| 5 | Platform | Does it behave the same on macOS and Windows, and does it ship? | 5a `Platform::` contract · 5b Media & devices · 5c Interop & hosting · 5d Build & distribution | [lenses/5-platform.md](lenses/5-platform.md) |
| 6 | Performance | What does it cost per frame, per block, per byte? | 6a Frame budget · 6b Cook economy · 6c Audio budget · 6d GPU cost · 6e Main-thread blocking · 6f Memory | [lenses/6-performance.md](lenses/6-performance.md) |
| 7 | Correctness | Is it right, and what proves it stays right? | 7a Math & numerics · 7b Invariants · 7c Edges & boundaries · 7d Verification | [lenses/7-correctness.md](lenses/7-correctness.md) |

Cite sub-lenses by number (`3c`, `2a`) in reports, so a plan line such as
"touches 2a + 3a" is unambiguous.

## Anchors are symbols, never line numbers

`src/main.cpp` is roughly 74k lines and changes daily, and the line ranges in
`ARCHITECTURE.md` were written when it was about 9k. Every anchor in the lens
files is a **symbol or grep pattern** (`DrawModMatrixDocked`,
`PushUndoCheckpoint`, `class ParamMailbox`). Resolve an anchor to a
`file:line` at the moment you cite it, and never copy line numbers from a lens
file or from `ARCHITECTURE.md` without re-grepping.

---

## Mode A — Change mode (before planning or editing)

Run this for any request that will modify code. The output is a **lens map**.

### A1. Restate the request in one line
Describe it as a change to behaviour, not as a file edit. "Delay gets a
ducking knob", not "edit AudioEffectNode.cpp".

### A2. Mark every lens
For each of the seven lenses, answer the lens's trigger questions (in its file)
and give it exactly one level:

| Level | Meaning | Obligation |
|---|---|---|
| **Primary** | The change *is* about this lens | Read the lens file in full, load its owning skills, and plan each touched sub-lens explicitly |
| **Touched** | Not the point of the change, but the change alters something here | Name the sub-lenses touched and load the one owning skill that guards them |
| **Clear** | Checked and confirmed not affected | Give a one-line reason with the evidence, e.g. "no `Platform::` call and no shortcut in the change". A Clear without a reason counts as unchecked |

Seven rows, always. A lens you skipped counts as unchecked, not Clear.

### A3. Apply the coupling rules
Some lens pairs almost never come apart in this codebase. When the left side is
Primary or Touched, the right side must be at least Touched unless you can give
a concrete reason it is Clear:

| If this is touched… | …then check this | Because |
|---|---|---|
| 1b Registration (new node / pin / cable) | 3b Persistence, 4b Canvas, 5a Platform | A registered-but-unsaved or unreachable node is the classic silent failure; every new pin needs a cable chain; new file types need both decode paths |
| 2a Threads (any value crossing main ↔ audio / worker) | 6c Audio budget, 7b Invariants, 2e Lifecycle | Cross-thread handoff is where use-after-free on delete and allocation on the audio thread come from |
| 3a Params (new or changed param) | 2a (`ParamMailbox`), 3b, 3c, 4a, 7b | A param must reach the audio thread, save, undo, sit on the knob grid, and not break an existing clamp |
| 3d Caches & revisions | 6b Cook economy, 7b Invariants | A dishonest revision stamp either freezes output or re-cooks every frame |
| 4a Node body | 4e Theme (light + dark), 3a (modulatable?) | node-ui-pillars is the regression contract; a new control is a new `ParamRef` question |
| 4d Input & shortcuts | 5a / 5d (Cmd vs Ctrl) | Cmd-only handlers are dead on Windows |
| 5b Media & devices | 2a, 6e | Device and decode code is where threads and main-thread stalls live |
| 6x any performance change | 7d Verification | A speed-up without a before/after measurement is a guess |
| 7b Invariant established | every sibling control after it, plus sibling nodes | This is `invariant-interaction-audit`'s whole reason to exist |

### A4. Load skills, bounded
- Always load `codebase-navigation` first, whatever the lens map says.
- For **Primary** lenses, load every skill listed as *owning* for the touched
  sub-lenses.
- For **Touched** lenses, load only the single skill that guards the touched
  sub-lens.
- **Clear** lenses load nothing.
- Never load all ~50 skills "to be safe". The lens map exists to keep the skill
  set small and justified.

### A5. Emit the lens map

```
## Lens map — <one-line request>

| Lens | Level | Sub-lenses | What changes / why | Skills |
|---|---|---|---|---|
| 1 Structure   | Primary | 1b | new EffectDef row + kernel | new-effect-node, cable-logic-sweep |
| 2 Execution   | Primary | 2a, 2d | kernel runs on audio thread; param via ParamMailbox | audio-pipeline-sweep |
| 3 Data/State  | Touched | 3a, 3b | 3 new params → VisitParams + patch round trip | audio-node-sweep |
| 4 UI/UX       | Touched | 4a | 7-knob body, mix slot bottom-right | node-ui-pillars, audio-node-ui |
| 5 Platform    | Clear   | — | pure C++ kernel, no Platform:: call, no shortcut | — |
| 6 Performance | Touched | 6c | per-sample loop, no alloc | field-realtime (checklist) |
| 7 Correctness | Touched | 7a, 7d | filter math → analytic DSPTEST fixture | invariant-interaction-audit |

Couplings applied: 3a → 2a/3b/3c/4a/7b (all covered above).
Open questions: <anything that is the user's call>
```

Hand this table to `infinite-planner`. Its plan should be organised **by
sub-lens** (one section per touched sub-lens) so that review can check coverage
row by row.

---

## Mode B — Deep-dive mode (understanding, not changing)

Run this for "deep dive into X", "how does X work", or "map X". The subject
can be a node, a system, a panel or a feature.

### B1. Pick the lenses
Run A2's marking on the *subject* rather than a change: which lenses does X
have real substance in? A Scope node is heavy in 2d/4a/6d and near-empty in 5.
Deep-dive only the lenses marked Primary, and list the rest with one line each.

### B2. Zoom, one level at a time (C4-style)

| Zoom | Shows | Form |
|---|---|---|
| **L1 System** | Where X sits in the whole app through this lens | One flow diagram plus 2–3 bullets |
| **L2 Subsystem** | Which sub-lenses X lives in; the boundaries it crosses; its entry points | Table of sub-lens → entry symbol → `file:line` |
| **L3 Component** | The classes and files involved and how they call each other | Link map `A (file:line) --calls/owns/reads--> B (file:line)` |
| **L4 Code** | The specific functions and the tricky lines | Only for the parts that are non-obvious or buggy; quote the minimum |

Stop at the zoom level the question needs. "How does X reach the audio
thread" is L3 on lens 2 only. Each lens file says what L1–L4 concretely
mean for that lens.

### B3. Cross-lens links
After the per-lens sections, add a **Links across lenses** block. It covers the
places where one lens's mechanism is another lens's risk, e.g. "2a
retire-one-generation handoff ↔ 2e delete-mid-playback ↔ 7d
AUDIOTEARDOWNSWEEPTEST". These edges are usually the most useful part of a deep
dive.

### B4. State what was not found
Per lens, list what you looked for and did not find, with the grep that proved
it (same rule as `codebase-navigation` §3).

### B5. Output format
Use diagrams, tables and bullets, never long prose. End with any open
questions for the user. For a heavy multi-lens dive, hand the lens list to the
`cartographer` agent and ask it to report per lens using B2's zoom levels.

---

## Keeping this skill alive

- When a task turns up a new recurring trap, add it to the **Known traps** block
  of the lens it belongs to, one line with the symbol. Cross-file wiring points
  still go in `codebase-navigation`'s living map; lens files link to it rather
  than duplicating it.
- A new skill added to `.claude/skills/` should be listed as *owning* in exactly
  the sub-lens(es) it guards, in the relevant lens file.
- If one sub-lens keeps collecting unrelated traps, split it and renumber in
  the lens file only. Keep sub-lens numbers stable once reports have cited
  them, and append new ones (`2f`) instead of renumbering.
