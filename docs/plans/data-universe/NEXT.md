# Next session: start here

Read this, then `next-phase.md` (approved plan, sections 1-12) and `README.md` (tracker). Nothing else.
Approved by the owner 2026-10-27: the whole of `next-phase.md` (incl. matrix link for W5, wavetable generator to improve the model only), the 2h C++ drum port, and the B5 redefinition (H_gen >= 0.90 x H_real).

## State at end of 2026-10-27

| Item | State |
|---|---|
| Steps 1, 3, 4 of the old plan | done |
| Step 2 Colab parity | done: 8/10, identical to Pro (`results/colab_2026-10-27.txt`) |
| Audio pass | ~121k rows in `audio/features.jsonl`; a 20-min chunk may still be running; resume until `collect_check.py` shows 0 missed |
| Step 5 render test | not started; needs a plugin state chunk from the Air; run after the audio pass ends |
| Python for universe tools | system python3 has NO numpy. Use `~/music videos/n1m21n-room/.venv/bin/python3 -I` |
| Antigravity Round 2 | task E done on the Pro; D (criteria scoreboard) and F (node proposals draft) not started |

## Order (one heavy job at a time, commit per step, tracker row per step)

1. Finish audio pass (`audio_features.py --per-folder 0 --budget 20 --resume`), re-run `collect_check.py`, rebuild atlas and clusters.
2. Step 5 render test (join preset to sound): yes/no.
3. Drum port 2h: hat-density limits in `ProposeGroove` / `SampleStyles.cpp`; sim reference `groove_sim.py` (AUC 0.591); build only via `scripts/ibuild.py`; rerun benchmark B4/B5 under the new B5 rule.
4. W1: `tools/universe/audio_features2.py` (pitch, chroma/key, tempo grid, MFCC, harmonic ratio, filename labels); test on 200 files, then chunks.
5. W4 mood layer and W2 sample-instrument builder (after W1 fields exist).
6. W5 link layer: count baseline, then low-rank matrix link; must beat baseline by >= 0.10 bits held out.
7. W3 wavetable generator whenever a heavy slot is free (never in `--scope shippable` without approval).
8. Retrain only when C1-C6 hold and G5 passes (Colab is available, `RETRAIN = True`).
9. Node proposals to the owner before anything is built.

## Rules

Pause the daemon before benchmarks; never delete datasets/builds without a per-folder yes; `main` not pushed; no Claude attribution in commits; new nodes/modes/panels need approval; park new ideas below.

## Parked

- Learned audio embedding on Colab GPU (model and any download need the owner's yes)
- Genre-conditioned B4 test
