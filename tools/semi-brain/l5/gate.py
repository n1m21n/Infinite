#!/usr/bin/env python3
"""
gate.py
Should the prompt hook show this brief at all? A brief that points nowhere costs the reader
attention and a detour; saying nothing costs nothing. l5/serve.py puts a prompt the gate
rejects in the "quiet" arm: no brief, but the would-be brief is still logged, so
5_evals/live.py can check the gate was right to be quiet (would_hit4 per arm).

Signals (brain_core.analyze_problem -> frame.confidence):
  sym_top  the best symbol's lexical score: >= 10 once a code name matches the prompt's words
  agree    how many of the file-ranking lists (symbols, network, this session, recent work,
           L0 notes) have the best non-hub file in their top 5

Calibration (2026-09-25; 400 real prompts from l1/state/outcomes.jsonl, useful = a non-hub
src/ file the turn read or edited is in the brief's top 4; live index, so mildly optimistic):
  every prompt          shown 100%  useful 16%
  sym_top < 20          n=80        useful  7%
  agree <= 1            n=33        useful  6%
  this gate             shown  73%  useful 19%, keeps 86% of the useful briefs
Only 30% of prompts touch a non-hub src/ file at all, and neither signal separates well - the
gate drops the clearly bad bins and nothing more. Retune from the live card's quiet/shown
would_hit4 once it has data.
"""

SYM_MIN = 20.0
AGREE_MIN = 2


def confident(confidence):
    """True to show the brief. An empty confidence (an engine without the signals) shows it."""
    if not confidence:
        return True
    return confidence.get("sym_top", 0.0) >= SYM_MIN and confidence.get("agree", 0) >= AGREE_MIN
