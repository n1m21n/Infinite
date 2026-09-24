# Infinite AI Agent Instructions & Skills Index

This repository contains dedicated skills and runbooks in `.claude/skills/<name>/SKILL.md` (also symlinked to `.agents/skills/`).
Whenever working on tasks in this repository, **you must consult and follow the relevant skill document before taking action**.

One owner per topic: if two files seem to disagree, the skill named in this table wins over memory notes, plan docs and other skills.

---

## Skills Catalog

| Skill | Category | Purpose |
|---|---|---|
| `field-language` | Field | Field syntax, domains, reserved words, the wrong/right table |
| `field-compiler` | Field | Lexer/AST/IR pipeline, backends, domain inference |
| `field-domains` | Field | Domain transfer operators (reduce/map/broadcast/resample/downsample) |
| `field-integration` | Field | Wiring a Field node into Infinite (`INode`, `ParamRef`, save/load) |
| `field-realtime` | Field | Realtime constraints for Field execution |
| `field-state` | Field | State/delay semantics in Field |
| `field-testing` | Field | Testing Field changes (corpus at `tests/field/corpus.txt`) |
| `field-pixel-presets` | Field | Writing Field Pixel presets that look right first time |
| `field-modifier-presets` | Field | Writing Field Modifier (geometry) presets |
| `new-audio-node` | New node | Adding an audio/note/synth node |
| `new-compositing-node` | New node | Adding a compositing node |
| `new-effect-node` | New node | Adding an Effects/Color node (FilterDef pattern) |
| `new-geometry-node` | New node | Adding a 3D/geometry node |
| `new-modulator-node` | New node | Adding a modulator node |
| `new-source-node` | New node | Adding a source node |
| `new-utility-node` | New node | Adding a utility node |
| `audio-node-sweep` | Sweep | Param round-trip + teardown invariants for audio nodes |
| `audio-pipeline-sweep` | Sweep | DSP correctness, param delivery, device loss, PDC |
| `av-sync-sweep` | Sweep | Audio/video sync in exported movies |
| `cable-logic-sweep` | Sweep | Connection rules - what can patch into what |
| `compositing-pipeline-sweep` | Sweep | 2D pipeline cook/bypass/cache correctness |
| `data-accuracy-sweep` | Sweep | Data corruption along patch chains |
| `geometry-transform-sweep` | Sweep | Transform propagation for geometry nodes |
| `modulation-sweep` | Sweep | Modulation sources/destinations/bindings end to end |
| `node-ui-sweep` | Sweep | Node UI regression sweep |
| `output-projection-sweep` | Sweep | Output/projection node checks |
| `panels-sweep` | Sweep | Panels subsystem checks |
| `rate-analysis-sweep` | Sweep | Sample/frame rate analysis |
| `render-pipeline-sweep` | Sweep | Render pipeline checks |
| `shortcuts-sweep` | Sweep | Keyboard shortcuts checks |
| `audio-node-ui` | UI | Layout/widget grammar for audio node UI |
| `node-ui-pillars` | UI | Symmetry/contrast rules for all node UI (load before any node UI edit) |
| `node-param-audit` | Params | Which controls are modulatable (ParamRef inventory) |
| `param-truth-audit` | Params | Does the DSP do what the control's range/gate/label claims |
| `rhythmic-quantization-standard` | Timing | Standardized rhythmic divisions & quantize table |
| `timeline-arrangement-architecture` | Architecture | How the Timeline/Arrangement actually works; read before touching `src/arrange/` |
| `codebase-lenses` | Process | Split a request into seven concern lenses; decides which skills to load |
| `codebase-navigation` | Process | How to search this codebase completely; living map of wiring hotspots |
| `bug-blast-radius` | Process | The 9-question impact analysis before fixing any bug (sole owner of the 9 questions) |
| `write-fix-brief` | Process | Turn a bug report / findings / idea into a verified implementation prompt |
| `invariant-interaction-audit` | Process | Before shipping any guarantee: can a sibling control or call site undo it |
| `infinite-code-review` | Process | Review code against Infinite's four standards |
| `semi-brain` | Process | Query the distilled brain before non-trivial architecture calls |
| `prior-art-scout` | Process | Search peer repos and public fixes for platform/audio/packaging problems |
| `git-branch-workflow` | Process | Branch-per-feature workflow for this repo |
| `windows-parity` | Platform | Writing Windows-safe code from macOS |
| `linux-parity` | Platform | Writing Linux-safe code; the container+Xvfb rig |
| `pillar-parity-audit` | Platform | Cross-platform feature coverage audit |
| `plugin-host-hardening` | Platform | Plugin host robustness |
| `run-infinite-hygiene` | Release | Build/test/self-test harness; owns the "Efficient routes" table |
| `ship-infinite` | Release | Full release/build/publish workflow |
| `release-notes-audit` | Release | Verify release notes match actual code |

## Agents (`.claude/agents/`)

| Agent | When |
|---|---|
| `triage` | First, for any new ask: classify, lens map, branch, route to `write-fix-brief` |
| `infinite-planner` | After triage, before any edit, for non-trivial work |
| `cartographer` | "How does X work end to end" deep reads |
| `prior-art-scout` | Root cause outside Infinite's code; platform/hosting/packaging work (runs the `prior-art-scout` skill) |
| `verify-gate` | Advisory pre-merge: runs every sweep the diff touches |

---

## Core Invariants & Rules

1. **Clean Room / Licensing**: Infinite is MIT licensed. Never open, read, grep, or reference GPL sources (e.g. Kronos, Cmajor, SuperCollider, BespokeSynth). Citing open academic papers (e.g. Kronos paper Norilo 2015) is permitted; GPL code is strictly prohibited.
2. **Syntax Rules**: Bare names, no sigils (e.g. `P.y += bass * 2`, never `@P.y += bass * 2`).
3. **Three platforms**: every `Platform::` function has macOS, Windows and Linux sides (`windows-parity`, `linux-parity`).
4. **Execution Rule**: Before making changes or running procedures related to any category above, always view and execute against the corresponding skill file.
