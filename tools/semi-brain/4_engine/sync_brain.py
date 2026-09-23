#!/usr/bin/env python3
"""
sync_brain.py
Automated incremental synchronization and learning flywheel for the Semi-Brain:
1. Detects new commits and deleted docs/prompts since last sync.
2. Ingests user/agent feedback and corrections into DPO preference pairs.
3. Re-compiles SFT & DPO training datasets automatically.
"""

import sys
import json
import argparse
import fcntl
import subprocess
from pathlib import Path

SEMI_BRAIN_DIR = Path(__file__).resolve().parents[1]
EXTRACTORS_DIR = SEMI_BRAIN_DIR / "1_extractors"
DATASETS_DIR = SEMI_BRAIN_DIR / "3_datasets"
DISTILLED_DIR = SEMI_BRAIN_DIR / "2_distilled_brain"

def run_extraction_pipeline():
    print("🔄 Running incremental extraction pipeline...")
    
    # 1. Mine git history
    subprocess.run(["python3", str(EXTRACTORS_DIR / "mine_git_history.py")], check=True)
    
    # 2. Recover deleted docs
    subprocess.run(["python3", str(EXTRACTORS_DIR / "recover_git_docs.py")], check=True)
    
    # 3. Parse skills and rules
    subprocess.run(["python3", str(EXTRACTORS_DIR / "parse_skills_and_rules.py")], check=True)
    
    # 4. Ingest BYOX blueprints
    subprocess.run(["python3", str(EXTRACTORS_DIR / "ingest_build_your_own_x.py")], check=True)

    # 5. Mine real Claude Code session history (every local transcript for this project)
    subprocess.run(["python3", str(EXTRACTORS_DIR / "mine_session_history.py")], check=True)

    # 5b. Mine Antigravity/Gemini agent transcripts for this project too, so the brain isn't
    # blind to work done outside Claude Code.
    subprocess.run(["python3", str(EXTRACTORS_DIR / "mine_antigravity_history.py")], check=True)

    # 6. Categorize session history (episodic/semantic/procedural/problem_solution), cluster into
    # topics, and pair problems with their solutions. Reuses cached embeddings when the turn count
    # hasn't changed, so this is cheap on every sync after the first.
    subprocess.run(["python3", str(EXTRACTORS_DIR / "analyze_session_linguistics.py")], check=True)

    # 6b. Classify session history against THIS repo's own taxonomy instead of a generic one:
    # Conventional Commit type/scope vocabulary (feat/fix/refactor/..., arrange/audio/field/...)
    # and node categories derived from src/nodes/*.{h,cpp} via ast_symbol_graph.json. Also buckets
    # turns by ISO week to track which scopes/work types/node categories are trending.
    subprocess.run(["python3", str(EXTRACTORS_DIR / "classify_dev_trajectory.py")], check=True)

    # 7. Compile datasets
    subprocess.run(["python3", str(DATASETS_DIR / "compile_training_data.py")], check=True)

    # 8. Rebuild the hybrid (FTS5 + dense vector) retrieval index from all corpora above
    subprocess.run(["python3", str(EXTRACTORS_DIR / "index_codebase.py")], check=True)

    print("✅ Semi-Brain successfully synchronized and datasets updated!")

def record_feedback(task: str, chosen_fix: str, rejected_fix: str, reason: str):
    """
    Appends an explicit user correction / feedback pair to the DPO dataset
    """
    DATASETS_DIR.mkdir(parents=True, exist_ok=True)
    dpo_file = DATASETS_DIR / "train_dpo_preference.jsonl"
    
    new_entry = {
        "prompt": f"Task: {task}\nFeedback / Correction Context: {reason}",
        "chosen": chosen_fix,
        "rejected": rejected_fix
    }
    
    with open(dpo_file, "a", encoding="utf-8") as f:
        f.write(json.dumps(new_entry) + "\n")
        
    print(f"✨ Recorded new feedback pair into {dpo_file}")
    print(f"Prompt: {task}")
    print(f"Reason: {reason}")

def main():
    parser = argparse.ArgumentParser(description="Synchronize and improve the Semi-Brain environment")
    parser.add_argument("--sync", action="store_true", help="Run full incremental sync across Git, docs, and datasets")
    parser.add_argument("--feedback", action="store_true", help="Record an agent correction / preference pair")
    parser.add_argument("--task", type=str, help="Task description for feedback")
    parser.add_argument("--chosen", type=str, help="The correct/preferred implementation")
    parser.add_argument("--rejected", type=str, help="The rejected naive implementation")
    parser.add_argument("--reason", type=str, default="", help="Why chosen was preferred over rejected")
    
    args = parser.parse_args()
    
    if args.feedback and args.task and args.chosen and args.rejected:
        record_feedback(args.task, args.chosen, args.rejected, args.reason)
    else:
        # post-commit and post-merge both fire this in the background, so a commit followed
        # by a merge used to run two syncs at once and interleave writes into the same corpus
        # files (git_commits_corpus.json came out as invalid JSON). One sync at a time; a
        # second one arriving mid-run skips; the next sync catches up on anything it missed.
        lock_file = open(SEMI_BRAIN_DIR / "1_extractors" / "output" / ".sync.lock", "w")
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("Another Semi-Brain sync is already running - skipping.")
            return
        run_extraction_pipeline()

if __name__ == "__main__":
    main()
