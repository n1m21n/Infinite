# 03 — Delay

Paste everything below into a fresh Claude Code session.

---

Implement the Delay node in Infinite (/Users/namansoni/infinte).

Spec: docs/plans/audio/P3c-P3a2-design.md §1.3. Read that section in full,
plus §0 (shared infrastructure — 0.1, 0.4, 0.5).
Category: AudioEffects. Node shape: audio effect.

Procedure: .claude/skills/new-audio-node/SKILL.md — prescriptive, follow it.
Body layout: .claude/skills/audio-node-ui/SKILL.md and
docs/plans/audio/audio-node-ui-system.md. Do not re-derive either; both have
been through multiple revisions and the reasons are written down.

Four rules that override anything you infer:
1. Clean room: do not open, read, grep or reference
   /Users/namansoni/BespokeSynth. Implement the DSP from the primary
   reference named in the spec section, not from any implementation.
2. Two objects: the INode (main thread) owns an AudioNode (audio thread);
   they communicate only through ParamMailbox and MeterRing.
3. CookIfNeeded does no DSP — drain meters, push dirty params, budget < 5 us.
4. On the audio thread: no allocation, locks, dynamic_cast, std::function/
   map/string, GL, ImGui, file I/O, or printf.

Reference nodes already in the tree: GainNode (smallest complete),
WavetableNode (largest, with its own DSP tests), EnvelopeNode (note-in,
modulator-out).

Write the DSP fixture alongside the node, not after — the spec section lists
the assertions. Then run /run-infinite-hygiene.

Two items from docs/plans/audio/gaps-addendum.md apply to this node — read A4
and A5 there:

- **A4: add a fifth `multitap` mode** to the four in §1.3 (simple / ping-pong
  / multiband / stutter), with tap count and per-tap time / level / pan. It is
  the same fractional-delay line, read at several offsets.
- **A5: decide the stutter question rather than inheriting it.** §1.3 itself
  hedges — "Stutter is the loosest fit here — split it out if the UI fights."
  You are the session where it either fits or does not. Either build it as a
  real mode with its own Tier 2 section, or split it into a separate Repeater
  node and say so. Record the decision and the reason in STATUS.md; do not
  leave it half-in.

Declare `prerequisites` / `uiOnly` on every gated param in your table entry —
the mechanism the Dynamics session added to `EffectParamDef` for exactly this.
Mode-gated params (the stutter and multitap controls, the ping-pong offset)
are the case it exists for, and the param sweep reports false failures
without it.

Done when all seven of new-audio-node SKILL.md §6's criteria hold. Report
each one explicitly, including the ones that did not pass.

When finished, update docs/plans/audio/STATUS.md to mark Delay shipped.
