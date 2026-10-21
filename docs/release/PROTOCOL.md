# Release bug-hunt protocol

The standing standard for **every** Infinite release. A release is not tagged until the hunt in this document has run on the final commit and the exit criteria are met. Skill: `release-bughunt` (`.claude/skills/release-bughunt/SKILL.md`). Per-release numbers, waivers and open decisions live in `docs/release/<version>.md`, never here.

Origin: `docs/release/RELEASE_BUGHUNT_PLAN.md` (the v0.6 plan this was generalised from; kept as the source, its tool paths were checked against disk and fixed here).

Files in this folder:

| File | What |
|---|---|
| `PROTOCOL.md` | This file: categories, severity, entry/exit, phases, roles, check catalog |
| `BUG_TEMPLATE.md` | One finding = one file in `docs/release/findings/<version>/` |
| `CHECKLIST_TEMPLATE.md` | Copy to `docs/release/checklists/<version>.md` for each release's run sheet |
| `<version>.md` | Per-release instance: pass bars, known issues carried in, waivers, pending decisions |
| `TOOLS.md` | Which tools exist, which are built, which are not (with effort) |
| `findings/` | Findings tracker (`tools/release/findings.py`) |

Rule of precedence: the **release gate of that version's quality plan** (for v0.6, `docs/plans/v0.6-quality/README.md`, "Release gate") sets the numbers; this protocol sets the structure. Where they disagree the gate's number wins and the difference is written in `<version>.md`.

## 1. Categories A to L

Run in this order: cheap automated catches before expensive human attention.

| # | Category | Covers |
|---|---|---|
| A | Crash, stability, soak | Teardown, fuzz chains, sanitizers, soak, plugin crashes, device loss, crash reports. Cuts across every engine. |
| B | Persistence and settings | What this version **writes**: save/load, autosave, undo, atomic save, settings survive upgrade, Show Set/lock. |
| C | File compatibility | What this version **reads**: old patches, shipped examples, corrupt files, media formats, non-ASCII paths, headless CLI contract. |
| D | Audio engine | DSP correctness, param delivery, PDC, aliasing, NaN/full-scale safety, plugins, MIDI/OSC, listening pass. |
| E | Video and compositing | 2D pipeline, goldens, determinism, A/V sync, output/projection, Field. |
| F | 3D engine | Transform propagation, render pipeline, offline vs live, sim stability, new 3D nodes. |
| G | Performance and efficiency | Cost: CPU, memory, startup, idle, A/B against the previous release, leaks, ledger freshness. |
| H | FPS and latency | What the user feels: frame pacing, input-to-sound, A/V sync, xruns, edit hitches. |
| I | UI/UX | Design lints, goldens, per-node screenshot judging, interaction, panels, shortcuts, i18n, first run, error states. |
| J | Platform | macOS, Windows, Linux: CI, parity greps, Windows manual pass, Linux rig, per-OS baselines, OS floor. |
| K | Security, I/O, integration | OSC/network, plugin loading, self-update, secrets in the bundle, licences. |
| L | Release artefacts and docs | Version string, release notes, manuals, installer, signing, website, rollback. |

Per-category checks, tools and pass bars: section 7. Per-release extra checks (new nodes, known blind spots) go in `<version>.md`.

## 2. Severity and priority

The reporter sets severity; the triage owner sets priority (a rare S0 on a path no user takes can rank below a common S2).

| Level | Name | Meaning | Release rule |
|---|---|---|---|
| S0 | Blocker | Crash, hang, data loss, corrupted save, loud or damaging audio (full-scale burst, NaN), app will not start | Must be fixed. Release stops. |
| S1 | Critical | Core feature broken or silently wrong with no workaround; any regression of a shipped behaviour | Must be fixed, or the owner signs a written waiver that names the feature "do not use". |
| S2 | Major | Wrong or ugly but a workaround exists; breaks a UI pillar on one node | Fix if cheap; otherwise a release-notes line. Capped (section 4). |
| S3 | Minor | Cosmetic, rare, edge case | Log it. No gate. |
| S4 | Wish | Not a bug (missing feature) | Roadmap, not this hunt. |

Priority: P1 fix before the next run, P2 fix before exit, P3 can ship waived or noted.

## 3. Findings lifecycle

`new -> triaged -> fixing -> fixed -> verified`, or `waived` / `wontfix`. One finding is one file from `BUG_TEMPLATE.md`; ID `B-<category letter>-<nnn>` unique per release. A bug without a repro that can be re-run is not fixed until a verifier has re-run it; a flaky bug is fixed only after N consecutive passes (category A: 20; others: 5 unless `<version>.md` says otherwise).

Fix discipline (owned elsewhere, referenced here): `bug-blast-radius` before each fix; one commit per bug with its regression test in the same commit; branch per `git-branch-workflow`; no push or merge without the owner's word. The blast-radius answers decide which categories re-run after the fix (section 6, P6).

Regression-test rule: every S0/S1 fix has a test that fails before and passes after (a new `INFINITE_*TEST` fixture or a corpus entry).

Pre-triaged known issues from the previous release or the quality scoreboard enter the tracker on day one with a severity and a fix-or-waive decision.

## 4. Entry and exit criteria

**Step 0, reconcile (every release).** Read that version's release gate and list gate items the protocol does not cover and categories the gate does not mention; decide each; write the result in `<version>.md`. Confirm the previous release's carried-over items.

**Entry (the hunt may start when all are true)**
1. Feature freeze: only fixes found by the hunt.
2. All blocks merged into one release-candidate branch; nothing unmerged on the side.
3. One clean Release build of that exact commit exists and its commit hash is visible (title bar or About box), so every finding names its build.
4. `.claude/skills/run-infinite-hygiene/driver.sh --fast` is green on that commit.
5. Baselines are current: `known-test-failures.txt` and `-linux.txt` (in `run-infinite-hygiene/`), `audio-param-sweep-expected.txt`, `tests/headless/census_baseline.json`, `tests/render/golden`, `tests/ui-golden`, `bench/baselines/`.
6. Findings tracker initialised and the pre-triaged issues loaded.

**Exit (the candidate is accepted when all are true)**
1. Zero open S0 and S1 (or signed S1 waivers).
2. Open S2 at or below the cap in `<version>.md` (default 10, none in A, B or C), each with a release-notes line.
3. Every category's pass bars are met or each miss has an owner-signed waiver.
4. A final full run on the final commit (`driver.sh --full`, all sweeps, the C corpus, the A soak). A fix after that run re-runs the affected categories only (by blast radius), plus `--fast` and the touched sweeps.
5. Windows pass (J) done on the final commit, not an earlier one.
6. `release-notes-audit` clean; `scripts/release_version_gate.py` passes.
7. The version's quality gate (scoreboard, known failures, reverts) holds on the final commit.
8. The S0/S1 regression-test rule holds.

`tools/release/findings.py exit --version <v>` checks items 1, 2 and 8's bookkeeping from the tracker; the rest is read off the run's table and the gate.

## 5. Machine constraint (applies to every phase)

One build at a time (`pgrep -fl "cmake --build|make|clang"` first; never a broad `pkill`); app-launching tests run serially, never in parallel; benchmarks only on a quiet machine with nothing else running; a fix batch means one build, then one serial run of the affected set. No session or agent fan-out for the hunt without the owner's explicit yes (usage rule in `AGENTS.md`).

## 6. Phases

| Phase | What | Where | Tool |
|---|---|---|---|
| P0 | Reconcile and entry criteria | Claude + owner | this protocol, `<version>.md` |
| P1 | Static and no-build checks: lints, ratchets, parity greps, ledger, i18n, brand, shortcuts | any machine, seconds to minutes | `tools/release/run_gate.sh --phase P1` |
| P2 | One build, then the serial automated battery: hygiene driver, sweeps, census, goldens, determinism, smoke | Mac, one at a time | `tools/release/run_gate.sh --phase P2` (never builds) |
| P3 | Quiet-machine measurements: benches, A/B against the previous release, ledger kits, soak | Mac with nothing else running; owner leaves it alone | `scripts/bench/run_all.sh --quiet`, `ab.sh` (not wrapped; slow) |
| P4 | Claude-driven review: release-diff code review, invariant audits, screenshot judging, fuzzing | cloud + Mac | `infinite-code-review`, `invariant-interaction-audit`, gallery tools |
| P5 | Owner's hands-on sittings: user checklist flows, listening, live rehearsal, Windows, hardware | owner | `docs/WINDOWS_VERIFICATION.md`, checklist |
| P6 | Fix loop: triage, fix, verify, re-run the affected categories | all | tracker |
| P7 | Final full run on the final commit; release-artefact checks | Mac + Windows | everything |

Schedule Windows early in P5, not last: its findings feed the fix loop.

## 7. Check catalog (tools verified on disk)

Owner key: **Auto** = a script gates it; **Claude** = Claude runs it and judges; **Owner** = needs hands, ears, eyes or a second machine. Paths: `HYG` = `.claude/skills/run-infinite-hygiene`, `RS` = `tools/re-survey`, `SW` = `.claude/skills/<name>/driver.sh`. `INFINITE_*TEST` names are env-gated fixtures in the app binary (run via the drivers). `[new]` means a gap with no tool yet (see `TOOLS.md`).

**A. Crash, stability, soak**
- Delete mid-playback or mid-render: `AUDIOTEARDOWNSWEEPTEST`, `DELETECRASHTEST`, `RECTEARDOWNTEST` (Auto): 0 crashes.
- Random chains, copy/paste, undo: `CHAINFUZZTEST`, `MIXEDSTRESSTEST`, `CRASHTEST`, `BUGTEST` at 3 seeds (Auto): 0 crashes.
- Every node alone and chained: `scripts/node_census.py --baseline tests/headless/census_baseline.json` (Auto): no new issue.
- Sanitizers ASan/UBSan/TSan: `[new]` (Auto): 0 reports in app code.
- Soak: `scripts/bench/run_all.sh --soak-long`, `--r5` (Auto, owner starts): RSS growth < 2%, 0 xruns, 0 crashes.
- Plugin host crashes: `RS/host_audit.py`, `HOSTENVTEST`, `VST3SCANTEST`, skill `plugin-host-hardening` (Auto + Claude): app survives, crashed plugin left out next launch.
- Device loss: `AUDIORECOVERYTEST` (Auto); unplug and sleep/wake by hand (Owner).
- Crash report written and readable: `[new]` forced-crash check (Claude).

**B. Persistence and settings**
- Save/load of every node and param: `ROUNDTRIPTEST` (also in `de`), `PATCHTEST`, `AUDIOPARAMSWEEPTEST` against `HYG/audio-param-sweep-expected.txt` (Auto): exact match.
- Corruption along a chain: `SW data-accuracy-sweep` (Auto).
- Autosave and recovery: `AUTOSAVETEST`, `AUTOSAVEMARKERTEST` (Auto); kill -9 mid-edit `[new]` (Claude).
- Undo/redo: `UNDOTEST`, `HISTORYTEST`, `GESTUREUNDOTEST`, `MODDROPDOWNUNDOTEST` (Auto).
- Atomic save (fault injection) `[new]`; save-load-save byte determinism `[new]`; settings migration from the previous release's settings file `[new]` (Claude + Owner).
- Show Set / Show lock persist: `SHOWSETTEST`, `SHOWSAFETYTEST` (Auto) + hand (Owner).
- Project-scale load/save: `RS/project_bench.py` (Auto): no worse than the previous release by more than 10%, no cliff.

**C. File compatibility**
- Every shipped `.inf` opens: `RS/project_bench.py`, `LEGACYLOADTEST`, `tools/website/community_patches.py --smoke` (Auto): 0 load errors.
- Old-patch fidelity vs the previous release's build: `[new]` `legacy_diff` (Auto + Claude judges).
- Forward-safety (previous release opens a new file): manual (Claude).
- Hostile/corrupt files: `[new]` `fuzz_inf` (Auto).
- Media formats round-trip: MEDIA group, `RS/av_bench.py`, `RS/render_bench.py`, a format fixture list `[new]` (Auto + Owner).
- Non-ASCII paths and `de` locale: `ROUNDTRIPTEST` in `de`; unicode-path save/load `[new]` (Claude).
- Headless CLI contract: `scripts/headless_smoke.sh`, `tools/determinism-check.py` (Auto): exit codes and JSON unchanged unless noted.

**D. Audio engine**
- DSP, param delivery, PDC, transport: `SW audio-pipeline-sweep`, `SW audio-node-sweep`, `SW modulation-sweep`; `DSPTEST`, `AUDIOPDCTEST`, `AUDIOPCMTEST`, `TRANSPORTTEST` (Auto).
- Batteries: `FXAUDIT`, `SYNTHAUDIT`, `MODAUDIT`, note and IO audits (Auto; Claude triages every FLAG against the baseline).
- Aliasing and noise floor: `ALIASTEST` (Auto): meets the registry target or has a waiver.
- Param truth: `scripts/audit_param_truth.py`, skill `param-truth-audit` (Auto + Claude).
- Master NaN/full-scale: master-over counter exists; extreme-param fuzz `[new]` (Auto).
- Plugins: `RS/host_audit.py` (Auto). MIDI/MPE/OSC: `MPCMODTEST`, `MPETEST`, hand with a controller (Owner). Listening pass of every new synth/effect, default then abused (Owner). Spatial: `tools/spatial` listening test (Owner).

**E. Video and compositing**
- 2D pipeline: `SW compositing-pipeline-sweep`, `BYPASSTEST`, `PALETTETEST` (Auto).
- Goldens: `tools/render-check.py` over `tests/render` (Auto).
- Per-node black frame, NaN, size, determinism: `scripts/node_census.py --baseline ...` (Auto).
- Determinism: `tools/determinism-check.py <patch.inf>` (Auto; by-design list in `tools/determinism-by-design.txt`).
- A/V sync: `SW av-sync-sweep`, `RS/av_bench.py`, `RS/render_bench.py` (Auto).
- Output/projection: `SW output-projection-sweep` (Auto) + second display and Syphon/Spout client (Owner).
- Field: `tests/field/corpus.txt` via `FIELD*TEST` (skill `field-testing`) (Auto).

**F. 3D engine**
- `SW geometry-transform-sweep`, `SW render-pipeline-sweep` (Auto); `DELETECRASHTEST`; `OFFLINERENDERTEST`, `OFFLINECLOCKTEST`; `CLOTHTEST`; heavy scene `RS/blender_bench.py` (Auto).
- New 3D nodes of the release: golden `.inf` per node in `tests/render` `[new]`; owner sitting first (Owner).

**G. Performance and efficiency** (quiet machine only, P3)
- A/B against the previous release: `scripts/bench/ab.sh <base> <branch> auto`, base from `scripts/bench/base.sh` (Auto). Under 5% is noise; 30% or worse on a bench is S1; 5-30% is S2 unless explained.
- Full suite: `scripts/bench/run_all.sh --quiet` vs `bench/baselines/m2-8gb*.jsonl` (Auto).
- Load per track `RS/load_bench.py`; idle `RS/app_bench.py`; startup `project_bench.py` (Auto).
- Leaks: `[new]`. Audio-thread allocation/lock review: skill `field-realtime` (Claude).
- Data-ledger freshness: `python3 RS/data_ledger.py lint` (Auto): 0 stale.
- Scoreboard gate: `python3 tools/quality/quality.py check <logs>` (Auto).

**H. FPS and latency**
- `FPSTEST`, `SW rate-analysis-sweep`, `AUDIOSPIKE` (hand-run, 65 s) (Auto); canvas navigation B6, projector B3, edit hitches `B3EDITS` (Auto).
- Latency: `RS/latency_probe.py` (needs BlackHole), `RS/av_bench.py` (Auto, owner sets up BlackHole).
- Xruns: all benches and soak; also 64/128 frames and 44.1/48/96 kHz. Pacing on 2 monitors / 120 Hz (Owner).

**I. UI/UX**
- Design lints: `tools/design/ratchet.py`, `lint_icons.py`, `motion_lint.py`, `contrast.py` (Auto): no ratchet increase.
- Component goldens: `driver.sh --group ui` vs `tests/ui-golden` (Auto).
- Screenshot-judge every node, closed/open, dark/light: `tools/design/gallery` (`shoot.py`, `sheet.py`, `sizes.sh`, `widen.py`), against the baseline shots (Claude judges, Owner spot-checks). Per-node checklist: section 8.
- `SW node-ui-sweep`, `SW panels-sweep`, `SW shortcuts-sweep` (`check.py`), `SW cable-logic-sweep`; `PINDUPTEST`, `SIZETEST`, `TEXTFIT`, `KB*TEST`, `NAVTEST` (Auto).
- i18n: `I18NTEST`, `tools/i18n/lint.py` (Auto).
- First run, error messages and empty states, hi-DPI/small window: user checklist (Owner); refusal list `[new]` (Claude).

**J. Platform**
- CI green on all three on the release commit: `.github/workflows/build.yml` (Auto).
- Parity: `.claude/skills/pillar-parity-audit/static-checks.sh`, skills `windows-parity`, `linux-parity` (Claude).
- Windows manual pass: `docs/WINDOWS_VERIFICATION.md` (Owner). CL.exe crash: `ci-failure-triage` (Claude).
- Linux: `tools/linux/xvfb-harness.sh`, `shots.sh` vs `tools/linux/reference-shots`, `distro-smoke.sh` on the AppImage (Auto).
- Per-OS baselines: `known-test-failures*.txt` (Auto). Oldest supported OS and base hardware (Owner).

**K. Security, I/O**
- OSC bind address and junk packets: static check + `[new]` packet fuzz (Claude); `OSCCONTROLTEST`, `NETWORKTEST`, `NDITEST` (Auto).
- Plugin loading from user paths: `HOSTENVTEST`, `plugin-host-hardening` (Claude).
- Self-update: `scripts/selfupdate_swap_test.sh` prints `SELFUPDATESWAP OK` (Auto).
- Secrets and personal paths in repo and bundle (grep for home paths and tokens); licences against Core Invariant 1 (Claude).

**L. Release artefacts and docs**
- One version everywhere: `scripts/release_version_gate.py` (Auto).
- Release notes true: skill `release-notes-audit` (Claude).
- Manuals match the build (`tests/headless/describe_join_baseline.json` vs the node manual) `[new]` (Claude).
- Clean-machine install, signing/notarisation, rollback (Owner). Website counts and links: `tools/brand/brand_lint.py` + hand (Claude + Owner).

## 8. Per-node screenshot checklist (category I)

1. Symmetric layout: equal left/right margins, knob grid aligned, no orphan control hanging off a row.
2. No stray gaps: no empty band above, below or between groups; no control alone in a wide row.
3. Controls that do not apply in the current mode are dimmed, not hidden and not left live. Switch each mode/type selector and re-shoot.
4. Mod-dot and `mix` slot where `node-ui-pillars` requires; text contrast readable in both themes.
5. Open (`o`) state does not overflow; `widen.py` flags unexpected growth.
6. Pins present and labelled; no duplicate pin ids.
7. Matches neighbours in its category (padding, label style).

One failure = one finding (S2 default; S3 for a 1 px matter).

## 9. Roles

| Role | Who | Does |
|---|---|---|
| Runner | Claude | Runs the batteries in phase order, one build at a time, attaches output to findings |
| Reviewer | Claude (a separate pass from the runner) | `infinite-code-review` on the release diff; `invariant-interaction-audit` for each guarantee added; screenshot judging |
| Gatekeeper | `verify-gate` agent | Advisory run of the sweeps a fix touches; never blocks or merges |
| Triage | Claude proposes, **owner decides** | Every S0/S1 call and every waiver; keeps the tracker |
| Hands-on | Owner | Listening, Windows, quiet-machine runs, plugins, hardware, live rehearsal |

## 10. Accepted gaps

- No ctest; CI runs only the GPU-free subset, so the final full run is manual.
- Some fixtures may not be in `driver.sh` groups (the v0.6 plan names `SHAPERESONATORTEST`, `FIELDGRAPHTEST`): list any such in `<version>.md` and run by hand in P2.
- Device unplug, sleep/wake and long takes are hand-only.
- S4 items are not part of the hunt.
