# Feel dataset (2026-10-28)
Built by `tools/universe/feel_measure.py` then `feel_dataset.py`. Files: 1_Instruments, 2_Sounds, 3_NoteModel, 4_Mix, 5_NodesKnobs (.csv) and feel_dataset.xlsx (all five).

| Table | Rows | Measured / labelled part | Rule (grade C) part |
|---|---|---|---|
| 1 Instruments | 14,161 | audio axes for 2,158 instruments (percentiles of centroid, flatness, attack, decay, rms, bands) | axes for the other 12,003 come from family + name-word priors; V/E/T by linear rule; effects from preset-chain lift |
| 2 Sounds | 2,180 | preset counts of moods, genres, characters, types, names, clusters | target axes from word lexicons |
| 3 Note Model | 18,210 | 15,400 chord-progression MIDI files and 1,078 EMOPIA clips measured (tempo, pitch, velocity, rests, off-beat, key/major-ness); 1,150 Groove MIDI loops; Plomp-Levelt dissonance computed for 25 intervals and 36 chords | tempo, scale, register, velocity, rest, swing rows use literature directions |
| 4 Mix | 1,273 | ERB/bark computed per 1/3-octave band | role x band shares, masking overlap, genre targets are rules from feel/B tables |
| 5 Nodes/Knobs | 6,974 | 148 synth knobs (signed slopes, six engines) and 121 effect knobs (3-setting response) measured | all other audio knobs: keyword rule; visual nodes: no mapping |

Checks: the cue rule vs human labels: chord-file V r=0.41, E r=-0.02 (chord files share one tempo, so E is not carried); EMOPIA V r=0.43, E r=0.77.
Not done: VGMIDI (phrase files are not on disk); the 3,894 unassigned instruments are flagged "needs render".
