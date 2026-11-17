# Departments: the unit of the hunt

The software is split into departments. For each one the question is the same: **does it do what it is supposed to do, and is there a test that would catch it if it stopped?** A day's slot = one department, worked with the method below.

## Method (every department, in this order)
1. **Contract.** Write down (in your notes, 5-10 lines) what the department must do. Sources, best first: the node/feature help text and `--describe`, the manual PDFs, docs/ (ARCHITECTURE.md, ARCHITECTURE_FLOWS.md, DECISIONS.md, the department's plan in internal/plans/ and docs/plans/), comments at the top of the code, then project memory-style rules in AGENTS.md. Where the code and the contract disagree, that is a bug candidate.
2. **Harness.** Find and run what already tests it: the department's `*-sweep` skill and its `check.py`/driver, the `INFINITE_*TEST` / `*AUDIT` fixtures (`grep -rn 'INFINITE_[A-Z]*\(TEST\|AUDIT\)' src`), `tests/<dir>`, `tools/bughunt/*`, `tools/release/run_gate.sh`. Run them via `.claude/skills/run-infinite-hygiene/driver.sh` routing. Every red result is a finding; a flake is not (check known-test-failures in that skill folder).
3. **Are the tests real?** Read the tests. For each contract line ask: if this broke, would any test fail? Look for tests that assert nothing, only check "did not crash", compare against a baseline that was regenerated from the bug, or are skipped or listed as known failures. A contract line with no test = an issue "[tests] <dept>: no test for <contract line>", with a concrete proposed test (inputs, expected output, which existing harness to extend). Do not write the test yourself.
4. **Probe behaviour directly** where cheap (headless, seconds): feed the department edge inputs from TECHNIQUES.md and check the output against the contract (finite, bounded, round-trips, matches the documented range). Prefer in-process fixtures over launching the app.
5. **Record.** Add to /Users/namansoni/Infinite/bughunt-state/departments.md one line: date, department, harness run, result, untested contract lines. Next time start from that line, so coverage improves instead of repeating.

## Departments (slot -> department; sources to read; harness to start from)
| Slot | Department | Code | Harness / tests to start from |
|---|---|---|---|
| 0 | Audio engine (graph, buffers, PDC, sample rate, master) | src/audio, src/core audio parts | audio-pipeline-sweep, av-sync-sweep, INFINITE_AUDIOPARAMSWEEPTEST, INFINITE_DSPTEST, tests/render/audio.inf, `scripts/audit_param_truth.py` |
| 1 | Audio and note nodes A-L (synths, effects, note tools) | src/nodes | audio-node-sweep, INFINITE_PARAMFUZZ, INFINITE_NOTEAUDIT, rate-analysis-sweep |
| 2 | Audio and note nodes M-Z | src/nodes | same as slot 1 |
| 3 | Persistence (save, load, autosave, undo, settings, presets) | src/core Patch*, Settings*, Undo* | tests/headless/{strict,keyed,format,filevol,topology}, `--validate`, patch_variants, selftest-synthetic-mouse notes |
| 4 | Graph and cable logic, bypass rules, modulation | src/core, src/nodes | cable-logic-sweep, modulation-sweep, tests/headless/topology, rule: 2+ input nodes never bypass, bypassed nodes do not cook |
| 5 | Video and 2D compositing, picture nodes, Scene 2D | src/engine, src/nodes picture | compositing-pipeline-sweep, render-pipeline-sweep, tests/render, tests/render3d, tools/render-check.py, determinism-check.py, frame_fuzz |
| 6 | 3D (geometry, transforms, sculpt, rigs, renderer) | src/engine, src/nodes 3D | geometry-transform-sweep, tests/headless/render3d, tests/render3d, frame_fuzz |
| 7 | Field language and Field nodes | src/core field parts, src/nodes Field* | tests/field, tests/headless/field, corpus.txt, infinite-field-language skill, data-accuracy-sweep |
| 8 | UI: panels, shortcuts, node cards, dock, browser, i18n | src/app | panels-sweep, node-ui-sweep, keys_gate (`tools/design/keys_gate.py`), tests/ui-golden, tools/design/*, tools/i18n/lint.py |
| 9 | Plugins hosting, MIDI, OSC, network, self-update | src/plugin, src/core io | tests/plugin (daw, soak, baseline), plugin_probe, scripts/selfupdate_swap_test.sh |
| 10 | Output, projection, export, render-to-file, headless CLI | src/engine, src/main.cpp, src/cli_* | output-projection-sweep, headless_smoke.sh, tests/headless, CLI contract in docs/headless |
| 11 | Platform (macOS, Windows, Linux) | src/platform | pillar-parity-audit/static-checks.sh, tests/platform, CI jobs, docs/WINDOWS_VERIFICATION.md |
| 12 | Chat, find, AI skills, MCP, website, docs, release artefacts | src/app chat parts, tests/chat, tests/website, website/, worker/, infinite-mcp-repo | tests/chat eval files, tests/website, release_version_gate.py, node manual vs `--describe`, node_param_audit.md, manual_gap.md |
| 13 | The tests themselves (cross-department) | tests/, tools/*/selftest, baselines | every tool's selftest; stale baselines (census_baseline.json, describe_join_baseline.json); known-test-failures lists; tests with no assertions; nodes in the census with no test |

## Per-department techniques
Each block: skills to read first (`.claude/skills/<name>/SKILL.md`) and the department's own checks, on top of TECHNIQUES.md 1-10.

**0 Audio engine.** Skills: audio-pipeline-sweep, rate-analysis-sweep, av-sync-sweep, rhythmic-quantization-standard, windows-parity, linux-parity. Checks: nothing allocates, locks, logs or does I/O in the audio callback; PDC matches the graph's latency; device loss and device switch recover; sample-rate and buffer changes rebuild every node's state; master limiter never lets NaN or > full scale out; tempo-sync divisions come from MusicTime.h only (no private division tables).

**1-2 Audio and note nodes.** Skills: audio-node-sweep, new-audio-node (its checklist is the contract every audio node must meet), audio-node-ui, node-ui-pillars, param-truth-audit, rhythmic-quantization-standard. Checks: every control saves, loads, reaches the audio thread, and changes the sound as claimed; mid-playback delete is safe; stuck notes; quantize and scale snapping not undone by a later clamp; sync and divisions match the standard; same param name and range as sibling nodes.

**3 Persistence.** Skills: data-accuracy-sweep, patch-variation, infinite-patch-authoring. Checks: save then load then save is byte-stable in meaning; every serialized key is read, and every read key is written; old patches load; unknown or hostile values clamp and warn, never crash; atomic write; autosave after a kill; undo and redo restore everything including selection, positions, modulation bindings.

**4 Graph, cables, bypass, modulation.** Skills: cable-logic-sweep, modulation-sweep, node-param-audit, invariant-interaction-audit. Checks: what may connect to what and why a cable is refused is consistent with the docs; 2+ input nodes never bypass; bypassed nodes do not cook; clips follow canvas bypass; deleting a node leaves no dangling cable, binding or modulation destination; ParamRef targets survive rename, copy, paste, group.

**5 Video, 2D, compositing.** Skills: compositing-pipeline-sweep, new-compositing-node, new-effect-node, new-source-node, data-accuracy-sweep. Checks: textures, alpha (premultiplied or not, consistently), cook memo and cache invalidation (revision bumped when an input changes), frame-persistent nodes reset on seek, size 0 or huge, determinism (same patch, same pixels), export quality never lower than preview rules.

**6 3D.** Skills: geometry-transform-sweep, render-pipeline-sweep, new-geometry-node. Checks: transform, Mapping, revision and Instances propagate through every geometry consumer; normals and UVs preserved or regenerated; simulation stability (NaN, explosion); camera and lights are nodes; offline render equals live render; the 3D composer arranges referenced outputs and never writes back.

**7 Field language.** Skills: field-language, field-compiler, field-domains, field-state, field-realtime, field-integration, field-testing, field-modifier-presets, field-pixel-presets. Checks: run the tests/field corpus golden harness; state cells reset on seek and loop and serialize; no heap or branching cost trap in real-time kernels; VM, GLSL and sample backends agree on the same kernel; presets use reserved names correctly.

**8 UI.** Skills: node-ui-sweep, panels-sweep, keys_gate (`tools/design/keys_gate.py`), node-ui-pillars, audio-node-ui, infinite-design-system, new-modulator-node. Checks: all of TECHNIQUES.md 10, plus drag, copy and paste, groups, undo of UI actions, pins and cables hit-testing in a panned and zoomed canvas, docking, shortcuts versus the help window, i18n wrapping, first run, empty and error states.

**9 Plugins, MIDI, OSC, network, update.** Skills: plugin-host-hardening, new-utility-node. Checks: a crashing or hanging plugin cannot take the app down or silence it; malformed MIDI or OSC input is dropped, never crashes; state save and restore for plugins; self-update verifies what it downloads and can roll back; no secrets in the bundle or logs.

**10 Output, projection, export, CLI.** Skills: output-projection-sweep, av-sync-sweep, new-utility-node, infinite-patch-authoring. Checks: Syphon, Spout, Projection and Output windows survive resize, monitor change and close; exported movie has audio, no drift, no dropped frames; headless CLI contract (exit codes, `--validate`, `--describe`) matches docs/headless.

**11 Platform.** Skills: pillar-parity-audit, windows-parity, linux-parity, ci-failure-triage, ci-preflight, build-discipline. Checks: every Platform:: function has three sides; no `_WIN32` in node code; path separators, non-ASCII paths, high DPI, per-OS file dialogs; CI red rows explained from the log.

**12 Chat, website, docs, release.** Skills: release-notes-audit, docs-screenshots, ship-infinite (read only; never run), quality-system, prior-art-scout. Checks: node help text, `--describe`, manuals and the node census agree (4 hand-kept help tables and per-OS #if branches: check both directions after any node or filter was added); release notes claims match the code at the tag; website links, downloads, version strings; brand tokens on brand surfaces.

**13 The tests.** Skills: quality-system, run-infinite-hygiene, tool-first. Checks: every tool's selftest runs; baselines were not regenerated from a bug; known-failure lists do not grow silently; each node type in the census has at least one test that asserts something about its output.
