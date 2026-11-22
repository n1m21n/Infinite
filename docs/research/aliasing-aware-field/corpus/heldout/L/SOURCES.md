# Held-out set L: literature kernels

32 sample-rate Field kernels. Each one takes its equation from a paper or book that was opened and read while writing it.
Every kernel file names its source and equation numbers in its header comment.

How the set was made:

- The kernels were written from the papers only. The author did not look at `src/core/field/AliasAnalysis.*`,
  `RatePlan.*`, `src/app/selftest/AliasTests.cpp`, `data/` or any phase 0-4 result.
- No code was copied from any implementation, GPL or otherwise. Every line is the paper's equation rewritten in Field.
- Only sample-rate Field is used: `in`/`out`, `state`, `delay(x, N)` with a literal N, and no `tanh`.
- Each kernel was compiled once in Infinite 2.0.0 (headless `infinite audio`, wavetable input) to check that it
  compiles and its output stays finite. That check did not measure aliasing.
- That check changed one kernel, l-27. It went over the 128-register cap, so it now reuses F0/F1 through `delay()`, as the
  paper suggests. Its fallback threshold was also raised to 0.01 because of float32 ill-conditioning. Both changes are
  recorded in its header.

## Shared assumptions

| Item | Value | Why |
|---|---|---|
| Sample rate for coefficients | 48 kHz | the rig rate; every bilinear or `exp` coefficient is worked at 48 kHz |
| `tanh` | Yeh, Abel and Smith eq. (19), x/(1+\|x\|^2.5)^0.4 | Field has no sample-rate `tanh`; that paper gives eq. (19) as a tanh-like curve |
| Diode Is, Vt (l-14, l-15) | 2.52 nA, 45.3 mV | not given in the paper; these are assumed values for a 1N914-class diode |
| sinh/cosh argument (l-14, l-15) | clamped to +-40 | float32 overflow guard |
| ADAA fallback threshold | 0.00001 (rectangular kernel), 0.01 (linear kernel) | the papers' ill-conditioned-case substitutes |
| Input and output gains | per kernel, in the file | sets the drive; not taken from the papers |

## Sources

| Ref | Source |
|---|---|
| EB12 | C. Enderby, Z. Baracskai, "Harmonic Instability of Digital Soft Clipping Algorithms", DAFx-12, York |
| JOS | J. O. Smith, *Physical Audio Signal Processing*, CCRMA online book, "Nonlinear elements" (hard clipping, soft clipping eq. 9.5) |
| EPVP17 | F. Esqueda, H. Pontynen, V. Valimaki, J. Parker, "Virtual Analog Buchla 259 Wavefolder", DAFx-17 |
| YAS07a | D. Yeh, J. Abel, J. O. Smith, "Simulation of the diode limiter in guitar distortion circuits by numerical solution of ordinary differential equations", DAFx-07 |
| YAS07b | D. Yeh, J. Abel, J. O. Smith, "Simplified, physically-informed models of distortion and overdrive guitar effects pedals", DAFx-07 |
| H04 | A. Huovilainen, "Non-linear digital implementation of the Moog ladder filter", DAFx-04 |
| TL11 | J. Timoney, V. Lazzarini, "Saturation non-linearities for Virtual Analog filters", Forum Acusticum 2011 |
| D97 | J. Dattorro, "Effect Design Part 1: Reverberator and Other Filters", JAES 45(9), 1997 |
| PZL16 | J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution", DAFx-16 |

## Kernels

`mem` = memoryless (no state and no delay). Under §13, memoryless kernels are scored but are outside the H1b/H3 pool.
`composite` = takes its equations from more than one source.

| File | Source, equations | Form | Notes |
|---|---|---|---|
| l-01-enderby-sine-clip | EB12 eq. 2 | mem | |
| l-02-enderby-exp2-clip | EB12 eq. 3, E = 2 | mem | |
| l-03-enderby-exp5-clip | EB12 eq. 3, E = 5 | mem | |
| l-04-enderby-tsq-clip | EB12 eq. 4 | mem | |
| l-05-enderby-cubic-clip | EB12 eq. 5 | mem | |
| l-06-enderby-reciprocal-clip | EB12 eq. 6 | mem | |
| l-07-smith-cubic-soft-clip | JOS eq. 9.5 | mem | |
| l-08-smith-hard-clip | JOS hard clipping | mem | |
| l-09-buchla259-static | EPVP17 eqs. 13-18 | mem | 5 V peak drive, as in their Fig. 4 |
| l-10-buchla259-filtered | EPVP17 eqs. 13-21 | state | output one-pole lowpass at 1.33 kHz, bilinear |
| l-11-yeh-rational-sat | YAS07b eq. 19 | mem | |
| l-12-yeh-distortion-pedal | YAS07b eqs. 15, 16, 19 and sec. 3.5 | state + delay | composite chain within one paper; the 0.5/0.5 tone weights are this kernel's choice |
| l-13-yeh-overdrive-pedal | YAS07b eqs. 19-23 | state + delay | feedback diode curve taken as static eq. 19 |
| l-14-yeh-diode-be-semiimplicit | YAS07a eq. 2, BE + one Newton step (sec. 2.1.7) | state | Is, Vt assumed; at 1x one step is inaccurate at high drive, which the paper also reports |
| l-15-yeh-diode-tr-semiimplicit | YAS07a eq. 2, trapezoidal + one Newton step | state | as l-14 |
| l-16-yeh-prefilter-static | YAS07a sec. 2.1.8 + YAS07b eq. 19 | state + delay | composite |
| l-17-huovilainen-ladder-1k | H04 eqs. 13-17, 21 | state | tanh -> YAS07b eq. 19 (composite); no 2x oversampling |
| l-18-huovilainen-ladder-3k-reso | H04 eqs. 13-17, 21, r = 0.9 | state | as l-17 |
| l-19-huovilainen-single-stage | H04 eqs. 21-22 | state | tanh -> YAS07b eq. 19 |
| l-20-timoney-config1 | TL11 eqs. 2, 6, 7; configuration 1 | state + delay | beta = 0.5 |
| l-21-timoney-config2 | TL11 eqs. 2, 6, 7; configuration 2 | state + delay | beta = 0.5 |
| l-22-timoney-config3 | TL11 eqs. 2, 6, 7; configuration 3 | state + delay | beta = 0.9 |
| l-23-timoney-exp-static | TL11 eq. 2, c = 9 | mem | |
| l-24-dattorro-chamberlin-clipped | D97 Fig. 18, eqs. 28-29 + JOS eq. 9.5 | state | composite; bandpass state clipped |
| l-25-dattorro-chamberlin-cascade | D97 Fig. 18, eqs. 28-29 + EB12 eq. 5 | state | composite; two sections, as his cascade footnote describes |
| l-26-parker-adaa-hardclip | PZL16 eqs. 9, 10, 24, 25 | delay | |
| l-27-parker-adaa-linear-hardclip | PZL16 eqs. 14-16, 24-26 | delay | threshold 0.01 (see header) |
| l-28-parker-adaa-lowpass | PZL16 Fig. 8, eqs. 9, 10, 24, 25 | state + delay | plain one-pole, not the delay-eliminated Figs. 9-10 |
| l-29-adaa-smith-cubic | PZL16 eqs. 9-10 + JOS eq. 9.5 | delay | composite; F0 worked by hand |
| l-30-adaa-enderby-reciprocal | PZL16 eqs. 9-10 + EB12 eq. 6 | delay | composite; F0 worked by hand |
| l-31-adaa-timoney-exp | PZL16 eqs. 9-10 + TL11 eq. 2 | delay | composite; F0 worked by hand |
| l-32-parker-moog-like | PZL16 Fig. 11 + YAS07b eq. 19 | state | composite; unit-delay feedback, k = 3 |

Counts: 32 kernels. 11 are memoryless (01-09, 11, 23) and 21 have state or delay.
