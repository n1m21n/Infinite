# Infinite v0.6: pre-release bug hunt and review protocol

Status: PLAN ONLY. Nothing here has been run. Written 2026-10-15 from `infinite-tests/TESTS_INDEX.md`, `infinite-index/INDEX.md`, `v06-block2/block2-final-report.md` and `USER_TEST_CHECKLIST.md`.

**Not read:** the release gate in `docs/plans/v0.6-quality/README.md` lives on the Mac at `~/Infinite/repo` and is not in this project or the GitHub copy I can reach. Step 0 below reconciles this plan with it. Where the two disagree, the gate's numbers win and this plan's structure stays.

Tool names marked **[exists]** come from the tests index. Tools marked **[new]** do not exist yet and are listed in section 8 so they can be built before the hunt starts.

---

## 1. How the hunt is organised

Twelve categories (A to L), run in this order, because cheap automated catches come before expensive human attention:

| # | Category | In your list? | Why it is a separate category |
|---|---|---|---|
| A | Crash, stability and soak | **added** | A crash on stage costs more than any wrong pixel. It cuts across every engine, so it needs its own owner and its own severity floor. |
| B | Persistence and settings | yes | Data loss is the second worst outcome. |
| C | File compatibility | **added** | Old v0.5 patches, shipped examples, and files from other machines must still open. Different from B: B checks what v0.6 writes, C checks what v0.6 reads. |
| D | Audio engine | yes (part of "audio/video/3D") | Most of the Block 2 churn is here. |
| E | Video and compositing engine | yes | |
| F | 3D engine | yes | Block 3 (Time Machine, Sweep, Revolve, Render 3D quality) is new and has had no sittings. |
| G | Performance and efficiency | yes | CPU, memory, startup, idle cost. |
| H | FPS and latency | yes | Kept apart from G: G is cost, H is what the user feels (frame pacing, input-to-sound, A/V sync). |
| I | UI/UX | yes | |
| J | Platform (macOS, Windows, Linux) | **added** | Block 2 left about a dozen items not compiled on Windows or Linux. The Windows check is on the pre-release list. |
| K | Security, I/O and integration surface | **added** | OSC server, plugin host, network and file import all take outside input. Small, but release-blocking if wrong. |
| L | Release artefacts and docs | **added** | Version string, installer, self-update, release notes and the node manual must match what ships. A wrong "0.5.0" title bar is already in the user checklist. |

## 2. Severity scale

| Level | Name | Meaning | Examples | Release rule |
|---|---|---|---|---|
| S0 | Blocker | Crash, hang, data loss, corrupted save, loud or damaging audio (full-scale burst, NaN), or the app will not start | Patch saves but reloads wrong; delete node mid-playback crashes; denormal/NaN reaches output | Must be fixed. Release stops. |
| S1 | Critical | Core feature broken or silently wrong, no workaround; regression from v0.5 | Param does not reach the audio thread; export out of sync; shipped example fails to load | Must be fixed, or the user signs a written waiver naming the feature as "do not use". |
| S2 | Major | Feature wrong or ugly but a workaround exists; breaks a UI pillar on one node | Cable refused with no reason shown; node has a stray gap; modulation lost after undo | Fix if the cost is small, otherwise ship with a release-notes entry. Cap below. |
| S3 | Minor | Cosmetic, rare, or edge case | Tooltip typo, 1 px misalignment, rare flicker | Log it. No gate. |
| S4 | Wish | Not a bug | Missing feature (for example Sampler Note Expression, Linear-phase EQ) | Goes to the roadmap, not to this hunt. |

Priority is separate from severity: a rare S0 on a path no user takes can be lower priority than a common S2. The triage owner (section 7) sets priority; the reporter sets severity.

**Existing known issues** from the Block 2 report (ALIASTEST Cycle Shaper -90.2 dB vs -105 target, Analog FM -47.9 vs -54, FXAUDIT Dynamics click +9.0 dB, MODAUDIT FRATE Envelope open, PLUGINDRAGTEST flaky, 5 stale data-ledger kits, three unwrapped i18n strings) are carried in as **pre-triaged**: each gets a severity in the first triage pass and a waiver or fix decision. The Dynamics click (+9.0 dB) is the one most likely to be S1.

## 3. Bug report template (repro steps)

Every finding, found by a tool, Claude or you, gets this record. One per bug, in `v06-release/findings/<id>.md` (or one row in a findings table; section 8 item 1 builds the tracker).

```
ID:            B-<category letter>-<nnn>
Title:         <what is wrong, one line>
Severity:      S0..S4          Priority: P1..P3
Category:      A..L            Found by: <tool | Claude | Naman>   Date:
Build:         branch + commit + OS + build type (Release/Debug)
Regression?:   new in v0.6 | present in v0.5 | unknown   (bisect if cheap)
Repro:         1. exact steps, or the INFINITE_<NAME>TEST command + env
               2. patch file attached (minimal, saved as .inf)
               3. frequency: always | n of m runs | once
Expected:      ...
Actual:        ...   (screenshot / log / crash report path attached)
Blast radius:  the 9 questions from the bug-blast-radius skill, answered
Fix:           commit hash
Verified by:   re-run of the same repro on the fix build
Status:        new -> triaged -> fixing -> fixed -> verified | waived | wontfix
```

A bug without a repro that can be re-run is not "fixed" until a verifier has re-run it. For flaky bugs, "fixed" means N consecutive passes (N set per category below).

## 4. Entry and exit criteria

**Step 0: reconcile (before anything else)**
- Read the gate in `docs/plans/v0.6-quality/README.md` on the Mac and list every gate item that this plan does not cover, and every category here the gate does not mention. Decide each.
- Confirm the open items from the Block 2 report: Block 1 merge (`git merge --no-ff bugfix/v06-audio-correctness`), the builtins-kit refresh, the 5 stale ledger kits (needs a quiet Mac), 2.37 Auto-Tune Pro check (needs Windows).

**Entry (hunt may start when all true)**
1. Feature freeze: no new features. Only fixes found by this hunt.
2. Block 1, 2 and 3 are merged into one release-candidate branch (`release/v0.6` or equivalent); nothing unmerged on the side.
3. One clean Release build of that exact commit exists, and the build is recorded (commit hash in the title bar or About box, so every bug names its build).
4. `driver.sh --fast` is green on that commit (baseline today: 53 passed, 0 failed).
5. Baselines are current: `known-test-failures.txt`, `census_baseline.json`, `audio-param-sweep-expected.txt`, `tests/render/golden`, `tests/ui-golden`.
6. Findings tracker exists and the pre-triaged known issues are in it.

**Exit (release candidate is accepted when all true)**
1. Zero open S0 and S1.
2. Open S2 count at or below an agreed cap (proposed: 10, none in categories A, B or C). Each has a release-notes line.
3. Every category's pass bars (section 6) are met, or each miss has a written waiver from you.
4. A **final full run** on the final commit: `driver.sh --full`, all sweeps, the corpus in C, and the soak in A. Any fix after that run restarts the affected categories only (use the blast-radius answers to decide which), and `driver.sh --fast` plus the touched sweeps for everything else.
5. Windows pass done (category J) on the final commit, not an earlier one.
6. Release notes audited against the code (`release-notes-audit`), and version gate passes (`scripts/release_version_gate.py`).
7. Fix regression rule: every S0/S1 fix has a test that fails before and passes after (a new `INFINITE_*TEST` or a corpus entry).

## 5. Phases and the machine constraint

The Mac allows **one build at a time at `-j4`, tests run serially**. Plan the hunt around that:

| Phase | What | Where | Rough order |
|---|---|---|---|
| P0 | Reconcile + entry criteria | Claude + you | first |
| P1 | Static and no-build checks: all `check.py` files, `audit_node_params.py`, `audit_param_truth.py`, `tools/i18n/lint.py`, design ratchet, `brand_lint.py`, `data_ledger.py lint`, `windows-parity` / `linux-parity` greps, `pillar-parity-audit/static-checks.sh` | any machine, seconds to minutes | cheap, so first |
| P2 | One build, then the serial automated battery: `driver.sh --full`, 13 sweep drivers, FXAUDIT, MODAUDIT, SYNTHAUDIT, note/IO audits, `node_census.py --baseline`, render goldens, determinism check, headless smoke | Mac, one at a time | after P1 |
| P3 | Quiet-Mac measurements: benches B1 to B10 via `run_all.sh --quiet`, `ab.sh` against the v0.5 base, data-ledger kits, load/project/av bench, soak | Mac with nothing else running | needs you to leave the Mac alone |
| P4 | Claude-driven review: code review of the release diff against the four standards, invariant audits, screenshot-judging every node, fuzzing of the corpus | cloud + Mac | can overlap P2 reading time, not P2 builds |
| P5 | Your hands-on sittings: `USER_TEST_CHECKLIST.md` flows, a live-performance rehearsal, Windows machine | you | after P2 is green so you do not waste time on known failures |
| P6 | Fix loop: triage, fix, verify, re-run affected categories | all | repeats until exit |
| P7 | Final full run on the final commit, release artefact checks | Mac + Windows | last |

Rules for execution (from your standing preference): never start a second build while one is running (`pgrep -fl "cmake --build|make|clang"` first), never `pkill` a broad pattern, never run app-launching tests in parallel. Every fix batch means one build, then one serial run of the affected set.

## 6. Categories: checks, tools, pass bars and owners

Owner key: **Auto** = a script gates it with no judgement; **Claude** = Claude runs it and judges or reviews; **You** = needs your hands, ears, eyes or a second machine. Most rows are a mix, shown as primary owner first.

### A. Crash, stability and soak (added)

*Why separate:* every other category assumes the app stays up. These bugs come from interactions between engines, which no single-engine sweep sees.

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| Delete any node mid-playback / mid-render | AUDIOTEARDOWNSWEEPTEST, DELETECRASHTEST, RECTEARDOWNTEST **[exists]** | Auto | 0 crashes over all node types |
| Random chain building, copy/paste, undo | CHAINFUZZTEST, MIXEDSTRESSTEST, CRASHTEST, BUGTEST **[exists]** | Auto | 0 crashes; run at 3 different seeds |
| Spawn every node type alone and in a chain | `node_census.py --baseline` **[exists]** | Auto | no new issue vs baseline |
| Sanitizer pass (ASan + UBSan, and TSan for audio/UI thread) over the fast and audio groups | **[new]** `tools/release/sanitize.sh` (separate build dir, so it needs its own single build slot) | Auto | 0 reports in app code; third-party reports triaged |
| Soak: 4000-node patch and a typical live set, 8 h | `run_all.sh --soak-long` (B7), `--r5` **[exists]** | Auto, you start it | RSS growth under 2%, 0 xruns, 0 crashes, no thermal collapse |
| Plugin host: crashing/hanging plugins | `host_audit.py`, HOSTENVTEST, VST3SCANTEST with the deliberately broken plugin **[exists]**; `plugin-host-hardening` skill | Auto + Claude | A crashed plugin is left out next launch; app survives |
| Device loss: unplug, sleep/wake, sample-rate change | AUDIORECOVERYTEST **[exists]** covers part; unplug and sleep/wake are **hand-run** (the sweep says so) | You | Audio comes back without restart; no crash |
| Crash reporting works (a forced crash writes a usable report) | **[new]** forced-crash check, plus read the `.ips` path | Claude | Report written and readable |

Flaky-bug rule here: "fixed" = 20 consecutive passes.

### B. Persistence and settings (yours)

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| Save/load of every node type, all params | ROUNDTRIPTEST (also run in language `de`), PATCHTEST, AUDIOPARAMSWEEPTEST **[exists]** | Auto | 0 fail; param sweep matches `audio-param-sweep-expected.txt` exactly (new blind spots fail) |
| Data corruption along a chain: copy/paste, save/load, caches, bypass | `data-accuracy-sweep` **[exists]** | Auto | all OK |
| Autosave and crash recovery | AUTOSAVETEST, AUTOSAVEMARKERTEST **[exists]**; **[new]** kill -9 mid-edit, relaunch, confirm recovery offer and content | Auto + Claude | recovered patch equals last autosave |
| Undo/redo of everything, including gesture undo | UNDOTEST, HISTORYTEST, GESTUREUNDOTEST, MODDROPDOWNUNDOTEST **[exists]** | Auto | all OK |
| Save is atomic: interrupted write never leaves a half file | **[new]** fault-injection (kill during save, full disk, read-only folder) | Claude | old file intact, user told of the failure |
| Save is deterministic: save, load, save gives identical bytes | **[new]** double-save diff over the shipped corpus | Auto | 0 diffs, or each diff explained |
| Settings/preferences persist and survive upgrade | **[new]** settings migration test: start from a v0.5 settings file and from none; change every setting; relaunch; compare. Include: audio device, sample rate, buffer, language, theme, window layout, MIDI/OSC mappings, plugin scan list, recent files | Claude + You | every setting survives; a missing or corrupt settings file falls back to defaults without crash |
| Dock/panel layout, window size/position on monitor change | hand | You | Window never restored off-screen |
| Show Set and Show lock (new in Block 2) persist and block what they claim | SHOWSETTEST **[exists]** + hand | Auto + You | Lock blocks delete/drag/new cable after relaunch |
| Project-scale load/save time | `project_bench.py` (10 to 4000 nodes) **[exists]** | Auto | no worse than v0.5 base by more than 10%; no cliff |

### C. File compatibility (added)

*Why separate:* B asks "does v0.6 write what it reads". C asks "does v0.6 read what v0.5 and other sources wrote". Release regressions here are silent: the file loads but a param changed meaning (Block 2 changed filter types, dynamics styles, envelope stages, so defaults for old files matter).

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| Every shipped `.inf` (examples, templates, presets) opens in today's build | `project_bench.py` "every shipped .inf" **[exists]**; LEGACYLOADTEST **[exists]** | Auto | 0 load errors |
| Old-patch fidelity: v0.5 patch renders/sounds the same in v0.6 for nodes whose defaults changed | **[new]** `tools/release/legacy_diff.py`: take a corpus of v0.5 patches, render with the v0.5 base build (`../infinte-base` worktree, built by `base.sh`) and v0.6, compare image and audio with the same tolerances as `tests/render` | Auto, Claude judges the diffs | each diff is either explained as intended in release notes or filed |
| Forward-safety: v0.5 opening a v0.6 file fails cleanly, not corrupts | **[new]** manual: open v0.6 file in v0.5 | Claude | Clear message, no crash, original untouched |
| Hostile/corrupt files: truncated, bit-flipped, huge counts, bad UTF-8 | **[new]** `tools/release/fuzz_inf.py` (mutate the corpus, load headless, expect error not crash) | Auto | 0 crashes, 0 hangs over 10k mutations |
| Media import/export formats: audio, video, image, 3D models, ADM | MEDIA group, `av_bench`, `render_bench` **[exists]**; **[new]** list of format fixtures | Auto + You | each listed format round-trips |
| Locale: non-ASCII paths and names, spaces, de language | ROUNDTRIPTEST in `de`; **[new]** path-with-unicode save/load | Claude | works |
| Headless CLI contract | `headless_smoke.sh`, determinism-check **[exists]** | Auto | exit codes and JSON unchanged unless noted |

### D. Audio engine

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| DSP correctness, param delivery, PDC, transport | `audio-pipeline-sweep`, `audio-node-sweep` **[exists]**; DSPTEST, AUDIOPDCTEST (dry and limiter path on the same sample), AUDIOPCMTEST, TRANSPORTTEST | Auto | all OK |
| Effects and synth measurement batteries | FXAUDIT, SYNTHAUDIT, MODAUDIT, note and IO audits **[exists]** | Auto, Claude triages FLAGs | no non-finite, no hung tail, no crash; every FLAG triaged against the baseline; Dynamics click (+9.0 dB) resolved |
| Aliasing and noise floor | ALIASTEST **[exists]** | Auto | meets target, or waived: Cycle Shaper (-90.2 vs -105), Analog FM (-47.9 vs -54) |
| Param truth: the knob does what its range and label say | `audit_param_truth.py` **[exists]**; `param-truth-audit` | Auto + Claude | no new mismatches, and every one of the 17 Block 2 nodes (FM, partials, string, convolution, saturator, tape, dynamics styles, filter types, splitter, envelope, mseg, voice spread...) checked once |
| Full-scale and NaN safety at the master | clipping notice from Block 1; **[new]** extreme-param fuzz (every param to min/max/random while audio runs, assert master finite and bounded) | Auto | 0 NaN/inf; no burst above the limiter ceiling |
| Plugin/AU/VST3 audio | `host_audit.py` | Auto | per installed plugin: no crash, no hang |
| MIDI/MPE/OSC input | MPCMODTEST **[exists]**; hand with a controller; OSC from `127.0.0.1:9100` | You | notes and note-expression arrive; no stuck notes after unplug |
| Listening pass: each new synth and effect, default patch, then abused | hand | You | no clicks, zipper noise, runaway feedback, or sound that differs from the node's label. Use `USER_TEST_CHECKLIST.md` Block 2 as the script |
| Spatial / multichannel | spatial listening test (`tools/spatial`) **[exists]** | You | scoring matches key |

### E. Video and compositing engine

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| 2D pipeline: textures, bypass, cook memo, alpha, caching | `compositing-pipeline-sweep`, BYPASSTEST, PALETTETEST **[exists]** | Auto | all OK, FLAGs match baseline |
| Render goldens | `tests/render/*.inf` vs `golden/` (image: mean and bad-fraction; audio: LUFS, peak, RMS) **[exists]** | Auto | within tolerance |
| Per-node black frame, NaN, size change, determinism | `node_census.py --baseline` **[exists]** | Auto | no new issues |
| Determinism | `determinism-check.py` **[exists]** | Auto | identical except the by-design list |
| Video playback and recording: A/V sync, drops, missing audio | `av-sync-sweep` **[exists]**; `av_bench.py`, `render_bench.py` (10-min A/V sync) | Auto | drift and drops within the sweep's thresholds; 10-minute export stays in sync |
| Output/projection: Syphon/Spout, Projection, Output, projector windows | `output-projection-sweep`; hand with a second display and a Syphon client | Auto + You | no black/flipped/stale frames; window survives display unplug |
| Time Machine (new): all-black delay map equals input; ramp gives time smear | per `USER_TEST_CHECKLIST.md` | You | as the checklist states |
| Field (pixel, modifier) | `tests/field/corpus.txt` via FIELD*TEST; skill `field-testing` | Auto | corpus golden unchanged |

### F. 3D engine

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| Transform / Mapping / revision / Instances propagation | `geometry-transform-sweep` **[exists]** | Auto | all OK |
| Pipeline from source to Render 3D | `render-pipeline-sweep` (GEOTEST, MESHOPTEST, SHADOWTEST, INSTANCESWEEPTEST, RENDER3DCACHESWEEPTEST, RENDER3DLIVETEST...) | Auto | all OK |
| Delete or rewire mid-render | DELETECRASHTEST | Auto | 0 crashes |
| New Block 3 nodes: Sweep, Revolve, Proximity Lines, Render 3D quality (AO, DoF, 16-bit float, supersample) | per `USER_TEST_CHECKLIST.md`; **[new]** golden `.inf` patches for each, added to `tests/render` | You first, then Auto | no holes or inverted normals; defaults off give a byte-identical picture to v0.5 (this one is a checkable claim) |
| Offline render vs live | OFFLINERENDERTEST, OFFLINECLOCKTEST; `--frames-dir --depth 16` | Auto | same frame content, correct frame count |
| Cloth/sim stability: long run, no explosion | CLOTHTEST, soak | Auto | stays finite |
| Heavy scene | B4 complex 3D, `blender_bench.py` | Auto | no worse than v0.5 base |

### G. Performance and efficiency

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| A/B against the v0.5 base | `scripts/bench/ab.sh <base> <branch> auto` | Auto | change under 5% is "noise"; any regression of 30% or more on B1 to B10 is S1; 5 to 30% is S2 unless explained |
| Full bench suite | `run_all.sh --quiet`, results in `bench/results/<machine>/` compared with `bench/baselines/m2-8gb*.jsonl` | Auto | **targets from the benchmark plan:** 0 xruns in 10 min at 256 frames; canvas p50 ≥ 60 fps and p95 ≥ 45 fps on a 300-node patch; soak RSS growth under 2% |
| Audio load per track | `load_bench.py` (8 to 128 tracks) | Auto | Block 2's 64-tracks-at-70% gate is **known unmet on 4P+4E cores**: needs a decision (waive or fix) |
| Idle cost | `app_bench.py` (launch, idle CPU, memory, threads) | Auto | idle CPU near zero with an empty patch; no busy-wait |
| Startup and project load | B5, `project_bench.py` | Auto | no regression vs base |
| Memory: footprint and leaks | B9; **[new]** leak check (`leaks`/Instruments on macOS, create/delete 1000 nodes) | Auto | no growth after delete cycles |
| Efficiency review | `field-realtime` checklist; hunt for allocation, locks and file I/O on the audio thread | Claude | none found, or each filed |
| Data-ledger freshness | `data_ledger.py lint` | Auto | 0 stale; the 5 stale kits (av-bench, builtins-kit, load-bench, project-bench, stress) re-run on a quiet Mac |

The Mac is slow, so run G only in P3 with nothing else running, and use `ab.sh` (interleaved) rather than comparing across days.

### H. FPS and latency

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| Frame-rate gate | FPSTEST, `rate-analysis-sweep`; AUDIOSPIKE (65 s, hand-run) | Auto | gate passes; no spike |
| Canvas navigation | B6 | Auto | p50 ≥ 60, p95 ≥ 45 fps at 300 nodes |
| Live performance, projector window | B3 | Auto | projector p99 within the benchmark plan's bar |
| Input-to-photon | benchmark plan target | Auto + You | ≤ 2 frames |
| Audio round-trip latency and jitter | `latency_probe.py` (needs BlackHole), `av_bench.py` | Auto, you set up BlackHole | report latency; no regression vs v0.5; jitter bounded |
| Audio-to-picture sync | `av_bench.py` (128 BPM click-and-flash), `av-sync-sweep` | Auto | drift 0 over the clip, beat lock holds |
| Xruns | all benches, soak | Auto | 0 at 256 frames in 10 min; also try 64 and 128 and at 44.1/48/96 kHz |
| Frame pacing with 2 monitors / 120 Hz / ProMotion | hand | You | no judder; VSync honoured |
| Edit hitches (dragging a node, adding a cable at heavy load) | Block 1 hitch probes **[exists]** | Auto + You | no hitch over 1 frame budget |

### I. UI/UX

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| Design lints: literal colours, icons, motion, contrast | `ratchet.py` (CI runs it), `lint_icons.py`, `motion_lint.py`, `contrast.py` **[exists]** | Auto | no ratchet increase; contrast passes |
| Component goldens | `driver.sh --group ui` vs `tests/ui-golden` | Auto | match |
| **Screenshot-judge every node**, closed and open, dark and light | `tools/design/gallery` (`shoot.py`, `sheet.py`, `sizes.sh`, `widen.py`) **[exists]**; compare with `baseline-2026-10-09` | Claude judges; you spot-check the contact sheets | Each node passes the checklist below; every failure filed |
| Node-body rules | `node-ui-pillars`, `audio-node-ui`; PINDUPTEST, SIZETEST, TEXTFIT | Auto + Claude | see checklist |
| Interaction: pins, cables, drag, groups, undo, copy/paste, selection | `node-ui-sweep` | Auto | all OK (PLUGINDRAGTEST flaky: needs 20 passes or a fix) |
| Panels: modulation and performance matrices, browser, viewport cards | `panels-sweep` | Auto | all OK |
| Shortcuts match the help window | `shortcuts-sweep/check.py` | Auto | static pass; Windows/Linux key does not collide |
| Keyboard navigation and accessibility | KB*TEST, NAVTEST **[exists]** | Auto | all OK |
| i18n | I18NTEST, `tools/i18n/lint.py`; the 3 unwrapped strings (BusyLine.h:50, ArrangeRender.cpp:558, :578) | Auto | lint clean |
| First-run experience: New Synth (Cmd+Shift+N), sample drop, presets, timeline keys | `USER_TEST_CHECKLIST.md` | You | each flow works as written |
| Error messages and empty states | hand; **[new]** list of refusals (cable refused, plugin failed, file missing) | Claude + You | each gives a reason and a next step |
| Hi-DPI, small window, large monitor, full screen | hand and gallery at several sizes | You | no clipped controls |

**Per-node screenshot checklist** (this is the judging form, your rules made explicit):
1. Symmetric layout: left and right margins equal, knob grid aligned, no orphan control hanging off a row.
2. No stray gaps: no empty band above, below or between groups; no control sitting alone in a wide row.
3. Controls that do not apply in the current mode are dimmed, not hidden and not left live. Check by switching each mode/type selector (for example filter type, dynamics style, tape switch, envelope stage curve) and re-shooting.
4. Mod-dot and `mix` slot in the position `node-ui-pillars` requires; text contrast readable in both themes.
5. Open (`o`) state does not overflow, and `widen.py` flags any node that grows wider than closed unexpectedly.
6. Pins present and labelled; no duplicate pin ids.
7. Matches the neighbours in its category (same padding, same label style).
Record: pass / fail per node per item in a table; one failure = one finding (S2 default; S3 if a 1 px matter).

### J. Platform: macOS, Windows, Linux (added)

*Why separate:* Block 2 changes were **not compiled on Windows or Linux**: 2.6, 2.15 to 2.17, 2.24 to 2.26, 2.31 to 2.33, 2.35, 2.36, 2.37. 2.31 touched NetCompat.h and the OSC sockets; 2.35 touched MidiWin, MidiLinux and both platform dialog files. This is the most likely place for a release-day red build.

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| CI green on all three on the release commit | `.github/workflows/build.yml` (macOS arm64, Windows x64 and ARM64, Linux) | Auto | green, including headless tests |
| Static parity greps | `windows-parity`, `linux-parity`, `pillar-parity-audit/static-checks.sh` | Claude | every `Platform::` function has all three sides |
| Windows build crash recipe if CL.exe dies | `ci-failure-triage` | Claude | n/a unless red |
| **Windows manual pass** (on the pre-release list) | hand on a Windows machine, `docs/WINDOWS_VERIFICATION.md` | You | launch, new synth sound, save/load, MIDI in (MidiWin), file dialogs, OSC (2.31), plugin editor guard and Auto-Tune Pro check (2.37), Spout, export a movie, installer. Run the Tier-1 selftests there if the harness exists |
| Linux | Xvfb rig `tools/linux/xvfb-harness.sh`, `shots.sh` vs `reference-shots`, `distro-smoke.sh` on the AppImage | Auto | smoke passes on the distros listed |
| Per-OS test baselines | `known-test-failures.txt` vs `-linux.txt` | Auto | no unlisted failures |
| Old hardware and OS floor | hand on the oldest supported macOS and the base M2 8 GB | You | launches, stays usable |

Windows is the single largest unknown in the plan, so schedule it early in P5, not last: findings from it feed the fix loop.

### K. Security, I/O and integration surface (added)

*Why separate:* these take input from outside the process. They are small checks but a hole here can be S0 even if nothing else is wrong.

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| OSC server: bound to loopback, bad packets | static check of bind address; **[new]** malformed-packet fuzz | Claude | loopback only (the Block 2 report says it is, with no token: record as accepted risk); no crash on junk |
| Network features: NDI, Syphon/Spout, any URL fetch | NETWORKTEST, NDITEST | Auto | OK |
| Plugin loading from user paths | HOSTENVTEST, plugin-host-hardening | Claude | broken plugin cannot take the app down |
| Self-update | `selfupdate_swap_test.sh` | Auto | prints SELFUPDATESWAP OK |
| Secrets and personal data in the repo and in the bundle | `run_secret_scanning` on the diff; grep the bundle for `/Users/namansoni` paths and tokens | Claude | none shipped |
| Dependency and licence check (MIT, no GPL/AGPL pulled in) | Claude reads the third-party list against Core Invariant 1 | Claude | clean |

### L. Release artefacts and docs (added)

| Check | Tool | Owner | Pass bar |
|---|---|---|---|
| Version string consistent everywhere (title bar shows "0.5.0" in the current test build) | grep CMake, plist, About box, installer; `release_version_gate.py` | Auto | one version, above every published one |
| Release notes match the code | `release-notes-audit` | Claude | every claim true; every S2 waiver listed |
| Node Reference Manual, Field Language Manual and Headless User Guide match this build | **[new]** diff node list (`--describe` join baseline) against the manual | Claude | no missing or stale node |
| Installer / app bundle / AppImage run on a clean machine or account | hand | You | first-run works with no leftover state |
| Signing and notarisation (macOS), installer signing (Windows) | hand | You | Gatekeeper and SmartScreen accept |
| Website node count and download links | `tools/brand/brand_lint.py`, hand | Claude + You | correct |
| Rollback plan: previous release stays downloadable | hand | You | confirmed |

## 7. Roles and flow

| Role | Who | Does |
|---|---|---|
| Runner | Claude (cloud or Mac session) | Runs the automated batteries in the phase order, one build at a time, attaches output to findings |
| Reviewer | Claude (separate pass from the runner) | Code review of the release diff with `infinite-code-review` (accuracy, experimentality, instrument-grade design, real-time quality); `invariant-interaction-audit` for every guarantee added in v0.6; screenshot judging |
| Gatekeeper | `verify-gate` agent | Advisory run of every sweep the diff touches after each fix batch. It never blocks or merges |
| Triage | Claude proposes severity and priority, **you decide** S0/S1 calls and every waiver | Keeps the findings table |
| Hands-on | You | Listening, Windows, quiet-Mac runs, plugins, hardware, live rehearsal |

Fix discipline: each fix on a `bugfix/` branch per `git-branch-workflow`; run the 9-question blast radius (`bug-blast-radius`) before the fix; one commit per bug; its regression test in the same commit; no push or merge to main without your word.

**Where hunting finds the most bugs (use this to spend your own time):** Block 2 audio nodes on Windows and Linux (never compiled there); old-file defaults for nodes whose parameters changed; Block 3 visuals that have had no sittings; mode-dependent dimming in the UI; and anything on the audio thread under heavy load.

## 8. New tools to build before the hunt (all optional; ranked)

1. **Findings tracker**: one markdown or TSV table in `v06-release/findings/` plus a script that prints counts by category and severity and checks the exit criteria. Small. Do first.
2. **`tools/release/run_gate.sh`**: one serial driver that runs P1 then P2 in order, one app at a time, logs to one folder, prints a single pass/fail table per category. Wraps existing tools; builds nothing itself.
3. **`legacy_diff.py`**: render a corpus of v0.5 patches in both builds, diff with the existing golden tolerances.
4. **`fuzz_inf.py`**: mutation fuzz over the patch corpus for the loader.
5. **Settings migration test**: scripted start from a v0.5 settings file.
6. **Extreme-param fuzz** at the master output (NaN and level).
7. **`sanitize.sh`**: ASan/UBSan/TSan build and run (uses the one build slot, so schedule it deliberately).
8. **Golden patches for Block 3 nodes** added to `tests/render`.
9. **Node-screenshot judge sheet**: extends `tools/design/gallery` to emit the per-node checklist table pre-filled with automated hits (dimming-check by re-shooting each selector value).

Items 1 and 2 are cheap and pay back immediately. Items 3 to 6 each close a gap the existing suite does not cover. Item 7 is the most expensive on a slow Mac.

## 9. Gaps in the existing suite this plan accepts

- No ctest; CI only runs the GPU-free subset, so most selftests run only on your Mac or the pre-push hook. The final full run in P7 is therefore manual.
- SHAPERESONATORTEST and FIELDGRAPHTEST are not in the driver. Either add them to a group in P1 or run them by hand in P2.
- Tier 1 timing figures in the docs are stale (the 8 s claim; the only full-suite timing is from 2026-08-30). Time the first full run and write it down.
- Real device unplug, sleep/wake and long takes are hand-only.
- No Sampler Note Expression, no Linear-phase EQ (2.34): S4, not part of the hunt.

## 10. Decisions needed from you before execution

1. Confirm the gate numbers in `docs/plans/v0.6-quality/README.md` (or give me the file) so Step 0 can be closed.
2. Is the S2 cap of 10 right?
3. Which build to use as the v0.5 base for A/B and legacy diffs (the persistent `../infinte-base` worktree)?
4. Windows machine: when is it available? It gates exit.
5. Which of the tools in section 8 do you want built (I recommend 1, 2 and 3 first)?
