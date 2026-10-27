# Axes strategy: what to measure, how it is perceived, how words sit on top

Status 2026-10-27: draft for sign-off on sections 1-7. Nothing below is built. Supersedes the "five axes + seven words" cut in `dials.md` (that file keeps the creativity definition and the freedom dial, which stand). Sources are from memory and named so they can be checked; none is quoted.

## 1. Goal

One coordinate system, grounded in how people hear, that (a) describes any sound or song by numbers anyone can audit, (b) maps everyday words (spacious, glitchy, warm, tense...) onto regions of it, (c) maps each Infinite node and knob onto the moves it makes in it, so that "make it more spacious, a bit glitchy" becomes a search the generator can run, and (d) places the owner's own releases in it as a personal centre.

## 2. Problem

| Problem | Evidence here |
|---|---|
| The five Spotify-style axes are consumer proxies, not perceptual dimensions | W4 correlation with declared pillars: r = 0.14 / -0.01 / 0.32 / -0.43 / -0.01 |
| Valence is the hardest axis to get from audio alone | Emotion-from-audio studies (Yang & Chen era work, Eerola) report arousal predicted far better than valence; valence leans on harmony, lyrics, culture. A bar of r >= 0.5 for valence from MIDI formulas was optimistic |
| Words were invented, not mined | The seven dial words came from one conversation; the preset corpora carry thousands of real tag words (Characters, Serum, u-he) |
| Western bias | 12-bin chroma + Krumhansl key profiles, 4/4 groove grid, 12-TET. The owner's releases include Hindi tracks; raga, microtones, tala do not fit |
| "Creativity" has no listener | It was defined against the corpus average only, but surprise is relative to a listener's learned norms, which differ by genre and subculture |

## 3. Scope

In: a layered descriptor set, the word layer mined from data, node and knob fingerprints measured by rendering, the owner's album profile, a creativity measure relative to genre and to self. Out: building any new node, panel or UI; any retrain; any download of datasets or models without a yes (EMOPIA, DEAM, MTG-Jamendo would give human emotion labels and need approval).

## 4. Existing constraints

8 GB Mac, one heavy job at a time, resumable chunks; raw audio never leaves the machine, only rows; shippable scope untouched by library or synthetic rows; new nodes, modes and panels need approval; the audio pass is mid-run.

## 5. Hypothesis (what research says music is, and how it is heard)

Music is organised sound that is made, shared and used by people; each of these three pulls on the axes differently.

| Lens | Finding (source, from memory) | What it demands of the axes |
|---|---|---|
| **Physics and hearing** | Ear resolves frequency in critical bands (Bark / ERB); loudness is not level (equal-loudness contours, Fletcher-Munson, ISO 226; LUFS for programme loudness); roughness peaks when partials sit about a quarter of a critical band apart (Plomp & Levelt 1965; Sethares); sharpness and fluctuation strength are standard psychoacoustic quantities (Zwicker & Fastl) | Measure on perceptual scales (ERB bands, loudness, roughness), not raw FFT bins |
| **Timbre** | Timbre space is low dimensional: spectral centre (brightness), attack time, spectral flux / irregularity (Grey 1977; McAdams et al. 1995; Peeters et al. Timbre Toolbox). Semantic studies of timbre words find luminance, texture, mass (Zacharakis, Pastiadis, Reiss) | A timbre block of about 6 descriptors; the word layer should land on these |
| **Scene analysis** | Listeners group sound into streams by common onset, harmonicity, spatial position, similarity (Bregman 1990); everyday listening hears sources and actions (Gaver; Smalley's source-bonding) | Add spatial width / depth and a stream-count proxy (density), not just spectrum |
| **Structure** | Tonal hierarchy and key (Krumhansl); tension in harmony (Lerdahl); meter as nested periodicities; groove peaks at medium syncopation (Witek et al. 2014) | Use inverted-U style features: syncopation and complexity are not "more is more" |
| **Expectation and surprise** | Pleasure follows prediction and prediction error (Meyer 1956; Huron, Sweet Anticipation, ITPRA; Berlyne's arousal potential and Wundt curve; Gold et al. on liking peaking at intermediate surprise) | Creativity is novelty at intermediate-to-high surprise that is still understood; we already own predictive nodes whose error is a direct surprise measure |
| **Affect** | Two core dimensions, valence and arousal (Russell 1980), plus tension and power in music-specific models (GEMS, Zentner et al. 2008: wonder, transcendence, tenderness, nostalgia, peacefulness, power, joyful activation, tension, sadness); mechanisms in BRECVEMA (Juslin 2013: brain stem reflex, rhythmic entrainment, evaluative conditioning, contagion, imagery, episodic memory, expectancy) | Keep affect small (valence, arousal, tension, power) and label it as inferred, with an honest confidence |
| **Culture and subculture** | Genre is a social identity as much as a sound (Hebdige on style; Thornton on subcultural capital; Frith; Small, Musicking; DeNora on music as everyday technology); norms of "good" differ: techno prizes repetition and low complexity, jazz prizes harmonic surprise, noise prizes roughness | Sound coordinates are universal; what counts as typical or surprising is conditional on genre (and on the owner) |
| **How music is made** | Notation and performance, then recording and tape (Schaeffer's musique concrete: sound objects, typology of attack and mass), electronic synthesis, drum machines, sampling, the DAW and the bedroom producer; the studio as instrument (Eno; Moylan; Zagorski-Thomas) | Words about sound often name a production move (tape saturation, bitcrush, sidechain, reverb throw); effects are the vocabulary of character, which is why the word x node matrix is the right bridge |
| **Words for effects** | Semantic audio work (SAFE plugins, Stables et al.; SocialEQ, Cartwright & Pardo) maps descriptors like warm or bright to measured settings from user data | This is the same method as `character_matrix.py`, validated in the literature |

Hypothesis: a four-layer system, perceptual percepts under musical structure under affect under words, with genre-conditional priors, predicts the node chains people choose (W5 bar, 0.10 bits) better than the flat five axes and keeps each number auditable.

## 6. Baseline (recorded once, not re-measured before the final comparison)

| Item | Value now |
|---|---|
| W4 mood axes vs declared pillars | r = 0.14 / -0.01 / 0.32 / -0.43 / -0.01 (7-8 groups) |
| Word list | 7 dial words, invented |
| Character x node matrix | 11 words, 26,915 presets; declared-vs-data agreement: spacious 2/3, glitchy 2/5, dirty 1/3, moving 2/4 |
| Audio descriptors in `features.jsonl` | bands, rms, crest, centroid, rolloff, flatness, corr, low/mid/high, bpm, groove; v2 adds pitch, chroma/key, tempo grid, MFCC, harmonic ratio |
| Owner profile | none yet; `album_profile.py` measuring 162 tracks, results below when complete |

## 7. Success metrics (bars to sign off)

| # | Bar | How measured |
|---|---|---|
| S1 | Reliable: every percept descriptor repeats within 5% (rank correlation >= 0.95) after gain change, 128k AAC vs lossless, and a different 15 s excerpt of the same loop | test-retest script over 500 files |
| S2 | Informative: the percept layer predicts the top 30 mined tag words with held-out AUC >= 0.70 on at least 20 of them | logistic on percepts, corpus tags |
| S3 | Distinct: no two percept axes with |r| > 0.8; any pair above 0.6 is documented as one factor | correlation matrix on 100k rows |
| S4 | Useful: word + genre + section predict instrument cluster and node chain 0.10 bits better than the count baseline (the existing W5 bar) | held-out bits |
| S5 | Heard: for each of the 7 chosen words, owner pairwise listening (20 pairs per word, one blind session) agrees with the axis ordering at least 75% | owner session; no word ships below the bar |
| S6 | Honest on affect: valence and tension reported with held-out R^2 against human labels, no bar on valence from audio alone (arousal bar R^2 >= 0.5); labels need an approved dataset | only if the owner approves a label set |
| S7 | Culture-fair: raga, non-4/4 and microtonal clips from the owner's own work are not scored as out-of-key or unstable by the tonal and meter descriptors | owner albums as the test set |

## 8. Methodology (proposed; not yet signed off)

**The four layers**

| Layer | Question it answers | Descriptors (all from measured audio or MIDI) |
|---|---|---|
| L1 Percepts | what the ear receives | loudness (LUFS, short-term range), brightness (spectral centroid on ERB, sharpness), roughness / noisiness (roughness model, flatness), attack and percussiveness (log attack time, flux), spatial width (inter-channel correlation, side/mid), depth (reverb tail estimate), density (onsets per second, concurrent partials) |
| L2 Structure | what the music does | pulse (tempo, beat strength, syncopation index, swing), tonality (key clarity on a 36-bin pitch histogram, not only 12-bin), harmonic tension, melodic range and contour, repetition (self-similarity), form (section count) |
| L3 Affect | what it may feel like | arousal, valence, tension, power: small regressors from L1+L2 against human labels; reported with confidence; genre prior mixed in as before |
| L4 Words | what people call it | each word = a vector in L1-L3 learned from tags in the corpora (Characters, Serum tags, u-he character, Guitar Rig, Logic folders), kept only with support and distinctiveness; the shipped list is the words that pass S2 and S5, not a chosen seven |

**Node and knob fingerprints (the scalable part).** Render each node headless at a grid of knob settings on a fixed test set (noise, a chord, a drum loop), measure L1-L2 before and after, store the delta. A word becomes a target vector, a node becomes a set of moves, and the generator picks nodes whose moves land near the target. Field-written effects join the library by being measured, not described, so the system extends with the language.

**Creativity, second reference.** Surprise is measured relative to a genre-conditional model (Huron: expectation is learned by exposure), and a second time relative to the owner's own centre from the album profile: "novel to the world" and "novel to me" are different dials. Boden's three kinds map onto the freedom dial: combinational (mix known moves), exploratory (reach the tails of known distributions: the dial c), transformational (change the rules: a new Field node or tuning).

**Culture-fair measures.** 36-bin (third-of-semitone) pitch histogram next to 12-bin chroma; tempo and meter from onset periodicity without assuming 4/4; groove by inter-onset interval statistics, not a 16-step grid.

## 9. Proposed trajectories

| Option | Pros | Cons | What if it fails |
|---|---|---|---|
| A. Keep five axes, add words (current) | no work | weak evidence, Western, words invented | already r ~ 0 |
| B. Four layers + mined words + node fingerprints (proposed) | each number has a perceptual meaning; scales to any new node; matches the literature | most work; affect needs labels | L1-L2 and the word matrix still stand without L3 |
| C. Learned embedding (Colab) replaces hand axes | strong on tags | not auditable, needs a model download (owner's yes) | keep as a parked option; B is the interpretable layer on top |

Recommended: B, built in the order L1+L2 on existing rows, mined words, node fingerprints, then L3 only if labels are approved.

## 10. Benchmarking

Filled when work lands, against sections 6 and 7.

## 11. Critique

- Word mining is only as good as the tags; Logic and Guitar Rig tags are Western and instrument-heavy (Drum Machine, Electronic Drums dominate). The owner's albums are the counter-check.
- Fingerprints measured on three test signals may not transfer to real programme material; verify on 20 real clips.
- Affect from audio alone is weak; saying so is better than a high-looking number.
- A system built from other people's presets learns their taste; the personal centre is how it avoids becoming only that.

## 12. Next steps

1. Sign-off on sections 1-7 (this file).
2. Album profile (running) and its report: the owner's centre, S7 test clips.
3. Percept and structure descriptors on existing rows; S1 and S3.
4. Word mining with support and distinctiveness; S2.
5. Node fingerprints by headless render (a heavy slot).
6. Owner listening session for S5.
7. Decision on a label set for L3 (a download: needs a yes).
