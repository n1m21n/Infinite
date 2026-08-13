# 02 — Dynamics

Paste everything below into a fresh Claude Code session.

---

Implement the Dynamics node in Infinite (/Users/namansoni/infinte).

Spec: docs/plans/audio/P3c-P3a2-design.md §1.2. Read that section in full,
plus §0 (shared infrastructure — 0.4, 0.5).
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

Two things carried over from the Audio Filter session, both of which cost
almost nothing now and get expensive after five more effects land on the
existing shape. Do both as part of this session:

1. **Visualizer dispatch by id, not by name.** main.cpp ~5726 currently
   selects the effect body with `if (n->Def().name == "Audio Filter")`.
   §0.4 specifies a visualizer id on the table for exactly this. Dynamics is
   the second entry, so convert the branch to dispatch on that id and add
   yours alongside — before it becomes a seven-way string ladder in the draw
   path.

2. **Let the param sweep know which params are gated.**
   `.claude/skills/audio-node-sweep/SKILL.md` §"Blind spots" documents why
   Audio Filter reports false failures: the sweep moves one param at a time
   from spawn defaults, so a param that is inert until another param enables
   it looks identical to a dropped mailbox push. Dynamics is gated harder —
   `ratio` is meaningless in limit mode, hold/range are greyed outside
   gate/expand, the sidechain params do nothing until the external toggle is
   on. Add an optional prerequisite declaration to `EffectParamDef` (which
   params must be set to which values first for this one to be live, and a
   flag for UI-only params with no DSP meaning), have the sweep honour it,
   and confirm both Dynamics and Audio Filter then report green for the right
   reason. If you conclude a different mechanism is better, say why and do
   that instead — but do not leave the sweep reporting failures that everyone
   is expected to know are spurious.

Done when all seven of new-audio-node SKILL.md §6's criteria hold. Report
each one explicitly, including the ones that did not pass.

When finished, update docs/plans/audio/STATUS.md to mark Dynamics shipped, and
update the Audio Filter sweep note in the P4 section if item 2 resolves it.
