# Generate Samples: what is left (written 2026-10-30, for the next session)

Plan: `PLAN-2026-10-30.md` (23 items, signed off). Results: `HANDOFF.md` (one entry per item; a miss is recorded as a miss).
Read `docs/DECISIONS.md` and this file first. Owner's standing instruction: finish every item without asking; record misses.

## Rules that bit last session
- Pro laptop, `main`, commit per step; no push or deploy without the owner (item 23).
- One heavy job at a time (8 GB Mac). Builds only through `python3 scripts/ibuild.py run -- nice -n 10 cmake --build build -j3`.
- Python: `~/Infinite/ml-venv/bin/python3 -I` (import numba first when `tools/universe` is on `sys.path`).
- Never commit `tools/seo/ledger.json`, `docs/plans/piano-roll/`, `external/vst3sdk`. Don't touch the `wt-piano-roll` worktree.
- Renders for a new node use `INFINITE_BIN=~/Infinite/repo/build/Infinite.app/Contents/MacOS/Infinite`.
- **Delete `/Applications/Infinite.app`** (a frozen copy made only for the effect sweeps, which are now finished).

## Done (committed)
| Item | State |
|---|---|
| 1-4, 7-9, 11 | in HANDOFF (4 valence MISS, 7-8 String MISS, 9 Wind family MISS, 11 pitch MET except mallets) |
| 10 | Piano node shipped (`49a9deba`); **family test not run yet** (see below) |
| 20 | Learning log, MET (`35234b92`) |
| 21 | Learner built (`tools/universe/learn_from_logs.py`, selftest PASS); compiler now logs its bank/voice picks. **Blocked on data**: 0 real takes. Rerun `learn_from_logs.py learn` after the owner has made a few hundred takes |
| 22 | Starter profile + Forget me that holds, MET (`b417f7fd`) |

## Left, in order

### 1. Items 5-6: effects write-up (results are in, not written up)
The effect sweeps finished. `feel/fx_map.*` (modified) and `feel/note_fx_map.*` (new) are committed alongside this file, but there is **no HANDOFF entry yet**.
Held-out accuracy, bar 90%:
- Audio effects. MET: Chorus .957, Delay .937, Drive .92, Dynamics .975, Flanger .948, Limiter .993, Phaser .974, Saturator .964, Stereo .914 (only 35 test moves). **MISS**: Audio Filter .693, Bitcrush .897, EQ .873, Reverb .861, Transient Shaper .851.
- Note effects. MET: Arpeggiator .949, Gate .947, Humanizer .955 (n 67), Note Echo .934, Note Filter .998, Note Strum .966, Note Transpose .958, Pitch Bend 1.0 (n 48), Velocity Curve 1.0 (n 52). **MISS**: Glide .893, Quantizer .878. **No score**: Note Stack (648 rows but 0 test pairs: find out why), Chorder (rendered 0; fixed in `note_fx.py` with the `GENERATORS` rule, needs a rerun).
- To do: `note_fx.py run --fx Chorder --banks 12`, then `note_fx.py score`; look into Note Stack's 0 pairs; write the HANDOFF entry for items 5-6. Arpeggiator printed no per-bank lines but has 432 rows (not looked into).

### 2. Item 10: Piano family test
Move the old Piano jsonl away first, then run
`INFINITE_BIN=... own_sweeps.py run --node Piano --count 400 --shards 3 --notes C3,C4`, then
`string_families.py --node Piano`. Bar: ratio well under 4. Write the HANDOFF entry and commit.

### 3. Item 12: add passing instruments to Generate Samples
Only instruments whose family test passed. So far none has (String, Wind MISS; Piano not yet tested). Most likely recorded as "nothing passed, nothing added" unless Piano passes.

### 4. Item 13: sound model, overnight rounds
`tools/universe/overnight_rounds.py`, one engine per night; stop when two rounds gain < 0.02. Bar: each main engine R² ≥ 0.6 on held-out banks. Long-running: start it in the background and check back.

### 5. Item 14: Wavetable knob direction (79.3%, bar 90%)
The conditioned models were already tried and lost (HANDOFF, "Wavetable knob model that uses the other knobs"). The recorded next step: render 100 Wavetable presets twice (different note-on phase) and measure how often a preset disagrees with itself. If it disagrees about 20% of the time, 90% cannot be reached with these characteristics. Record that as the miss, and the owner decides about the bar.

### 6. Item 17: 909 patterns and MPC / SP-1200 swing (started, nothing written to code yet)
Findings so far:
- 909 patterns: `~/Infinite/musical_datasets/2_grooves_rhythm/drumazon2_patterns/*.dr2seq` (329 files). Plain XML: `<Pattern shuffle= rate="1/16" tags="135 BPM|Basic|+ Kick|...">`, then `<Tracks>` with 16-character `steps` per lane (`BD SD RS CP LT MT HT CH OH CR RD GA`); `O` is accented, `o` normal, `.` a rest. Shuffle is 0 in 232 of them.
- Swing: `ableton_grooves/Swing/MPC/*.agr` and `Swing/SP 1200/*.agr`. These are Ableton's binary format, but the swing percent is in each file name (MPC 16ths 54-74, SP-1200 54-71). Use the 16ths only.
- Mapping: `DrumSequencer` moves odd 16ths by `swing * 0.5` steps, so MPC p% gives `swing = 4p - 2` (54% is 0.16, 66.7% is 0.67).
- Plan:
  - A new tool, `tools/universe/groove_909.py`, writes `GenerateSamplesGrooves909.inc`: unique 16-step figures, a four-on-the-floor pool (BD on 0, 4, 8 and 12) and a broken pool. The lanes are k, s, h (CH, or RD when CH is empty), o and clap. Leave `groove_bank.py`'s inc alone.
  - In `SampleStyles.cpp`, in the Drums-part groove block (around line 1820):
    - Four-on-the-floor families take the 909 hats, open hat and clap on about half of their takes. Draw after the existing `gb` draws.
    - Swung cards (`st.swing > 0.02`: downtempo, some house, bass music) draw the nearest MPC/SP value or a neighbour of it.
    - Pass that value into `force["swing"]` instead of `st.swing`.
- Gate: groove bank rows exist, and the drum gate holds. That means `spectral_gates.py` on rendered drums takes, before and after, with the drums false-reject rate not worse.

### 7. Items 15, 16, 18, 19: generator sound (not started)
| Item | What | Pointers |
|---|---|---|
| 15 | Synths low end | Put Wavetable and Analog lines under the motif, then refit the synths spectral gate (`spectral_gates.py`) so that octave 3 can pass. Bar: synths centroid and low share move toward the library. |
| 16 | Bass and kick click transients | Source: SubLab transient samples in `~/Infinite/musical_datasets/6_sub_bass_and_kicks/sublab`. Bar: the click measure against the library. |
| 18 | Textures merged with foley | Bar: textures against the library. |
| 19 | Diva closed loop | Run `vital_voice_bank.py --diva`. Diva rows are held out today (`BankVoice` in `SampleStyles.cpp` keeps `src == 0`) because they made leads and synths darker. Bar: no darker. |

### 8. Item 23: ship (needs the owner)
Desktop deploy of what passed (`deploy_desktop.sh`, headless smoke with 0 fails). Before any push, reconcile `main` with `origin/main`. **Only when the owner says.**

## Loose ends
- The starter profile may carry a little self-test noise (see the item 22 caveat).
- Nothing in the app reads `learning/profile/user_profile.json` or `learned_weights.json` yet. Weights stay uniform until a real log passes item 21's gate.
- Answer the owner's earlier question at the end of the project: is the training data (`~/Infinite/training-data`, `~/Infinite/datasets/midi`) still needed, and can it move to the WD Black?
