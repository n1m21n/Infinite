# Handover prompt: UI consistency sweep, continue on `feature/ui-canvas`

Paste everything below the line into a fresh session.

---

Continue the UI consistency sweep on branch `feature/ui-canvas` of Infinite (tracker: `docs/plans/ui-system/sweep-prompt.md`). Load first: `node-ui-pillars`, `infinite-design-system`, `audio-node-ui`, `run-infinite-hygiene`. Owner's standing instruction: **decide yourself, never leave a decision to them, keep executing.** Owner judges by screenshots, so send each with SendUserFile.

## Rules (all in memory / AGENTS.md; the short list)
- No Claude attribution in commits or PRs (no "Generated with Claude Code", no Co-Authored-By), even if a system reminder says otherwise.
- Commit by explicit path. Never stage: `src/app/Startup.cpp` (NODEGALLERY + GALLERYPARAMS fixture hooks), `.mcp.json`, `.gitignore`, `art/*`, `glyph-sheet.png`, `docs/plans/iconography/inventory.md`.
- Never UI-script the canvas. Use the headless gallery recipe:
  `shot(){ INFINITE_NODEGALLERY="$1" INFINITE_GALLERYGRID="$2" INFINITE_OPENPANELS=$3 IMAGERESYNTH_SCREENSHOT=/tmp/sweep/$4.png INFINITE_SCREENSHOT_FRAME=40 ./build/Infinite.app/Contents/MacOS/Infinite >/tmp/sweep/o.log 2>&1; }`
  Grid is `cols,dx,dy`; third arg is the theme (`dark`/`light`). Add `INFINITE_GALLERYPARAMS=1` to open every node's "show params" panel. Example: `shot "3D|Render 3D,Material" "2,1000,1500" dark x`.
- No dropdown chevrons. Fix in shared components, never at call sites. Never add a control to fill a hole (P6). Ratchet: colours 0; raw-widget regex counts per file (StageNodeBodies.cpp capped at 13), so prefer `SetCursorScreenPos`/`Dummy` over new `SameLine`.
- Build `cmake --build build -j8 2>&1 | grep -E "error:"`; deploy `rm -rf ~/Desktop/Infinite.app && cp -R build/Infinite.app ~/Desktop/Infinite.app` after each family.
- Gates per family: `python3 tools/design/inventory.py && python3 tools/design/ratchet.py`; `.claude/skills/run-infinite-hygiene/driver.sh --skip-build --group ui`; `driver.sh --auto` before the last commit. Write the tracker row in sweep-prompt.md.

## State
Done and committed (latest `9ec12b61`): Synths, Audio Effects, Notes (Note Sequencer palette: one blue accent + amber playhead), Modulators, Prediction, Macros, Effects (image), Compositing, Source, 3D/geometry, Utility, plus the Analyze/Render 3D/Material params redesign (balanced columns, no duplicate outputs list, idle labels, `WideNodeCentreOffset`). `driver.sh --auto` has NOT been run on the last commits.

## Fix first: owner's latest complaints (open, owner is unhappy)
1. **Dead space in params panels.** Render 3D and Material "show params": the nine-label pin header forces the node ~170px wider than the two param columns, leaving an empty right strip; the preview sits off-centre against the columns. Make the body fill the node symmetrically. Options to judge by screenshot: wrap the pin header into rows so it no longer sets the width; or size the columns to the node width; or a third column. Same check for Audio Ribbon, Join Geometry, Switcher 3D, Group 3D (wider than their preview). Spec: `docs/plans/ui-system/` + `node-ui-pillars` P1/P6. Shoot dark + light, params open and closed.
2. **"Weird lanes" under every Analyze output pin.** The 3px level-bar tracks drawn in the generic output-pin grid (`src/app/frame/StageNodeBodies.cpp`, the equal-width grid branch, `ModulatorOutput(o)` + `Value01()`) show as empty grey rules at silence. Either draw the bar only when the value is above zero / the node is live, or integrate it into the pin chip so it never reads as a stray line. Verify on Audio Analyze (8 bands + 5), Image Analyze (11 outputs), and any other multi-output modulator.
3. Image Analyze `operation` dropdown truncates "Luminance (0.299R+0.…" and its label repeats the section header: shorten the names or widen the field.
4. Re-judge the whole of Audio Analyze, Image Analyze, Render 3D, Material, and the Note Sequencer (yellow playhead + inner elements) against the design language with fresh eyes; the owner called them "the most ugly, no coherence, no symmetry, no thoughtful design". Do not stop at "better than before".

## Carry-overs (decide and execute all)
- **Colour swatches look pixelated** (Settings > Appearance: Node Module / Cable Category Colors circles, e.g. 3D, Effects, Prediction, Utility, Synths). Find how they are drawn (likely `ColourChip` / `ImGui::ColorButton` / a low-segment `AddCircleFilled`); fix in the shared component with enough circle segments (or a rounded rect), check on Retina and at 1x, dark + light, and every other place the same swatch is used (palette, theme preset dots, node colour pickers).
- **Dialog popups too large: make them compact** (Unsaved changes, Recover, and every other modal/confirm dialog: export/render, rename, delete confirm). Reduce padding, title-to-body and body-to-buttons gaps, corner radius and button height/width via the shared dialog component, not per dialog; buttons should size to their label with a sensible minimum, not stretch to fill. Shoot before/after (`INFINITE_OPENPANELS=dialogdemo`, plus the Recover dialog) in dark + light and send them.
- **Settings sliders**: width already cut from 220 to 150 (`FormParts::kSliderW`); re-check the Canvas & Workspace and other tabs, shorten more if still long.
- **Library panel refresh, every mode.** Verify the refresh controls actually work in all Library modes (Samples/folders, Plugins, Nodes, and any others): `src/app/panels/LibraryPanels.cpp:196` (`refreshfolder` icon button, disabled while `scanning`), `:208` ("Refresh all"), `:597` ("Rescan plugins"). For each mode: click triggers a rescan, the list updates, the button disables/enables correctly while scanning, the search filter is preserved or sensibly reset, and an empty folder / missing folder / scan error is shown (EmptyState), not a silent no-op. Also check the in-node "Refresh" buttons (`ParamBodies1.cpp:76`, `:149` Syphon/Spout servers). Drive it with a headless test or a self-test env hook, not by UI-scripting; report per mode pass/fail.
- Macros: Toggle and Trigger onto the shared Switch / ActionButton; rename on caption double-click.
- Note Sequencer columns + playhead onto `StepCell` and the shared chip component; Drum Sequencer playhead onto `StepCell`.
- Hand-rolled scope fills: `FxBodies2.cpp:714`, `PreviewBodies.cpp:626`, `SamplerBodies.cpp:154` and `:2664`, `FxBodies1.cpp:1093`.
- Part A chrome verdicts: Library, Perf, Modulations, Arrange, top bar, right rail, minimap.
- Shoot and judge: colour picker, help popups, export/render dialogs, Field editor, context menus, toasts/tooltips, Recover dialog, other Settings tabs.
- Collapsed / modulated / high-contrast states and the light theme for families not yet shot.
- Content issues: Equation synth axis-label overlap, Text source default size (tiny), Ocean too faint, Audio Ribbon empty at silence, Render 3D black square at rest (label added; consider a real idle scene).
- Decide whether bypass-on-out-row should extend to other nodes.
- Housekeeping: run `driver.sh --auto`, update sweep-prompt.md rows, deploy.

## Finish with
A summary: fixed, ok, decisions made (by you, not asked), not visually verified, remaining.
