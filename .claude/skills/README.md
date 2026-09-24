# Infinite skills

Project skills for Infinite (`/Users/namansoni/infinte`). Each directory holds a
`SKILL.md`; Claude Code selects one by its frontmatter `description`, so the
description says *when* to load it, not what it contains.

Keep each `description` to about 330 characters: one line on what it covers,
then its main trigger phrases. Long descriptions get truncated in the skill
listing, and a truncated skill is rarely selected. The full scope, including
every trigger phrase and edge case, goes in the body's first section,
`## When to use (full scope)`. Add new use cases there, not to the frontmatter.

## Field — the embedded language

Field is Infinite's embedded language. It is **built and shipped** (sources in
`src/core/field/`; nodes Field Modifier, Field Effect, FieldPixel, Field
Primitive, Field Synth, Field Graph). These skills were written as the contract
before the code; where a skill and the code disagree, the code is the truth and
the skill should be fixed.

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
grows. For preset strings, use `field-pixel-presets` / `field-modifier-presets`.

**Two rules that apply to every one of them:**

- **Clean room.** Infinite is MIT. Never open, read, grep or reference GPL
  sources — Kronos, Cmajor, SuperCollider, or BespokeSynth (also at
  `/Users/namansoni/BespokeSynth`). The Kronos *paper* (Norilo, Computer Music
  Journal 39:4, 2015) is citable freely; its code is not.
- **Bare names, no sigils.** `P.y += bass * 2`, never `@P.y += bass * 2`.

Questions marked **OPEN** in these skills are genuinely open. Put them to the
owner; do not resolve them silently in code.

## Everything else

The full catalog (one line per skill, plus the agents) is in the repo-root
`AGENTS.md`. Keep that table as the only list, so the two never drift apart.
