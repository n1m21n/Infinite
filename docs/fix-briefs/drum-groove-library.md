# R540 - Drum Sequencer groove library + bundled kit (verified brief)

Branch: feature/drum-groove-library (worktree /Users/namansoni/infinte-wt-drum-groove, off main aeefff74).
Source: PR #24 (ricardopalmieri/Infinite, branch turbo/windows-only), commit a3ab7319 "0.47.0".

## Verified facts

1. Licence: fork LICENSE is MIT, "Copyright (c) 2026 n1m21n" (same text as ours). Taking its DrumPatterns.h
   and DrumSequencerNode diff is licence-compatible. Credit Ricardo Palmieri (README already credits the fork).
2. Library file: src/nodes/DrumPatterns.h (780 lines, header-only table). 141 grooves (counted), 10 categories,
   each Groove = category, name, bpm (display only), rate, swing, 3 Parts (A verse / B bridge / C chorus), tones[8].
   Part = steps + 8 lane strings (X accent 1.0, x 0.75, o ghost 0.4, . rest, nullptr = silent lane).
   Lane roles fixed: kick, snare, closed hat, open hat, clap/rim, low tom, high tom, bell.
3. BLOCKER - step count. Our DrumSequencerNode::kMaxSteps = 8 (src/nodes/DrumSequencerNode.h), grid is 8x8.
   The PR's node has kMaxSteps = 128, numSteps int, editPage (16 steps/page). Part lengths in the library:
   8 (9 parts), 9 (9), 10 (3), 12 (30), 14 (3), 16 (328), 20 (3), 24 (9), 32 (29).
   So ~96 percent of parts are longer than 8; a straight port clamped to 8 (what the PR's ApplyPattern would do
   here) truncates nearly every groove into garbage. Library cannot ship before the node supports 32 steps.
   Recommended: kMaxSteps = 32, 16 steps per page, 2 pages (no need for 128). Confirm with owner.
4. Step-count touch points in our tree (grep kMaxSteps / numSteps):
   - src/nodes/DrumSequencerNode.cpp: audio mStepVel[kNumLanes][kMaxSteps] atomics, PushDirtyParams step loop (~632),
     PushGlobals clamp (~656), step index modulo in ProcessBlock (~194-223), Randomize/ClearPattern/RandomizeLane
     loops (~827-878), VisitParams lane%d_step%d loop (~696).
   - src/main.cpp: DrawDrumSequencerBody (~17509: stat line, grid loop ~17548, KnobInt("steps") ~17695),
     per-cell click handling (~17303), RunDrumSequencerFixture (~55810) fixture + save/load round trip (~56437).
   - Save/load: params are per-cell names lane%d_step%d. Raising kMaxSteps only ADDS names (steps 8..31), so old
     patches load (missing keys keep default 0). Verify the loader tolerates absent keys. New patches opened in an
     old build lose steps >= 8: acceptable, note in release notes.
   - Memory: 8 lanes x 32 steps floats x 3 copies (UI, shadow, audio atomics) is trivial.
5. Reference implementation in the PR (read from the fork's MIT diff, 182 lines in DrumSequencerNode.*):
   ApplyPattern(groove, part) sets numSteps/rate/swing/stepVel/laneAccentPitch + patternName/patternPart;
   LoadKitIntoEmptyLanes(kitDir) loads KitFile(lane) into empty lanes and puts closed+open hat in choke group 1;
   new per-lane param lane%d_accentPitch (semitones added to velocity>=0.99 steps; audio side
   mLaneAccentPitch atomic, PushLaneAccentPitch, applied in voice rate); new saved params patternName (Text),
   patternPart (Int). Picker UI in the PR (main.cpp +316 lines) is Turbo style: group dropdown, groove < >, A/B/C
   buttons. Do NOT copy it; redraw in our UI (see Scope).
   The PR also fixes a crash: a lane whose sample is replaced retires the old buffer while voices still read it
   (mSampleSlots[lane].SwapIn() now returns bool; voices of that lane are deactivated). Our tree has the same
   pattern: AudioDrumSequencerNode::ProcessBlock does `for (auto& slot : mSampleSlots) slot.SwapIn();` and ignores
   the result, although SampleSlot::SwapIn() (src/audio/SampleSlot.h:121) already returns bool. Voices keep their
   old buffer pointer, so this is a probable use-after-free on drop-onto-sounding-lane (not reproduced by me). Treat as a separate bugfix
   branch (bugfix/drum-lane-swap-voices), re-verified by the implementer; do not bundle.
6. Kit provenance (8 mono 16-bit 44.1 kHz WAVs, ~13 kB-62 kB each, assets/drumkits/turbo-basic/). Checked by
   downloading to the scratchpad: no LIST/INFO metadata, no embedded strings; every file peaks at exactly 29162
   (0.89 FS, i.e. script-normalised); kick zero-crossing frequency falls 206 -> 59 -> 44 Hz (pitch-dropped sine);
   low tom 118 -> 103 Hz; bell steady ~882 Hz. This reads as synthesized, not sampled. BUT the PR ships no generator
   script, so provenance is asserted only by the commit message ("bundled synthesized kit"), not provable.
   Decision: do not ship those WAVs. Write our own tools/make-drumkit.py (numpy, deterministic, MIT, committed),
   generate assets/drumkits/infinite-basic/ from it, and keep the same 8-lane file naming so the table maps 1:1.
   This removes any licence question and is cheap (8 simple voices).
7. Bundling: no existing runtime path for non-icon audio assets; CMakeLists.txt copies fonts/icons into
   Resources (macOS bundle ~L1279 RESOURCE, plain copy ~L969-1022 for non-bundle). Add drumkits the same way for
   macOS bundle, Windows and Linux; resolve the dir through an existing Platform:: resource-dir helper (grep it;
   do not hand-roll a path). If none exists, add one three-sided (macOS/Windows/Linux).
8. Content risk: ~25 entries are song-named ("Billie Jean feel (Michael Jackson)", "Amen break feel (The Winstons)",
   "We Will Rock You feel, Queen", ...). Drum rhythms are not copyrightable the same way as melodies, but titles
   with artist names in a shipped MIT product is a call for the owner. Recommend renaming to generic style names
   (e.g. "Four-on-snare backbeat", "Break A") and dropping artist attributions. Flag as OPEN QUESTION below.

## Plan (3 blocks max, commit per step, one branch)

Block 1 - step count to 32 (own commits, own fixture):
  a. kMaxSteps 8 -> 32 plus editPage (16/page) in DrumSequencerNode; update every loop in point 4.
  b. Grid UI: 2 pages of 16 columns with a page toggle in our node-ui-pillars style; playhead highlight follows
     page. Load node-ui-pillars + audio-node-ui BEFORE editing Draw*Body.
  c. RunDrumSequencerFixture: add a 32-step render (step 20 fires at the right sample), save/load round trip of
     step 31, old-patch (8-step) load.
Block 2 - library + kit:
  d. Port DrumPatterns.h (credit header: from PR #24, MIT). Rename song-titled entries per owner decision.
     Add a self-test: every Part.steps <= kMaxSteps, every lane string length >= steps, rate index valid.
  e. ApplyPattern, laneAccentPitch (ParamRef decision below), patternName/patternPart persistence.
  f. tools/make-drumkit.py + assets/drumkits/infinite-basic + CMake copy + LoadKitIntoEmptyLanes.
Block 3 - picker UI:
  g. Picker in node body: category dropdown (long list filter box if existing combo helper has one), groove < >,
     A/B/C segmented buttons, "Load kit" button (or auto-load into empty lanes on first pick; recommend explicit
     button, owner call). PushUndoCheckpoint before ApplyPattern. Label lanes with LaneRole().
  h. node help text (project_node_help_coverage: 4 hand-kept tables), docs/node_param_audit.md regenerate
     (node-param-audit), ARCHITECTURE.md Node Library row.

## Judgment calls left open (recommendation given)
- kMaxSteps 32 vs 128: recommend 32 (library max is 32).
- laneAccentPitch: new per-lane knob = new ParamRef question (3a/2a). Recommend NOT modulatable, saved only;
  surface it only as a read-only "two-tone" indicator, not a knob (KHS-minimal, node UI stays 8 lane cards).
- Song names: rename (see point 8).
- Whether picking a groove also overwrites rate/swing: PR does; recommend yes but one undo step.

## Skills the implementer must load
new-audio-node (#5 minimalism), audio-node-ui, node-ui-pillars, audio-node-sweep (param round trip + teardown),
audio-pipeline-sweep (step firing after the 32-step change), rhythmic-quantization-standard (rate values come from
src/audio/MusicTime.h only), windows-parity + linux-parity (resource dir, CMake copy), invariant-interaction-audit
(numSteps vs paging vs ApplyPattern vs Randomize: can a later control undo the pattern's step count?).

## Out of scope
Claude chat panel, MCP drum_pattern RPC (can follow later as its own quest), Chord Progression qualities, any
other 0.47-0.48 content from PR #24, the SwapIn crash fix (separate bugfix branch, see point 5).

## Build / verify
  cmake --build build -j"$(sysctl -n hw.ncpu)"
Then the self-test route in run-infinite-hygiene (DRUMSEQ fixture) must pass; confirm compiles clean on all three
platforms' code paths (no _WIN32 in nodes). Do not UI-script the canvas; owner verifies the picker visually.
