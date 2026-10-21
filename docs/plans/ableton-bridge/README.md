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
| Live Object Model over OSC (AbletonOSC) | full control of tracks, clips, devices, parameters | yes (any edition, Live 11+) | Documented LOM, open-source Remote Script; no Max for Live needed. M4L stays an alternative |
| Remote Scripts (Python in Live) | control surface API | yes | Unofficial and unstable; not used |

## Prior art (found online, 2026-10-14; licences to be confirmed from each LICENSE file before any reuse)

| Project | What it is | Use for us |
|---|---|---|
| [AbletonOSC](https://github.com/ideoforms/AbletonOSC) (ideoforms, NIME 2023 paper) | A Remote Script that puts the **whole Live Object Model on OSC**: listens on UDP 11000, replies on 11001, addresses mirror the LOM (`/live/clip/...`, wildcards allowed), Live 11+. Reported MIT | The live tier, with no Max for Live needed. Infinite speaks OSC; the user installs one folder into Live's Remote Scripts. Replaces the M4L spike |
| Ableton MCP servers (`ahujasid/ableton-mcp`, `Simon-Kansara/ableton-live-mcp-server`, LiveMCP and others) | An MCP server plus a Remote Script and a local socket (JSON over TCP, or OSC) | Proof that agents already drive Live; confirms the architecture. All need Live running; none is headless |
| `pyableton` (PyPI), `@pohy/ableton` (npm), `bravoh-daw` (Rust), `daw-schemas` (JSON schemas incl. a Live 12 `.als` schema) | Parsers for `.als` (gzip + XML), clips, MIDI export | The offline tier: use the schema as a reference for our own small reader; do not add a dependency for what gzip + XML parse does |
| PRONOM / Ableton forum threads | Confirm `.als` is gzipped XML from 8.2.1 on; warn to work on copies | Rule: the writer never edits a user's set in place |

Consequence: the **live tier becomes AbletonOSC**, not an M4L device. Still true: nothing runs
without Live open, and no source of closed Live internals is read.

## Headless (owner requirement, 2026-10-14)

Live itself cannot run headless: no CLI, no render flag, and its Python API exposes no export or
freeze (confirmed by several MCP projects, which fall back to GUI scripting or real-time capture
through a virtual audio device). So "headless Ableton" is built the other way round:
**Infinite is the headless engine; the `.als` file is the interchange.**

| Need | Headless route | Limit |
|---|---|---|
| Read a Live set | `als.py` (gzip + XML), no Live | none |
| Play or render a Live set | Convert to an Infinite patch: MIDI clips and tempo to note nodes, audio clips to sample players, Live's built-in devices to the nearest Infinite node; render with the CLI (`--render`, `--audio-summary`) | Plugin state and Live-only instruments are opaque; they become silent placeholders or a stem the user exports once |
| Write a Live set | `.mid` and minimal `.als` from Infinite data | Opens in Live as plain tracks and clips |
| Live-only action (fire clips on a running set) | AbletonOSC, needs Live open | Not headless; optional, never required |

Rule: no feature of the bridge may require Live to be running except the live tier, and that tier is
marked as such in the node UI.

## Decision

Two tiers, in order:

1. **Offline file bridge** (the CLI part). `tools/ableton-bridge/als.py`: read an `.als` into a
   small JSON (tempo, tracks, clips with notes and times, audio file refs) and write a minimal
   `.als` or `.mid` from Infinite data. Commands: `als-read`, `als-notes`, `als-write-midi`. Works
   with no Live running, on the Air, in tests. Fixtures: the user's own sets, never committed.
2. **Live link**: MIDI clock/notes plus Ableton Link for tempo, then AbletonOSC for clip and parameter control. Only after tier 1 proves what data is worth moving.

## User must have

Ableton Live (any edition for tiers 1 and Link; Suite or Max for Live for the LOM device). Without
Live, `.als` reading still works; the live link nodes show "Live not found".

## Plan

| Step | Work | Done when |
|---|---|---|
| 1 | Inspect real `.als` files (Live 12.x): write `als.py` reader, schema notes in `SCHEMA.md` | tempo, tracks, MIDI notes of 3 sets match what Live shows |
| 2 | `.mid` writer for Infinite note data, plus Infinite import path check | clip opens in Live with the same notes |
| 3 | Set writer for a minimal valid `.als` (one MIDI track) | Live opens it without a repair prompt |
| 3b | `als2inf.py`: set to Infinite patch (notes, tempo, audio clips; devices mapped by a small table) and render it headless | a real set renders audio from the CLI with the right tempo and notes |
| 4 | Link tempo sync spike; licence decision recorded | tempo follows Live within one beat |
| 5 | AbletonOSC spike: Infinite sends `/live/clip/fire`, reads tempo | one clip fires from Infinite, tempo read back |

## Limits

- Plugin state inside a set is opaque binary; it is carried as is, never edited.
- Each Live version can change the XML; every release of the bridge records the Live version it was
  tested on.
- No Live source or SDK internals are read; only the documented formats and the file we are given.
