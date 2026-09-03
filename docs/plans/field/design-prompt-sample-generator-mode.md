# Design prompt: FieldSample generator mode (freq/gate reserved symbols)

Status: **implemented**, on `feature/field-sample-generator-mode`. `freq`/
`gate` landed as reserved sample-domain symbols per this doc; see
`docs/plans/field/step-09-sample-domain.md` §0.5 for exactly what changed and
where. Originally saved 2026-09-03 during the node-UX pass
(`feature/field-node-ux-fixes`) at the user's explicit request — they wanted
to reason through it as its own session rather than fold it into a UX-fix
branch.

## The gap this closes

`FieldElementNode` and `FieldPixelNode` can both act as generators when
nothing is wired into their input (Element: a fresh point set sized by
`generateCount`, per this session's fix; Pixel: an unconnected `src` is
already a valid 1x1 black texture, so any kernel that only writes `col` is
already a generator in practice). `FieldSampleNode` cannot: its kernel only
ever reads `in`, and there is no way to express "make a tone" from inside
Field. Today, building a synth voice out of a FieldSampleNode requires
patching a separate oscillator node into `in` first — which the user found
confusing enough to flag directly ("why do i even need an audio source
first itself? is the field sample not able to generate audio natively").

## What's actually being asked

Add `freq` and `gate` (naming below is provisional, see open question 1) as
new **reserved sample-domain symbols**, following the exact precedent of
`in`/`out`/`sr`/`n` (`src/core/field/Infer.cpp`, seeded read-only per
`.claude/skills/field-language/SKILL.md` §5). A kernel could then write
e.g.:

```
state float phase = 0
phase = phase + freq / sr
phase = phase - floor(phase)
out = sin(phase * 6.283185) * gate
```

and get a working oscillator voice with no upstream audio patch — `notes`
already feeds the per-voice register machine (see `NoteInputSlot`,
`FieldSampleNode.h`), so `freq`/`gate` are just exposing values the voice
dispatch already computes internally but currently discards.

## Why this is a real design decision, not a small add

1. **Where do `freq`/`gate` come from per-voice?** The per-sample kernel
   runs inside `AudioFieldSampleNode::ProcessBlock`'s per-voice loop
   (`src/nodes/FieldSampleNode.cpp`). Need to confirm: is there already a
   per-voice note-frequency/gate value available at that point (from the
   `NoteCable`'s active-note tracking, same machinery `WavetableSynthCore`
   or `SamplerNode` use for voice allocation), or does one need to be
   computed fresh? Reuse existing voice-alloc code rather than inventing a
   second one.
2. **Naming.** `freq`/`gate` are the obvious choices but check against the
   existing convention table in `.claude/skills/field-language/SKILL.md` §5
   before committing — e.g. does any other domain already use `gate` or
   `freq` for something incompatible, and should this be `hz` to match any
   existing unit convention elsewhere in Infinite's synth nodes?
3. **Interaction with `in`.** `in` and `freq`/`gate` are not mutually
   exclusive — a kernel could read both (e.g. a synth voice that also
   applies a filter driven by `in` as a sidechain). Don't gate their
   availability on whether `input` is connected; they should simply always
   be reserved and readable, exactly like `sr`/`n` are always available
   regardless of connection state. (This differs from FieldElement's
   `generateCount`-when-unconnected pattern — don't copy that pattern here
   without re-checking it's actually appropriate.)
4. **Silence when no notes are active.** If no note is currently gating the
   voice, what does the kernel see — `gate == 0` and `freq` holds last
   value, or does the voice not run its kernel at all (current behavior
   presumably: per-voice; confirm how voice lifecycle already handles
   idle/no-note voices before adding these symbols, so `state` cells reset
   correctly on note-on exactly as the header's `state` doc comment already
   promises).
5. **Docs.** `docs/plans/field/step-09-sample-domain.md` §5 (reserved
   words) and `.claude/skills/field-language/SKILL.md` §5 both need the new
   symbols added once the naming is settled, plus a worked oscillator
   example alongside the existing one-pole filter example (§13).

## Suggested first step for whoever picks this up

Read `AudioFieldSampleNode::ProcessBlock` and its voice-allocation path in
full first (this session did not — the note-cable → per-voice frequency
plumbing this needs to tap into is understood by inference from
`WavetableSynthCore`'s pattern, not confirmed by reading `FieldSampleNode`'s
own voice code path end to end). Confirm the per-voice frequency/gate
values already exist somewhere in that loop before designing the IR/lexer
changes — if they don't yet exist, that's a bigger prerequisite than adding
two reserved names.

## Explicitly out of scope for this document

- The separate mutable/dynamic-pins design — see
  `docs/plans/field/design-brief-dynamic-pins.md` (a different, already-
  written brief from this same session).
- Any UI change to `FieldSampleNode`'s params panel — that's a follow-on
  once the language side is settled (would likely mirror this session's
  `generateCount`-slider treatment on `FieldElementNode`).
