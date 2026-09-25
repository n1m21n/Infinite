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

# Fast Apple Metal Local Neural Router (LoRA fine-tuned Qwen2.5-0.5B):
python3 tools/semi-brain/4_engine/semi_brain_cli.py "<description of bug or feature>" --neural
```

To run the automated 30-case benchmark evaluation suite:
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
`knowledge_index_private.db`) never leave this machine. They are gitignored,
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
