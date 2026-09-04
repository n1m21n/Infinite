# Design brief: OPEN-A/B/C/D — the four language questions blocking rung 1-4 algorithms

## Status

Not started. This is a decision brief, not an implementation plan — no
compiler or node code has been written against it. It exists to hand a
concrete, scoped set of questions to whoever owns the Field language
design, so they can answer them once instead of each answer being
improvised ad hoc inside a future build step. Treat every file:line
citation below as current as of the commit this file was added in
(`git log -1` on this file to check drift before acting on it).

## Motivation

`docs/plans/field/algorithms.md` is a catalogue of ~40 real DSP/graphics/
generative algorithms, sorted by whether Field v1 can express them today.
Four of them cannot be written **at all**, in any node type, because the
language is missing a primitive — not because the algorithm is hard, but
because the syntax to say it doesn't exist yet. `algorithms.md` §4 names
these four gaps `OPEN-A` through `OPEN-D`, tags every catalogue entry that
needs one, and says explicitly for each: **"ask the owner."** Nobody has.

This matters beyond the four tagged entries. Per `algorithms.md:1342-1352`
(§9.7), the entire "rung 4" learning tier — learned control curves, node
suggestion, learned groove, note-transition prediction — is *also* gated
behind these same four answers, because rung 4 needs a real per-user data
store and OPEN-A/D are the primitives that store would be built out of.
Answering these four questions is the actual prerequisite for a large
fraction of Field's stated long-term potential, not just for four DSP
recipes.

**None of this blocks what's already shipped.** Steps 1-17 (expression→IR,
the five domains, dynamic pins, FieldGraph encapsulation, `.infdev`) do not
depend on any OPEN-* answer. This is scoped work for *future* catalogue
entries and rung-4 features, not a regression or a blocker on anything
currently working.

## The four questions, with citations

### OPEN-A — bounded `state` arrays and a ring cell

**Who needs it:** any delay line (comb, reverb, chorus), pitch-period
repetition, LPC/LMS above order ~8 without 32 hand-written lines, any
lookup/transition table (node co-occurrence, note-transition prediction).

**The conflict, as written today** (`algorithms.md:153-161`): `field-language`
§14 row 6 says `float buf[512]` is not in v1 — no arrays, period.
`field-realtime` §2 says a fixed-size state buffer with a runtime read
index is exactly how every delay line in the codebase already works. These
two rows contradict each other and nothing resolves it.

**Options on the table** (`algorithms.md:163-169`):

| Option | Spelling | Cost |
|---|---|---|
| (a) no arrays, ever | unroll everything | delay lines past ~16 taps become unwritable |
| (b) fixed-size `state` cell, ring access | `state float buf[2048]=0`, `buf.write(x)`/`buf.read(d)` | size is a literal; read offset is clamped, so it can't go out of bounds |
| (c) fixed-size `state` cell, raw indexing | `buf[k]`, `k` an int | needs int/modulo semantics and a bounds decision (clamp/wrap/error) — three more open questions |
| (d) `map` with `state` inside | already legal | one cell per mapped element, no cross-element indexing — builds a filter bank, not a delay line |

### OPEN-B — ordered-neighbour reads in `element`

**Who needs it:** differential growth, any spring/rope/cloth constraint,
curve smoothing or resampling.

**The gap** (`algorithms.md:182-184`): `element`'s reserved set (`P N uv Cd
i count`) gives the index `i` and the total count, but there is no way to
read element `i-1`'s `P`. A chain, a rope, and a growing curve are all
"this element and the one next to it," and none of them are writable.

| Option | Spelling | Cost |
|---|---|---|
| (a) no neighbour access | already legal | a chain is impossible — mean-of-all-P is not the neighbour |
| (b) ordered read on an attribute | `P.at(i-1)` | index clamped to `[0,count-1]`; reads the **previous cook's** value (a delay, like `state`), so the pass stays order-independent and parallelizable |
| (c) explicit two-phase kernel | gather pass, then update pass | honest and slow, doubles SoA traffic |
| (d) `neighbours` construct with topology input | biggest surface | needs mesh adjacency, which `Mesh.h` does not carry |

### OPEN-C — offset reads of a `pixel` state cell

**Who needs it:** every interesting pixel algorithm — reaction-diffusion
needs a 4-tap Laplacian, advection needs a read at `uv - flow`, a fluid
pressure solve needs both.

**The gap is sharper than the other three** (`algorithms.md:204-207`):
Infinite's *existing shipped* Feedback / Trails / Reaction Diffusion nodes
already do this. A pixel `state` cell that can only be read at the current
`uv` cannot express nodes the app already ships today — this isn't a
future capability question, it's a gap between the current spec and
current behavior.

| Option | Spelling | Cost |
|---|---|---|
| (a) current-pixel reads only | existing `field-state` §8 example | Trails works; blur/RD/advection/fluid/every existing feedback node does not |
| (b) call form | `prev(uv + d)` → one `texture()` fetch | fetch count belongs on the node face next to the byte count |
| (c) explicit sampler function | `fetch(prev, uv + d)` | unambiguous, but a fifth spelling next to `reduce`/`resample`/`downsample`/`map` |
| (d) fixed neighbour offsets only | `prev.at(-1,0)` | covers RD/blur; not advection, which needs a continuous coordinate |

Also unstated: what happens outside `[0,1]` — clamp, wrap, or border. RD
looks completely different under wrap (seamless tiling) vs. clamp (edge
accumulation). The doc's own position: **the answer is a per-cell
declaration, not a global one** (`algorithms.md:216-219`).

### OPEN-D — a second `sample` input, and note/event input

**Who needs it:** two-input adaptive filters (echo cancellation, noise
cancellation), and every rung-4 entry that learns from what the user
*plays* (note-transition prediction).

**The gap** (`algorithms.md:229-233`): `sample`'s reserved set is `in out
sr n` — one input, one output. That covers the adaptive-line-enhancer form
of LMS (reference is the delayed input itself), not the textbook two-input
form. Separately, **no domain has a note or event name at all** — a kernel
cannot see that the user played a C#.

| Option | | Cost |
|---|---|---|
| (a) `in` becomes a vector (`in.x`, `in.y`) | smallest surface change | conflicts with `in` being a plain `float` everywhere else |
| (b) `in2` as a second reserved name | explicit | reserved set becomes node-shaped, which nothing else in Field is |
| (c) a `note` domain, or note names reserved in `frame` | the honest rung-4 answer | a sixth domain is a large decision |

## Proposed answers, for discussion — not a decision

These are one person's read of the tradeoffs above, offered as a starting
point for the owner's actual decision, not a recommendation to implement
without sign-off.

| | Proposed answer | Reasoning |
|---|---|---|
| OPEN-A | (b) now. Revisit (c) only if a table-shaped consumer (note-transition, node co-occurrence) is actually being built, and scope it as a distinct declaration rather than general indexing | (b) covers the large majority of real demand (every delay-line effect) with a bound that can't go out of range by construction. Raw indexing (c) opens three more open questions and should stay unopened until something concrete needs it |
| OPEN-B | (b) | Consistent with how `state` already works (previous-cook read, not same-pass), which is *why* it stays parallelizable — this is a constraint worth keeping, not just a cheap option. (d) requires mesh-adjacency infrastructure that doesn't exist and shouldn't be built speculatively |
| OPEN-C | Prefer reusing `resample` over inventing (b)/(c) as a new spelling — a pixel state cell read at an offset coordinate is the same shape as `resample`'s "read domain A while standing in domain B," and Field's four-operator vocabulary (`reduce`/`map`/`resample`/`downsample`) is worth protecting from a fifth. The clamp/wrap/border choice should be per-cell regardless of which spelling wins | Keeps the operator surface small; matches an existing concept instead of adding a parallel one |
| OPEN-D (second input) | (b) | (a) breaks `in`'s type everywhere else in the language to fix two node types. (b)'s "node-shaped reserved set" cost is real but narrowly contained to the rare two-input adaptive-filter case |
| OPEN-D (note input) | (c), but folded into `frame`'s reserved set (`noteOn`, `notePitch`, `noteVel`) rather than a new sixth domain | Gets the rung-4 note-aware entries unblocked without the larger commitment of a new domain/backend |

## Use cases, from a user's perspective

Each OPEN question reads as compiler trivia until it's phrased as what a
person is actually trying to do. All four block real, specific things a
user would reasonably expect to build:

| | The user's action | What breaks today, in plain terms |
|---|---|---|
| OPEN-A | Drags out a Delay/Reverb/Chorus node, opens its Field body to tweak the feedback math themselves | There's no way to say "remember the last 2048 samples" in Field — only "remember one number." The node's own delay line can't be written as a Field kernel at all, so the "open it and customize it" promise stops at exactly the nodes people most want to customize (echo, comb, reverb tail) |
| OPEN-A (table use) | Types a melody into a Note Sequencer and expects Infinite to notice "after a C, I usually play an E" | Needs a 144-cell transition table, not a single remembered value — impossible with today's `state`, so this feature can't exist yet even as a first draft |
| OPEN-B | Builds a rope/chain/growing-vine effect from a row of points, expects each point to react to its neighbour | A point in Field can see its own position but has no way to ask "what is the point next to me doing" — so physically-linked chains (rope, cloth, growth) aren't buildable in Field, only hand-coded in C++ |
| OPEN-C | Opens the existing Reaction Diffusion or Trails node's Field body to understand or extend how it works | The node visibly reads neighbouring pixels (that's the whole effect) but the current Field spec has no syntax that describes that read — so the shipped node's *own* behavior can't be re-expressed in the language meant to replace its C++ implementation |
| OPEN-C | Wants a fluid/smoke-like effect, or a "melt/flow" pixel distortion | Needs to read a pixel state cell at a *shifted* coordinate (advection); today's spec only allows reading the exact same pixel back, so flow-based effects have no path to being user-editable Field nodes |
| OPEN-D (2nd input) | Wants an echo-cancellation or noise-reduction node where a reference signal cancels out of the main one | Needs two separate audio streams inside one kernel; `sample` domain only has one `in` today |
| OPEN-D (note input) | Plays a melody and expects the patch to notice which notes were played, not just what came out as audio | No domain has any concept of "which note," at all — a Field kernel is deaf to the note the user pressed, only ever seeing the resulting waveform |

The common thread: every one of these is a case where a user opens a node
expecting to edit or extend it in Field, and hits a wall that has nothing
to do with their idea being hard — the language simply has no word for
what they're trying to say yet.

## Performance metrics of the overall software

Figures below are the load-bearing constants `algorithms.md` §3 already
uses for every cost estimate in the catalogue — restated here so a
decision on OPEN-A/B/C/D can be weighed against the actual budget it has
to fit inside, not an abstract one.

### Per-domain throughput (today, unaffected by these decisions)

| Domain | Rate | Budget |
|---|---|---|
| `frame` | 60/s | whole-frame budget |
| `element` | 60 × 5 000 = **300k invocations/s** | ~2 ms/frame (`field-realtime` §5) |
| `pixel` | 60 × 2 073 600 (1080p) = **124.4M invocations/s** | GPU frame budget |
| `sample` | 48 000/s | ~5.3 ms per 256-frame audio callback block (`field-realtime` §5) |
| node types in Infinite today | 167 | full self-test round trip (`run-infinite-hygiene`) |

### What each OPEN answer would cost, concretely

| | Cost of the proposed answer | Where it lands in the budget |
|---|---|---|
| OPEN-A (b), comb/delay ring buffer | 2048–4096 samples × 4 B = 8–16 KB **per voice**; × 8 voices (Infinite's standard poly count) = **64–128 KB** | Negligible against system memory; the real constraint is the audio callback's ~5.3 ms/block, and a ring read/write is O(1) — no risk to the audio budget |
| OPEN-A (c), a 144-cell transition table (note-transition use case) | 144 × 4 B = **576 B** | Trivial; the risk isn't memory, it's that indexing semantics (clamp/wrap/error) need deciding once, not per-node |
| OPEN-B, `P.at(i-1)` neighbour read | One extra read per element invocation; still 300k invocations/s, ~2 ms/frame budget unchanged | The doc's "reads the previous cook" rule is what keeps this at O(1) per element instead of requiring a synchronization barrier mid-pass |
| OPEN-C, offset pixel read (`prev(uv+d)`) | Reaction-diffusion's existing measured cost: **8 texture fetches + ~25 ALU per pixel per frame** → at 1080p × 60 fps = **~1 G fetches/s**, which `algorithms.md:515` already identifies as "the real ceiling, not the ALU" | This is the one OPEN answer with a real, already-measured performance ceiling — fetch count, not compute, is what will need a budget line on the node face, the same way byte count already gets one |
| OPEN-C, state-cell memory (any pixel effect with feedback) | R16F ping-pong pair at 1080p = **8.29 MB**; R32F (needed once a value integrates for more than a few seconds) = **16.6 MB**; RGBA32F 4-cell pack = **66.4 MB** at 1080p, **8.39 MB** at a 512² simulation resolution | This is already a live cost for the *existing* Feedback/Trails/RD nodes — OPEN-C doesn't add new memory pressure, it just makes the existing cost expressible in Field text instead of hidden in C++ |
| OPEN-D, second sample input | 0 B extra state; doubles the audio-domain input reads per sample | Well inside the ~5.3 ms/block budget — the cost here is entirely at the language-surface level (a second reserved name), not runtime |
| OPEN-D, note names in `frame` | 0 B extra state; note events arrive at 60/s frame resolution, not per-sample | The tradeoff is precision, not performance: `frame`'s 60 Hz can't express sample-accurate note timing, which is why the doc flags a dedicated `note` domain as "the honest answer" even though it's the heavier option |

**The overall read:** none of the four proposed answers threaten Infinite's
real-time budgets (~2 ms/frame element, ~5.3 ms/block audio, GPU frame
time for pixel) — every cost lands in the tens-of-KB-to-tens-of-MB range,
well inside what the existing Feedback/Trails/RD nodes already spend. The
one number worth tracking going forward is OPEN-C's **~1 G texture
fetches/s at 1080p**, since that's the one place the catalogue itself
identifies an actual ceiling rather than headroom.

## Open questions requiring the owner's decision

1. Which OPEN-A option — and if (b), is raw indexing (c) ever added, or is
   a table need served by a separate, more restricted declaration?
2. OPEN-B: is "(b), reading the previous cook's value" acceptable given it
   means a neighbour read is always one frame stale, never same-pass?
3. OPEN-C: which spelling, and is the per-cell clamp/wrap/border decision
   made in the `state` declaration itself, or somewhere else?
4. OPEN-D: is a sixth `note` domain ever wanted for reasons beyond what
   folding note names into `frame` would already provide (e.g. sample-
   accurate note timing, which `frame`'s 60/s rate cannot give)?
5. **Sequencing**: do all four need answers before the next catalogue
   entries get built, or can they be answered and shipped one at a time,
   unblocking their dependent entries independently? (§9.7's rung-4 tier
   needs both A and D; the DSP-only entries in §6-8 mostly need only A, B,
   or C individually.)

## Sizing

This is language/compiler-design work (`src/core/field/FieldIR.h`,
`FieldParse.cpp`, the domain-inference fixpoint), not node-wiring work —
whoever picks up implementation after these are answered should load the
`field-compiler` and `field-domains` skills first. The four answers
themselves are a discussion, not a build step; expect each to also need a
short follow-up brief (grammar changes, IR representation, backend lowering
per domain) once decided, sized independently per question rather than as
one lump.
