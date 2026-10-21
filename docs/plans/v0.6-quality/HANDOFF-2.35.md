# Handover: v0.6 Block 2, from 2.35 on (2026-10-14)

Branch `feature/v06-sound`, last commit `421d4052` (2.33 String). Do not push. Do not merge main.
Untracked file, do not commit.

## Where things stand

| Item | State |
|---|---|
| 2.1-2.33 (except 2.34) | committed, one commit each |
| **2.35 Show set** | **half written, not committed** (see below) |
| 2.36 Show safety | not started |
| 2.37 Plugin GL editor (Windows) | not started |
| 2.38 Bar-quantized launch | not started; reuses `MusicTime::LaunchQuantize` from 2.35 |
| End of block | `HANDOFF-block2.md`, refresh builtins-kit, `data_ledger.py lint`, final report |

## 2.35: what is already in the tree (uncommitted)

| File | What |
|---|---|
| `src/audio/AudioEngine.{h,cpp}` | Show-set tail: `ArmTail(switchBeat, fadeFrames)`, `DisarmTail`, `TailActive`, `LastSwitchBeat`, `SwitchLanded`, `ResetBlockPeakFloor`/`BlockPeakFloor`. The next `SetTopology` keeps the outgoing list as a tail. `RunTail` keeps the tail at full level and the new list muted until the switch beat. At the switch beat the new list ramps in over 5 ms and the tail fades out with a cos curve. While the tail plays, CompletedGeneration is held at the tail's generation so the retired nodes are not freed. `PollTail` runs in PumpMainThread. |
| `src/audio/MusicTime.h` | `LaunchQuantize` enum {none, beat, bar, 2 bars}, `LaunchQuantizeList`, `LaunchQuantizeBeats`, `NextLaunchBeat(beats, q, minLead)` |
| `src/platform/Platform.h` | `MidiProgramChangeSlot` / `MidiStoreProgramChange` / `MidiPollProgramChange` (one atomic word holding seq, channel and program); `OpenShowSetDialog` / `SaveShowSetDialog` (`.infshow`) |
| `Platform.mm`, `win/MidiWin.cpp`, `linux/MidiLinux.cpp` | Program Change (0xC0 / `SND_SEQ_EVENT_PGMCHANGE`) stored in the slot |
| `Platform.mm`, `win/PlatformWin.cpp`, `linux/PlatformLinux.cpp` | the two show-set dialogs |

Build: these pieces compile clean on macOS (`cmake --build build -j6`, exit 0, 2026-10-14). Nothing has run yet, and nothing calls `ArmTail` yet, so behaviour is unchanged.

## 2.35: still to write

1. **`src/app/graph/ShowSet.cpp`** (add it to CMakeLists, then `cmake -S . -B build`), with declarations in AppShared.h:
   - Entries `{path, name, shared_ptr<Patch::Data>, future, mtime, error}`. `Patch::Read` runs on `std::async` when an entry is added or a set is opened; re-read when the file's mtime changes.
   - `ShowSetGo(index)` and `ShowSetStep(+1/-1)`. Quantize defaults to bar; fade defaults to 1000 ms (0-8000).
   - `ShowSetTick(frameId, window)`, called in StageFramePump.cpp after `PollOscListeners()` (line ~359):
     - Poll MIDI Program Change: program n goes to entry n+1, i.e. index n.
     - A second switch waits while `TailActive()`.
     - Target beat = `NextLaunchBeat(Beats(), q, 0.25)`. Load when target - now ≤ 1 beat, or at once for none/beat or when the transport is stopped.
     - Load sequence:
       1. Capture the projectors.
       2. Call `ArmTail(playing && q != none ? target : -1, fadeMs * sr / 1000)`.
       3. Copy the Data and overwrite `data.transport` with the current tempo, time signature, key and scale, so the tempo does not jump before the switch.
       4. Call `LoadPatchDataImpl(copy, path, false)`. On failure call `DisarmTail` and end the hold.
       5. Remap the windows: the k-th window goes to the k-th OutputNode, extra windows go to the last Output, and with no Output the window's nodeIndex is -1, so it closes after the hold.
       6. If the patch had unsaved edits, say in `gPatchStatus` that they were discarded.
     - Apply the entry's own transport record when `SwitchLanded()`, or when no tail was armed.
     - Release the hold when the switch has landed, at least 3 frames have passed since the load, and the new Output's texture is non-zero (cap 120 frames).
   - Projector capture: in the main context, blit each window's last live texture (recorded by the hook) into a show-set-owned `GLUtil::Fbo` (`GL_RGBA16F`) using a temporary read FBO and `glBlitFramebuffer`.
   - Hooks, both in StageTestsB.cpp's projector loop (~line 764):
     - At the very top of the loop body, before the `src == nullptr` close: `if (ShowSetDrawHeldProjector(i)) continue;`. It makes the context current, draws the held texture with `DrawTextureToScreen` and calls glFlush.
     - After tex/texW/texH are computed: `ShowSetNoteProjectorTexture(i, tex, texW ? texW : pw, texH ? texH : ph)`.
     - StageTestsB.cpp has another session's 18-line hunk: stage only my hunk with `git apply --cached`.
   - Save/open `.infshow`: one absolute path per line.
2. **Window** "Show Set": write it in StageSidePanels.cpp (next to the Formula editor, ~line 716) so it can use `BeginEditorWindow`/`EndEditorWindow`. It has:
   - the list with the current entry highlighted
   - Add current patch, Add file (`OpenPatchDialog`), Remove, Up/Down
   - Open/Save set, Prev/Next
   - the quantize combo (`FieldWell::PushStyle` + `MenuParts::BeginCombo`/`Choice`, with no chevron)
   - the fade slider (`FieldWell::Slider`)
   - Add the `bool gShowSetOpen` global in AppState.cpp and AppShared.h.
3. **Menu**: a "Show Set" submenu in the Menu tile, StageMenuBar.cpp, before `SubMenu(L("Nodes"))` (~line 384). It has a Check for the window, plus Next (PgDn) and Previous (PgUp).
4. **Keys**: PageDown = next and PageUp = previous, in StageKeyboard.cpp (`!typing` guard) and in `ProjectorKeyCallback` (Projector.cpp:445). Add rows to the "All shortcuts" help in HelpWindows.cpp.
5. **OSC** (OscApi.cpp, plus its header comment list): `/infinite/show/next`, `/infinite/show/prev`, `/infinite/show/go <index>`.
6. **i18n**: every new L()/T() string needs a tsv line in `resources/lang/{de,es,ja,ru,zh}.tsv`. I18NTEST checks this.
7. **SHOWSETTEST** (frame-driven, from ShowSetTick when the env var is set):
   1. Start audio (`StartAudioEngine`) if it is not running, and set the tempo to 120 with the transport playing.
   2. Build patch A: Oscillator 220 Hz -> Audio Out, and a red Formula (`return vec4(1,0,0,1);`) -> Output. Save it to /tmp. Build B with 330 Hz and blue, and save that too.
   3. Load A and add A and B to the set. Open a projector on the Output (`OpenProjectorWindow`, as in StageBenchHarness.cpp:319).
   4. Measure the pre-switch peak floor, then request Next mid-bar with q = bar, then switch back with q = none.
   5. Gates: `BlockPeakFloor` ≥ 0.5x the pre-switch floor; xruns delta 0; `LastSwitchBeat` within 2 samples of a bar line; latency ≤ 1 bar for a request made more than 0.25 beat before the line; black projector frames = 0 (centre pixel read in the hook, test-only); the window ends up on B's Output.
   6. Add `"SHOWSETTEST:1"` to driver.sh in both places, after `"STRINGTEST:1"`.
8. Build. Run Tier 1 (`driver.sh --fast`), then PINDUPTEST, AUDIOTEARDOWNSWEEPTEST, I18NTEST and AUDIOPARAMSWEEPTEST (grade expected `298 0 0`).
9. Fill in README row 2.35 (line 107) as "**Done.** … Decided: …" with "Measured: …" in the last column. Add the NOW line with `now.py`, anchor `"- 2026-10-14 Agent: v0.6 2.33 done."`. Commit `v0.6 2.35: Show set - ...`.

Decisions already settled for 2.35:
- A switch discards unsaved edits without asking (it's a live show) and says so in the status line.
- The old patch's sequencers keep running through the fade, so it is a real crossfade.
- A load that finishes late switches at the next block.
- The 0.25-beat lead means a request made right before the bar line lands one bar later (worst case 1 bar + 0.25 beat). Write this down as "Decided".

## Rules (unchanged)

- One commit per item. Stage explicit paths only. No Co-Authored-By and no "Generated with" lines. No push.
- Never commit:
  - `tools/roadmap/{audit,metrics,quests}`
  - `docs/node_param_audit.md`, `external/vst3sdk`
  - the HANDOFF-*.md files, `docs/handoff/`, `docs/plans/agent-language/`, `docs/plans/two-laptops/`
  - other sessions' files: `scripts/sweep_runner.sh`, `scripts/audio_param_sweep_check.sh`, `scripts/{install_laptop_sync,sync_laptops}.sh`, `scripts/dispatch_watch.py`, `AGENTS.md`
  - the NOW.md "Laptops" hunk
- Build with `cmake --build build -j6` in the background. Run `pgrep -fl "MacOS/Infinite"` before any test.
- Pause and report after each committed item.

## Must be in the final report

- **ALIASTEST xfails**: Cycle Shaper sine @500 −90.2 vs −105; Analog FM −47.9 vs −54.
- **Test status**:
  - RECEXPORTTEST's stale baseline (now passes).
  - PLUGINDRAGTEST is flaky.
  - SHAPERESONATORTEST and FIELDGRAPHTEST are not in the driver.
- **Block 1 merge is blocked**: the owner runs `git merge --no-ff bugfix/v06-audio-correctness` in `/Users/namansoni/infinte-main`.
- **Still to run**: 1.7 and 1.14 need quiet-Mac runs.
- **Not compiled locally on Windows/Linux**: 2.6, 2.15-2.17, 2.24-2.26, 2.31-2.33, 2.35. 2.31 changed NetCompat.h and the OscControl sockets. 2.35 touches MidiWin, MidiLinux and both platform dialog files.
- **Audits**: FXAUDIT Dynamics click +9.0 dB; MODAUDIT FRATE Envelope.
- **Known gaps**:
  - Sampler has no Note Expression.
  - `INFINITE_NO_FMA` moves macOS scalar output by a few ulps.
  - The FM default patch has a −39 dB non-harmonic component.
  - 2.24's 64-tracks-at-70% gate is not met on 4P+4E.
  - Three unwrapped i18n literals: BusyLine.h:50, ArrangeRender.cpp:558 and :578.
- **Notes on items 2.25-2.33**:
  - 2.25: the diode is darker than the ladder by design.
  - 2.26: pinch aliases at −50.6 dB.
  - 2.31: OSC is loopback only, with no token.
  - 2.32: a high note with formant 0 gets quieter, by design.
  - 2.33: String|release xfails the param sweep because the rig sends no note-off, and a bowed note takes up to about a second to settle.

## 2.36 Show safety: checkpoint (2026-10-14, paused by owner)

2.35 is committed (6e4613f9). For 2.36 this is in the tree, uncommitted, and builds clean:
- `external/imgui-node-editor/imgui_node_editor.{h,cpp}`: `ed::SetEditsLocked(bool)` / `AreEditsLocked()`. While locked, `DragAction::Accept` and `CreateItemAction::Accept` refuse: no node drag, no cable drag from a pin. Documented as a row in `docs/plans/i18n/imgui-patches.md`.

Design settled (not written yet):
- **Show lock** (`gShowLock`, Cmd/Ctrl+Shift+L, Show submenu): calls `ed::SetEditsLocked`. Also blocks:
  - delete: `doDelete` and the X-deletes-cables path in StageKeyboard.cpp (~274, ~327); the `ed::BeginDelete` loop (~871), via `RejectDeletedItem`; the cable trash in StagePopupsB.cpp (~640)
  - `Undo()` / `Redo()`, checked inside the functions so the menu is covered too
  - the status line says "Show is locked".
- **Panic** (Cmd/Ctrl+Period, also in ProjectorKeyCallback; Show submenu). It latches; press again to resume.
  1. Request `MidiOutNode::Panic` and the plugin note flush (`mFlushRequested`) on every node.
  2. Stop the transport.
  3. `ArmTail(-1, min(5 ms, block))`, then publish an empty topology. The old list fades out in the first block and then plays nothing.
  4. While panicked, `RebuildAudioTopology` publishes an empty topology and skips PrepareToPlay.
  5. Resume: set every audio node's `preparedForSampleRate = 0`, then `RebuildAudioTopology` (PrepareToPlay resets voices and tails).
  6. Projectors: ShowSet.cpp gets `ShowSetPanicHold(on, window)`. It captures if not already holding, `ShowSetDrawHeldProjector` honours it, and it cancels any pending show-set request.
- **Pre-show check** (Show window card plus a menu item). It lists:
  - every `node.Issue()`
  - Text params named path/pathB/filePath/file/folder whose file is missing
  - audio device not open (`gAudioStartError`)
  - a MIDI Out whose `device` is not in `MidiOutListDevices()` or whose `DeviceError()` is set
  - MIDI CC/Trigger `IsBound()` with an empty `MidiDeviceName(device)`
  - a projector whose `returnToMonitor` is set (its display is gone)
  - an Output with nothing cabled into "in"
  - audio nodes with no Audio Out.
- **Quit watchdog**: in main.cpp right after the loop exits, a detached thread waits 4 s, then fflush and `std::_Exit`. Test hook `INFINITE_QUITSTALLTEST` sleeps 30 s inside `AudioEngine::Stop` and prints OK from the watchdog (FAIL if Stop returns).
- **SHOWSAFETYTEST**:
  - a broken patch with a missing sample, a missing plugin id, a MIDI Out on an absent device, and an Output with no input: the check lists all four
  - panic: the block peak after the request is ≤ −60 dB by the end of the first block
  - lock: an injected node drag does not move the node, and a knob still sets its param.
