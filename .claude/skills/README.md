# Infinite skills

Project skills for Infinite (`/Users/namansoni/infinte`). Each directory holds a
`SKILL.md`; Claude Code selects one by its frontmatter `description`, so the
description says *when* to load it, not what it contains.

## Field — the embedded language

Field is a new embedded programming language being designed for Infinite. It is
**not implemented yet**; these skills were written before the code so that
implementation sessions have a contract instead of a blank page.

Read them in this order:

| # | Skill | What it settles |
|---|---|---|
| 1 | [`field-language`](field-language/SKILL.md) | the one primitive, the five domains, inferred rates, bare-name syntax, `attrib`/`param`/`state`, types, operators, the wrong/right table |
| 2 | [`field-compiler`](field-compiler/SKILL.md) | lexer → AST → typed IR → three backends; domain inference as a dataflow fixpoint; error UX; retargetability |
| 3 | [`field-state`](field-state/SKILL.md) | `state` as delay sugar, the cycle legality rule, reset/serialize/hot-reload, the per-domain memory cost table, pixel ping-pong |
| 4 | [`field-domains`](field-domains/SKILL.md) | `reduce`, `map`, `broadcast`, `resample`, `downsample` — legality, cost, worked crossings |
| 5 | [`field-realtime`](field-realtime/SKILL.md) | the safety constraints as a diff checklist, plus the branching cost model per domain |
| 6 | [`field-integration`](field-integration/SKILL.md) | how a Field node joins `INode` / `ParamRef` / `ParamMailbox` / `GLUtil` / patch save-load without breaking anything |
| 7 | [`field-testing`](field-testing/SKILL.md) | the regression corpus, the golden-value harness, per-domain conformance, exit criteria for each of the 10 build steps |

1–2 are the core pair. 3–7 are subsets that deepen independently as the language
grows.

**Two rules that apply to every one of them:**

- **Clean room.** Infinite is MIT. Never open, read, grep or reference GPL
  sources — Kronos, Cmajor, SuperCollider, or BespokeSynth (also at
  `/Users/namansoni/BespokeSynth`). The Kronos *paper* (Norilo, Computer Music
  Journal 39:4, 2015) is citable freely; its code is not.
- **Bare names, no sigils.** `P.y += bass * 2`, never `@P.y += bass * 2`.

Questions marked **OPEN** in these skills are genuinely open. Put them to the
owner; do not resolve them silently in code.

## Everything else

The remaining skills cover the shipped app and are selected by their own
descriptions. Broadly:

- **Adding a node** — `new-audio-node`, `new-source-node`, `new-effect-node`,
  `new-geometry-node`, `new-compositing-node`, `new-modulator-node`,
  `new-utility-node`
- **Node appearance** — `node-ui-pillars` (the regression contract),
  `audio-node-ui` (the layout grammar)
- **Sweeps and audits** — `audio-node-sweep`, `audio-pipeline-sweep`,
  `av-sync-sweep`, `cable-logic-sweep`, `compositing-pipeline-sweep`,
  `data-accuracy-sweep`, `geometry-transform-sweep`, `modulation-sweep`,
  `node-param-audit`, `node-ui-sweep`, `output-projection-sweep`,
  `panels-sweep`, `pillar-parity-audit`, `rate-analysis-sweep`,
  `render-pipeline-sweep`, `shortcuts-sweep`
- **Process** — `bug-blast-radius`, `invariant-interaction-audit`,
  `codebase-navigation`, `git-branch-workflow`,
  `infinite-code-review`, `write-fix-brief`, `run-infinite-hygiene`,
  `ship-infinite`, `release-notes-audit`, `windows-parity`,
  `plugin-host-hardening`
