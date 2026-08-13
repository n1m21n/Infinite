# 09 — Note Modify

Paste everything below into a fresh Claude Code session.

---

Implement the Note Modify node in Infinite (/Users/namansoni/infinte).

Spec: docs/plans/audio/P3c-P3a2-design.md §2.4. Read that section in full,
plus §0 (shared infrastructure — 0.1, 0.3).
Category: Notes. Node shape: note processor.

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

Done when all seven of new-audio-node SKILL.md §6's criteria hold. Report
each one explicitly, including the ones that did not pass.

This node changes or suppresses note pitches, so it must keep a 128-entry
map from incoming note number to emitted note number, written on note-on and
read on note-off. A note-off that is not transformed identically to its
note-on leaves a voice stuck on. A note-on that was dropped must have its
note-off dropped too. See §2.3.

When finished, update docs/plans/audio/STATUS.md to mark Note Modify shipped.
