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
# symbols with file:line, the code area, past fixes, skills to load. Start here.
python3 tools/semi-brain/4_engine/semi_brain_cli.py --brief "<description>"   # --json for tools

# Fast Apple Metal Local Neural Router (LoRA fine-tuned Qwen2.5-0.5B):
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

The same brief, plus L0, is available as MCP tools (server `semi-brain` in
`.mcp.json`, `l0/mcp_server.py`): `brain_brief`, `brain_recall`,
`brain_assert`, `brain_retract`.

**main.cpp is a virtual split of ~80 regions** (`l4/regions.py`, built by
`1_extractors/build_main_cpp_regions.py` from co-edit history, the call
graph, name families and `// ====` banners; regenerate after a large
main.cpp reorganization with `python3 1_extractors/build_main_cpp_regions.py`).
A brief names the region instead of the bare path -
`src/main.cpp@drawoscillator L13399-14991` - so read that one region, not
the whole 95k-line file. `python3 4_engine/semi_brain_cli.py --regions`
prints the table of contents. The replay's `region_hit` (additive, not part
of `gate`) scores whether the best-ranked main.cpp symbol lands in the same
region as the target, not just the same file - "right file, wrong 30k
lines" is a miss there even though file MRR can't see it; baseline on the
181-commit replay is 0.20.

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
L0 notes, proposals, learned weights) never leave this machine. They are gitignored,
and `.git/hooks/pre-push` blocks them. The rest of the brain (code, public
`knowledge_index.db`, `3_datasets/*.jsonl`) may be committed, in its own
commit, not inside a feature commit.

---

## 4. Key Distilled References

* **System 1 Priors & Negative Taboos**: `tools/semi-brain/2_distilled_brain/system1_taste_and_priors.md`
* **System 2 Systems Logic & Blast Radius**: `tools/semi-brain/2_distilled_brain/system2_systems_logic.md`
* **Choice Tree Evaluator**: `tools/semi-brain/2_distilled_brain/choice_tree_evaluator.md`
* **Categorization & Rate Hierarchy**: `tools/semi-brain/2_distilled_brain/categorization_matrix.md`
* **From-Scratch Systems Blueprints**: `tools/semi-brain/2_distilled_brain/first_principles_blueprints.md`
