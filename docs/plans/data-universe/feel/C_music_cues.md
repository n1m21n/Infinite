# C. Music cues -> V / E / T with numbers (research, 2026-10-28)

Read-only research. Replaces the direction-only tables in `../FEEL-MAP.md`. Plain ASCII; no Mermaid.
V = valence, E = energy arousal, T = tension arousal. "Our axes" = the 22 in `A_node_map.md` section 1.1:
1 loudness, 2 brightness, 3 sharpness, 4 warmth, 5 weight, 6 fullness, 7 noisiness, 8 roughness (audio), 9 attack, 10 decay, 11 movement,
12 width, 13 pitch height, 14 register spread, 15 roughness (music), 16 tonal tension, 17 harmonic rhythm, 18 density, 19 contour,
20 space, 21 vowel, 22 glide. Where a cue has no axis it says "NONE" (that is a gap in our axes, not in the research).

## 0. Grades (stricter than FEEL-MAP)

| Grade | Meaning |
|---|---|
| A | I opened the paper/PDF/page this session and the number is copied from it |
| B* | a search summary of the abstract/paper was returned this session, paper NOT opened; number comes from that summary |
| B | widely reported; named from memory, not seen this session (verify before it becomes a rule) |
| C | my inference; no study behind it |

Important caveat on every "A" betas row: the studies rate EMOTION WORDS (happy/sad/peaceful/scary, or valence/activity/tension on film music),
not our exact V/E/T. Mapping happy=V+E+, sad=V-E-, peaceful=V+E-T-, scary=V-T+ is grade C.

## 1. The ten strongest numeric findings (summary)

1. Cue importance (partial correlation sr^2, median over 4 emotions, 46 listeners, 6 cues factorial): mode .29 > tempo .14 > register .08 > dynamics .04 > articulation .02 > timbre .01. Linear terms explain 77-89% of rating variance (R2adj .77-.89); quadratic terms add 0-8%; no cue x cue interaction survives correction. (Eerola, Friberg, Bresin 2013; A)
2. Mode on "happy": beta +1.77 (sr^2 .48); on "sad": beta -1.60 (sr^2 .54). Tempo on "sad": beta -.32 (sr^2 .21); on "happy": +.25 (sr^2 .12). (same; A)
3. From audio features, 5-fold CV: PLS model predicts activity R2 .85, tension .79, valence .72 (film soundtracks, N=110). Valence/tension correlate r = -.83 in that set; valence/activity r = -.08. (Eerola, Lartillot, Toiviainen ISMIR 2009; A)
4. Pitch height dominates valence in isolation: mean max-min swing in valence (0-1 scale) = .45 across C1-C7, only .103 inside C3-C7; mode (major minus lowest minor) .14; dynamics .028. Major at C1 is negative; minor above C5 can be positive. (Hong, Mo, Horner 2018; A)
5. Performance macro-rules that listeners decoded: anger = notes 15% shorter IOI, +8 dB (91% correct); happy = 20% shorter IOI, +3 dB (97%/63% on two pieces); sad = 30% longer IOI, -6 dB (69%/59%); tender is the weak one (53%/25%). (Bresin & Friberg 2000; A)
6. Tempo beats mode for happy-vs-sad: Emotion x Tempo F(1,48)=227.8, partial eta^2 .83 vs Emotion x Mode F=77.5, eta^2 .62; slow = 41-87 bpm, fast = exactly 2x (82-174 bpm). Conflicting cues (fast-minor, slow-major) lower both happy and sad and raise mixed ratings. (Hunter, Schellenberg, Schimmack 2010; A)
7. Groove is an inverted U in syncopation: quadratic fit R2 .35 (want to move) and .43 (pleasure); r(move, pleasure) = .964; syncopation index 0-81 at 120 bpm; entropy and onset count do not predict. (Witek et al. 2014; A)
8. Real major-vs-minor repertoire: major pieces have 29% higher attack rate (5.86 vs 4.56 notes/s) and ~1.8 semitones higher mean pitch (Bach WTC + Chopin preludes); but Chopin preludes show 0% attack-rate difference. Do not hard-couple mode to tempo. (Poon & Schutz 2015; A)
9. Learnability ceiling: EMOPIA piano clips, 4-class accuracy .68 (LSTM-Attn symbolic) / .68 (audio ResNet); arousal-only .82-.92, valence-only .56-.83. Arousal is solved, valence is not. (EMOPIA; A)
10. STILL UNKNOWN (no number found): chord-progression feel (I-V-vi-IV etc.), harmonic rhythm, modulation, 3/4 vs 6/8 vs 5/4, drum-pattern types, rest proportion, attack time in ms, vibrato rate/depth, tonal-tension-model correlations, modes beyond major/minor in effect-size units. See section 9.

## 2. Tempo, dynamics, articulation, attack, vibrato

### 2.1 Cue levels the A studies used (so betas can be converted to our units)

| Study | Tempo | Dynamics | Articulation | Register | Mode | Source |
|---|---|---|---|---|---|---|
| Eerola 2013 | 5 levels: 1.2, 2, 2.8, 4.4, 6 notes/s (NOT bpm) | 5 levels: -10, -5, 0, +5, +10 dB | 4 levels: note length / IOI = 1, .75, .5, .25 | 6 levels: mean pitch F3,B3,F4,B4,F5,B5 (MIDI 53..83, 6 semitones apart) | Ionian vs Aeolian | A |
| Hunter 2010 | slow 41-87 bpm vs fast 82-174 bpm (8 Bach inventions, factor 2) | not varied | not varied | not varied | major vs minor | A |
| Bresin & Friberg 2000 | IOI -20% .. +80% | -6 .. +8 dB | duration-contrast "airy" or legato | not varied | not varied | A |

### 2.2 Standardised betas (Eerola 2013 Table 1; A)  [R2adj: scary .85, happy .89, sad .89, peaceful .77]

| Cue (up) | scary beta (sr2) | happy beta (sr2) | sad beta (sr2) | peaceful beta (sr2) | Our axes |
|---|---|---|---|---|---|
| Mode: minor->major | -.74 (.08) | +1.77 (.48) | -1.60 (.54) | +.43 (.05) | 16 tonal tension (weak proxy; no axis for mode itself, see sec 4) |
| Tempo up | +.07 (.01) | +.25 (.12) | -.32 (.21) | -.27 (.15) | 18 density, 11 movement, 17 harmonic rhythm |
| Register up | -.23 (.15) | +.18 (.09) | -.05 (.01) | +.15 (.06) | 13 pitch height |
| Dynamics up | +.20 (.08) | -.01 (.00) | -.05 (.01) | -.28 (.14) | 1 loudness |
| Articulation legato->staccato (the paper's coding is length ratio 1->.25; sign shown = more legato) | -.03 (.00) | +.14 (.02) | -.18 (.04) | -.10 (.01) | 10 decay, 9 attack |
| Timbre (flute->horn->trumpet; brighter) | +.15 (.02) | +.01 (.00) | -.14 (.01) | -.45 (.13) | 2 brightness, 3 sharpness |
| Music structure 3 (the "scary" excerpt) | +1.56 (.33) | -.53 (.02) | -.49 (.04) | -.99 (.12) | NONE (composition-level; not a single axis) |
| Quadratic terms add | +3% | 0% | ~0% | +8% | (curved cues: timbre, dynamics, register) |

Sign caution: the extraction I read lists articulation betas without stating direction of coding; treat articulation as "very small" only. (A for sizes, C for sign.)

### 2.3 Cue by cue, with every number found

| Cue | Measured effect | V | E | T | Our axes | Source | Grade |
|---|---|---|---|---|---|---|---|
| Tempo, sad vs happy | tempo beta -.32 (sad), +.25 (happy); sr2 .21 / .12 | small + | strong + | + (weak, scary beta +.07) | 18, 11 | Eerola 2013 | A |
| Tempo vs mode strength | tempo partial eta^2 .83 vs mode .62 (happy-sad separation) | - | - | - | 18 vs 16 | Hunter 2010 | A |
| Tempo vs mode (earlier) | both matter alone; tempo more salient even after salience equalised | - | - | - | - | Gagnon & Peretz 2003 | B* |
| Tempo (notes/s) in real repertoire | major mean 5.86 notes/s, minor 4.56 (+29%); Bach preludes +55%, fugues +43%, Chopin preludes 0% | - | - | - | 18 | Poon & Schutz 2015 | A |
| Tempo +20% faster / +30% slower IOI | happy -20% IOI, anger -15%, sad/tender/solemn +30%, fear +80% (and irregular) | - | - | - | 18, 11 | Bresin & Friberg 2000 | A |
| Tempo up -> tension | fast music rated more tense; fast speech less pleasant (3-dim ratings, 64 music + 64 speech excerpts) | - (music) | ++ | + | 18, 1 | Ilie & Thompson 2006 | B* |
| Loudness vs tempo for arousal | in continuous ratings, loudness changes dominate arousal; tempo second; models explained 33-73% of variance per piece | weak | ++ | ++ | 1, 18 | Schubert 2004 | B* |
| Loudness -> V/E/T | loud = more pleasant, energetic, tense (both music and speech) | + (ratings) | ++ | ++ | 1 | Ilie & Thompson 2006 | B* |
| Dynamics level | +-10 dB; sr2 median .04; peaceful beta -.28, scary +.20 | 0 | + | + | 1 | Eerola 2013 | A |
| Dynamics effect on valence in piano scales/chords | forte minus piano: +.017 (chords, significant), +.011 (combined, n.s.), overall swing .028 of 0-1 | ~0 | - | - | 1 | Hong 2018 | A |
| Sound level as emotion rule | anger +8 dB, happy +3, solemn +3; sad/fear/tender -6 | - | - | - | 1 | Bresin & Friberg 2000 | A |
| Articulation | sr2 median .02; legato lowers happy slightly; length ratio 1 -> .25 | ~0 | small + (staccato) | small + | 10, 9 | Eerola 2013 | A |
| Staccato ratio, performance cue profile | happy = staccato, sad = legato, tender = legato, anger = staccato/non-legato, fear = staccato | - | - | - | 10 | Juslin cue table as reproduced by Mina 2022 (secondary) | A(secondary), original B |
| Attack time (ms) | NO ms values retrieved. Qualitative: happy fast attack, anger abrupt, sad/tender slow | + fast | + fast | + abrupt | 9 | Juslin table via Mina; Eerola, Ferrer, Alluri 2012 (attack slope is one of 3 predictors of V/E/T) | B |
| Timbre predictors (attack slope, high/low energy ratio, spectral flux) | 110 isolated instrument sounds rated on V, E, T; these three descriptors carried the affect dims (R2 per FEEL-MAP: 50-57%, NOT re-checked) | - | - | - | 9, 2, 11 | Eerola, Ferrer, Alluri 2012 (Music Perception 30:49) | B* |
| Vibrato | only qualitative: irregular vibrato = fear; NO rate (Hz) or depth (cents) retrieved. From memory typical expressive vibrato is ~5-7 Hz, extent tens of cents | - | + | + | 11 | Juslin table via Mina | B (rate/extent: C) |
| Tempo variability | happy = small tempo variability; fear = large time deviations, final acceleration | - | - | + | 11 | Bresin & Friberg 2000 table | A |
| Pauses / rests | fear profile has pauses between phrases; no proportion measured | - | - | + | 18 (inverse) | Juslin table via Mina | B |

### 2.4 Recognition accuracy for the rule-based performances (Bresin & Friberg 2000; A) - what a pure tempo+dB+articulation recipe can and cannot say

| Intended | Correct on piece 1 (Ekorrn) | Correct on piece 2 (Mazurka, minor) | Main confusion |
|---|---|---|---|
| Anger | 91% | 91% | - |
| Happiness | 97% | 63% | 28% called anger on the minor piece |
| Sadness | 69% | 59% | tender 28-38% |
| Solemnity | 72% | 59% | - |
| Tenderness | 53% | 25% | sad 28-50% |
| Fear | 44% | 47% | tender 31-41% |
| Chance | 14% (7 categories) | | |

Reading: tempo + level + articulation separate high-E from low-E reliably, but sad/tender/fear need mode, register or harmony. PCA of the 7 setups: factor 1 = 61% of variance (sound level + tempo), factor 2 = 29%.

## 3. Pitch

| Cue | Effect | V | E | T | Our axes | Source | Grade |
|---|---|---|---|---|---|---|---|
| Register (octaves) in a factorial | 6 levels F3-B5; happy +.18 (sr2 .09), scary -.23 (sr2 .15), peaceful +.15, sad -.05 | + | + (happy) | - (scary) | 13 | Eerola 2013 | A |
| Absolute pitch height, scales and chords | valence swing .45 over C1-C7; .103 over C3-C7; major 50% inflection about C2; minor scales about C3; minor chords: melodic minor ~C3, harmonic ~C4, natural ~C5 | ++ below C3, then flat | - | - | 13 | Hong, Mo, Horner 2018 | A |
| Average pitch in cadences | medium positive effect on valence, only cue with real arousal effect | + | + | - | 13 | Smit et al. 2020 (Music & Science) | B* |
| Pitch height, 3-dim ratings | high-pitched speech and LOW-pitched music got higher valence (opposite to Hong/Eerola/Smit) | conflict | - | - | 13 | Ilie & Thompson 2006 | B* |
| Pitch vs mode in real music | major corpus mean ~1.8 semitones higher (43.6 vs 41.8 on 88-key index); Bach fugues +2.5, Chopin preludes +3.2, Bach preludes 0 | - | - | - | 13 | Poon & Schutz 2015 | A |
| Minor themes | 9,788 instrumental themes: minor-key themes sit lower and use slightly smaller intervals (weak but significant); no effect sizes retrieved | - | - | - | 13, 19 | Huron 2008 | B* |
| Lowering scale degrees 3 and 6 | near-optimal way to shrink average melodic interval size from major | - | - | - | 19 | Huron & Davis 2012 | B* |
| Interval size and valence | positive correlation between mean interval / range and valence in Baroque + modern music, and in vocal melodies; repeated notes and note density correlate with arousal | + | + (density) | - | 19, 14, 18 | Frank 2020 thesis (Stellenbosch) | B* |
| Rising vs falling contour | Hevner: "not clear-cut"; Schubert 2004: only a trend for ascending = happier; 2025 pure-tone intervals: ascending happier, larger for big intervals | + small | ? | ? | 19 | Hevner 1936, Schubert 2004, 2025 study | B* |
| Pitch range (register spread) | NO measured effect size found. FEEL-MAP row "wide = + + +" stays B/C | ? | ? | ? | 14 | none | C |
| Step vs leap, melodic complexity, pitch variability | NO numbers found for emotion (only Huron interval size above; melodic-complexity work exists, e.g. Eerola on expectancy models, not opened) | ? | ? | ? | 19, 14 | none | C |

## 4. Harmony

### 4.1 Modes and scales

| Item | Effect | V | E | T | Our axes | Source | Grade |
|---|---|---|---|---|---|---|---|
| Major vs minor (Ionian vs Aeolian) | happy +1.77 / sad -1.60 (standardised); highest-ranked cue | ++ | small | small | 16 (no mode axis exists) | Eerola 2013 | A |
| Major vs minor family in piano scales/chords | valence (0-1): Major - Melodic minor .095; Major - Natural minor .123; Major - Harmonic minor .125; Melodic - Natural .043; Melodic - Harmonic .035; Natural - Harmonic .030. Scales: harmonic minor most negative; chords: natural minor most negative | ++ | - | - | 16 | Hong 2018 | A |
| 7 diatonic modes (same tonic C), happiness | Ionian happiest, happiness falls as scale degrees are flattened; Lydian is the exception (less happy than Ionian). Order from memory: Ionian > Lydian > Mixolydian > Dorian > Aeolian > Phrygian > Locrian. Familiarity explains most but not all | order | - | - | 16 | Temperley & Tan 2013 (Music Perception) | B* (order: B) |
| Major-third modes vs minor-third modes | Ionian/Lydian/Mixolydian rated happier and less tense than Dorian/Aeolian/Phrygian/Locrian | + | - | - | 16 | Bostwick et al. 2018 | B* |
| Pentatonic, blues, whole-tone, chromatic, harmonic minor beyond Hong | NO emotion effect sizes found. C guess: pentatonic = low roughness / V+; whole-tone = ambiguous tonic, T+ low, "floating"; chromatic = T+; blues = V- with E+ | ? | ? | ? | 15, 16 | none | C |
| Key clarity / majorness as audio features | in Anger and Tenderness MLR models: key clarity beta -.07 (anger) / +.08 (tender); majorness -.08 (tender) - tiny | ~0 | - | - | 16 | Eerola 2009 | A |
| Minor key in real repertoire | slower attack rate (-29%) and lower pitch (~1.8 semitones) than major, with exceptions (Chopin) | - | - | - | 18, 13 | Poon & Schutz 2015 | A |

### 4.2 Chord qualities

| Chord / feature | Finding (what is known) | V | E | T | Our axes | Source | Grade |
|---|---|---|---|---|---|---|---|
| 14 chords: major/minor/dim/aug triads, dom7, maj7, min7, inversions, piano vs strings | all differ significantly on emotion ratings; inversion and timbre also matter. No table values retrieved | ? | ? | ? | 15, 16, 2 | Lahdelma & Eerola 2016 (Psychology of Music 44) | B* |
| 15 piano chords over 2 octaves, N=410 | perceived dissonance strongly tied to tension; mildly dissonant min9, maj9, min7 preferred over plain triads regardless of musical sophistication | - for strong dissonance | ? | ++ with dissonance | 15, 16 | Lahdelma & Eerola 2016 (i-Perception) | B* |
| Major vs minor in progression | major chords more pleasant, less tense; difference vanishes when the progression ends unstably | + | ? | - | 16 | Acta Psychologica 2025 (title not captured) | B* |
| Fast major sequences vs minor (N=20) | major: faster heart rate, "vitality"; minor: "unease" | + / - | + | ? | 16, 18 | Affective experiences to chords ... meter, tempo, entrainment (2020) | B* |
| Dim, aug, sus2/sus4, 9ths, add9 | NO numbers retrieved beyond "mild dissonance liked" above. C: dim = T high V low; aug = T medium, unresolved; sus = suspended T low-medium, E flat | ? | ? | ? | 15, 16 | none | C |
| Inversions | exist as an effect in Lahdelma & Eerola 2016, size not retrieved | ? | ? | ? | 14, 16 | same | B* |

### 4.3 Cadences, progressions, harmonic rhythm, modulation

| Item | Finding | V | E | T | Our axes | Source | Grade |
|---|---|---|---|---|---|---|---|
| Authentic / plagal / half / deceptive cadences | arousal effects small except pitch height; major cadence valence "substantially higher" than minor; plagal, adding a 7th (tetrad), and ending on a minor chord each give weak NEGATIVE valence. Exact standardised betas not retrieved | major ++ | ~0 | ? | 16, 17 | Smit, Dobrowohl, Schaal, Milne, Herff 2020 (Music & Science) | B* |
| Unexpected chords | arousal rises with prediction error and uncertainty of chord progressions | ? | + | + | 16 | arXiv 2024 chord-progression study (title not captured; arXiv 2410.11885 is the nearby "body maps" paper) | B* |
| I-V-vi-IV, vi-IV-I-V, i-VI-III-VII, I-vi-IV-V, ii-V-I, i-iv-v, I-IV-V | NO listener-rating study with numbers found. C only: I-V-vi-IV = bright-bittersweet loop (V+ with minor-vi lift), i-VI-III-VII = epic/modal-minor loop, ii-V-I = resolved jazz, tension then release, I-IV-V = plain stable | C | C | C | 16, 17 | none | C |
| Harmonic rhythm (chords per bar) | NO study retrieved. C: faster chord change -> more E and T, slower -> calm; bounded by tempo | ? | + | + | 17 | none | C |
| Key change / modulation | NO study retrieved. C: up a semitone/tone = lift (E+, V+), to a distant key = T+ | ? | ? | ? | 16 | none | C |

### 4.4 Roughness and tension models

| Item | Numbers | Our axes | Source | Grade |
|---|---|---|---|---|
| Plomp-Levelt | maximum roughness for two sine tones about 25% of a critical bandwidth apart (35 Hz at 1 kHz); Zwicker & Fastl put the peak nearer 1/2 critical band (70 Hz at 1 kHz); roughness ~0 beyond one critical band | 8, 15 | auditory.org thread 1999 summarising both papers | B* |
| Parncutt closed form | R = [e * (b/0.25) * exp(-b/0.25)]^2 with b = separation in Bark, R = 0 for b > 1.2 (as printed in the source; the "e *" form is my reading of the garbled text) | 8, 15 | scitepress 2007 paper | B* |
| Sethares constants | dstar .24, s1 .0207, s2 18.96, c1 5, c2 -5, a1 -3.51, a2 -5.75 (Python port); newer version uses min of the two partial amplitudes | 8, 15 | gist 3066664 | B* |
| Roughness as an audio feature for emotion | roughness beta only +.05 in the Anger MLR model (standardised) - small once other features are in | 8 | Eerola 2009 | A |
| Roughness vs tension | dissonance <-> tension strongly correlated in 15-chord study (r not retrieved) | 15, 16 | Lahdelma & Eerola 2016 | B* |
| Spiral Array tension (Chew; Herremans & Chew 2016) | three measures: cloud diameter (spread), cloud momentum (movement), tensile strain (distance from global key). Validated against Farbood 2012 perceived tension, correlations NOT retrieved | 16 | Herremans & Chew preprint | B* |
| Lerdahl tonal pitch space | tension = distances in pitch space; a 2020 model beat Lerdahl and MorpheuS on 12 chord progressions, no numbers retrieved | 16 | PMC7712964 | B* |

## 5. Rhythm

| Cue | Finding with numbers | V | E | T | Our axes | Source | Grade |
|---|---|---|---|---|---|---|---|
| Syncopation | 50 synthesised funk drum breaks, 120 bpm, index 0-81; quadratic fit R2 = .347 (want to move) and .427 (pleasure); N=66; medium > low > high for moving, medium > (low = high) for pleasure; main effect of syncopation F(1.62,79.2)=15.7; dancers rate higher (F=13.5) | peak at medium | peak at medium | ? | 18, 11 | Witek et al. 2014 | A |
| Syncopation vs onsets / entropy | r(syncopation, total onsets) = .09; r(syncopation, joint audio entropy) = .26; entropy predicts nothing quadratic | - | - | - | 18 | Witek 2014 | A |
| Note density | low-arousal EMOPIA quadrants have longer notes and fewer per beat; Q1/Q2 (high arousal) higher density; used as top discriminative feature, with velocity | - | ++ | + | 18 | EMOPIA (Hung et al. 2021) | A |
| Swing ratio | not fixed 2:1: ratio falls roughly linearly with tempo, 2:1 only around 200 quarter-note bpm; recordings 110-330 bpm; short note stays ~100 ms absolute; listeners prefer larger swing at slow tempo, ~none at fast | ? | ? | ? | NONE (nearest 11 movement) | Friberg & Sundstrom 2002 (via Honing & de Haas 2008 summary) | B* |
| Microtiming | drum pattern shifted by -25, -15, 0, +15, +25 ms (93 music students): large deviations lower groove ratings; fully quantised got the highest; no effect sizes retrieved | ? | groove - | ? | NONE | Frühauf, Kopiez, Platz 2013 (via Frontiers 2016 account) | B* |
| Timing patterns vs tempo/dynamics | timing patterns communicate emotion less effectively than tempo and dynamics | - | - | - | NONE | Juslin & Madison 1999 | B* |
| Meter (3/4, 6/8, 5/4, 7/8) | NO effect sizes found. The 2020 chord study (N=20) varied isochronous / metrical / random sequences: random metre raised respiration rate | ? | ? | ? | NONE (18 only) | Affective experiences to chords 2020 | B* |
| Polyrhythm | NO emotion numbers found | ? | ? | ? | NONE | - | C |
| Four-on-floor, breakbeat, half-time, trap hats, fills | NO listener numbers found. C: four-on-floor = high E, low T when steady; half-time = heavy, E- at same tempo; trap hat rolls = density spikes -> E+ T+; fills = brief T+ | C | C | C | 18, 11 | none | C |
| Silence / rest proportion | NO numbers; fear profile carries pauses; EMOPIA shows low-arousal = longer notes | - | - | + | 18 (inverse), 20 | Juslin via Mina; EMOPIA | B / A |

## 6. Interactions (what adds, what does not)

| Pair | Evidence | Verdict | Source | Grade |
|---|---|---|---|---|
| All 6 primary cues together | no significant 2-way interaction after multiplicity correction; linear R2 .77-.89, quadratic +0 to +8% | ADDITIVE as first model | Eerola 2013 | A |
| Mode x tempo (happy, sad) and register x mode | significant only WITHOUT correction | possible small interaction; test it | Eerola 2013 | A |
| Mode x tempo, conflicting cues | fast-minor and slow-major lower both happy and sad ratings versus consistent cues, and raise mixed happy/sad perception (mixed perceptions stronger than mixed feelings) | NOT additive at the conflict corner | Hunter 2010 | A |
| Pitch x mode | Major is negative at C1; minor scales positive above ~C5-C7; "contrary examples" (major-negative or minor-positive) are most likely where pitch is extreme | interaction at extremes only; flat in C3-C7 | Hong 2018 | A |
| Mode x tempo in repertoire | the mode-tempo link is not a law (Chopin 0% attack-rate gap) | generator must not couple them | Poon & Schutz 2015 | A |
| Loudness x rate x pitch | the authors report interactive effects among intensity, rate, pitch height; sizes not retrieved | some interaction | Ilie & Thompson 2006 | B* |
| Register x loudness | no study found; the Eerola dynamics beta flips by emotion (scary +.20, peaceful -.28), which says loudness acts on the emotion, not on V directly | C | Eerola 2013 | A for beta, C for reading |
| Tension: harmony x register x dynamics | no study retrieved | unknown | - | C |

Rule of thumb that the data supports: score V/E/T as a sum of cue terms; add one penalty term for conflicting mode-tempo (fast-minor, slow-major) and one for pitch extremes; no other interaction has numbers.

## 7. Labelled datasets we could use

| Dataset | Size | Labels | Domain | License / URL | Source | Grade |
|---|---|---|---|---|---|---|
| EMOPIA | 1,087 clips from 387 songs (Q1 250, Q2 265, Q3 253, Q4 310; 32-41 s each); 4 annotators | 4 quadrants of valence x arousal (HVHA, LVHA, LVLA, HVLA) | pop piano covers, audio + MIDI (transcribed) | paper CC BY 4.0; data on Zenodo 5090631; audio via YouTube links only; repo github.com/annahung31/EMOPIA | arXiv 2108.01374 | A |
| MTG-Jamendo | >55,000 full tracks; 195 tags; mood/theme subset 59 tags (56 in splits) | multi-label mood/theme tags (no V/A numbers) | full-length audio; raw_30s 508 GB, mood subset 152 GB | code Apache 2.0, metadata CC BY-NC-SA 4.0, audio various CC, non-commercial research only; github.com/MTG/mtg-jamendo-dataset | GitHub README | A |
| Soundtrack110 / film set | 360 expert-picked excerpts, 110 rated | valence, activity, tension (3-D) + 5 basic emotions; Cronbach alpha > .99 | film soundtracks | published with Eerola & Vuoskoski 2011; licence not checked | ISMIR 2009 paper | A (size), B (access) |
| DEAM | 1,802 songs and excerpts | valence + arousal, dynamic at 2 Hz and static | Creative Commons audio | cvml.unige.ch/databases/DEAM (server refused connection this session) | search summary | B* |
| 1000 songs (DEAM predecessor) | 744 after dedup (619 dev / 125 eval), >= 10 raters each | valence + arousal | Free Music Archive CC | cvml.unige.ch/databases/emoMusic | search summary | B* |
| PMEmo | 794 chorus clips (767 with static labels), 457 annotators | V/A 1-9 every 0.5 s, plus EDA (7,962 signals at 50 Hz) | Billboard / iTunes / UK charts choruses | next.zju.edu.cn/publications/pmemo-dataset; licence not retrieved | search summary | B* |
| VGMIDI | 200 labelled piano pieces + 3,850 unlabelled (older papers: 95 labelled of 823); ~30 raters per labelled piece | valence-arousal pair | video-game piano MIDI | github (lucasnfe / bardo-composer); licence not retrieved | Ferreira homepage, TIB | B* |
| Music4All-Onion | 109,269 tracks, 119,140 listeners (Last.fm) | 26 audio/video/metadata feature sets, tags; no V/A annotation found in the summary | recommender resource | zenodo 6609677 | CIKM 2022 | B* |
| MuSe | 90,408 songs | valence, arousal, dominance from Last.fm tags | tag-derived, noisy | zenodo 4281165 | search summary | B* |
| AMG1608 | from memory: 1,608 30-s clips, ~665 annotators (AllMusic), V/A | V/A | pop | not verified | - | B (memory) |

Use: EMOPIA is the only one with symbolic data and quadrant labels fitting our generator; DEAM/PMEmo give continuous V/A on audio (needs our audio axes, not MIDI). Data downloads need a yes (axes-strategy.md section 3).

## 8. Models that predict V/A and how well

| Model | Input | Result | Source | Grade |
|---|---|---|---|---|
| Linear / PLS regression on MIRtoolbox features (29 predictors, Box-Cox) | audio, film soundtracks N=110 | 5-fold CV R2: MLR valence .64 activity .75 tension .67; PLS valence .70 .77 .71; PLS+Box-Cox valence .72 activity .85 tension .79. Basic emotions R2 .58-.74 (angry .70, scary .74, happy .68, sad .69, tender .58) | Eerola, Lartillot, Toiviainen ISMIR 2009 | A |
| Features that matter in that study | RMS variance, fluctuation peaks, key clarity, majorness, roughness, spectral centroid variance, tonal novelty | tenderness: RMS variance beta -.44 (largest); anger: fluctuation peaks -.14 | same | A |
| SVR regression | audio, own set | R2 arousal 58.3%, valence 28.1% | Yang, Lin, Su, Chen 2008 | B* |
| Later feature selection on the same set | audio | R2 arousal 63%, valence 35.6% | follow-up cited in search | B* |
| Continuous-time system identification | per-song time series | mean R2 .777 valence, .751 arousal (within-song, so not comparable) | Korhonen & Clausi 2004 | B* |
| Time-series regression on differenced features | loudness, tempo, contour, texture, centroid; 67 listeners, 4 pieces | 33-73% of variance per piece; arousal from loudness (dominant) and tempo; valence from contour (inconclusive) | Schubert 2004 | B* |
| EMOPIA baselines | symbolic: logistic reg. on note density, length, velocity, key one-hot; LSTM-Attn + MIDI-like / REMI tokens; audio: logistic reg., short-chunk ResNet | 4-class / arousal / valence: symbolic LR .581/.849/.651; LSTM+MIDI-like .684/.882/.833; LSTM+REMI .615/.890/.746; audio LR .523/.919/.558; ResNet .677/.887/.704 | EMOPIA paper | A |
| Gaussian mixture on intensity/timbre/rhythm | 800 classical clips, 4 Thayer quadrants | accuracy not retrieved | Lu, Liu, Zhang 2006 | B* |
| Tension models (Spiral Array, Lerdahl) | symbolic | no correlation values retrieved | Herremans & Chew 2016; Farbood 2012 | B* |

Take-aways: symbolic features reach valence .83 accuracy (binary) on EMOPIA only with a sequence model; hand features give .65. Arousal ~.85-.92 from almost anything. Valence-from-audio linear ceiling ~R2 .3-.7 depending on set.

## 9. What is still unknown (gaps to fill by our own measurement, C5)

| Gap | Why it matters | Cheapest way to fill |
|---|---|---|
| Effect sizes for chord progressions (I-V-vi-IV etc.), harmonic rhythm, modulation | axes 16, 17 have no weights | render progressions through our generator, score with EMOPIA-trained classifier, check against Smit 2020 direction |
| Modes beyond major/minor in standardised units | pentatonic, blues, Dorian etc. used heavily | Hong-style listening test or classifier on rendered scales |
| Attack time in ms, vibrato rate/depth | axes 9, 11 | read Eerola, Ferrer, Alluri 2012 tables (jyx link 404 here) |
| Meter, polyrhythm, drum patterns, rest proportion | no axis for meter | outside the 22 axes; decide whether to add |
| Swing and microtiming | no axis | same |
| Tension-model correlations (Spiral Array, Lerdahl) | axis 16 definition | open Herremans & Chew 2016 and Farbood 2012 |
| Pitch-height sign conflict | Hong/Eerola/Smit: higher = more positive; Ilie & Thompson: lower-pitched music more positive | check their manipulation range (octave shift) |
| Whether Eerola's emotion betas transfer to V/E/T | 4 emotion words, not 3 dims | Eerola, Ferrer, Alluri 2012 used V/E/T on timbre only |

## 10. Sources (URLs)

| Short name | URL | Opened? |
|---|---|---|
| Eerola, Friberg, Bresin 2013 | https://www.frontiersin.org/articles/10.3389/fpsyg.2013.00487/full | yes |
| Eerola, Lartillot, Toiviainen ISMIR 2009 | http://ismir2009.ismir.net/proceedings/PS4-8.pdf | yes (text extracted) |
| Hong, Mo, Horner 2018 | https://research.uca.ac.uk/7016/1/2018%20-%20The%20Effects%20of%20Mode%2C%20Pitch%2C%20and%20Dynamics%20on%20Valence%20in%20Piano%20Scales%20and%20Chord%20Progressions.pdf | yes |
| Witek et al. 2014 | https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0094446 | yes |
| Bresin & Friberg 2000 | https://continuum-hypothesis.com/music/FULLTEXT01.pdf | yes |
| Hunter, Schellenberg, Schimmack 2010 | https://sites.utm.utoronto.ca/sites/sites.utm.utoronto.ca.glenn_website/files/download/HunterEtAl2010.pdf | yes |
| Poon & Schutz 2015 | https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2015.01419/full | yes |
| EMOPIA | https://arxiv.org/abs/2108.01374 | yes |
| MTG-Jamendo | https://github.com/MTG/mtg-jamendo-dataset | yes |
| Mina 2022 (Juslin cue table, secondary) | https://arar.sci.am/Content/352949/179.pdf | yes |
| Temperley & Tan 2013 | https://labs.sonicfield.org/library/emotional-connotations-of-diatonic-modes | summary only |
| Ilie & Thompson 2006 | https://researchers.mq.edu.au/en/publications/a-comparison-of-acoustic-cues-in-music-and-speech-for-three-dimen/ | summary only |
| Lahdelma & Eerola 2016 | https://jyx.jyu.fi/jyx/Record/jyx_123456789_48313/Description (single chords); DOI 10.1177/0305735614552006 | summary only (PDF too large) |
| Smit et al. 2020 | https://labs.sonicfield.org/library/perceived-emotions-of-harmonic-cadences | summary only |
| Gagnon & Peretz 2003 | https://doi.org/10.1080/02699930302279 | summary only |
| Friberg & Sundstrom 2002 | https://acoustics.org/pressroom/httpdocs/137th/friberg.html ; https://mcg.uva.nl/mcg-2023/papers/honing-haas-2008.pdf | summary only |
| Frühauf, Kopiez, Platz 2013 | https://sage.cnpereading.com/doi/10.1177/1029864913486793 | summary only |
| Schubert 2004 | https://sonicfield.org/library/modeling-perceived-emotion-with-continuous-musical-features | summary only |
| Yang et al. 2008 | search summary (IEEE TASLP) | summary only |
| Herremans & Chew 2016 | https://www.dorienherremans.com/sites/default/files/paper_tenor_dh_preprint_small.pdf | summary only |
| Eerola, Ferrer, Alluri 2012 | https://jyx.jyu.fi/handle/123456789/48672 | summary only |
| Plomp-Levelt / Sethares | https://auditory.org/mhonarc/1999/msg00231.html ; https://gist.github.com/3066664 ; https://www.scitepress.org/PublishedPapers/2007/21388/21388.pdf | summary only |
| DEAM / 1000 songs | https://cvml.unige.ch/databases/emoMusic/ ; https://cvml.unige.ch/databases/DEAM/manual.pdf | connection refused |
| PMEmo | https://www.next.zju.edu.cn/publications/pmemo-dataset | summary only |
| Music4All-Onion | https://zenodo.org/record/6609677 | summary only |
| MuSe | https://zenodo.org/record/4281165 | summary only |
| VGMIDI | https://homepages.dcc.ufmg.br/~lferreira/resources.html | summary only |
