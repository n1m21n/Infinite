# What each note-effect knob does (note_fx.py; held out by phrase, bar 90%)

Phrase -> Keyboard -> node -> sine Oscillator, read back from the audio. Turning the knob up moves these measures (agree = share of clear changes in that direction).

## Arpeggiator: held-out 0.949 on 468 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| mode | span down (0.83, +0.00) |
| octaves | register up (1.00, +22.22), span up (1.00, +14.90), poly up (1.00, +0.00), cents up (1.00, +0.00) |
| rateMode | onsets down (0.91, -20.21), span down (0.98, -33.40), poly down (1.00, +0.00), ioi_cv down (0.84, -0.03), grid up (0.94, +43.21), level down (1.00, -2.31), vel_sd down (0.96, -1.76), cents down (1.00, -0.00) |
| rateSeconds | onsets down (1.00, -2.86), span down (1.00, -8.44), poly down (0.90, +0.00), ioi_cv down (1.00, +0.00), level down (0.89, +0.00), vel_sd down (0.90, -0.00) |
| gatePercent | duty up (1.00, +0.45), cents down (0.86, -0.00) |

## Chorder: held-out 0.934 on 649 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| scale | poly down (1.00, -0.29) |
| root | onsets up (1.00, -3.61), register up (0.85, +1.44), grid down (1.00, -1.17), level down (0.80, -0.33) |
| chordSize | poly up (1.00, +2.79), duty up (1.00, +0.00), cents up (1.00, +7.61) |
| strumMs | onsets up (0.86, +13.54), register up (1.00, -0.53), poly down (0.80, -0.69), vel_sd down (1.00, -1.46), duty up (1.00, +0.00), cents down (1.00, -3.83) |
| humanizeTimingMs | onsets up (1.00, -1.47), register up (1.00, -0.01), span up (0.80, +0.00) |
| humanizeVelocity | register down (1.00, -0.06), span down (1.00, +0.00), poly down (1.00, -0.59), level up (1.00, +2.21), vel_sd up (0.88, +2.29) |
| upperHarmonics | register up (1.00, +1.16), span up (0.86, +1.48), poly up (1.00, +0.72), duty down (1.00, +0.00), cents up (1.00, -2.15) |
| mode | poly down (1.00, -0.08), duty up (1.00, +0.00), cents up (1.00, +0.95) |
| progSteps | duty up (1.00, +0.00) |

## Gate: held-out 0.947 on 245 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| holdMs | onsets up (1.00, +1.30), register down (1.00, +0.00), poly up (1.00, +1.50), ioi_cv up (0.83, +0.01), level down (1.00, -1.00), vel_sd up (0.96, +0.00), duty up (1.00, +0.75), cents up (0.96, +0.09) |

## Glide: held-out 0.893 on 187 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| glideMs | onsets up (0.88, +8.68), ioi_cv up (0.90, +0.34), vel_sd up (0.94, +0.51), duty up (1.00, +0.00), cents down (1.00, -4.93) |

## Humanizer: held-out 0.955 on 67 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| timingMs | nothing reliable |
| velocityPct | ioi_cv up (1.00, +0.00), level up (0.91, +0.78), vel_sd up (0.98, +3.36), cents up (1.00, +0.00) |

## Note Echo: held-out 0.934 on 361 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| rateMode | ioi_cv up (1.00, +0.00), grid up (0.89, +0.00), duty up (1.00, +0.16) |
| rateSeconds | span up (1.00, +0.00), duty up (1.00, +0.00) |
| repeats | nothing reliable |
| decay | onsets up (1.00, +0.00), register up (0.95, +0.00), span up (0.91, +0.00), poly up (0.95, +0.00), ioi_cv up (1.00, +0.00), grid up (0.88, +0.67), level up (1.00, +0.00), vel_sd up (0.95, +0.01), duty up (0.93, +0.06), cents up (0.80, +0.00) |
| transposePerRepeat | onsets down (1.00, +0.00), register up (1.00, +0.00), ioi_cv down (1.00, +0.00), vel_sd down (0.88, +0.00), cents down (1.00, +0.00) |
| muteDry | span down (0.80, +0.00), poly down (1.00, +0.00), level down (1.00, -76.32), vel_sd down (0.88, -0.60) |

## Note Filter: held-out 0.998 on 404 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| scale | nothing reliable |
| root | nothing reliable |
| rangeLow | onsets down (1.00, +0.00), register down (0.96, +0.00), span down (1.00, +0.00), poly down (1.00, +0.00), ioi_cv down (1.00, +0.00), grid down (1.00, +0.00), level down (1.00, +0.00), vel_sd down (1.00, +0.00), duty down (1.00, +0.00) |
| rangeHigh | onsets up (1.00, +0.00), register up (1.00, +0.00), span up (1.00, +0.00), poly up (1.00, +0.00), ioi_cv up (1.00, +0.00), grid up (1.00, +0.00), level up (1.00, +0.00), duty up (1.00, +0.00) |
| chance | onsets up (1.00, +0.00), span up (1.00, +0.00), ioi_cv up (1.00, +0.00), grid down (1.00, +0.00), vel_sd up (1.00, +0.00), duty up (1.00, +0.00) |

## Note Stack: held-out 0.942 on 447 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| semi0 | register up (0.96, +6.08), poly up (1.00, +0.00) |
| semi1 | register up (0.98, +6.11), poly up (1.00, +0.20), cents down (0.94, -2.43) |
| semi2 | register up (1.00, +5.37), cents down (0.89, -0.24) |
| semi3 | register up (0.93, +5.92) |
| semi4 | register up (0.94, +6.01), poly up (0.80, +0.00), ioi_cv down (0.83, +0.00) |
| semi5 | register up (0.93, +5.49), span down (0.80, +0.00), poly up (0.94, +0.00), cents down (0.93, -0.58) |
| semi6 | register up (1.00, +3.59), poly up (0.93, +0.00) |
| semi7 | register up (1.00, +5.85) |

## Note Strum: held-out 0.966 on 59 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| strumMs | register down (1.00, +0.00), span down (1.00, +0.00), poly down (0.91, +0.00), ioi_cv up (1.00, +0.00), duty up (1.00, +0.00) |

## Note Transpose: held-out 0.958 on 190 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| semitones | onsets down (0.92, +0.00), register up (0.95, +62.83), span up (1.00, +3.08), poly up (1.00, -0.34), ioi_cv down (0.93, +0.00), grid down (0.89, +0.00), vel_sd down (1.00, -0.27) |

## Pitch Bend: held-out 1.0 on 48 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| bendSemitones | onsets down (1.00, +0.00), register up (1.00, +4.75), ioi_cv down (0.86, +0.00), vel_sd down (1.00, +0.01) |

## Quantizer: held-out 0.878 on 115 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| div | onsets up (0.89, +2.24), register up (1.00, +0.00), span up (1.00, +0.00), grid up (0.82, +21.53), level up (1.00, -0.98), cents down (0.83, +0.00) |

## Velocity Curve: held-out 1.0 on 52 moves

| Knob | Moves (up/down, agree, median change per full turn) |
|---|---|
| curve | onsets down (1.00, +0.00), level down (1.00, -14.54), vel_sd up (1.00, +0.00), duty down (1.00, +0.00) |
