# B. Mix, spectrum, space, structure, groove: numbers for the feel table

Date 2026-10-28. Scope: frequency/spectrum, mix quality, breathing space, structure, groove,
arrangement. Each number carries a grade:

  A  = found AND opened/read this session (source URL given)
  A* = stated in a search-result summary of that source this session, page itself not opened
  B  = widely reported / textbook / industry convention, NOT re-opened this session
  C  = my inference (flagged; do not treat as literature)

Web access this session was poor: several PDFs were image-only or blocked (Pestana 2013,
Deruty-Pachet 2015, Honing-de Haas 2008, Deruty 2014 JAES figure). Where a paper's key
number could not be read, it is listed in "Gaps" rather than guessed.

Axes shorthand: loud, bright, sharp, warm, weight, full, noisy, rough, attack, decay, move,
width, space, vowel, glide, pitchH, regSpread, tension(tonal), harmRhythm, density, contour.
Mood: V valence, E energy arousal, T tension arousal.

---------------------------------------------------------------------------------------
## 1. Frequency regions: what each does

Band edges are convention (B). Measure with librosa STFT / essentia BandRatio or
EnergyBand (all computable). Mood links from Eerola 2013 (A) and general cue literature.

  Region    Hz          What it carries                     Too much          Too little      Axes touched     Mood
  --------  ----------  ----------------------------------  ----------------  --------------  ---------------  ----------------
  sub       20-60       kick fundamental, 808, sub bass;    mud, speaker      thin, no        weight, loud     E up, T slight
                        felt as much as heard (B)           overload          power           (LUFS cost)
  bass      60-250      bass fundamentals, kick body,       boom              no foundation   weight, full,    E up
                        low male voice, floor (B)                                             warm
  low-mid   250-500     warmth/body; "mud" zone (B)         muddy, boxy       hollow          warm, full       V slightly up
                                                                                              (if moderate)
  mid       500-2k    vowel/formant region, snare body,    honky, nasal      distant         vowel, full      T up w/ excess
                        guitar, piano, vocal (B)
  upper-mid 2k-5k       ear's most sensitive zone (ISO     harsh, fatiguing  dull, veiled    sharp, rough,    T up, E up,
            (presence)  226 contour dips here) (B); vocal                                    attack           V down if harsh
                        intelligibility, attack
  brilliance 5k-10k     sibilance, cymbals, "bite" (B)      sizzle, sibilance dark            bright, sharp,   E up
                                                                                              noisy
  air       10k-20k     openness, shimmer, noise floor      brittle           closed          bright, space,   V up (light),
                        of cymbals/reverb tails (B)                                            noisy            E slight up

Perception caveat (A): the 50-phon ISO 226:2003 equal-loudness weighting strongly attenuates
bass relative to the raw power spectrum; Deruty/Meredith/Lattner apply it to 30,435 pop
tracks 1961-2022 because raw spectra do not reflect what is heard.
  https://arxiv.org/pdf/2408.08127

Critical-band resolution (needed for masking, section 3):
  ERB(f) = 24.7 * (4.37*f_kHz + 1) Hz   [Glasberg & Moore 1990]   (A)
  ERB at 100 Hz = 35.5 Hz, 1 kHz = 132.6 Hz, 4 kHz = 456.5 Hz (A, computed from formula)
  https://en.wikipedia.org/wiki/Equivalent_rectangular_bandwidth
  Bark scale: 24 bands to 15.5 kHz, ~100 Hz wide below 500 Hz, ~20% of f above (B).

---------------------------------------------------------------------------------------
## 2. Spectral shape / centroid / tilt targets

Long-term average spectrum of popular recordings (Pestana, Ma, Reiss, Barbosa, Black,
AES 135, paper 8960, 2013, 1950-2010): consistent "target EQ curve" from industry practice,
partly mimicking acoustic ensembles (A*, abstract only).
  https://ciencia.ucp.pt/en/publications/spectral-characteristics-of-popular-commercial-recordings-1950-20/
  Deruty & Pachet (JAES 2015, "The MIR perspective on the evolution of dynamics in
  mainstream music" / "mixing target ~ average spectrum of a large commercial set") (A*).
  The Deruty/Meredith/Lattner power spectrum (A) is "consistent with Pestana et al." but
  the slope value was not readable from the text layer.

Numbers I could NOT verify but are standard (grade B):
  - Pink noise = -3 dB/octave in power per Hz (equal energy per octave).
  - Typical mastered pop long-term spectrum falls roughly -4 to -5 dB/octave (per-Hz
    density) above ~200 Hz; the "-4.5 dB/oct tilt" target used by plugin vendors (B/C;
    my search for a primary source returned none).
  - Acoustic music sits near pink; EDM/hip-hop tilt brighter at top (sizzle) AND more bass
    (forum anecdote only, not a source).

Measured per-INSTRUMENT spectral centroid in real multitrack mixes (A, De Man et al.
ISMIR 2014, 8 songs x 8 engineers; column order inferred from table: kick, snare, rest-of-
drums, bass, vocal, average, mix):

  Source        Centroid Hz   (+/- between-song std)   Rolloff-95 Hz
  ------------  ------------  -----------------------  -------------
  kick          2254          1066                     8880
  snare         4395          1449                     13451
  rest-drums    4131          1228                     13373
  bass          1047          520                      4389
  vocal         2920          452                      9879
  all-track avg 2949          872                      9995
  full mix      2479          518                      9679
  https://archives.ismir.net/ismir2014/paper/000293.pdf  (raw table scrambled by PDF text
  extraction; ordering verified only by plausibility: bass lowest, snare highest)

  => Full-mix spectral centroid target ~2.5 kHz (+/-0.5k) on LIBROSA-like linear spectrum
     with their windowing (A); real values vary with FFT and sample-rate, so calibrate
     against our own corpus (C).

Dance-music tonal observations (A, iZotope): content cut sharply above 20.1 kHz on one
track; "Dreamin" vocal harshness in 5-8 kHz. No numeric band balance published.
  https://www.izotope.com/en/learn/dance-electronic-analysis

Mapping:
  centroid, rolloff, tilt        -> bright, sharp (weighted), warm (inverse), weight (low-band share)
  band-energy ratios             -> weight, full, warm, noisy (HF share)
  flatness                       -> noisy
  mood: brighter timbre raises E and T and lowers "peaceful" (Eerola 2013, timbre beta:
        scary +0.15, sad -0.14, peaceful -0.45) (A)

Measurable: librosa.feature.spectral_centroid / rolloff / flatness / contrast; own tilt =
linear fit of dB spectrum vs log2(f) over 100 Hz-10 kHz (~10 lines numpy); essentia
BandRatio, LogSpectrum, SpectralComplexity.

---------------------------------------------------------------------------------------
## 3. What makes a good mix

### 3.1 Loudness, dynamics

  Quantity                         Value                                   Grade  Source
  -------------------------------  --------------------------------------  -----  ---------------------------
  Chart dance/EDM integrated LUFS  -6.2, -10.3, -7.7, -8.7, -11.3, -9.1,   A      iZotope, 10 tracks
                                   -7.7, -7.7, -7.0, -7.0  (mean -8.3)
  Loudness range (LRA) same set    4.8 3.5 6.7 10.0 13.8 4.2 5.8 6.6 5.4   A      iZotope
                                   (typical 4-7 LU; median ~5.8)
  True peak same set               -0.29 ... +1.50 dBTP (several > 0)      A      iZotope
  Short-term peak loudness chorus  -4.5 LUFS short-term in loudest chorus  A      iZotope ("Miles On It")
  2024 chart average (stated)      ~-8.3 LUFS; two outliers ~-2.5 LUFS     A      iZotope
  Streaming normalization          Spotify -14 LUFS (loud mode -11); Apple B->A*  secondary sites
                                   Sound Check -16; YouTube -14 (down only)
  Genre integrated LUFS (blogs)    EDM/dubstep -4..-9; pop/hip-hop -7..-9; A*     soundcamps blog, soundboost
                                   rock -8..-10; jazz/classical/acoustic          (not peer-reviewed; the two
                                   -12..-18; ambient -10..-13; classical          sources disagree by 1-3 LU)
                                   -18..-23
  Loudness war peaked              2007 (7,200 tracks, 1967-2014)          A*     Deruty & Pachet ISMIR 2015
  1984-2004 period                 "transient degradation", macrodynamics  A*     Deruty & Tardieu JAES 2014
                                   largely intact (LinkedIn summary)
  EBU R128 broadcast target        -23 LUFS integrated, -1 dBTP            B      EBU
  Recommended headroom pre-master  -3 to -6 dB peak; -18 dBFS RMS mix      B/C    mixing practice
  Quality ratings correlate with   loudness/dynamic-range compression      A*     Wilson & Fazenda JAES 2016
  Crest factor (100 ms window)     kick raw ~3.6 +/- 0.6 (units as paper)  A      De Man 2014
  Crest factor (1 s window)        kick raw ~9.8 +/- 3.1                   A      De Man 2014
  Mixing raises per-track crest    "short-term dynamic range effectively   A      De Man 2014
                                   expanded" (compressor lets transient
                                   through)

Genre guess table for our model (C, derived from above; refine from our own corpus):

  Genre          LUFS integrated   LRA LU    Crest (PLR) dB   Note
  -------------  ----------------  --------  ---------------  ---------------------------
  club EDM/DnB   -6 to -8          3-6       6-8              (A for EDM chart set)
  pop/hip-hop    -7 to -10         4-8       8-11             (A* blog)
  rock/metal     -8 to -11         5-9       8-12             (A* blog)
  acoustic/folk  -12 to -16        8-12      12-16            (A* blog)
  jazz/classical -14 to -23        10-20     14-24            (A* blog)
  ambient        -10 to -18        3-8       10-16            (A* blog; wide)

Mapping: LUFS -> loud (direct), also E (louder = more E; Eerola dynamics: scary +0.20,
peaceful -0.28, A). LRA/crest -> movement, density(inverse), attack. Hyper-compression
lowers dynamic contrast: lower movement, higher noisy/fullness.
Measurable: pyloudnorm (integrated LUFS, BS.1770); librosa rms -> crest = peak/rms; LRA via
pyloudnorm is NOT built in -> use ffmpeg ebur128 or implement EBU TECH 3342 gating
(10th-95th percentile of short-term loudness); essentia LoudnessEBUR128 gives LRA.

### 3.2 Balance of parts (A, De Man 2014; 8 songs x 8 engineers)

  Lead vocal level relative to full-mix loudness: -2.7 +/- 1.6 LU  (A) => "vocal target
    about -3 LU re mix"
  Kick track loudness relative to mix: -13.15 +/- 4.05 LU (A, includes drum-mic crosstalk)
  Other elements: no significant mean difference from each other, i.e. roughly equal
    loudness except vocal (A)
  Bass: the only element significantly more centered than the full mix in the lowest band
    (0-250 Hz); most low-frequency sources centered (A)
  Per-track activity (fraction of time above -30 LU gate): kick ~0.68; others 0.84-0.99 (A,
    first value 0.676 is the first column, order uncertain)
  Mix variation: engineers vary on 4 axes: loudness/dynamics, brightness, bass, stereo width
    (A*, Wilson thesis 2017)
  https://archives.ismir.net/ismir2014/paper/000293.pdf
  https://salford-repository.worktribe.com/output/1389142/

### 3.3 Stereo width and mono compatibility

  Measured:
   - side/mid ratio and L/R imbalance are significantly higher for non-pop/rock songs
     than for pop/rock (p<1e-6) (A, De Man 2014)
   - low band (0-250 Hz) panning RMS lowest; bass most centered (A)
  Conventions (B, practitioner; no corpus study found):
   - bass/kick/lead vocal/snare centered; mono below ~100-150 Hz; wide above ~1.5 kHz
     (plugin vendor guidance, A* search)
   - correlation (L,R) should stay > 0 most of the time; long stretches < 0 = mono risk
     (A* search)
  C: numerical targets for us: Side/Mid energy ratio 0.1-0.4 pop, 0.3-1.0 ambient/orchestral;
     correlation 0.5-0.9 pop; low-band (<150 Hz) correlation > 0.95.

Mapping: side/mid ratio, correlation, per-band correlation -> width; mono compat is a QA
check not an axis. Mood: wider = more V, "space" (C).
Measurable: numpy mid=(L+R)/2, side=(L-R)/2 -> RMS ratio; np.corrcoef per octave band;
pyloudnorm on M and S separately; essentia StereoSignalWidth does not exist, compute manually.

### 3.4 Masking and instrument slots

  Masking is set by critical bands (ERB above, A). Rule-of-thumb consequences (B/C):
   - two sources with fundamentals within one ERB (e.g. 100 Hz +/-18 Hz; 1 kHz +/-66 Hz)
     compete; separate by octave or by time (sidechain/duck) or by pan.
   - bass and kick share 40-120 Hz: only one gets the sub; fix by arrangement (C).

  Role slot table (B/C, conventional mix practice; centroids from A table above):

  Role        Fundamental region     Main body       Presence/air           Pan     Level (re mix)
  ----------  ---------------------  --------------  ---------------------  ------  ----------------
  kick        40-100                 60-120 + click   2-5k (beater)          C       ~ -13 LU (A)
                                                                                      track-level
  bass        40-200                 80-300           700-1.5k (definition)  C       ~ -? ; centroid
                                                                                      ~1.0 kHz (A)
  snare       150-250                200-400          3-6k crack             C or +-5  centroid 4.4k (A)
  vocal       lead 100-300 (f0)      300-1.5k         3-5k, air 10k+         C       -2.7 LU (A)
  pad         100-500                200-2k           low presence            wide    -6 to -9 (C)
  lead synth  200-1k                 500-4k           4-8k                   C/near  -3 to -6 (C)
  hats/perc   (n/a)                  (n/a)            6-16k                  +-20..60 -10 to -14 (C)
  texture/FX  -                      -                8k+                    wide    -12+ (C)

Mapping: roles count -> density, fullness; register separation -> regSpread; overlapping
bands -> rough/muddy (inverse "clarity"). Measurable: per-stem band energy overlap =
sum_b min(E_i(b), E_j(b)) / sum_b E_i(b); needs stems (we have them from our generator).

### 3.5 Reverb / space amounts

  Wet level (C ranges from practice; one A* study):
   - De Man/McNally/Reiss JAES 2017: perceived total reverb amount predicted by relative
     reverb loudness and early decay time (A*). No ranges read.
   - Typical RT60 by genre (B): pop 1.0-2.0 s plate/hall; intimate/dry 0.3-0.8; ambient
     3-8 s; cathedral/organ 4-8 s.
   - Wet/dry send level re dry signal (C): vocal -18 to -12 dB; snare -15 to -10; pad
     -8 to 0; ambient +0 dB or more.
  Mapping: RT60, DRR, predelay -> space (direct), decay, roughness(inverse), width.
  Measurable: Schroeder RT60 from our known impulse response when we render; for finished
  audio estimate DRR/RT via essentia? no direct; use spectral decay after offsets (C).

---------------------------------------------------------------------------------------
## 4. Breathing space, negative space, silence

  Quantity                        Value                                       Grade  Source
  ------------------------------  ------------------------------------------  -----  --------------------
  Silence perceived through       silences differ acoustically only by        A*     Margulis 2007 Music
    context                       duration; felt tension depends on harmonic         Perception
                                  closure before it (post-closure silences
                                  rated less tense, identified faster)
  No numeric tension threshold in seconds found in sources reached.
  Recommended rest duty (C): vocal activity 0.6-0.8 of time in verse; track activity of kick
    0.68 (A, De Man) while other stems 0.84-0.99 (continuous pads/room mics inflate)
  Typical rest/pause lengths (B/C):
   - 1/16 to 1/4 note gaps = articulation (staccato; Eerola articulation levels 1.0, .75,
     .5, .25 note-length/IOI, A)
   - 1 beat = phrase breath (C)
   - 1 bar silence before drop / cutoff before chorus = "drop-out pause" 0.5-2 s (C)
   - full stop > 2 s = dramatic (C)
  Eerola 2013 articulation beta (A): scary -0.03 n.s.; happy +0.14 (shorter = happier);
    sad -0.18; peaceful -0.10.  => staccato raises V and E slightly, legato lowers E.

Mapping: rest density and gaps -> density(inverse), movement, space (perceived), tension
(delay before resolution), contour. Mood: long gaps + low density -> low E, higher T if
unresolved (Margulis context effect, A*), lower T if post-cadence.
Measurable: librosa.onset.onset_detect -> inter-onset intervals; frame RMS < -50 dBFS
fraction = silence ratio; pause length distribution from RMS gate; essentia OnsetRate.

---------------------------------------------------------------------------------------
## 5. Song structure and build-up

### 5.1 Section statistics

  Quantity                               Value                              Grade  Source
  -------------------------------------  ---------------------------------  -----  -------------------
  Pop corpora: verse share of annotated  verse ~34% of time, chorus ~24%    A*     ISMIR 2013 (649
    time                                                                           songs)
  100 Chinese pop songs: sections        verse and chorus in 100/100;       A      arXiv 2609.15675
    present                              pre-chorus 38; bridge 43
  Same: unique raw section orders        76 distinct orders; 49 after       A      arXiv 2609.15675
                                         collapsing repeats
  Same: most frequent transitions        verse->chorus 116; chorus->        A      arXiv 2609.15675
                                         interlude 111
  Harmonix Set (912 pop songs)           beat/downbeat/segment labels exist A*     github.com/urinieto/harmonixset
                                         (we can COMPUTE section bar
                                         lengths from it)
  Typical pop section bars               intro 4-8, verse 8-16, pre 4-8,    B      songwriting convention
                                         chorus 8 (sometimes 16), bridge 8
  EDM template bars (practitioner)       intro 8-16, build 8-16 (8 typical),B/A*   cymatics, samplefocus,
                                         drop 16-32, breakdown 8-16               forum
  EDM at 140 bpm: bar 32 ~ 55 s          16 intro + 8 break + 8 build       A*     producer forum
  Typical total song length              pop 3-4 min; club track 5-7 min    B

  Derived (C): pop section durations at 100-120 bpm 4/4: 8 bars = 16-19 s; 16 bars = 32-38 s.
  Phrase unit 4 / 8 / 16 / 32 bars (powers of 2) in dance music (B).

### 5.2 Energy arcs

  Typical arcs (B/C; shape, not measured here):

  Genre / form     Arc shape in E over time                                  Peaks
  ---------------  --------------------------------------------------------  ------------
  pop              verse (E=0.4) -> pre (0.55) -> chorus (0.85) -> verse    chorus x2-3,
                   (0.45) -> chorus -> bridge (dip 0.3-0.5) -> final        last chorus
                   chorus (0.95)                                           highest
  EDM/house        intro (0.3) -> build (0.3->0.8) -> drop (1.0) ->         2 drops
                   breakdown (0.3) -> build -> drop 2 -> outro (0.4)
  dubstep/trap     shorter build (8 bars), 16-32 bar drop with half-time
  ambient          no arc; slow drift, +-10% loudness over minutes
  jazz/classical   AABA, theme-solos-theme; dynamics via LRA 10-20 LU
  Build-up devices (B): riser noise (sweep, increases bright/noisy), snare roll doubling
    rate 1/4 -> 1/8 -> 1/16 -> 1/32 across 8 bars, high-pass filter sweep up (rise in centroid),
    kick removal then return on drop ("drop-out pause" 1 beat to 1 bar).
  Drop-ratio measurable (C): short-term LUFS jump at section boundary ~2-5 LU; centroid
    jump +/- 500-1500 Hz; LRA within loop 3-5 LU.

Mapping: section arcs -> contour, movement, harmRhythm, density (layers added each bar-group),
tension(tonal) (V-I, secondary dominants before chorus). Mood: build-up = T up, E up; drop =
E max, T release; breakdown = E down, V steady. Eerola 2013 (A): tempo (notes/sec 1.2 -> 6)
beta: happy +0.25, scary +0.07, sad -0.32, peaceful -0.27.
Measurable: librosa.segment (self-similarity, recurrence_matrix), msaf/allin1 for sections;
short-term LUFS timeline; novelty = librosa.onset.onset_strength; essentia SBic.

---------------------------------------------------------------------------------------
## 6. Groove

### 6.1 Swing ratio (beat : upbeat of an eighth-note pair)

  Tempo (bpm)   Mean swing ratio (ride, jazz)   Grade  Source
  ------------  ------------------------------  -----  ----------------------------------
  <100          ~2.1 (1 excerpt: 71 bpm 2.11)   A      Dittmar et al. 2017, 67 excerpts
  100-130       ~2.7 (e.g. 3.07 @120, 3.40 @121) A    same
  130-160       ~2.5 (range 1.79-3.50)          A      same
  160-200       ~2.3 (2.57 @181, 1.90 @199)     A      same
  >200          ~1.8 (1.13-1.24 at 288-338 bpm) A      same
  slow tempo ceiling                  ~3.5 : 1  A      Friberg & Sundstrom 2002
  fast tempo floor                    ~1 : 1    A      same
  2:1 only exactly near ~200 bpm      A         Friberg & Sundstrom
  short (upbeat) note duration        ~100 ms constant at medium-fast tempi; A  same
                                      416 ms at 72 bpm (DeJohnette)
  per-excerpt std of ratio            0.01-0.53 (drummer-dependent)   A  Dittmar 2017
  Mean swing ratio in stimulus used for  2.66 at 150 bpm                A  Frontiers 2017 (Study B)
    groove listening test
  https://acoustics.org/pressroom/httpdocs/137th/friberg.html
  https://www.audiolabs-erlangen.com/resources/MIR/2017-JNMR-SwingRatio

  Formula (A, derived from the above): swing ratio r ~ clip(3.5 - 0.0125*(bpm-72), 1.0, 3.5)
    is NOT in a paper; it is my C fit to the table: r = 3.5 at slow, 2 at ~200 bpm, 1.0 at ~330.
  Shuffle/funk (non-jazz): triplet feel 2:1 (66.7% of beat) nominal (B); MPC swing 50-75%
    (50% = straight, 66.7% = triplet) (B, hardware convention).

### 6.2 Microtiming

  Quantity                              Value                                 Grade  Source
  ------------------------------------  ------------------------------------  -----  -------------
  JND for timing deviation              ~6 ms for IOI < 240 ms; ~2.5% IOI     A      Friberg &
                                        for IOI 240-1000 ms                           Sundberg 1995,
                                                                                      cited in Frontiers 2017
  Standard timing deviation (STD)       swing 27.2 ms (6.8% of beat);         A      Frontiers 2017
    of real performance                 funk 15.7 ms (2.6% of beat)
  Density-adjusted STD                  funk 9.1% of mean IOI, swing 10.6%    A      same
  Bass shifted vs drums +/-8/16/24 ms   NO significant groove effect           A      same (Study A,
                                        (n=160)                                       Frontiers 2017)
  Snare displaced inside drum track     significant: entrainment/enjoyment    A      same
    same magnitudes                      inverted-U, irritation U-shaped
                                        (irritation p<0.001)
  Previous: bass-drum shift 10/20/30 ms little effect (Butterfield 2010); snare or
    bass-drum +/-15/25 ms reduces groove (Fruehauf 2013); bass up to +/-62.5 ms hurts
    (Matsushita & Nomura 2016)            A (as cited in Frontiers 2017)
  Bass-drum asynchrony +/-80 ms        pupil dilation up; tapping stability   A*     Skaansar, Laeng,
                                        down                                          Danielsen 2019
  Typical "laid-back" bass vs kick     bass 10-30 ms late (D'Angelo style)   B/C    Danielsen (not
                                                                                      opened)
  => Rule for generator (C): inter-voice offset of 0-25 ms is perceptually tolerated;
     keep kick/snare timing random jitter <= ~10-15 ms STD; keep bass lag <= ~20 ms;
     random offsets beyond ~30 ms start to feel sloppy.

### 6.3 Syncopation and accent

  Quantity                            Value                                  Grade  Source
  ----------------------------------  -------------------------------------  -----  ----------
  Groove / wanting to move vs sync    inverted-U: medium syncopation peaks;  A      Witek et al.
                                      entropy a poor predictor                      PLOS ONE 2014
  Numeric syncopation index at peak   not found in abstract (LHL index,     gap
                                      funk breaks; need full text)
  Eerola: sync/rhythm not tested directly in that paper; tempo only           A

  Kick/bass relationship (B/C):
   - kick on 1 (downbeat); bass root on 1 with kick -> "locked"; bass anticipates 4-and (1/16
     before beat 1) = funk push; EDM bass off-beat (on the "and") with sidechain duck of
     ~4-8 dB, release 100-250 ms (B, widely reported).
   - snare on 2 and 4 (backbeat) = default pop/rock; half-time snare on 3 = trap/dubstep.
  Measurable: librosa beat_track + onset_strength; syncopation = sum of weights at off-beat
  onsets (LHL metric via simple 16-grid weights); swing ratio = IOI(beat->upbeat) /
  IOI(upbeat->next beat) from onset times in the eighth grid; microtiming STD = std of onset
  deviation from fitted grid in ms; kick-bass lag via cross-correlation of low-band
  (<150 Hz) onset curves vs mid.

Mapping: swing ratio, STD, syncopation -> movement, attack(regularity), harmRhythm, density.
Mood: Eerola tempo beta (A) says faster -> happier, scarier less sad; syncopation medium ->
groove pleasure (A); too much microtiming error -> irritation (A) -> V down.

---------------------------------------------------------------------------------------
## 7. Instrument / layer structure

No corpus study of layer counts found this session (gap). Observations:
  - De Man: bass is on ~84-85% of time; vocal ~85-91% of time; kick ~68% (A).
  - Practitioner arrangement (B/C): intro 1-3 layers; verse 3-5 (drums, bass, harmony,
    vocal, +1); chorus 5-8 (adds doubles, pad, lead, percussion); breakdown 1-3; drop 6-10.
  - Voice allocation by role: "kick/bass/pad/lead/texture" = low-end anchor, harmonic bed,
    melodic top, noise/ear-candy. Typical simultaneous voices (C): 4 verse, 6-7 chorus.
  - Concentration of energy: in pop, kick+bass+vocal carry most of the loudness (vocal ~ -3
    LU, kick ~ -13 LU track-level, A).

Mapping: layer count -> density, fullness; role frequencies -> regSpread, pitchH.
Measurable: stem activity (fraction of time above gate), source separation counts (demucs
4 stems) for external audio; onset density per stem; polyphony from basic-pitch /
essentia MultiPitchMelodia.

---------------------------------------------------------------------------------------
## 8. Mood cue coefficients (A, Eerola, Friberg, Bresin 2013, Frontiers Psychol.)

Standardized betas on emotion ratings (df 9,190); NOT valence/energy/tension directly, but
happy~V+, scary~T+, sad~V-/E-, peaceful~V+/E-:

  Cue          scary   happy   sad     peaceful   Reading for V/E/T (C)
  -----------  ------  ------  ------  ---------  --------------------------------
  mode major   -0.74   +1.77   -1.60   +0.43      major -> V up (dominant)
  tempo        +0.07   +0.25   -0.32   -0.27      faster -> E up, V up, calm down
  register     -0.23   +0.18   -0.05   +0.15      high -> V up, T down
  dynamics     +0.20   -0.01   -0.05   -0.28      louder -> T up, peaceful down
  articulation -0.03   +0.14   -0.18   -0.10      staccato -> V up
  timbre       +0.15   +0.01   -0.14   -0.45      brighter -> peaceful down, T up
  Levels used: tempo 1.2..6 notes/s; dynamics -10..+10 dB; register MIDI 53..83;
  articulation 1.0..0.25; mode major/minor.
  https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2013.00487/full
  Order of cue strength: mode > tempo > register > dynamics > articulation > timbre (A).

---------------------------------------------------------------------------------------
## 9. Measurability cheat sheet (librosa / essentia / pyloudnorm)

  Quantity                          Tool / call                               Cost
  --------------------------------  ----------------------------------------  ----
  Integrated LUFS, short-term LUFS  pyloudnorm Meter(sr).integrated_loudness; ffmpeg ebur128 for LRA   low
  LRA                               essentia LoudnessEBUR128 or ebur128       low
  True peak                         pyloudnorm? no -> 4x oversample numpy / ffmpeg ebur128 peak=true   low
  Crest / PLR                       numpy: 20log10(peak/rms) per window       low
  Spectral centroid/rolloff/flatness/contrast  librosa.feature.*              low
  Spectral tilt                     fit dB-vs-log2 f slope 100 Hz..10 kHz     low
  Band energies                     librosa.stft + band mask / essentia EnergyBand  low
  Stereo width, correlation         numpy M/S, np.corrcoef per band            low
  Silence/rest ratio, pauses        RMS gate -50 dBFS, run lengths             low
  Onsets, IOI, swing, micro-timing  librosa.onset + beat_track + 16-grid fit   medium
  Syncopation (LHL)                 weight table over 16 grid + onset flags    medium
  Section boundaries                librosa.segment / msaf / allin1            medium-high
  Roles / layers                    stems (ours) or demucs (external)          high
  Reverb RT60/DRR                   from known IR (ours); estimate for external  high

---------------------------------------------------------------------------------------
## 10. Ten-line summary and gaps

1. Loudness: chart EDM sits -6..-11 LUFS (mean -8.3, A) with LRA 3.5-14 (typical 4-7 LU);
   streaming normalizes to -14/-16 so extra loudness is wasted (B).
2. Vocal sits ~-2.7 +/- 1.6 LU re mix loudness; kick track ~-13 LU; other parts roughly equal (A).
3. Per-source centroid (A): bass ~1.0k, vocal ~2.9k, snare ~4.4k, full mix ~2.5k Hz;
   rolloff-95 for the mix ~9.7k.
4. Bass is the only source significantly more centered than the mix in <250 Hz; non-pop/rock
   mixes are wider (side/mid higher, A).
5. Swing ratio is tempo-dependent: ~3.5:1 slow -> 2:1 near 200 bpm -> ~1:1 fast; short note
   ~100 ms at medium-fast tempi (A); our generator should use r(bpm) not a fixed 2:1.
6. Groove tolerates bass-vs-drum shifts to +/-24 ms (no significant effect) but snare
   displacement of the same size is penalized (A); JND ~6 ms for short IOIs.
7. Medium syncopation is the groove optimum (inverted U, A); no numeric index recovered.
8. Mood cues: mode >> tempo > register > dynamics > articulation > timbre (A); brighter
   timbre lowers peaceful (-0.45) and raises scary (+0.15).
9. Structure: 8/16-bar blocks; EDM 8-16 bar builds into 16-32 bar drops (B/A*); verse ~34%
   and chorus ~24% of song time in a 649-song set (A*); Harmonix lets us compute bar stats.
10. Biggest gaps: (a) primary -4.5 dB/oct LTAS tilt per genre not recovered (Pestana PDF
    blocked); (b) no genre-resolved LUFS/crest corpus - compute from our own audio;
    (c) no layer-count or rest-duty study; (d) no numeric syncopation index at the
    groove peak; (e) reverb wet ranges by role are practice, not measurement; (f) mid/side
    ratio targets are C.

Recommended next actions (tool-first): run pyloudnorm + librosa over Air's preset renders
and any reference set we have, build the genre table in section 3.1 from OUR data, and pull
the Harmonix segments to compute section-bar and arc statistics directly.
