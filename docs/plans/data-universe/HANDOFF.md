# Handoff 2026-10-27 (supersedes NEXT.md order; read this, then README.md tracker)

Approved by the owner: next-phase.md whole, 2h drum port, B5 rule (H_gen >= 0.90 x H_real), matrix link for W5, wavetable generator (model-only), axes-strategy sections 1-7. Rules unchanged: one heavy job at a time (8 GB), commit per step with no Claude attribution, new nodes/panels/downloads need a yes, `main` not pushed, python = `~/music videos/n1m21n-room/.venv/bin/python3 -I`.

## Where things stand

| Area | State |
|---|---|
| Audio pass | 148,043 rows; `collect_check.py` not re-run; atlas and clusters not rebuilt |
| W1 v2 features | extractor done; chroma fixed (16384 frames, peaks, 110-2000 Hz); uncommitted edit in `audio_features2.py`; full library run not started |
| W3 wavetable generator | done, bar 1 met (AUC 0.558); bar 2 unmeasured |
| W4 mood | built; correlation bar unmet (r ~ 0 to 0.32); superseded by axes strategy |
| Character x node matrix | done (11 words, 26,915 presets); more corpora to add |
| Node registry | done (342 types, 6,974 params); role priors cover 29% |
| Axes strategy, recipes, dials | designs written; axes approved; nothing in recipes built |
| Album profile | 162 tracks measured, `albums/n1m21n_tracks.jsonl` |
| W5 link layer | `link_baseline.py` written; first run NOT valid (below) |

## Owner profile (measured, vs 2,294 library loops)

Tempo median 122 BPM (81st pct), loudness RMS 98th pct, centroid 425 Hz (37th), crest 4.9 (15th: heavily compressed), low share 70th pct, stereo corr 0.68 (44th). Keys: mostly minor (Am 29, Gm 25, G#m 14, Bm 11 of 162). Read: loud, dark, compressed, minor, mid-fast. Caveat: songs vs library loops; the library has no key yet (needs the v2 run).

## W5 baseline: why the first run is not trustworthy

Only 3,148 of 127,640 songs carry a genre label (family is mostly "unlabeled"), and the label set is 9 genres. Result: B0 0.0259, B1 0.0273, B2 0.0281 bits/event; genre and mood make it WORSE, which means the event is almost always 0 (threshold >= 0.5 of bars) and the test is degenerate. Fix before the matrix link: (1) event = activity above the role's own median, or use the continuous activity with squared error; (2) genre from the path-word fallback in `mood.check` to widen labels; (3) report bits against a role+tenth baseline.

## Order for next session

1. Commit `audio_features2.py` chroma fix + `link_baseline.py` (separately from the unrelated SEO files on this branch).
2. Audio pass to 0 missed, then atlas and clusters rebuild.
3. Fix W5 baseline (above), then the low-rank matrix link: must beat the count baseline by >= 0.10 bits held out. Add freedom dial c and per-knob mean+spread (dials.md).
4. v2 library run in 20-min chunks; W1 bar >= 95% rows with the new fields.
5. Recipes step 1: response harness (render each node over knob grids, measure percept delta). Heavy slot.
6. Axes: percept + structure descriptors on rows (S1, S3), word mining (S2), then owner listening session (S5).
7. W2 sample-instrument builder; step 5 render test (needs the Air plugin state chunk).
8. Retrain only if C1-C6 and G5 hold; node proposals to the owner.

## Open owner decisions

B5 cluster entropy (0.672 vs ~0.842); label set for L3 affect (a download); contrast as a dial word; album count (owner says 11-12, music.json has 20 releases).
