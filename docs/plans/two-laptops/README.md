# Two laptops, two Claude accounts

Pro (`Namans-MacBook-Pro-366`, account 1, main work) and Air (`Namans-MacBook-Air`, account 2).
Both keep the repo at `/Users/namansoni/infinte`. They sync with each other over SSH first;
GitHub gets only what has been merged on one laptop. Tool: `scripts/sync_laptops.sh`.

## Roles (fixed)

| | **Pro = Primary** | **Air = Secondary + Marketing** |
|---|---|---|
| Claude account | 1 | 2 |
| Merges to `main`, pushes `origin` | **yes, only here** | no |
| Commits | on its branches | **yes**, on its own `air/<slug>` branch, when the work is done |
| Edits code | yes | **yes**, in its own worktree `~/infinte-wt/<slug>`, one branch per task |
| Builds, benches, CI, releases | yes | no (a sweep that needs no bench is fine) |
| Main work | v0.6 blocks, `feature/v06-sound`, core `src/` | **marketing lane**: motion design, films, videos, brand, website, social, press kit; plus audits, sweeps, docs, research, review |
| Starts a task by | writing a brief, `dispatch` | reading `docs/handoff/<slug>.md` |
| Finishes by | fetch, review, merge, gates, push | committing on its branch and stopping |

Rules:

1. Only the Pro writes `main` and `origin`. The Air commits; the Pro merges.
2. One branch, one owner laptop, one task. Never edit the same branch on both.
3. The Air may touch code, but not files the Pro's open branch is changing (check `status` and `wip` first). Overlap goes in the brief.
4. The Air works from a committed brief; anything it must read is committed or inside the brief.
5. The two laptops sync over the local network (same Wi-Fi). Away from each other, sync pauses and nothing is lost; it catches up on the next tick once they are back on one network.

### Marketing lane (Air)

Owns: `website/`, `docs/brand/`, `tools/brand/`, `press_kit/`, film and video work (`~/infinite-launch-video`, `~/infinite-node-gallery`), social and thumbnails.
Uses the global skills (`motion-film`, `motion-animation`, `motion-typography`, `motion-sfx`, `film-direction`, `film-editing`, `node-tutorial-film`, `camera-language`, ...) and the film/brand memories, which are copied to the Air.
Brand surfaces follow `docs/brand/BRAND.md`. No film renders until the owner asks.

## What is shared, and how

| Thing | Shared? | How |
|---|---|---|
| Code, branches, commits | yes | git: each laptop is a remote of the other (`air` on the Pro, `pro` on the Air) |
| Skills, agents, `AGENTS.md`, `docs/NOW.md` (claims, decisions) | yes | they are in the repo, so they travel with git |
| Claude memory (`~/.claude/projects/-Users-namansoni-infinte/memory/`) | yes, manual | `scripts/sync_laptops.sh memory` (newest file wins, nothing deleted) |
| Global skills `~/.claude/skills/` (with their scripts), `~/.claude/CLAUDE.md` | yes, every 5 min | the `tick` job, newest file wins |
| Machine installs in `~/.local/bin` (`heygen`, `graphify`, `repowise`) | no | install per laptop |
| Film/video working folders (`~/infinite-launch-video`, ...) | no, copied once | `rsync` by hand when needed |
| Uncommitted files | **no** | commit a WIP before you leave a laptop |
| Chat corpora, `tools/semi-brain` data | **no, on purpose** | local only, never pushed; each laptop regenerates its own |
| Build outputs, caches, `external/vst3sdk` | no | rebuild per laptop |
| Claude account, `~/.claude/settings`, plugins, MCP servers | no | per account; copy once if you want them identical |
| Running sessions and chat history | no | each account sees only its own |

## The loop

```
start of session (either laptop)
  1. scripts/sync_laptops.sh status      who is on what, what differs
  2. scripts/sync_laptops.sh fetch       get the other laptop's branches
  3. read docs/NOW.md, add a claim row   Session name starts with pro- or air-
work on ONE branch per ask               feature/<x> or bugfix/<x>, commit per step
end of session
  4. commit (WIP is fine), update NOW.md, remove the claim
  5. scripts/sync_laptops.sh memory      merge memory both ways
merge point (one laptop only)
  6. fetch the other laptop, merge both branches into main here
  7. gates pass, then push origin from this laptop only
```

## Who does what

| Rule | Why |
|---|---|
| One branch has one owner laptop. Never edit the same branch on both | no conflicts to resolve |
| The claim row in `docs/NOW.md` names the laptop (`pro-...` / `air-...`) | the other account sees it after a fetch |
| Pro: the long v0.6 block work, builds, benches | the main machine; `feature/v06-sound` lives here |
| Air: independent lanes that touch different files: sweeps and audits (no bench), docs and research, site, films, review | little overlap with the Pro's files |
| Both can run a bench, but one bench per laptop at a time | each laptop is its own quiet machine |
| Only one laptop merges to `main` and pushes `origin` | one clean history |
| The Air never pushes to the Pro's checked-out branch; it asks the Pro to fetch | git refuses it anyway |

## Commands (`scripts/sync_laptops.sh`)

| Command | Use |
|---|---|
| `status` | which branches differ, uncommitted counts on both laptops |
| `fetch` | pull the other laptop's committed branches |
| `wip` | list the other laptop's uncommitted-work snapshots (kept fresh every 5 min) |
| `wip diff <branch>` | what changed in that snapshot |
| `wip branch <branch>` | turn the snapshot into a local branch `wip/<peer>-<branch>` |
| `handoff <slug> <branch> <brief-file>` | deliver a task brief to the other laptop at `docs/handoff/<slug>.md`; tell its Claude "read that file and do it" |
| `dispatch <slug> <branch> <brief>` | deliver the brief and run the Air's Claude on it, in its own worktree |
| `memory` | full memory merge, including the `MEMORY.md` index |
| `tick` | what the background job runs: snapshot, fetch, safe memory, global skills and `~/.claude/CLAUDE.md` (newest wins, nothing deleted) |

Background job: `scripts/install_laptop_sync.sh` (launchd, every 5 min, log `~/Library/Logs/laptopsync.log`; `remove` uninstalls).

## Giving the other laptop's Claude work

The two accounts cannot message each other. The Primary writes a brief, `handoff` delivers it, and
the owner tells the Secondary's Claude to read it. Or, without touching the Air:

```
scripts/sync_laptops.sh dispatch <slug> <branch> <brief-file>   # worktree ~/infinte-wt/<slug> on the Air, Claude runs headless
scripts/sync_laptops.sh dispatch-log <slug>                     # tail its log
scripts/sync_laptops.sh fetch                                   # then review air/<branch> and merge on the Pro
```

It uses the token in `~/.claude-remote-token` on the Air (`claude setup-token`), edits only inside its
own worktree, and may not push or merge. Tools allowed: Read, Edit, Write, Glob, Grep and a few
`git`/`python3`/`scripts/` Bash patterns (override with `DISPATCH_TOOLS`).

## Rules of thumb

- If the claim table is stale on the other laptop, run `fetch` first, then read its `NOW.md` from `air/<branch>`.
- Conflicts in `docs/NOW.md` are expected; keep both rows.
- If you forget which laptop has the newest work: `scripts/sync_laptops.sh status`.
