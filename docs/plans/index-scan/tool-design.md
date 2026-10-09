# Index scan: tool-design cluster (Orca, Motion Canvas, Penrose)

NOTE: indexopensource.com is blocked; candidates are from memory and were NOT cross-checked against the index list. Skills loaded: codebase-navigation, codebase-lenses.

## Projects read
| Repo | Licence (verified from raw LICENSE) | Branch | Read |
|---|---|---|---|
| hundredrabbits/Orca | MIT (LICENSE.md, (c) 2017 Hundredrabbits) | main | desktop/sources/scripts/core/orca.js, operator.js, library.js (first 80 lines: A/B/C/D) |
| motion-canvas/motion-canvas | MIT (LICENSE) | main | packages/core/src/threading/ThreadGenerator.ts, flow/all.ts, signals/SignalContext.ts (first 200 lines), scenes/timeEvents/TimeEvents.ts |
| penrose/penrose | MIT (LICENSE) | main | README only (Domain/Substance/Style trio). No source read. |

Skipped: noflo/noflo - LICENSE and LICENSE.md both 404 on master, so licence unverifiable. No other peers fetched. Source of all three was only read, nothing ported.

## What Infinite does today
- Field source editor is a plain `ImGui::InputTextMultiline` in a side panel with explicit Apply/Revert buttons and one red error line: `src/app/frame/StageSidePanels.cpp:808-835` (pixel; same pattern at :625, :695, :759). Edits do not recompile until Apply.
- Compile errors carry a structured span (`SourceSpan{offset,line,col,length}` plus `hint`) in `src/core/field/FieldError.h:10-34`, but nodes flatten it to a string, e.g. `src/nodes/FieldPixelNode.cpp:1021-1037`, `FieldPrimitiveNode.cpp:760-781`. Grep `span\.(line|col)` outside selftest: only these string-building sites, nothing that highlights text.
- Keep-last-working and hot-reload state transplant exist: `src/core/field/FieldState.h:52`, `PinTable.h:124`.
- `param float p = a..b` exposes a modulatable knob (`StageSidePanels.cpp` hint text ~line 855; `ParamTable` in `FieldPixelNode.h:151`).
- Timeline is tick-based (kPPQ=960, `src/arrange/ArrangeModel.h:29-31`) with sorted Markers (`ArrangeModel.h:230,292,458-478`) and marker navigation. Grep for ripple/insert-time/shift-later ops in ArrangeModel.h: nothing (only unrelated lane-reorder `shift` at ArrangeModel.cpp:1082).
- Step-grid style UIs already exist (DrumSequencerNode.h, NoteNodes.h, ModulatorNodes.h hits for step grid).

## What the peers do differently
- Orca (core/orca.js, operator.js): the whole program is one string grid; each frame `parse()` builds operators from glyphs, `operate()` runs them in scan order, and `lock()` marks ports an operator reads/writes so a cell is not written twice a frame (operator.js `run`). Ports are declared relatively (`ports.a={x:-1,y:0}`), upper/lower case = passive/active, `*` bang = trigger. Spatial adjacency is the patch cable.
- Motion Canvas: animation is a generator (`ThreadGenerator`, `yield*` sequence, `yield task` concurrent, `all()` join); properties are Signals with automatic dependency collection (`SignalContext.getter` startCollecting/finishCollecting), `.save()`/`.reset()`/`isInitial()` and tween queues; time events (`TimeEvents.set(name, offset, preserve)`) are named markers whose offset can be dragged on the timeline, optionally preserving the timing of later events.
- Penrose: separate declarative trio (domain / substance / style), layout solved by optimisation. Relevant idea is only separation of "what" from "how it looks"; no concrete tie to Infinite.

## Concrete improvements
Existing-node upgrades:
1. Field editor inline diagnostics (Field, UI/UX 4c, S-M). Use the existing `FieldError.span` (line/col/length) to underline/mark the error line and show `hint`, instead of the flat string. No peer code needed (Motion Canvas editor idea only). Skills: field-integration, node-ui-pillars.
2. Live apply with debounce plus keep-last-working (M). Orca and Motion Canvas both re-evaluate on edit; Infinite already has keep-last-working (`PinTable.h:124`) and state transplant (`FieldState.h:52`), so removing the Apply click is mostly UI. Risk: compile on main thread per keystroke; debounce ~300 ms. Skill: field-integration, field-state.
3. Timeline: "preserve following" for marker moves (S-M). Motion Canvas `TimeEvents.set(name, offset, preserve)` shifts one event while holding later ones in place; Infinite `MoveMarker` (ArrangeModel.h:460) moves one marker only. Optional ripple mode. Must bump `gArrange.revision` (codebase-navigation hotspot). Skill: timeline-arrangement-architecture.
4. Dependency-collected derived values for Field params (L, speculative). Motion Canvas signals recompute only when dependencies change; Infinite recompute is per-kernel anyway, little gain. Not recommended.

New nodes / ideas:
5. Orca-style glyph-grid sequencer node (M-L). A text grid where operators (add, clock, delay, random, note out) interact by adjacency, emitting notes/triggers. Distinct from the existing step grids because logic lives in the grid. Skill: new-audio-node (note out), rhythmic-quantization-standard for clock divisions. Licence MIT; if operator semantics (letters A-Z, lock rule) are copied closely, include Hundredrabbits MIT attribution in a source comment/NOTICE; a re-design with own glyph set needs none.
6. Named-cue generator scripting for timeline (L, speculative). Motion Canvas sequences via generators; Infinite's Field is declarative per-element and has no sequential-time construct (grep not exhaustive for "sequence" in field-language; not checked). Parked.

Nothing to learn: Penrose (constraint layout solving has no Infinite use); node-editor patch format (not read, noflo skipped).

## Candidates
Field error span highlighting | clearer live-coding feedback, data already exists | S | none (own design)
Marker move with preserve-following (ripple) | faster timeline retiming | S-M | MIT idea, no code copied
Orca-style glyph-grid sequencer node | new live-coding note generator | M-L | MIT, attribute only if near-port
Debounced live apply for Field editor | removes Apply click | M | none
