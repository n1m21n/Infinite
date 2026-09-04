# Infinite AI Agent Instructions & Skills Index

This repository contains dedicated skills and runbooks in `.claude/skills/` (also symlinked to `.agents/skills/`).
Whenever working on tasks in this repository, **you must consult and follow the relevant skill document before taking action**.

---

## Skills Catalog

| Skill | Category | Purpose | Path |
|---|---|---|---|
| `field-language` | Field | Field language design/spec | `.claude/skills/field-language/SKILL.md` |
| `field-compiler` | Field | Lexer/AST/IR pipeline, backends, domain inference | `.claude/skills/field-compiler/SKILL.md` |
| `field-domains` | Field | Domain transfer operators (reduce/map/broadcast/resample/downsample) | `.claude/skills/field-domains/SKILL.md` |
| `field-integration` | Field | Wiring a Field node into Infinite (`INode`, `ParamRef`, save/load) | `.claude/skills/field-integration/SKILL.md` |
| `field-realtime` | Field | Realtime constraints for Field execution | `.claude/skills/field-realtime/SKILL.md` |
| `field-state` | Field | State/delay semantics in Field | `.claude/skills/field-state/SKILL.md` |
| `field-testing` | Field | Testing strategy for Field programs | `.claude/skills/field-testing/SKILL.md` |
| `new-audio-node` | New node | Procedure for adding an audio/note/synth node | `.claude/skills/new-audio-node/SKILL.md` |
| `new-compositing-node` | New node | Procedure for adding a compositing node | `.claude/skills/new-compositing-node/SKILL.md` |
| `new-effect-node` | New node | Procedure for adding an Effects/Color node (FilterDef pattern) | `.claude/skills/new-effect-node/SKILL.md` |
| `new-geometry-node` | New node | Procedure for adding a 3D/geometry node | `.claude/skills/new-geometry-node/SKILL.md` |
| `new-modulator-node` | New node | Procedure for adding a modulator node | `.claude/skills/new-modulator-node/SKILL.md` |
| `new-source-node` | New node | Procedure for adding a source node | `.claude/skills/new-source-node/SKILL.md` |
| `new-utility-node` | New node | Procedure for adding a utility node | `.claude/skills/new-utility-node/SKILL.md` |
| `audio-node-sweep` | Sweep | Param round-trip + teardown invariants for audio nodes | `.claude/skills/audio-node-sweep/SKILL.md` |
| `audio-pipeline-sweep` | Sweep | DSP correctness, param delivery, device loss, PDC | `.claude/skills/audio-pipeline-sweep/SKILL.md` |
| `av-sync-sweep` | Sweep | Audio/video sync in exported movies | `.claude/skills/av-sync-sweep/SKILL.md` |
| `cable-logic-sweep` | Sweep | Connection rules - what can patch into what | `.claude/skills/cable-logic-sweep/SKILL.md` |
| `compositing-pipeline-sweep` | Sweep | 2D pipeline cook/bypass/cache correctness | `.claude/skills/compositing-pipeline-sweep/SKILL.md` |
| `data-accuracy-sweep` | Sweep | Data corruption along patch chains | `.claude/skills/data-accuracy-sweep/SKILL.md` |
| `geometry-transform-sweep` | Sweep | Transform propagation for geometry nodes | `.claude/skills/geometry-transform-sweep/SKILL.md` |
| `modulation-sweep` | Sweep | Modulation sources/destinations/bindings end to end | `.claude/skills/modulation-sweep/SKILL.md` |
| `node-ui-sweep` | Sweep | Node UI regression sweep | `.claude/skills/node-ui-sweep/SKILL.md` |
| `output-projection-sweep` | Sweep | Output/projection node checks | `.claude/skills/output-projection-sweep/SKILL.md` |
| `panels-sweep` | Sweep | Panels subsystem checks | `.claude/skills/panels-sweep/SKILL.md` |
| `rate-analysis-sweep` | Sweep | Sample/frame rate analysis | `.claude/skills/rate-analysis-sweep/SKILL.md` |
| `render-pipeline-sweep` | Sweep | Render pipeline checks | `.claude/skills/render-pipeline-sweep/SKILL.md` |
| `shortcuts-sweep` | Sweep | Keyboard shortcuts checks | `.claude/skills/shortcuts-sweep/SKILL.md` |
| `audio-node-ui` | UI | Layout/widget grammar for audio node UI | `.claude/skills/audio-node-ui/SKILL.md` |
| `node-ui-pillars` | UI | Symmetry/contrast rules for all node UI | `.claude/skills/node-ui-pillars/SKILL.md` |
| `node-param-audit` | UI | Param declaration/UI audit | `.claude/skills/node-param-audit/SKILL.md` |
| `rhythmic-quantization-standard` | Timing | Standardized rhythmic divisions & quantize table | `.claude/skills/rhythmic-quantization-standard/SKILL.md` |
| `git-branch-workflow` | Process | Branch-per-feature workflow for this repo | `.claude/skills/git-branch-workflow/SKILL.md` |
| `bug-blast-radius` | Process | 9-question impact analysis before fixing a bug | `.claude/skills/bug-blast-radius/SKILL.md` |
| `write-fix-brief` | Process | Turn a bug report into a verified implementation prompt | `.claude/skills/write-fix-brief/SKILL.md` |
| `codebase-navigation` | Process | How to search this codebase completely | `.claude/skills/codebase-navigation/SKILL.md` |
| `infinite-code-review` | Process | Review code against Infinite's standards | `.claude/skills/infinite-code-review/SKILL.md` |
| `pillar-parity-audit` | Process | Cross-platform feature coverage audit | `.claude/skills/pillar-parity-audit/SKILL.md` |
| `plugin-host-hardening` | Process | Plugin host robustness | `.claude/skills/plugin-host-hardening/SKILL.md` |
| `windows-parity` | Process | Writing Windows-safe code from macOS | `.claude/skills/windows-parity/SKILL.md` |
| `run-infinite-hygiene` | Release | Pre-commit build/test/self-test harness | `.claude/skills/run-infinite-hygiene/SKILL.md` |
| `ship-infinite` | Release | Full release/build/publish workflow | `.claude/skills/ship-infinite/SKILL.md` |
| `release-notes-audit` | Release | Verify release notes match actual code | `.claude/skills/release-notes-audit/SKILL.md` |

---

## Core Invariants & Rules

1. **Clean Room / Licensing**: Infinite is MIT licensed. Never open, read, grep, or reference GPL sources (e.g. Kronos, Cmajor, SuperCollider, BespokeSynth). Citing open academic papers (e.g. Kronos paper Norilo 2015) is permitted; GPL code is strictly prohibited.
2. **Syntax Rules**: Bare names, no sigils (e.g. `P.y += bass * 2`, never `@P.y += bass * 2`).
3. **Execution Rule**: Before making changes or running procedures related to any category above, always view and execute against the corresponding skill file (`.claude/skills/<skill-name>/SKILL.md` or `.agents/skills/<skill-name>/SKILL.md`).
