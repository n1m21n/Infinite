# Roadmap: quest ledger + board

A small project-management system that runs on git. Every piece of work is a **quest** in an append-only ledger.
Git, not memory, says when each quest started, when it closed and which release shipped it.

```
 asks, bugs, plans, issues, PRs          git merges + direct commits        GitHub release notes
            │  roadmap.py add / sync              │                                 │
            ▼                                     ▼                                 ▼
      quests.jsonl  ◀──────────────  backfill.py (evidence → opened / closed / release, gaps → quests)
            │                                     │
            ▼                                     ▼
  roadmap.py build ──▶ roadmap.html          audit.json (coverage)
```

## Files

| File | What |
|---|---|
| `quests.jsonl` | The ledger. One JSON event per line, append-only. Never edit or delete lines. |
| `roadmap.py` | CLI plus board fold plus page build. `serve` is a thin file server for `roadmap.html`: it rebuilds in a fresh `build` process when the page is older than the ledger, template or `roadmap.py` (or 10 min old), sends `no-store`, and the page reloads every 20 s while visible. launchd `com.infinite.roadmap.live` runs it on port 47650. |
| `backfill.py` | Reconciles the ledger with git and the release notes. Safe to rerun: a second run writes nothing. |
| `audit.json` | Coverage written by `backfill.py` and shown on the quest board. |
| `template.html` → `roadmap.html` | The dashboard. |
| `milestone.json` | The next-release target shown as the slider above the tabs. Hand-edited **each release**: bump `version`/`since`, rewrite `goals`, list still-open quest ids per goal. Merged quests no tag contains yet count as done automatically; the first matching goal claims a quest (growth quests and epics are skipped). |
| `metrics.jsonl` | Daily stars/downloads snapshots and GA rows. |
| `auto.sh` | `backfill.py` then `build`. Runs hourly (launchd) and on git post-merge. Locked by `/tmp/infinite-roadmap.lock`. |

## Ledger schema

| Event | Fields | Meaning |
|---|---|---|
| `add` | `id ts track kind horizon title why src origin after user` | Quest opened. `ts` is when it was **logged**. |
| `done` / `drop` | `id ts why [release]` | Closed (merged to main) or retired (for example a duplicate: `why` names the survivor). |
| `edit` | any of `title src horizon …` | Plain field changes. |
| `edit` (evidence) | `opened closed release commits epic` | Written by `backfill.py` or `link`. These values **override** the typed ones. |
| `move` / `shipped` | `horizon` / `release` | Legacy. Evidence now sets `release`. |

**`src` keys make reruns idempotent.** One key means one quest, forever, even after a drop.

| Key | Source |
|---|---|
| `merge:<branch>` / `merge:<sha7>` | A merge into main (the sha7 form is for unnamed merges). |
| `note:<tag>:<n>` | Release-note bullet *n* that no quest covered. |
| `upkeep:<tag>` | Leftover direct commits in that release window: docs, tests, skills, tooling. |
| `plan:` `brief:` `issue:` `pr:` | `docs/plans`, `docs/fix-briefs`, GitHub issues and PRs (`roadmap.py sync`). |

## Rules

1. **Never type a date or a release.** Link commits and let `backfill.py` derive the rest:
   - `opened` = the first commit
   - `closed` = the merge (or the last commit)
   - `release` = the first `v*` tag containing it
2. **Closed ≠ shipped.** Closed means merged to main. Shipped means a tagged release contains it. A quest with no release shows as "merged, not released".
3. **Every merge into main is a quest.**
   - Kind comes from the branch prefix:
     - `feature/` → feature
     - `bugfix/`, `fix/`, `perf/` → fix
     - `docs/`, `chore/`, `test/` → infra
   - Syncs, `release/*` merges and empty merges are skipped.
   - A merge already linked to a hand-logged quest is not added again.
4. **Every direct-to-main commit belongs to a quest.** It goes to the most similar quest of its release, or else to that release's upkeep quest.
5. **Every release-note bullet is covered.** It is either matched to a quest shipped within ±1 tag, or it becomes a `note:` quest linked to the direct commits it describes.
6. **Epics group quests.** `edit Rn --epic a,b,c`:
   - An epic earns 0 XP; its children earn it.
   - It spans the children's dates (plus any commits it owns).
   - It ships when its last child ships.
7. **Duplicates are dropped, never deleted.** `drop Rdup --why "duplicate of Rkeep"`. Dropping works on done quests too.
8. XP: feature/growth = 10, fix/infra = 5. Downloads are Gold. Levels are the user base only.

## Commands

```bash
python3 tools/roadmap/roadmap.py add --track nodes --kind feature --horizon next --title "..." --why "..."
python3 tools/roadmap/roadmap.py done R3 --why "what shipped" --commits <merge-sha>
python3 tools/roadmap/roadmap.py link R3 --commits abc1234,def5678        # attach evidence later
python3 tools/roadmap/roadmap.py edit R3 --title "..." --src merge:feature/x --epic R4,R5
python3 tools/roadmap/roadmap.py drop R9 --why "duplicate of R3"
python3 tools/roadmap/backfill.py --dry     # preview on a temp copy of the ledger (prints + adds, ~ edits)
python3 tools/roadmap/backfill.py           # reconcile for real, then write audit.json
python3 tools/roadmap/backfill.py --audit   # coverage only
python3 tools/roadmap/roadmap.py build --open
```

## Reading `audit.json`

| Field | Healthy |
|---|---|
| `merges` [covered, total] | Equal |
| `direct` [attributed, total] | Equal |
| `evidence` [with, done] | Near equal. The gap is epics, PR reviews and note quests dated only to their tag. |
| `loose` | Empty. Otherwise these are unreleased direct commits waiting for a `link`. |
| `no_evidence` | Quests with no git trail. Check that each one is expected. |

## Reusing it for another repo

1. Copy this folder.
2. In `roadmap.py`, set `TRACKS`, `KINDS` and the GitHub repo.
3. In `backfill.py`, set `TRACK_WORDS` and `HOUSEKEEPING`.
4. Tags must be `v*`.
5. Run `backfill.py --dry` and read the adds.
6. Curate them (drop duplicates, create epics).
7. Run it for real.
