---
name: semi-brain
description: Infinite's distilled-brain CLI (tools/semi-brain). Query it before a non-trivial architecture decision - fix-in-place vs rewrite/refactor, a new invariant, a choice between designs - and fold its Branch A/B/C scores and invariant checklist into the recommendation. Also use after a user correction to log a DPO preference, or to add a newly confirmed bug pattern to 2_distilled_brain/.
---

# Semi-Brain: Cognitive Twin & Invariant Engine

The Semi-Brain encodes Infinite's core engineering reflexes: System 1 (Intuitive taste, zero-sigil clarity, realtime safety, UI symmetry), System 2 (Systems thinking, 9-question blast radius, invariant interaction audit), and Choice Tree MCTS rollouts.

---

## 1. When to Consult the Semi-Brain

- **Before a non-trivial architecture decision**: fix-in-place vs rewrite,
  a new invariant, a choice between designs. Not for routine bug fixes or
  one-file edits.
- **Not a replacement for other skills**: the 9-question bug analysis is
  owned by `bug-blast-radius`, the sibling-undo check by
  `invariant-interaction-audit`. The brain scores options; those skills do
  the analysis.

---

## 2. How to Run the Semi-Brain from CLI

To get an immediate architectural breakdown and invariant brief for any task:
```bash
# Cognitive reasoning analysis (RAG + AST + System 1/2 + MCTS Choice Tree):
python3 tools/semi-brain/4_engine/semi_brain_cli.py "<description of bug or feature>"

# Short brief (~200 tokens): likely files with the past fix/chat behind each,
# symbols with file:start-end, the code area, past fixes, skills to load. Start here.
python3 tools/semi-brain/4_engine/semi_brain_cli.py --brief "<description>"   # --json for tools

# Manual diagnostic only: the LoRA router (Qwen2.5-0.5B), adapter from 2026-09-15 (before
# v2), free prose, never scored and not in the brief. Decided 2026-09-25: not retrained or
# wired in unless a replay shows it catches what L0 notes miss.
python3 tools/semi-brain/4_engine/semi_brain_cli.py "<description of bug or feature>" --neural
```

How a query is answered: L2 searches each compartment (code, history, skills,
plans, conversations, research, external) separately and merges them by quota;
L3 follows the hits along doc -> code edges (a commit's files, files/symbols a
chat or skill names) to rank files; L4 adds files that usually change with the
best ones; L5 writes the brief. On the historical replay this scores 0.568
(file MRR 0.80, top file right 49% of the time) against 0.28 before Block B.

**The brief arrives by itself.** A UserPromptSubmit hook
(`4_engine/hooks/prompt_brief.py`) asks the daemon's warm brief server
(`l5/serve.py`, ~150 ms) and adds the brief to every prompt as "Semi-Brain
brief (a lead, not a fact)". Treat it as a lead: check the files it names
before acting on them. When a session id is known, the files edited earlier
in that session, and lately anywhere, count too (`l3/recent.py`, with
weights from `l3/weights.py`).

**The goal is to save Claude time and tokens, so every hooked prompt is put in an arm**
(`l5/serve.py`): `holdout` (25%, random) gets no brief and is the baseline; `quiet` gets
none because `l5/gate.py` judged the brain unsure (best symbol score < 20 or fewer than two
ranking lists agreeing on the top non-hub file; calibration in its docstring: only 30% of
prompts touch a non-hub src/ file, and the brief's top 4 hits in 16%); `shown` gets it.
Every brief is logged with its arm and confidence either way. `l1/outcomes.py` records what
each turn cost (tool calls, calls up to the first edit, context and output tokens), and
`python3 5_evals/live.py` prints `savings`: median cost per arm, `shown_vs_holdout`
(negative = the brief saves), and `would_hit4` per arm (was the quiet arm right to be
quiet). That comparison, not the replay, answers "does the brief pay for itself"; it needs
about 50 working turns per arm before it means anything.

The same brief, plus L0, is available as MCP tools (server `semi-brain` in
`.mcp.json`, `l0/mcp_server.py`): `brain_brief`, `brain_recall`,
`brain_assert`, `brain_retract`.

**main.cpp is a virtual split of 73 contiguous, non-overlapping regions**
(`l4/regions.py`, built by `1_extractors/build_main_cpp_regions.py` from
co-edit history, the call graph, name families and `// ====` banners, full
file coverage, 3000-line cap; regenerate after a large main.cpp
reorganization with `python3 1_extractors/build_main_cpp_regions.py`).
`Regions.rank_regions(symbols)` ranks regions by a weighted vote over a
result's matched symbols (each votes `1/(rank+1)` for its region, so several
agreeing lower-ranked symbols can outrank one stray higher-ranked one) -
shared by the brief (shows the top 2, `src/main.cpp@drawoscillator
L13399-14991`, instead of the bare 95k-line path) and by the replay's
`region_hit`/`region_r3` (additive, not part of `gate`): whether the
top-ranked (or top-3) region matches the target's, not just the file -
"right file, wrong 30k lines" is a miss there even though file MRR can't see
it. On the 181-commit replay: region R@1 0.20 (baseline) -> 0.34 (contiguous
split) -> 0.36 (weighted-vote ranking), region R@3 0.54.
`python3 4_engine/semi_brain_cli.py --regions` prints the table of contents.
`5_evals/live.py` scores the same thing against real turns: region hit@1/@2
from git-diffing the commits of turns that touched main.cpp (only turns that
committed are scorable - `l1/outcomes.py` has no line-level record for
edits that were never committed). The replay's `focus_hit` asks the next step down: does the
best matched symbol inside a top region overlap the edited function? 11% on commits, 0% on
prompts, with a ceiling of 12% (0% on prompts) for any matched symbol, so the brief does not show a focus
span; symbol matching, not the pick, is what limits line-level precision.

To run the automated 30-case benchmark evaluation suite (self-confirming; the
replay above is the real gate):
```bash
python3 tools/semi-brain/5_evals/run_evals.py
```

---

## 3. How to Update & Improve the Brain After Completing Work

Syncing is automatic: a launchd agent (`l1/brain_watchd.py`, installed by
`4_engine/install_hooks.sh`) watches git, `src/`, skills, `docs/` and the
session transcripts and runs an incremental `sync_brain.py --sync` 2 s after
things go quiet; without the daemon, post-commit/post-merge do it (see
`run-infinite-hygiene` "Efficient routes"). A sync takes seconds. Session
analysis / dev trajectory refit at most every 30 min (`--sync --full` forces
them). Don't run it by hand, and never `git commit -am`, or the regenerated
corpora get swept into your commit. Retrieval changes are gated by the
historical replay: `python3 tools/semi-brain/5_evals/replay/replay.py --label X`
must not score below the last kept scorecard in `5_evals/replay/history.jsonl`.

**Two replays gate a change**, and both must hold:

| Replay | Command | Gate |
|---|---|---|
| Past fix commits (181) | `replay.py --label X` | `gate` >= the last `replay` card |
| Real prompts -> files that turn edited (local only) | `replay.py --label X --cases sessions` | `nohub_gate` does not drop |

`nohub` scores leave `src/main.cpp` out. It is a target in 87% of prompts,
so with it every answer looks right. Each full replay takes about 1-2.5 min.
Run replays only while no benchmark is measuring (`ab.sh`/`run_all.sh`, or
the watch daemon paused).

**Leave notes for later sessions (L0).** When a session establishes something
the next one should know, assert it: a fact you verified, a decision and its
reason, an alternative you rejected and why, an open question, or where to
look for what.
```bash
python3 tools/semi-brain/l0/cli.py assert "<claim>" --kind fact|decision|rejected|question|hint \
  --evidence commit:<hash> --evidence symbol:<Name> --evidence file:<path>[:line] --confidence 0.7
python3 tools/semi-brain/l0/cli.py recall "<question>"   # what a query would see
python3 tools/semi-brain/l0/cli.py retract <id> --reason "<why it was wrong>"
```
- Assert only what the session established, never a guess.
- The tier is set for you:
  - verified: some evidence resolves in the repo
  - claude: nothing resolves
  - owner: only on the owner's own word (`--owner`, `approve`); never on your own inference
- A note's weight is capped at 0.6, so it can never outrank code.
- Repeating a claim, or retrieving it, never adds weight. Only later edits to the note's files do, credited nightly.
- Matching notes show as `Note (...)` lines in the brief.

**Sleep (nightly, automatic).** `l1/sleep.py` runs at 04:30 via launchd:
- It retunes the weights on the prompt replay: even cases pick, odd cases must confirm, and the with-main.cpp score must not drop.
- It credits L0 notes from real edits.
- It writes "look here" proposals: files the brief keeps missing, with the words of those prompts.
- It skips itself while Infinite runs or the daemon is paused.

The owner reviews the proposals:
```bash
python3 tools/semi-brain/l1/sleep.py --proposals
python3 tools/semi-brain/l1/sleep.py --approve <id>   # owner only -> owner-tier L0 note
python3 tools/semi-brain/l1/sleep.py --reject <id>
```

What *is* manual:
1. **Log a user correction** into the DPO preference dataset:
   ```bash
   python3 tools/semi-brain/4_engine/sync_brain.py --feedback \
     --task "<task description>" \
     --chosen "<what was the correct invariant-safe fix>" \
     --rejected "<what was the flawed/naive approach>" \
     --reason "<why chosen was necessary>"
   ```
2. **Grow the brain**: when a session confirms a bug pattern that
   `2_distilled_brain/` doesn't have, propose adding it there.

**Privacy**: chat/session-derived corpora (`session_history_corpus.json`,
`antigravity_history_corpus.json`, `session_analysis_corpus.json`,
`session_embeddings_cache.npy`, `dev_trajectory_corpus.json`,
`knowledge_index_private.db`, and all of `l1/state/`: outcome log, briefs,
L0 notes, proposals, learned weights, the fastembed model cache) never leave
this machine. They are gitignored, and `.git/hooks/pre-push` blocks them.
The rest of the brain (code, public `knowledge_index.db`,
`3_datasets/*.jsonl`) may be committed, in its own commit, not inside a
feature commit.

**Watch daemon gotcha**: `com.infinite.semi-brain.watchd` pauses itself
during a bench run and does not always resume on its own afterward - if
`l1/state/briefs.jsonl` stops growing across a session while `outcomes.jsonl`
keeps growing (it syncs independently, on its own cadence), the daemon is
probably down. Check with `launchctl list | grep brain`; restart it with
`launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.infinite.semi-brain.watchd.plist`.

---

## 4. Key Distilled References

* **System 1 Priors & Negative Taboos**: `tools/semi-brain/2_distilled_brain/system1_taste_and_priors.md`
* **System 2 Systems Logic & Blast Radius**: `tools/semi-brain/2_distilled_brain/system2_systems_logic.md`
* **Choice Tree Evaluator**: `tools/semi-brain/2_distilled_brain/choice_tree_evaluator.md`
* **Categorization & Rate Hierarchy**: `tools/semi-brain/2_distilled_brain/categorization_matrix.md`
* **From-Scratch Systems Blueprints**: `tools/semi-brain/2_distilled_brain/first_principles_blueprints.md`
