# The characteristic set (P1, 2026-10-28)

The core of the sound model (HANDOFF.md section 1): one space whose axes are measurable rules, for timbre and music alike.
Each axis is one opposite pair (dark / bright is one axis), has one math rule, one unit, and is measured as a **curve over time**;
a sound or phrase is the set of its curves, summarised as mean, spread (std) and slope per second.
Tool: `tools/universe/characteristics.py` (P2). Status: approved by the owner 2026-10-28; axes 20 (space), 21 (vowel) and 22 (glide) added the same day.

```
            timbre axes (audio, mel, band rows)                    music axes (MIDI; synth rows give pitch from their note)
 loudness ─────────────── shared ─────────────── velocity      pitch height  register spread  tonal tension
 brightness  sharpness  warmth  weight  fullness  noisiness     harmonic rhythm  contour
 roughness ─────────────── shared ─────────────── chord dissonance (same Plomp-Levelt rule)
 attack  decay  movement  width                  density ───── shared ───── onsets per second
```

## Timbre axes

| # | Axis (low ↔ high) | Rule | Unit | Rests on |
|---|---|---|---|---|
| 1 | **loudness** quiet ↔ loud | 10·log10 of total band power per frame; MIDI: 40·log10(velocity/127) of sounding notes | dB, relative to the source's own reference | Timbre Toolbox energy envelope (Peeters 2011); velocity curve (Dannenberg 2006) |
| 2 | **brightness** dark ↔ bright | log2 of the power-weighted spectral centroid | octaves (log2 Hz) | spectral centroid, Peeters 2011; Grey 1977 timbre space |
| 3 | **sharpness** dull ↔ sharp | Zwicker weighted bark centroid: Σ N'(z)·g(z)·z / Σ N'(z), N' = (band power per bark)^0.23, g = 1 below 15.8 Bark, 0.066·e^(0.171 z) above | acum-proportional (0.11 · Bark) | Zwicker and Fastl 1999; DIN 45692 |
| 4 | **warmth** thin ↔ warm | share of power with band centre in 120-500 Hz | dB of total | low-mid emphasis studies of "warm" (Brookes and Williams 2007; Zacharakis 2014 luminance-texture-mass) |
| 5 | **weight** light ↔ heavy | share of power below 120 Hz | dB of total | Zacharakis 2014 "mass" axis |
| 6 | **fullness** narrow ↔ full | spectral spread: power-weighted std of log2 frequency around the centroid | octaves | spectral spread, Peeters 2011 |
| 7 | **noisiness** tonal ↔ noisy | spectral flatness (Wiener entropy): geometric / arithmetic mean of band power density | 0-1 | Dubnov 2004; Peeters 2011 |
| 8 | **roughness** smooth ↔ rough | Sethares sum over pairs of spectral peaks: min(a1,a2)·(e^(-3.5 s Δf) - e^(-5.75 s Δf)), s = 0.24 / (0.021 f_min + 19), peaks normalised to the loudest | relative (dissonance units) | Plomp and Levelt 1965; Sethares 1993; Vassilakis 2001 |
| 9 | **attack** soft ↔ hard onset | log10 of time from 20% to 90% of the peak amplitude envelope (one number per note or onset) | log10 s | log attack time, McAdams 1995; Peeters 2011 |
| 10 | **decay** short ↔ long | time from the peak until the level is 20 dB under it; censored at the window end (flagged) | s | decay / release in Peeters 2011 |
| 11 | **movement** static ↔ evolving | spectral flux: mean over audible bands of the absolute dB change frame to frame | dB per second | spectral flux / variation, Peeters 2011; McAdams 1995 "spectral flux" timbre axis |
| 12 | **width** mono ↔ wide | 1 - correlation of left and right | 0-2 | inter-channel coherence (Blauert 1997) |
| 21 | **vowel** (formants) closed ↔ open, back ↔ front | first two spectral envelope peaks F1, F2 from a smoothed (cepstral) envelope of each frame; not measured yet, needs audio or finer bands than 64 mel | Hz (two numbers) | Peterson and Barney 1952; Fant 1960 |
| 22 | **glide** pitch falling ↔ rising | pitch change over the note: semitones per second of the tracked fundamental (an 808 or kick drops, a riser climbs); not measured yet, needs a pitch tracker on audio | semitones per second | pitch envelope; YIN pitch tracking (de Cheveigné and Kawahara 2002) |
| 20 | **space** dry ↔ roomy | level of the tail after note release relative to the held level, in dB (reverb and delay leave a tail; a dry pluck drops to the floor); not measured yet, needs renders that record the release | dB | reverberance and clarity C50 / C80 (ISO 3382) |

## Music axes

| # | Axis (low ↔ high) | Rule | Unit | Rests on |
|---|---|---|---|---|
| 13 | **pitch height** low ↔ high | velocity-weighted mean of sounding MIDI pitches per frame; a synth row takes its note | semitones (MIDI number) | pitch height, Shepard 1982 |
| 14 | **register spread** close ↔ open | highest minus lowest sounding pitch per frame | semitones | voicing span (Huron 2001) |
| 15 | **roughness** (axis 8, music side) consonant ↔ dissonant | the same Sethares rule over the sounding notes, each with 6 harmonics at amplitude 0.88^k | relative, same units as 8 | Plomp and Levelt 1965; Sethares 1993 |
| 16 | **tonal tension** stable ↔ tense | (1 - r)/2, r = correlation of the frame's duration-weighted pitch-class profile with the phrase key's Krumhansl-Kessler profile (best of 24 keys over the whole phrase) | 0-1 | Krumhansl 1990; Lerdahl 2001 (tonal pitch space, simplified to key distance) |
| 17 | **harmonic rhythm** slow ↔ fast changes | pitch-class set changes per second (Jaccard similarity to the previous non-empty set < 0.5) | changes per second | harmonic rhythm (Piston 1941; Lerdahl and Jackendoff 1983) |
| 18 | **density** sparse ↔ dense | note onsets per second; audio: onsets per second from loudness rises > 6 dB | onsets per second | event density (Gabrielsson and Lindström 2010) |
| 19 | **contour** falling ↔ rising | slope of the top voice: semitones per second (linear fit over the phrase; the curve is the frame-to-frame change) | semitones per second | melodic contour (Dowling 1978; Huron 1996) |

## Measurement rules

| Rule | Value |
|---|---|
| Frame | audio and mel 62.5 ms (the stored mel grid: 48 frames over 3 s); MIDI 250 ms (chords need a longer window) |
| Summary per curve | mean, std, slope per second; curves are kept for every source that has time |
| Spectral axes 2-7 | computed from one form for every source: (band centre Hz, band power, band width Hz). Mel rows, 32-band rows and audio all pass through the same functions, so the numbers are comparable across sources (P4 checks this) |
| Silence | frames under -60 dB of the source's peak are dropped from the spectral axes; loudness reads them as peak - 60 dB, so each form's own noise floor never enters a mean |
| Repeat | the same input gives the same numbers bit for bit (no randomness); checked on every run |
| What is not here yet | perceived pitch of unlabelled audio (needs a pitch tracker; synth rows carry their note, samples carry it only in names), formants / vowel colour, rhythmic feel (groove rows hold swing and tightness already, `groove_2h.md`) |

## P2 result (2026-10-28): every held source measured

`characteristics.py rows | midi`, outputs in `~/Infinite/training-data/characteristics/`. Total run time about 1 minute; every run
re-measured its first 50 inputs and got identical numbers. `characteristics.py test` passes 13 rule checks on synthetic sounds
(saw brighter than its filtered copy, noise noisier and sharper, minor second rougher than a fifth in audio and in MIDI, C - F# - C
tenser in the middle, rising scale has positive contour, ...).

Coverage ("curve" = a curve over time; "1" = one number per row; % = share of rows that have it):

| source | rows | loud | bright | sharp | warm | weight | full | noisy | rough | attack | decay | movement | width | pitch | spread | tension | harm. rhythm | density | contour |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Diva v2 mel rows (`diva_m1`) | 4,935 | curve | curve | curve | curve | curve | curve | curve | - | 1 | 1 | curve | 1 (5%) | 1 | - | - | - | curve | - |
| Diva v1 band rows | 20,924 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | - | 1 | 1 (90%) | - | - | - | - | - | - | 1 | - |
| samples and loops (`features`) | 182,276 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | - | 1 (69%) | 1 (58%) | - | 1 | - | - | - | - | 1 | - |
| Air samples | 12,119 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | - | 1 (95%) | 1 (71%) | - | 1 | - | - | - | - | 1 | - |
| plug-in renders (`render_features`) | 10,765 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | - | 1 | 1 (94%) | - | 1 (97%) | 1 (15%) | - | - | - | 1 | - |
| MIDI, 50 files per dataset (21) | 906 | curve | - | - | - | - | - | - | curve | - | - | - | - | curve | curve | curve | curve | curve | curve |

Same render measured two ways (Diva v2 rows hold both a mel and 32 bands), correlation of the means over 4,935 notes:
brightness 0.96, sharpness 0.92, warmth 0.88, weight 0.87, fullness 0.88, noisiness 0.79, loudness 0.51.
On synthetic sounds wav and mel agree to 3 decimals; the band rows differ because they average power over the window before
the rule (a time-average spectrum weights loud frames more). Loudness is the weak one: mel is a relative level and the band rows a
power mean, so loudness is compared within a source until P4 calibrates the offset.

Gaps, and what closes each (no collection until P3 asks for it):

| Gap | Why | Closes with |
|---|---|---|
| roughness of every stored audio row | needs partials; mel and 32 bands are too coarse | storing a peak list per render (render_pass), or the wav when the drive is mounted (`characteristics.py audio`) |
| music axes of audio (pitch of samples, chords of loops) | no pitch tracker | pitch tracker on wavs; samples carry key only in names |
| curves for samples | `features.jsonl` stores averages only | re-measure from the WD-Black drive when mounted (not mounted 2026-10-28) |
| MIDI beyond 906 phrases | sampled on purpose | full corpus about 2.5 h at this speed, only if P4 needs it |
| drum-only MIDI (about 100 of 1,011 files) | music axes skip channel 10 | drum density from groove rows (`groove_2h.md`) |
