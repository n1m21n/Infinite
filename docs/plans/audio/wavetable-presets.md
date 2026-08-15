# Wavetable bank expansion — spec

Proposal to grow `Wavetable`'s bank from 12 tables (indices 0–11, frozen) to 22
tables by appending indices 12–21. No C++ changes are made in this pass; this
is the spec for a follow-up implementation session.

## Audit of the existing 12

| # | Name | Mechanism | Bucket |
|---|------|-----------|--------|
| 0 | Basic Shapes | 4-way Fourier-series blend (sine/tri/saw/square) | analogue-adjacent |
| 1 | Harmonics | soft-edged harmonic-count reveal, 1/h roll-off | bright/digital |
| 2 | Odd Only | odd harmonics, power-law exponent sweep | hollow/woody |
| 3 | Formant | single Gaussian peak walking up the series | vocal/formant |
| 4 | Pulse | duty-cycle Fourier series, 50%→4% | analogue-adjacent |
| 5 | Vocal | 3-formant Gaussian, ah→ee 2-point slide | vocal/formant |
| 6 | Bell | 14 fixed prime-ish partials, decay-shape sweep | metallic/inharmonic |
| 7 | Saturate | saw + exponent sweep (brightness) | bright/aggressive |
| 8 | Comb | saw × moving cosine notch | bright/digital |
| 9 | Drift | 2 fixed random spectra, crossfaded, scattered phase | noisy/diffuse |
| 10 | Sub | fundamental-dominant, h≤8 | bass/sub |
| 11 | Glass | perfect-square partials only, scattered phase | metallic/bright |

Real gaps: no physically-modeled struck/plucked instrument, no FM/Bessel
family, no phase-distortion family, no hard-sync family, no real organ
additive set, no multi-vowel path (only a 2-point one), no Walsh/Hadamard or
prime-sieve harmonic selection, no plucked/struck bucket at all, and the
noisy/diffuse bucket has exactly one entry.

## Proposed additions (indices 12–21)

Written in the same style as the existing `HarmonicAmp` switch — each block
is what the new `case N:` would contain. `Lerp`, `GaussPeak`, `SawAmp`, `Lcg`
are the helpers already in `Wavetable.cpp`.

---

### 12 — "FM Bell"

Bright, dense, metallic clang; sine at rest, dense bright sidebands at full
sweep.

**Morph journey (t 0→1):** modulation index β sweeps 0→9. At β=0 the carrier
is a pure sine; as β rises, energy spreads into ever more sidebands, reading
as a classic FM bell "bloom."

**Why it's harmonic-legal:** carrier and modulator are both integer multiples
of the fundamental (ratio 1:7), so every FM/PM sideband `c + n·m` automatically
lands on an integer harmonic of f0 — no detuning needed.

```cpp
case 12: // FM Bell — Bessel-spectrum FM/PM, carrier:modulator = 1:7
{
   constexpr int c = 1, m = 7;
   const float beta = Lerp(0.0f, 9.0f, t); // 0 = pure sine, 9 = dense clangor
   auto besselJ = [beta](int n) -> float {
      const int k = std::abs(n);
      const float j = (float)std::cyl_bessel_j((double)k, (double)beta);
      return (n >= 0 || (k & 1) == 0) ? j : -j; // J(-n) = (-1)^n J(n)
   };
   float amp = 0.0f;
   for (int n = -10; n <= 10; n++)
   {
      const int pos = c + n * m;
      if (pos == h)       amp += besselJ(n);
      else if (pos == -h) amp -= besselJ(n); // negative-freq sideband folds, sign flips
   }
   return amp; // 0 for h not reachable as |c + n*m|, |n| <= 10
}
```

**Phase:** zero. Bessel amplitudes already carry the sign (fold-back) information.

**Closest existing / why separate:** Bell (6) is a *fixed* 14-partial prime
list whose only journey is a decay-shape change; FM Bell's partial set is
dense and keeps growing denser with β, and the amplitude law is a genuinely
different family (Bessel sidebands, not a hand-picked list).

**Citation:** nth-harmonic amplitude of tone-modulated FM ∝ Jₙ(β) — [FM Harmonic Amplitudes (Bessel Functions), dsprelated.com](https://www.dsprelated.com/freebooks/sasp/FM_Harmonic_Amplitudes_Bessel.html); [Exploring Bessel Functions, allaboutcircuits.com](https://www.allaboutcircuits.com/technical-articles/exploring-bessel-functions-understanding-the-spectrum-of-tone-modulated-fm/).

---

### 13 — "Sync Sweep"

The screaming, sweeping timbre of analogue hard-sync, approximated as a
static spectral envelope (see honesty note below).

**Morph journey (t 0→1):** the sinc lobe's centre (and width) N sweeps
1→16 — at N=1 only h=1 survives strongly (near-sine); as N grows the lobe
widens and slides up, reading as the classic sync "sweep."

```cpp
case 13: // Sync Sweep — hard-sync spectral envelope, moving sinc lobe
{
   const float N = Lerp(1.0f, 16.0f, t); // sync ratio, slave:master
   const float x = ((float)h - N) / N;
   const float lobe = (fabsf(x) < 1e-6f) ? 1.0f : sinf((float)M_PI * x) / ((float)M_PI * x);
   return (1.0f / (float)h) * fabsf(lobe);
}
```

**Phase:** zero.

**Honesty note:** real hard sync is a multi-cycle, phase-reset process; a
single-cycle spectral table can only borrow its *envelope shape* (a sinc
lobe centred on the sync ratio), not the reset transient itself. Stated
explicitly per the format's inharmonic-approximation precedent (Bell).

**Closest existing:** Formant (3) — also a single moving peak, but Formant's
peak is Gaussian over a 1/√h floor and walks smoothly; Sync Sweep's envelope
is a sinc (with real zero-crossings, not just decay) and both its centre and
width move together, giving a much harsher, "buzzier" texture at low N.

**Citation:** sync spectrum envelope is a cardinal-sine (sinc) shape centred
at the sync-ratio harmonic — [Virtual Analog Oscillator Hard Synchronisation: Fourier Series Analysis, DAFx-12](https://dafx12.york.ac.uk/papers/dafx12_submission_37.pdf).

---

### 14 — "Drawbar"

Hammond-style additive organ tone; hollow two-stop registration to a full,
buzzy nine-drawbar sound.

**Morph journey (t 0→1):** each of the nine drawbar harmonics' weight
lerps from a sparse, hollow registration (only 16'+8', i.e. h=1,2) toward a
full-drawbar registration (all nine present, tapering gently with harmonic
number).

```cpp
case 14: // Drawbar — Hammond tonewheel harmonic set, hollow -> full registration
{
   static const int   kHarm[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
   static const float kLo[]   = { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.0f,  0.0f  };
   static const float kHi[]   = { 1.0f, 0.85f, 0.7f, 0.65f, 0.55f, 0.5f, 0.4f, 0.35f, 0.3f };
   for (int i = 0; i < 9; i++)
      if (kHarm[i] == h)
         return Lerp(kLo[i], kHi[i], t);
   return 0.0f; // every h other than the nine drawbar harmonics
}
```

**Phase:** zero — drawbars are simple additive sine stops.

**Closest existing:** Bell (6) — also a fixed short partial list — but Bell's
list is prime numbers chosen for metallic shimmer; Drawbar's list is the
actual physical Hammond tonewheel harmonic numbers (1,2,3,4,6,8,10,12,16),
targeting an organ, not a bell, and the journey is a registration crossfade
rather than a decay-shape sweep.

**Citation:** Hammond drawbar harmonic numbers (16′,8′,5⅓′,4′,2⅔′,2′,1⅗′,1⅓′,1′
= harmonics 1,2,3,4,6,8,10,12,16 of the fundamental) — [The Science of Hammond Organ Drawbar Registration, stefanv.com](https://www.stefanv.com/electronics/hammond_drawbar_science.html); [Synthesizing Tonewheel Organs, Sound on Sound](https://www.soundonsound.com/techniques/synthesizing-tonewheel-organs-part-1).

---

### 15 — "Reso Saw"

Casio-CZ-flavoured resonant sawtooth; a soft bump sharpening into a piercing
whistle riding on a full saw.

**Morph journey (t 0→1):** a fixed-position resonance (at h=16) narrows
from wide/soft (width 10) to narrow/piercing (width 1) while the saw floor
stays constant — the resonance "focuses," it doesn't move.

```cpp
case 15: // Reso Saw — CZ-style resonant sawtooth, resonance narrowing in place
{
   const float width = Lerp(10.0f, 1.0f, t); // wide/soft -> narrow/piercing
   const float peakH = 16.0f;                // fixed resonance position
   return SawAmp(h) + 2.5f * GaussPeak(h, peakH, width);
}
```

**Phase:** zero.

**Closest existing:** Formant (3) — also a Gaussian bump on a decaying floor —
but Formant's bump *moves* (position sweeps 1→34) at constant width over a
1/√h floor; Reso Saw's bump is pinned at h=16 and only *narrows*, over a full
1/h saw floor, which is the CZ "resonance" character (a filter-like peak
sharpening, not migrating).

**Citation:** CZ resonant sawtooth/triangle/trapezoid waveforms mimic a
band-pass filter via phase distortion, without an actual filter — [Casio CZ Series History, Perfect Circuit](https://www.perfectcircuit.com/signal/casio-cz-series); [Phase distortion synthesis, Wikipedia](https://en.wikipedia.org/wiki/Phase_distortion_synthesis).

---

### 16 — "Marimba"

Woody mallet-bar tone; bright/hard-mallet to pure/soft-mallet.

**Morph journey (t 0→1):** only three fixed partials (1, 4, 10) exist ever;
t sweeps from a hard-mallet spectrum (4th and 10th present at moderate level)
to a soft-mallet spectrum (nearly pure fundamental). This is a mallet-hardness
axis, not a time-decay — the table format has no room for genuine multi-cycle
decay.

```cpp
case 16: // Marimba — struck-bar modal ratios (1, 4, 10), hard -> soft mallet
{
   if (h == 1)  return 1.0f;
   if (h == 4)  return Lerp(0.55f, 0.15f, t);
   if (h == 10) return Lerp(0.35f, 0.05f, t);
   return 0.0f; // every h other than 1, 4, 10
}
```

**Phase:** zero.

**Closest existing:** Bell (6) — also a hand-picked partial list — but Bell's
14 partials are primes spread across the whole spectrum for a shimmering
metallic character; Marimba's 3 partials sit at the specific low-integer
ratios (1, 4, 10) that real bar tuners cut mallet-percussion overtones to,
giving a woody, thuddy result, and the morph axis (mallet hardness) differs
from Bell's (decay-shape).

**Citation:** struck-bar overtones are inharmonic in general but tuned bars
(marimba/vibraphone/xylophone) are cut so their 2nd partial lands near a
low-integer ratio of the fundamental (commonly cited near 4× for marimba,
~3× for xylophone) — [Timbre of Idiophones, music.drewpendergrass.com](http://music.drewpendergrass.com/IHT/idiophones); [Struck idiophone, Wikipedia](https://en.wikipedia.org/wiki/Struck_idiophone).

---

### 17 — "Tine EP"

Electric-piano (Rhodes/Wurlitzer-tine-style) tone; bright bark to mellow.

**Morph journey (t 0→1):** fundamental always present; the 2nd harmonic and
a bell-like 7th harmonic (the tine's characteristic high partial) both fade
from present to nearly silent — a brightness/tone-knob axis, again standing
in for the time-decay this format can't represent.

```cpp
case 17: // Tine EP — fundamental + 2nd + bell-like 7th, bright -> mellow
{
   if (h == 1) return 1.0f;
   if (h == 2) return Lerp(0.5f, 0.2f, t);
   if (h == 7) return Lerp(0.35f, 0.03f, t);
   return 0.0f; // every h other than 1, 2, 7
}
```

**Phase:** zero.

**Closest existing:** Sub (10) — also fundamental-dominant with h≤8 — but
Sub's journey *adds* body as t rises and never has content above h=8; Tine EP
*removes* brightness as t rises and keeps a specific 7th-harmonic bell
overtone throughout, which is the tine "bark" Sub has no equivalent of.

---

### 18 — "Prime Comb"

Metallic/inharmonic, growing progressively "grainier" rather than just
decaying differently.

**Morph journey (t 0→1):** the fundamental is always present; every
prime-numbered harmonic below a moving ceiling is also present. The ceiling
grows 5→97 as t rises, so the *set of partials itself* — not just their
decay shape — grows across the morph.

```cpp
case 18: // Prime Comb — prime-indexed partials, admission ceiling grows with t
{
   auto isPrime = [](int n) {
      if (n < 2) return false;
      for (int d = 2; (long long)d * d <= n; d++)
         if (n % d == 0) return false;
      return true;
   };
   if (h == 1) return 1.0f;
   const float ceiling = Lerp(5.0f, 97.0f, t);
   if ((float)h > ceiling || !isPrime(h))
      return 0.0f;
   return 1.0f / sqrtf((float)h);
}
```

**Phase:** zero.

**Closest existing:** Bell (6) — the only other prime-flavoured table — but
Bell's 14 partials are all present from t=0, only their decay exponent
changes; Prime Comb's partial *population* grows with t (few, sparse primes
at t=0 → dozens by t=1), and it uses the actual prime sequence (not a curated
14-entry list), giving a denser, more shimmering result at the top of the
sweep than Bell ever reaches.

**Citation:** primes as a harmonic/spectral selection concept — [Simple wave-optical superpositions as prime number sieves, arXiv:1812.04203](https://arxiv.org/abs/1812.04203); [Harmonic Representation of Prime Numbers, ResearchGate](https://www.researchgate.net/publication/362033974_Harmonic_Representation_of_Prime_Numbers).

---

### 19 — "Walsh Square"

Lo-fi/digital; a sparse, widely-clustered-pair buzz rather than a smooth
tapering spectrum.

**Morph journey (t 0→1):** the number of admitted Walsh-pair "steps" m
grows 1→8; each admitted pair sits far out in the spectrum (centred near
32, 96, 160, …), so the sweep adds increasingly distant buzzy detail rather
than brightening a continuous decay.

```cpp
case 19: // Walsh Square — Walsh/Hadamard square-wave pairs, more pairs admitted as t rises
{
   const int m = 1 + (int)roundf(Lerp(0.0f, 7.0f, t)); // 1..8 admitted pairs
   for (int j = 1; j <= m; j++)
   {
      const int center = 32 * (2 * j - 1);
      if (h == center - 1 || h == center + 1)
         return 1.0f / (float)(2 * j - 1);
   }
   return 0.0f; // every h not adjacent to an admitted Walsh pair centre
}
```

**Phase:** zero.

**Closest existing:** Comb (8) — also produces widely-separated spectral
features — but Comb is a *dense* saw (every harmonic present) with periodic
*nulls* punched in; Walsh Square is the opposite structure — almost every
harmonic is silent except the specific widely-separated pairs the
Walsh/Hadamard expansion of a square wave puts energy into.

**Citation:** truncated Walsh expansion of a sinusoid produces widely
separated nonzero harmonic pairs (e.g. 31st/33rd, 63rd/65th for 8 terms) —
[Walsh-Hadamard Transform, Mathworks](https://www.mathworks.com/help/signal/ug/walshhadamard-transform.html); search-summarised from Walsh-function synthesis literature.

---

### 20 — "Static"

Noisy/diffuse hiss with a spectral-tilt sweep, texturally distinct from
Drift's smooth two-anchor blend.

**Morph journey (t 0→1):** each of the 8 frames draws its *own* independent
random spectrum (seeded by frame index, not crossfaded from two fixed
anchors), while a spectral-tilt exponent sweeps 0.2→1.4 — bright/white-ish
hiss darkening toward pink/rumble as t rises.

```cpp
case 20: // Static — per-frame independent noise draw, white -> pink tilt
{
   const int frameIdx = (int)roundf(t * (float)(kFrames - 1));
   Lcg g(0x8B2A7C31u + (uint32_t)frameIdx * 2246822519u + (uint32_t)h * 3266489917u);
   const float p = Lerp(0.2f, 1.4f, t); // spectral tilt exponent
   return g.Unit() / powf((float)h, p);
}
```

**Phase:** scattered — same reasoning as Drift (9) and Glass (11): flattening
phase removes any pitched, impulsive click and reads as diffuse texture
rather than a buzzy tone.

**Closest existing:** Drift (9) — Drift crossfades between exactly *two*
fixed random spectra (only 2 independent draws total, replicated across all
8 frames via interpolation), giving a smooth, single-direction morph; Static
draws a fresh independent spectrum *per frame* (8 uncorrelated draws) and
layers an explicit tilt sweep on top, giving a rougher, broadband-hiss
character across the whole journey rather than one blend.

---

### 21 — "Vowel Path"

Five cardinal vowels (a-e-i-o-u), richer than the existing 2-point Vocal
diphthong.

**Morph journey (t 0→1):** the formant triplet steps through five real
vowel targets (a→e→i→o→u) via four equal-length linear segments, each a
distinct timbral landmark rather than one smooth glide between two points.

```cpp
case 21: // Vowel Path — five cardinal vowels a-e-i-o-u, real formant data at a 110 Hz voice
{
   struct V { float f1, f2, f3; };
   static const V kPath[] = {
      { 700.0f, 1220.0f, 2600.0f },  // a
      { 500.0f, 1700.0f, 2600.0f },  // e (interpolated within the a-i formant trend)
      { 300.0f, 2300.0f, 3000.0f },  // i
      { 450.0f,  800.0f, 2500.0f },  // o (interpolated within the a-u formant trend)
      { 300.0f,  600.0f, 2300.0f },  // u
   };
   const float f0 = 110.0f; // reference fundamental (low male voice)
   const float seg = t * 4.0f;
   const int i = std::min(3, (int)seg);
   const float f = seg - (float)i;
   const float F1 = Lerp(kPath[i].f1, kPath[i + 1].f1, f);
   const float F2 = Lerp(kPath[i].f2, kPath[i + 1].f2, f);
   const float F3 = Lerp(kPath[i].f3, kPath[i + 1].f3, f);
   const float body = 1.0f / (float)h;
   return body * (GaussPeak(h, F1 / f0, 2.0f) + 0.6f * GaussPeak(h, F2 / f0, 3.0f) +
                  0.3f * GaussPeak(h, F3 / f0, 4.0f));
}
```

**Phase:** zero.

**Closest existing:** Vocal (5) — same Gaussian-formant mechanism — but
Vocal is a single 2-point ah→ee glide; Vowel Path is a 5-point path through
distinct cited vowel targets, giving audibly distinguishable vowel landmarks
at several points across the sweep instead of one continuous slide.

**Citation:** a/i/u formant values from search-summarised phonetics
references — [Phonetics and Theory of Speech Production, Aalto](http://research.spa.aalto.fi/publications/theses/lemmetty_mst/chap3.html); [Acoustic Phonetics: Formants, University of Manitoba](https://home.cc.umanitoba.ca/~krussll/phonetics/acoustic/formants.html). e/o values are interpolated within the same front/back vowel-space trend, not independently cited — noted here rather than presented as sourced.

---

## Coverage after the addition

| Bucket | Before | After |
|---|---|---|
| bass/sub | Sub | Sub |
| analogue-adjacent | Basic Shapes, Pulse | + Reso Saw |
| bright/aggressive/digital | Harmonics, Saturate, Comb | + FM Bell, Sync Sweep, Walsh Square |
| hollow/woody/reed | Odd Only | + Reso Saw |
| vocal/formant | Formant, Vocal | + Drawbar, Vowel Path |
| metallic/inharmonic | Bell, Glass | + FM Bell, Prime Comb |
| plucked/struck | *(none)* | + Marimba, Tine EP |
| soft/pad | *(none dedicated)* | *(still none dedicated — see rejected ideas)* |
| noisy/diffuse | Drift | + Static |
| lo-fi/chiptune | *(none)* | + Walsh Square |

## Totals

- Table count: 22 (12 existing + 10 new)
- Bank size: 22 × 320 KB = **6,880 KB ≈ 6.9 MB**
- Estimated build time: 22 × ~8.3 ms/table ≈ **~180 ms**, linear extrapolation
  from the current ~100 ms / 12 tables. Comfortably under the ~32-table soft
  ceiling (22 of 32).

## Ideas considered and rejected

- **Supersaw-style detuned partials.** Needs fractional-harmonic detuning —
  not representable on an integer harmonic series. Ruled out by the format,
  not by taste.
- **Karplus-Strong-style plucked string decay.** Needs a genuine multi-cycle
  time evolution (string decay over seconds); a single 8-frame morph can only
  fake a decay-like *axis*, which is what Marimba/Tine EP already do — a
  dedicated pluck table would just be a third instance of the same trick.
- **Drum/membrane modes (clamped circular membrane, ratio ≈1.59 for the
  first overtone).** The real ratio is irrational and nowhere near an
  achievable low integer; rounding it to 2 misrepresents the instrument badly
  enough that it reads as generic rather than drum-like. Skipped rather than
  ship a dishonest approximation.
- **A second FM/Bessel table (bass growl, ratio 1:2 or 1:3).** Redundant
  math family right next to FM Bell; spent the budget on distinct mechanisms
  (Walsh, primes, modal) instead of a second flavor of the same one.
- **Pluck-position comb (harmonic envelope keyed to a moving pick position).**
  Too close to the existing Comb (8) table — both are "saw × moving notch
  pattern" — without a clearly distinct journey to justify a 320 KB separate
  entry.
- **Soft-clip/tanh harmonic series via Chebyshev-derived Fourier
  coefficients.** The closed-form harmonic series for a symmetrically
  clipped sine is a real derivation, but the resulting brightness-vs-drive
  journey lands too close to the existing Saturate (7) table's exponent
  sweep to justify a second entry.
- **Dedicated "soft/pad" table.** Every pad-like spectrum considered (soft
  low-pass-filtered saw, slow harmonic fade-in) either duplicated Harmonics
  (1) or Odd Only (2)'s existing journeys closely enough that it failed the
  "closest existing" test. Left as a real, acknowledged gap rather than
  padded out with a marginal entry.
