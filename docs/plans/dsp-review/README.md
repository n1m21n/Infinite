# DSP review of synth and audio-effect nodes (2026-10-30)

Owner asked for a pass on every synth / effect node's DSP against references, using the existing measurements.
Measured with `INFINITE_SYNTHAUDIT`, `INFINITE_FXAUDIT`, `INFINITE_ALIASTEST` (all on today's `main` build), then
the flagged nodes' code read against their references. Fixes go one at a time, top of the table first.

## What the measurements say
| Audit | Result |
|---|---|
| SYNTHAUDIT | PASS, 0 flags. Analog / Wavetable / Oscillator / Equation alias -82..-88 dB, pitch within 0.03 c. Default level spread 31 dB (Analog -8.1 rms, Spectral Synth -39.0) |
| SYNTHAUDIT coverage | FM, Partials, String, Wind, Piano, Resynthesize are not in it |
| FXAUDIT | 28 effects, 19 flags: default gain (Saturator +10, Wavetable Shaper +9, Formant +8, Shape Resonator tone -29 / noise +11), stress peaks (Transient Shaper 15.8 at all-max), 3 rate-dependent releases, 5 param clicks near +9 dB |
| ALIASTEST | FAIL on one case: Cycle Shaper sine @ 500 Hz -90.2 dB (gate -105). Ungated: Wavetable asym+ 100% -16 dB, Saturator rectify reads +81.8 (metric) |

## Code read against references
| Node | Algorithm today | Reference | Verdict |
|---|---|---|---|
| Piano | Chaigne-Askenfelt hammer, waveguide + 4 dispersion allpasses + Thiran, detuned unisons; soundboard = 12 fixed resonators, Q 4-8 | Bank et al. 2010 (modal piano); Smith & Van Duyne 1995 (commuted soundboard) | String part is textbook. The soundboard is the "synthy" part: a real board is hundreds of overlapping modes and reads as a short dense reverb, 12 peaks read as a filter |
| String | Karplus-Strong / waveguide + dispersion; body = 10 bandpass modes per type | Karjalainen & Välimäki 1993, Smith 1993 (commuted body IR) | Same gap: 10 modes is why box / guitar / harp / violin / cello differ by 1.7-4 dB only |
| Wind (brass) | Lip valve on bore, one-pole bell loss, linear-interpolated delay read | Hirschberg 1996 (brassiness = nonlinear wave steepening); Välimäki-Laakso 2000 (fractional delay) | Missing the steepening that makes loud brass "blare"; linear interpolation damps and detunes high notes (Piano already uses Thiran) |
| Saturator / Drive / Wavetable Shaper | Oversampled, ADAA, unit-slope curves; amount = drive gain with no makeup | Parker-Zavalishin-Le Bivic 2016 (ADAA); Pirkle ch. 19 | DSP is right. Default +10 dB is the default amount (0.3 = 11 dB of drive) with output at 0 dB |
| EQ | Vicanek matched biquads | Vicanek 2016 | Already the reference. 1.5 dB at a 10 kHz shelf is the 3-point match's residual; leave |
| Wavetable asym+ | Power-law phase skew, mip by read speed, no BLAMP (cusp) | Code comment says it needs the voice oversampled | Known limit, -16 dB at 100% on a 1.75 kHz note |
| Cycle Shaper | Zero-crossing PLL, quintic estimates, band-limited tables | own design | -90 dB at 500 Hz is inaudible but under its own gate: a regression to find, not a design fault |
| Shape Resonator | Modal bank, T60 per mode at the true rate, 48 kHz-referenced input gain | Adrien 1991 modal synthesis | Correct. Release "x1.24" is more modes fitting under Nyquist at 96 kHz; tone -29 dB is a tone off-mode. No fix |
| Key-Snap, Convolution | releases 0.05-0.06 s | - | x1.17-1.25 is one hop of measurement grain on a 50 ms tail. No fix |
| Transient Shaper | Dual-envelope difference, gain up to +24 dB | Case, Sound FX ch. 9 | 15.8 peak is the +24 dB knob at max; by design |
| FM | 8x oversampled operators | Chowning 1973 | Fine |

## Ranked fix list (impact x effort)
| # | Fix | Nodes | Why | Effort | How it is checked |
|---|---|---|---|---|---|
| 1 | Dense body: commuted body/soundboard IR (filtered decaying noise per body type, 60-300 ms, convolved once per note-excitation) replacing the 10/12 mode banks | String, Piano | Both owner complaints (body types sound alike; piano synthy); one shared component | M | string_families.py / family ratio for Piano (6.26 today) and String bodies; body A/B rms + centroid gap; STRINGTEST |
| 2 | Brass: pressure-dependent steepening (amplitude-driven lowpass opening in the bore, Hirschberg) + Thiran fractional delay for the bore read | Wind | Brass never "blares"; high notes dull and off-tune | M | wind family test; pitch error across C2-C6; spectral centroid vs breath |
| 3 | Default levels into one band (-12 +-4 dBFS rms): Spectral Synth +27 dB, PaulStretch, Granular / Sampler / Grain Molder, Wave Terrain | synths | 31 dB spread at defaults; a new node should not need a gain node | S | SYNTHAUDIT LEVEL lines |
| 4 | Effect defaults at unity: default `output` cancels the default drive (Saturator, Wavetable Shaper), Formant / Shape Resonator level trims | effects | Dropping a saturator on a track jumps +10 dB; loudness reads as "better" in any A/B | S | FXAUDIT gain flags go to 0 |
| 5 | SYNTHAUDIT covers FM, Partials, String, Wind, Piano, Resynthesize (level, alias, pitch, rate, determinism) | audit | Six synths have no measurement at all | S | the audit itself |
| 6 | Param click on output / makeup / inGain / sustain knobs (+9 dB): smooth gain knobs in dB over 20 ms | Dynamics, Drive, Wavetable Shaper, Limiter, Transient Shaper | Zipper / click when turned live (flow break) | S | FXAUDIT click column |
| 7 | Cycle Shaper 500 Hz alias regression: bisect, fix | Cycle Shaper | Own gate fails | S | ALIASTEST |
| 8 | Saturator rectify alias metric: measure at 2 f0 (rectify doubles the fundamental) | test | False +81.8 reading hides real numbers | S | ALIASTEST |
| 9 | Wavetable asym+ above 50%: 2x oversample that voice only | Wavetable | -16 dB aliasing on high notes | M | ALIASTEST asym cases get a gate |

Not fixing: EQ shelf residual, rate-dependent releases, Transient Shaper max boost, Shape Resonator off-mode tone (see table above).

## Order
3, 4, 5, 6, 8 are small and independent: one commit each. Then 1 (biggest audible win), 2, 7, 9.

## Done
| # | Result |
|---|---|
| 1 | `src/audio/dsp/DenseBody.h`, used by String (96-200 modes per type) and Piano (224). String body types against box, rms dB of spectrum difference at body 0.5: 1.7-4.1 before, 5.9-8.3 after. Stereo with centred bass, left/right never anti-phase. STRINGTEST and factory presets pass |

Piano family score after #1: 7.72 (was 6.26), a miss on that metric. Same loudness as before now, so the rise is real but small: fullness gap 0.13 -> 0.26 sd, attack 0.30 -> 0.51 sd (still inside the library's quartiles). Brightness and warmth moved closer. The largest gap, before and after, is loudness (2.4 sd), which is not timbre. Piano presets remain close: nearest other preset 1.7-4.8 dB, because all eight share one board.

Open: board size per preset (grand / upright).

## Electric pianos in the Piano node (2026-10-30)
`src/audio/dsp/ElectricPiano.h`: `model` = grand / rhodes / wurlitzer, 8 new presets. Four tip modes struck by a Hunt-Crossley hammer, read through the pickup's nonlinear field law, a preamp and a speaker.

| Part | Source | Measured or voiced |
|---|---|---|
| Rhodes pickup field, velocity bark | Falaize & Hélie 2017 | form from the paper, distance by knob |
| Rhodes Q per pitch, inharmonic overtones die fast, centring shifts to 2nd partial | Shear 2011 | measured there |
| Wurlitzer reed, dominant first mode, tip displacement, electrostatic pickup | Pfeifle 2017; Pfeifle & Münster 2017 | measured there; pickup law is a soft curve of that shape |
| Preamp high-pass, tube amp, speaker curves, Wurlitzer Q | GroupDIY 200A notes | voiced, not measured |

C4 at velocity 100, direct out, rms over 0.5 s: Rhodes -15.9, Wurlitzer -15.4, grand -18.6. Soft to hard at C4: Rhodes H2 -21 to -1 dB, Wurlitzer -36 to -12 dB. Only the owner's ear can sign off the sound.
