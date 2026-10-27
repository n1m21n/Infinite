# Dials: the model's subjective words and the numbers under them

Status 2026-10-27: decided, not built. Extends W4 (mood) and W5 (link layer). No new node or panel; the dials are inputs to the generator and a readout of what it made.

## What "average" means here

The gathered data gives, per context (genre, mood, section, instrument cluster): P(knob value), and per knob a usage rate u_k = share of real patches where knob k leaves its default. The average model plays the typical values and moves the typical knobs (reverb mix, cutoff). That is the consistent baseline.

## Creativity, defined

Creativity = how far a result sits from the average model while staying playable.

| Part | Meaning | Number |
|---|---|---|
| Novelty N | surprise of the chosen values under the average model | mean surprisal in bits over the chosen knobs and pitches, divided by the uniform-random surprisal (0 = typical, 1 = random) |
| Reach R | knobs moved that real patches rarely move | share of moved knobs with u_k < 0.2, plus share of pitches outside the central 80% of the context's register |
| Coherence K | still passes the critic | critic score and gates B1-B10 style checks; a hard floor, not a weight |
| Variety V | different each time | mean pairwise distance between renders from the same context and different seeds (the B5 diversity measure) |

Creativity = (N + R) / 2, counted only on outputs with K above the floor. Variety is reported next to it, never folded in.

## The freedom dial c in 0..1 (how the generator uses it)

- Value sampling: temperature T = 1 + 2c on P(value | context), so c = 0 reproduces the average and c = 1 reaches the tails.
- Knob choice: knob k is touched with probability proportional to (1 - u_k)^c, so a high c moves the knobs that are normally left alone; c = 0 moves exactly the usual ones.
- Pitch: sample register from the context's pitch histogram flattened by the same T.
- Floor: every candidate is judged by the critic; below the floor it is redrawn, up to a retry cap. Freedom never skips the critic.
- Seed: same context and same seed give the same result; a new seed gives a different one.

## The final vocabulary (7 words)

| Word | What it is | Built from |
|---|---|---|
| Creativity | distance from the average, kept playable | N and R above |
| Consistency | sameness across seeds | 1 - V |
| Energy | drive and density | mood energy (note rate, velocity, drum density, tempo) |
| Brightness | spectral and register height | mood brightness, spectral centroid |
| Groove | pulse and regularity | mood danceability |
| Mood | light to dark | mood valence |
| Space | depth and width | reverb mix, delay, stereo width from knob data |

Each word is a number 0..1 under it, shown as a percentile among all rows (the `_pct` axes), so 0.5 is typical for the corpus. Declared genre values stay a prior; measured values are the evidence.

## Checks (before the words are trusted)

- c = 0 reproduces the average: Creativity ~ 0, Consistency high.
- Creativity rises monotonically with c on held-out contexts; K stays above the floor at c = 0.7.
- Reach: at c = 0.7 the share of rare knobs moved is at least 3x the c = 0 share.
- Owner listening check on c = 0, 0.5, 1 renders from one context before the dial is exposed anywhere.

## Where it plugs in

W5 matrix link: add c as a column of the context vector x, and the param block outputs a mean and a spread per knob so the spread can be scaled by T. The per-knob u_k table comes from the preset and param rows already gathered; build it first (small, no drive needed for the MIDI and param side).
