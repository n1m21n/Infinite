# Ableton bridge

Goal: Infinite and Ableton Live exchange music data both ways: Live's arrangement, clips, tempo and
audio into Infinite (to drive visuals and sound nodes), and Infinite's notes, audio and tempo back
into Live.

## The hard fact (differs from Blender)

Live has **no command line and no headless mode**. "All Ableton features as CLI commands" is not
possible; there is no catalogue to ask. What exists, by layer:

| Layer | What it gives | Needs Live running | Notes |
|---|---|---|---|
| Set file `.als` | gzipped XML: tracks, clips, notes, tempo, device chains, automation | no | Read and write offline. Schema is undocumented and changes per Live version; version-lock and validate by re-opening |
| Exports | audio stems, MIDI clips (`.mid`), `.alc` clips | no (the user exports) | Already importable by Infinite's MIDI/audio nodes |
| MIDI and virtual ports | notes, CC, clock, transport | yes | Infinite already has MIDI in and out plans (`docs/plans/midi-out`) |
| Ableton Link | shared tempo, beat and phase on the local network | yes | Open protocol, GPL-2.0-or-later with a commercial option: licence check before linking it into an MIT app; or implement only the documented behaviour, no source reading |
| Max for Live / Live Object Model | full control of tracks, clips, devices, parameters | yes, needs Max for Live (Suite) | Official, documented LOM; a small M4L device can speak OSC to Infinite |
| Remote Scripts (Python in Live) | control surface API | yes | Unofficial and unstable; not used |

## Decision

Two tiers, in order:

1. **Offline file bridge** (the CLI part). `tools/ableton-bridge/als.py`: read an `.als` into a
   small JSON (tempo, tracks, clips with notes and times, audio file refs) and write a minimal
   `.als` or `.mid` from Infinite data. Commands: `als-read`, `als-notes`, `als-write-midi`. Works
   with no Live running, on the Air, in tests. Fixtures: the user's own sets, never committed.
2. **Live link**: MIDI clock/notes plus Ableton Link for tempo, then an optional M4L device over OSC
   for clip and parameter control. Only after tier 1 proves what data is worth moving.

## User must have

Ableton Live (any edition for tiers 1 and Link; Suite or Max for Live for the LOM device). Without
Live, `.als` reading still works; the live link nodes show "Live not found".

## Plan

| Step | Work | Done when |
|---|---|---|
| 1 | Inspect real `.als` files (Live 12.x): write `als.py` reader, schema notes in `SCHEMA.md` | tempo, tracks, MIDI notes of 3 sets match what Live shows |
| 2 | `.mid` writer for Infinite note data, plus Infinite import path check | clip opens in Live with the same notes |
| 3 | Set writer for a minimal valid `.als` (one MIDI track) | Live opens it without a repair prompt |
| 4 | Link tempo sync spike; licence decision recorded | tempo follows Live within one beat |
| 5 | M4L device spike (OSC) | one clip fire from Infinite |

## Limits

- Plugin state inside a set is opaque binary; it is carried as is, never edited.
- Each Live version can change the XML; every release of the bridge records the Live version it was
  tested on.
- No Live source or SDK internals are read; only the documented formats and the file we are given.
