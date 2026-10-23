# How work moves through Infinite

The full process lives in one place: [`.claude/skills/infinite-lifecycle/SKILL.md`](../.claude/skills/infinite-lifecycle/SKILL.md).
This page is the short version for people. The owner's standing decisions: [`docs/DECISIONS.md`](DECISIONS.md).

## Three loops

```mermaid
flowchart LR
    Q["QUALITY loop · per release<br/>goal → ship → measure escapes → name the leaking gate"]
    B["BUILD loop · per ask<br/>triage → lane → 12 stages with gates → merged"]
    R["RULE loop · monthly<br/>rule audit → keep / merge / retire"]
    B -. release content .-> Q
    Q -. leaking gate .-> R
    B -. corrections .-> R -. fewer, truer rules .-> B
```

- **Quality** judges each release by what escaped it (fixes after the tag, reverts, known
  failures, crashes, bug issues). Downloads and reach are context, not the measure.
- **Build** keeps each change correct, safe and finished.
- **Rule** keeps the rules few and true, so the process doesn't grow forever.

## The build loop at a glance

| Phase | Stages | Done means |
|---|---|---|
| Frame | 1 Strategy · 2 Research & baseline | Fits a pillar and the release goal; baseline repeats |
| Plan | 3 Plan · 4 Set up | Every concern lens marked; one branch per ask |
| Make | 5 Code · 6 Content · 7 Docs & i18n | Contracts kept on 3 platforms; presets compile; help covered |
| Check | 8 Review · 9 Test & perf | Invariants hold; tests, goldens and perf order pass |
| Land | 10 Merge · 11 Release · 12 Measure | Merged = closed; released (owner's call) = shipped; escapes traced to a gate |

Small fixes skip the frame and most of the plan (Lane 1); a new node runs most stages (Lane 2);
subsystems run all of them (Lane 3). Films, patches, brand and research have their own lanes
with done checks.

## Tools

```bash
python3 tools/lifecycle/rule_audit.py --memory ~/.claude/projects/-Users-namansoni-infinte/memory
```

Architecture diagrams: [`ARCHITECTURE_FLOWS.md`](ARCHITECTURE_FLOWS.md).
