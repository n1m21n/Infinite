# Node polish, round 2: execution prompt

Paste this into a new session on branch `feature/ui-canvas` (or a fresh branch off it).
Load first: `infinite-design-system`, `node-ui-pillars`, `run-infinite-hygiene`, `codebase-navigation`, `bug-blast-radius`.
Read `docs/plans/ui-system/node-polish-brief.md` for R1-R12 and I1-I13; this file only lists what is left.

Rules that still hold: one commit per step, gates green first (`driver.sh --skip-build --group ui,modulation[,audio]`), final `driver.sh --auto`.
No Claude attribution in commits. Commit by explicit path; never stage `.mcp.json`, `.gitignore`, `art/*`, `glyph-sheet.png`, `docs/plans/iconography/inventory.md`, `external/vst3sdk`.
Never UI-script the canvas; use the gallery env hooks (`tools/design/gallery/`). Fix in shared components, never per call site.
Decide yourself and report; do not hand design decisions back to the owner.
A step is done only when its Acceptance line holds on screenshots in dark and light.

State at hand-off: steps 1-5 committed (last: Arpeggiator `sync` rename, after 535d4f54). `driver.sh --auto` was green (47 passed). Desktop app deployed.
Execute one block at a time, in this order: A, B, K, D, H, F, E, G, I, J, then C. Each block ends with Block C's gates (inventory/ratchet, driver groups, `--auto`, deploy, before/after sheets for what it touched).
Lesson from last time: renaming a param label can duplicate a pin id on the same node (`PINDUPTEST`). Run `--auto` after every label rename.

---

## Block A: bugs the owner just found (do these first)

### A1. Popups ignore canvas zoom badly (dropdown and right-click menu)
Screenshots: Arpeggiator mode dropdown and the node right-click menu are huge next to a node drawn at ~0.2 zoom.
`PopupZoomNow()` (`src/app/AppShared.h:1318`) returns `clamp(canvasZoom, 0.5, 1)`, so a zoomed-out canvas still gets a half-size popup that dwarfs the node. The node context menu is not on that path at all and is drawn at 1.0.
Needed: one popup-scale system, not per call site.
- Decide the rule and write it in `infinite-design-system`: popups are chrome, not canvas content. Proposal: screen-space constant (scale 1.0) at any zoom, anchored next to the control, never covering the control it edits, flipped/clamped to the window. Check high zoom too (canvas 2x must not leave a tiny popup).
- Route every popup through it: dropdown (`gDropdown`), modulation binding menu, colour picker, node context menu, link/pin menus, tooltips. List them with `grep -rn "PopupZoomNow\|BeginPopup\|OpenPopup" src`.
- A popup with many rows (Arpeggiator list is 15 rows plus search) must cap its height to the window and scroll.
- Fixture: extend `MODDROPDOWNUNDOTEST` or add a gallery shot that opens a dropdown and the context menu at zoom 0.15, 0.5, 1, 2 and screenshots all four.
Scope is every popup that opens from the canvas or a node, not only dropdowns: dropdown lists (with and without search), param right-click menu (Enter Value / Performance Matrix / MIDI learn / Expression / Recording), modulation binding menu (lo/hi, Full range, Invert, Unbind), node menu, pin menu, cable menu, canvas background menu, colour picker, ramp/curve stop menus, tooltips, toasts. Inventory them first and list each one in the commit.
One `PopupList` / `PopupMenu` component with one spec in `tokens.json`:
- Row height one fixed value (about 28 px), same padding, same font size in every popup. Today the Datamosh list, the Ramp list, the blend-mode list and the right-click menus all differ (about 30, 48, 36 and 48 px rows).
- Width: min = the anchor control's width, max about 280 px, never driven by a row's helper text. Over-long text is ellipsised.
- Helper and reason text ("already driven by something else", "wire input, press Learn") goes in a tooltip on a disabled row, never into the row. The MIDI learn row is currently what widens that whole menu.
- Controls inside a menu (lo / hi fields) use the menu's width and do not enlarge it.
- Scale: constant on screen, changed only by the global UI scale setting, never by canvas zoom.
- Placement: anchored to the control, flips above/below and left/right to stay in the window, caps its height and scrolls.
Acceptance: at every zoom the popup text height and row pitch are identical on screen across all popup kinds, widths are within the min/max, and the popup stays inside the window.

### A2. Cable delete (trash) icon bleeds through other UI
The trash button at a cable's midpoint draws on top of nodes it overlaps and stays visible while Settings is open (last screenshot: small icon over the Settings panel).
Code: `src/app/frame/StagePopupsB.cpp` around line 625 (`sTrashLink`, `sTrashSeen`, `GetLinkMidpoint`).
- Draw it in the canvas layer so nodes in front of the cable cover it, or hide it when its midpoint lies inside any node rect.
- Never draw it while a modal/floating window (Settings, Field editor, help) is open or when the mouse is over another window: check `ImGui::IsWindowHovered(AnyWindow)` / the window stack, not just the cable hover.
- Nudge it along the cable to the nearest free point if the midpoint is covered.
Acceptance: screenshot with the midpoint behind a node, and with Settings open over a hovered cable: icon not visible.

### A3. ImGui boundary assertion on Compare (light theme)
Screenshot: "Code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries. Please submit an item e.g. Dummy() afterwards" on the Modulators `compare` node (A > B, "A: patched", "B (no cable)").
Find the body (`grep -n "no cable" src/app/bodies/*.cpp`), end every `SetCursorScreenPos` with a `Dummy`, and check the sibling bodies using the same pattern (grep `SetCursorScreenPos` followed by no item). Also check the Null Modulator and range-to-range bodies I edited.
Acceptance: no ImGui message in the log for a gallery shot of every Modulators node, dark and light.

### A4. Zoom-level simplification (canvas level of detail)
Zoomed out, whole node bodies shrink into unreadable specks (see the zoomed-out screenshots). Below a threshold a node should collapse to title, preview and pins; the full body returns when zoomed in. Pick the thresholds, apply them in one shared place (not per body), keep pins, cables and bindings stable across the switch (no param ordinal may shift: `gParamCounter` must still advance for hidden params, see `DrawRateModeControls`), and keep hit-testing correct.
Collapsed nodes must still show their state: selected, bypassed, modulated, and the error/warning badge from Block F. A badge on a collapsed node at 0.15 is the main way to find a broken node in a big patch.
Text crispness when zoomed in: node text at 2x is today a scaled bitmap (README L9). Measure it at 1, 1.5, 2; either rebuild the font atlas for the active zoom band (check atlas memory with CJK merged) or record the limit in `infinite-design-system`.
Acceptance: shots at 0.15, 0.3, 0.6, 1, 2 zoom, dark and light; save/load, bindings and `PINDUPTEST` unaffected; text at 2x is sharp, or the limit is recorded.

---

## Block B: leftovers from the polish brief

1. Step 6 live-state pass. Add a fixture env hook that loads a bundled sample, starts transport and runs N frames, then shoot Sampler, Slicer, Granular, MPC and Looper waveforms, the effect visualisers, the sequencers while playing, and MIDI File with a file loaded. Dark and light.
2. R3: body width, `out` right-aligned.
3. R7: `geo` to `out`; ASCII `->` to an arrow; colour/color spelling one way.
4. R8: icon-row gap on Join, Union, Material, Wrap, Camera, Light.
5. R5: remaining hint sites: Syphon, NDI, Slideshow (and their `EmptyState`/Refresh).
6. Predictive Coloring: "20% conf" still sits beside Learn; give it its own status line.
7. MPC "shot" label: decide and fix or justify.
8. Remaining ad-hoc button rows to `ActionButton::Row`: Sampler, Output, Drum Sequencer (Stop toggle, `StepCell`, `SectionCard`).
9. Not yet looked at: Compressor (find its name in `src/audio/EffectDefs.cpp`, shoot with `Audio Effects|<name>`: white dot and GR meter), Field Synth grammar, Wavetable `SectionCard`s, Note Router pin lighting, Mixer "channels", Image Analyze real preview, Audio Analyze idle `EmptyState` and two-row out grid, Video stray separator above out pins, Draw "animation" sub-header, paired checkboxes on the Video In grid, Curves anti-diagonal guide.
10. I8 (prediction) on the axis, invert and flip discrete controls; remaining I1-I12 named tests through the driver groups.
11. Dialogs (handover carry-over): make every modal compact through the shared dialog component (padding, gaps, radius, buttons sized to label with a minimum). Shoot Unsaved changes, Recover (never shot), Export/render (tracker `todo`), rename, delete confirm. Fix the light-theme card edge that barely separates from the scrim.
12. Library panel (tracker `todo`): verify refresh in every mode (Samples/folders, Plugins, Nodes, others): `src/app/panels/LibraryPanels.cpp` `refreshfolder`, "Refresh all", "Rescan plugins"; button disables while scanning, list updates, filter kept, empty/missing folder and scan error show an `EmptyState`. Drive it with an env hook, report pass/fail per mode. Same for the in-node Syphon/Spout Refresh (`ParamBodies1.cpp`).
13. Macros Toggle and Trigger onto the shared Switch / `ActionButton`; rename on caption double-click. Note Sequencer columns and playhead onto `StepCell` and the shared chip; Drum Sequencer playhead onto `StepCell`.
14. Hand-rolled scope fills onto the shared scope: `FxBodies2.cpp:714`, `PreviewBodies.cpp:626`, `SamplerBodies.cpp:154` and `:2664`, `FxBodies1.cpp:1093`. Same check for the small level/gain-reduction bars drawn inside node bodies (not the Meter node): they are drawn separately in about ten body files (`grep -rln -i meter src/app/bodies`); give them one look via `AudioBodyKit` (same scale, peak hold, clip colour) or record why one differs.
15. Chrome verdicts still open in `sweep-prompt.md` Part A: Library, Perf, Modulations, Arrange, top bar, right rail, minimap; plus colour picker, help popups, Field editor, toasts, other Settings tabs. Shoot dark and light, fix or mark ok in the tracker.
16. Param behaviour check, no redesign: the param system stays as is. Verify on one node per control type that double-click, hover-type, right-click menus, Shift-record, Tab/arrow keys and modulation all still work after this round's changes (I1-I13 tests). Report anything that behaves differently between control types; do not add new modifiers.

## Block D: audits against professional standards
Measure first, then fix in shared components. Report findings even when nothing needs changing.
1. Keyboard and focus: every popup, field and menu reachable by keyboard with a visible focus ring; Esc closes; Tab/arrows in lists. Not in scope now: shortcut rebinding and screen readers (README L11); say so in the report so keyboard work is not read as accessibility done.
2. Contrast: automated check of text and control contrast in both themes against the tokens (target WCAG AA for text); add it to `tools/design/`.
3. Tooltips: one delay, one style, one max width; every control has a short hint. Node help (the existing per-node help text) is the longer explanation; no separate Info View. Check help coverage both directions after any rename (see the node help tables).
4. Colour never the only cue: resolve the listed role collisions (record/solo, learn/modulation) by pairing colour with a glyph or label.
5. Motion: nothing over 200 ms, values never animate, reduce-motion honoured; grep for ad-hoc timings.
6. Automate the before/after contact sheets (one script producing dark and light sheets per family) so Block C is a command, not manual work.
6b. Goldens (README L12): store reference shots per component and per family, compare with a pixel-diff threshold in `driver.sh --group ui`, fail on regressions. Block G runs the same goldens on Windows and Linux with their own references.
7. Compact density mode: decide yes/no and record the reason; do not build it without a user-grounded need.

## Block E: first run and templates
Pros invest here (Ableton Live Packs and demo sets, TouchDesigner examples palette, Figma templates and the first-file tour).
1. Find what exists today (File menu, welcome screen, bundled patches; `grep -rni "template\|example" src resources`, last check found nothing). Report it before building.
2. Author starter templates headless with `infinite-patch-authoring` (write, validate, render): one per category (Source, Compositing, Effects, 3D, Modulators, Macros, Notes, Synths, Audio Effects, Prediction, Utility, Field) plus 2-3 "first patch" ones (make a sound, make an image, make something that moves with the music). Small, each runs standalone and looks or sounds good immediately.
3. Explain every template with `Comment` nodes (one header per band, one line per node group: what it is, what to try changing). Plain words, no jargon.
4. Surface them: a templates entry on the empty canvas / File menu, with a thumbnail rendered by the headless renderer, opening as an untitled copy so the original is never overwritten.
5. Every template goes in the round-trip test (`ROUNDTRIPTEST`/patch validation) so it cannot rot; `release-notes-audit` lists them.
Acceptance: a new user can open a template, hear or see output without touching anything, and understand it from the comments alone.

## Block F: errors and recovery states
Review and design, with the same component rules (EmptyState, toast, dialog; calm wording, always say what happened, what is safe, what to do next):
- failed or partial patch load (missing node type, newer file version, corrupt file),
- missing plugin / VST3 not found / plugin crashed (see `plugin-host-hardening`),
- missing media (image, video, sample path moved), relink flow,
- audio device lost or changed, sample-rate mismatch, no input permission (`AUDIORECOVERYTEST` covers the logic, not the wording),
- camera or screen-recording permission denied,
- crash recovery: autosave and "restore last session" prompt,
- export failures (disk full, codec missing).
- Node error and warning badges on the canvas: one shared badge on the node title for every node-level problem (Field compile error, missing media, missing or crashed plugin, no input, device lost for audio-in nodes). Hover shows the reason, click opens the fix (relink, rescan, open Field editor). Visible at every zoom including collapsed nodes (A4). Error = red, warning = amber, always with a glyph (colour never the only cue).
- Busy and progress states: one shared component for work that takes time (patch load, media decode, sample folder scan, plugin scan, export, Field compile of a large graph). It says what is happening ("Loading 12 of 40 samples"), shows progress when known and a calm spinner when not, can be cancelled where the work allows, and never leaves the UI frozen without explanation. Inventory every long task first (where it runs, whether it blocks the UI thread, what it shows today), then route each through the component; anything that blocks the UI thread for over ~100 ms is listed as a finding.
- Diagnostics export: Help > "Copy system info" (version, build, OS, GPU/GL, audio device and sample rate, plugin count, last errors) and "Reveal logs" (opens the log folder on each OS through `Platform::`). Nothing is sent anywhere; the user pastes it into a bug report. Three platform sides.
Deliver an inventory table (state, where it surfaces today, wording, fix) before editing, then fix in shared components.

## Block G: Windows and Linux parity for the UI
Everything above was shot on macOS only. For each Block A/B/D change: run the gallery on Windows and on Linux (container + Xvfb rig, `linux-parity`; Windows via CI per `windows-parity`), compare against the macOS sheets, and check font fallback and glyph coverage (icon font), HiDPI/UI scale, popup placement near screen edges, modifier names in menus and shortcuts (Cmd vs Ctrl), file dialogs. Also check that panel docks, sizes and the open panels survive a restart on each OS, and that a projector window dragged to a display with a different scale factor redraws at the right size, and unplugging that display mid-show neither crashes nor loses the output (it returns when the display comes back). Report differences as a table; fix in shared code, never behind `#ifdef` in components.

## Block H: performance does not regress
Benchmarks already exist (`scripts/bench`, `docs/plans/perf/README.md`, B1-B10; B6 is canvas pan/zoom). After A1/A4 (popups, level of detail) run B6 and B8 with `scripts/bench/ab.sh <variant> auto` against `main`, keep only if not worse, and add a B6 variant at zoom 0.15 and on a 400-node patch if none exists. Quiet-machine rules: see the bench prereqs (pause the daemon, nothing heavy running).
Cook-time overlay: a canvas toggle (View menu, off by default, saved in settings, not in patches) that shows each node's cook time in ms on the node, plus a heat tint on the slowest nodes. Video nodes measure CPU cook time (GPU time only if a timer query is cheap); audio nodes show their share of the audio callback. The overlay itself must cost nothing when off and stay within B6 when on.

## Block I: undo history view and text-scale check

### I1. Undo history panel
Finding: `UndoEntry` (`src/app/AppShared.h:5326`) is a whole-patch snapshot with no name, and `PushUndoCheckpoint()` takes no description, so there is nothing to list yet.
1. Give every checkpoint a short label: `PushUndoCheckpoint("Move node")` with an overload keeping the old call working, and a default derived from context (the param label being edited, "Connect cable", "Delete node", "Move clip"). Fill the labels for the ~dozen most common sites first (param edit, cable connect/disconnect, node add/delete/move, bypass, paste, clip edits); an unlabelled checkpoint shows as "Edit".
2. Merge a continuous drag into one entry (one knob drag = one row, "amount 0.20 → 0.55").
3. Show it as a docked panel through the existing panels system (like the Mod Matrix), opened from Edit > History and a shortcut; list newest on top, the current state highlighted, undone entries dimmed below it (redo side). Click an entry to jump there (apply N undos or redos in one step, one undo transaction on screen). Cap at the stack limit and say so.
4. Also show the label in the Edit menu items ("Undo Move node", "Redo ...") and the toast.
5. Never serialise the labels into patches; loading a patch still clears history (already tested).
Acceptance: do five different edits, the panel lists them with right names, clicking the third jumps there, redo entries stay until a new edit, save/load unchanged. Add a fixture test.

### I2. Text scale and longer text
Check at UI scale 100/125/150/200 % and with strings 40 % longer than English (pseudo-localisation: pad every `T()`/`L()` string): nodes, popups, menus, Settings, dialogs, timeline header. Nothing clipped or overlapping, popups stay within the window, node widths follow the rules (`node-ui-pillars`). Report what breaks, fix in shared components. Decide whether the UI strings should go through one lookup table (they mostly do via `T()`/`L()`); do not translate anything.

## Block J: copy and paste across patches, and distribution without a paid certificate

### J1. Cross-patch copy and paste
Owner requirement (important): copy a node group in one patch, paste into another patch opened in a second Infinite window/instance (or second patch tab if one exists), keeping the nodes' params and the cables between the copied nodes.
Current state to verify first: paste lives in `src/app/frame/StageKeyboard.cpp` (~line 440, `SpawnNode` + `CopyParams`, `newByOrig` remap) and appears to be an in-process buffer. Check whether the app can hold two patches at once at all; if each patch is its own process, the buffer must go through the system clipboard.
Plan:
1. Serialise the selection with the existing patch format (same code path as save, restricted to the selected nodes and links between them) into the system clipboard as text with a recognisable header, e.g. `infinite-nodes v<N>`; keep the in-process fast path.
2. Paste reads the clipboard, validates the header and version, remaps ids with the existing `newByOrig` logic, places the group at the cursor, one undo step.
3. Things that must survive: params, bindings between copied nodes, expressions, Field graphs (the copy's identity must diverge, see the existing comment), group/comment nodes; things that must not: cables to nodes that were not copied, node uids that collide.
4. Media paths: keep absolute paths, flag missing ones with the Block F relink state. Plugins: keep id, show the missing-plugin state if absent.
5. Different versions: refuse with a calm message when the clipboard is from a newer format; migrate older ones through the existing loader.
Acceptance: fixture test (copy to the clipboard buffer, load a second patch, paste, compare the graph); manual two-window check on macOS, plus Windows and Linux (clipboard behaviour differs).

### J2. Installer, first launch, update and about without Apple Developer membership
Decision recorded: no paid developer account for now. Today `package.sh` ad-hoc signs, README and `dmg-extras/Fix & Open Infinite.command` tell the user to right-click Open or strip quarantine. The job is to make that path as calm and clear as possible, not to remove it:
1. Audit the first-run path on a clean macOS user account: download, open the DMG, drag, first launch, Gatekeeper warning, the Fix & Open helper. Screenshot every screen. Make the DMG window itself carry the 2-step instruction (background image with arrow and "right-click, Open").
2. Windows: unsigned installers trigger SmartScreen ("More info, Run anyway"); document it the same way, and look at free options (SignPath Foundation offers free signing for open source projects, and Azure Trusted Signing is cheap); evaluate and report, do not sign up.
3. Linux: AppImage/tarball needs no signing; check the executable bit and desktop entry.
4. Update path: About already has "Check for updates" (`1d19f3bf`). Verify only: shows version and build, opens the releases page, never phones home silently, works on all three OSes.
5. About window: version, build, licence (MIT), third-party notices (`THIRD_PARTY_NOTICES`), links.
6. Consider Homebrew cask (macOS) and winget (Windows) as no-cost distribution that also avoids some quarantine friction; report effort, do not publish.
Acceptance: a person with no technical background gets from download to a running app by following only what is on screen.

## Block K: finding things in large patches
WASD already pans and the minimap exists; what is missing is search.
1. Cmd+F (Ctrl+F on Windows/Linux) opens a find field over the canvas: matches node title, node type, comment text and param names as you type; arrows step through matches; Enter jumps to the node (centre it, keep the zoom unless the node would be under LOD, then zoom to readable) and selects it; Esc closes and returns. Matches also light up on the minimap.
2. One shared popup (A1's `PopupList`), keyboard-first (D1), listed in the shortcuts window and `shortcuts-sweep`.
3. Fixture test: a 400-node patch, find by type, by title and by comment, check the selected node and view centre.

## Not now (recorded so they are not re-proposed)
User presets per node, panic/blackout keys (Start/Stop Audio covers it), performance lock mode, shortcut rebinding, screen reader bridge, frame-selected keys (WASD covers navigation), colour-managed viewer, multi-select param edit.

## Block C: finish

- Reshoot every family, dark and light, into `~/infinite-node-gallery/after-<date>-sheets/` and send before/after contact sheets with `SendUserFile` (baseline: `~/infinite-node-gallery/baseline-2026-10-09-sheets/`).
- `python3 tools/design/inventory.py && python3 tools/design/ratchet.py` (raw widgets 442 vs stored 470: lower the stored number).
- `driver.sh --skip-build --group ui,modulation,audio`, then final `driver.sh --auto`.
- Deploy: `rm -rf ~/Desktop/Infinite.app && cp -R build/Infinite.app ~/Desktop/Infinite.app`.
- Small user check before the final report: 2-3 people, three timed tasks (make a sound, make an image, find a broken node in a large patch); record where they stall and fold the fixes into shared components.
- Final report: fixed / decisions made / not visually verified / remaining.
