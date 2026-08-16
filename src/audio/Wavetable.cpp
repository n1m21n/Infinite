#include "Wavetable.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
   using namespace Wavetable;

   // ---------------------------------------------------------- table spectra
   // Each table is a function (frame -> harmonic -> amplitude), plus an
   // optional integer phase offset per harmonic. `t` is the frame's normalised
   // morph position, 0 at frame 0 and 1 at frame kFrames-1.
   //
   // Written from the standard Fourier series of each waveform (saw 1/h over
   // all h; square and triangle over odd h at 1/h and 1/h^2; pulse of duty d
   // at (2/(pi*h)) * sin(pi*h*d)) and from ordinary spectral shaping - moving
   // Gaussian peaks for the formant/vocal tables, exponent sweeps for the
   // brightness tables. No sampled or third-party table data is used anywhere
   // in this file.

   struct Lcg
   {
      uint32_t s;
      explicit Lcg(uint32_t seed) : s(seed ? seed : 1u) {}
      uint32_t Next() { s = s * 1664525u + 1013904223u; return s; }
      float Unit() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
   };

   float Lerp(float a, float b, float t) { return a + (b - a) * t; }

   // Amplitude of harmonic h (1-based) for the four base shapes.
   float SineAmp(int h) { return h == 1 ? 1.0f : 0.0f; }
   float TriAmp(int h) { return (h & 1) ? 1.0f / (float)(h * h) : 0.0f; }
   float SawAmp(int h) { return 1.0f / (float)h; }
   float SquareAmp(int h) { return (h & 1) ? 1.0f / (float)h : 0.0f; }

   float GaussPeak(int h, float center, float width)
   {
      const float d = ((float)h - center) / width;
      return expf(-0.5f * d * d);
   }

   // J_n(x) for integer n >= 0 via its power series. libc++ (this project's
   // standard library) does not implement std::cyl_bessel_j - the Mathematical
   // Special Functions TS is unimplemented there - so FM Bell needs its own.
   // The series converges factorially once m exceeds x/2, which is comfortably
   // true for the beta <= 9 this table ever calls it with; double accumulation
   // keeps the alternating sum from cancelling down to float noise.
   float BesselJ(int n, float x)
   {
      const double halfX = (double)x * 0.5;
      double term = 1.0;
      for (int i = 1; i <= n; i++)
         term *= halfX / (double)i;
      double sum = term;
      const double halfX2 = halfX * halfX;
      for (int m = 1; m <= 60; m++)
      {
         term *= -halfX2 / ((double)m * (double)(n + m));
         sum += term;
         if (fabs(term) < 1e-14 * fabs(sum) + 1e-30)
            break;
      }
      return (float)sum;
   }

   // Returns the amplitude of harmonic `h` in `table` at morph position `t`.
   float HarmonicAmp(int table, float t, int h)
   {
      switch (table)
      {
         case 0: // Basic Shapes - sine -> triangle -> saw -> square
         {
            const float seg = t * 3.0f;
            const int i = std::min(2, (int)seg);
            const float f = seg - (float)i;
            switch (i)
            {
               case 0: return Lerp(SineAmp(h), TriAmp(h), f);
               case 1: return Lerp(TriAmp(h), SawAmp(h), f);
               default: return Lerp(SawAmp(h), SquareAmp(h), f);
            }
         }

         case 1: // Harmonics - successive harmonics fade in, one octave of them per frame
         {
            const float reach = powf(2.0f, t * 8.0f); // 1 -> 256 harmonics
            if ((float)h <= reach)
               return 1.0f / (float)h;
            // Soft edge rather than a hard cutoff: a cliff in the spectrum
            // makes each frame's Gibbs ringing audible as the morph crosses it.
            const float over = (float)h / reach;
            return over < 2.0f ? (1.0f / (float)h) * (2.0f - over) : 0.0f;
         }

         case 2: // Odd Only - hollow, clarinet-ish; brightness sweeps with t
         {
            if (!(h & 1))
               return 0.0f;
            const float p = Lerp(2.2f, 0.6f, t);
            return powf((float)h, -p);
         }

         case 3: // Formant - one resonant peak walking up the series
         {
            const float center = Lerp(1.0f, 34.0f, t);
            return GaussPeak(h, center, 3.5f) / sqrtf((float)h);
         }

         case 4: // Pulse - duty cycle sweeping 50% -> 4%
         {
            const float duty = Lerp(0.5f, 0.04f, t);
            // Sign kept, not magnitude: the alternating sign is what makes the
            // narrow-duty frames read as a pulse rather than as a dull comb.
            return (2.0f / ((float)M_PI * (float)h)) * sinf((float)M_PI * (float)h * duty);
         }

         case 5: // Vocal - three formants sliding from an "ah" to an "ee"
         {
            const float f1 = Lerp(4.0f, 2.0f, t);
            const float f2 = Lerp(9.0f, 22.0f, t);
            const float f3 = Lerp(16.0f, 31.0f, t);
            const float body = 1.0f / (float)h;
            return body * (GaussPeak(h, f1, 2.0f) + 0.62f * GaussPeak(h, f2, 3.0f) +
                           0.34f * GaussPeak(h, f3, 4.0f));
         }

         case 6: // Bell - sparse, metallic partials thinning as t rises
         {
            static const int kPartials[] = { 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 41, 53 };
            for (int i = 0; i < (int)(sizeof(kPartials) / sizeof(kPartials[0])); i++)
            {
               if (kPartials[i] != h)
                  continue;
               const float decay = Lerp(1.6f, 0.55f, t);
               return powf(1.0f / (float)(i + 1), decay);
            }
            return 0.0f;
         }

         case 7: // Saturate - a saw whose high end lifts as if driven harder
         {
            const float p = Lerp(2.0f, 0.45f, t);
            return powf((float)h, -p);
         }

         case 8: // Comb - a moving notch pattern over a saw
         {
            const float notches = Lerp(1.0f, 9.0f, t);
            return (1.0f / (float)h) * fabsf(cosf((float)M_PI * (float)h * notches / 32.0f));
         }

         case 9: // Drift - two fixed pseudo-random spectra, crossfaded by t
         {
            Lcg a(0x51ED270Bu + (uint32_t)h * 2654435761u);
            Lcg b(0x1A2B3C4Du + (uint32_t)h * 40503u);
            const float ra = a.Unit(), rb = b.Unit();
            const float roll = 1.0f / powf((float)h, 1.15f);
            return Lerp(ra, rb, t) * roll;
         }

         case 10: // Sub - fundamental-dominant, gains a little body with t
         {
            if (h == 1)
               return 1.0f;
            if (h > 8)
               return 0.0f;
            return t * 0.42f / (float)(h * h);
         }

         case 11: // Glass - only square-numbered partials, very bright
         {
            const int r = (int)(sqrtf((float)h) + 0.5f);
            if (r * r != h)
               return 0.0f;
            return powf(1.0f / (float)r, Lerp(1.8f, 0.7f, t));
         }

         case 12: // FM Bell - Bessel-spectrum FM/PM, carrier:modulator = 1:7
         {
            constexpr int c = 1, m = 7;
            const float beta = Lerp(0.0f, 9.0f, t); // 0 = pure sine, 9 = dense clangor
            auto bessel = [beta](int n) -> float {
               const int k = std::abs(n);
               const float j = BesselJ(k, beta);
               return (n >= 0 || (k & 1) == 0) ? j : -j; // J(-n) = (-1)^n J(n)
            };
            float amp = 0.0f;
            for (int n = -10; n <= 10; n++)
            {
               const int pos = c + n * m;
               if (pos == h)       amp += bessel(n);
               else if (pos == -h) amp -= bessel(n); // negative-freq sideband folds, sign flips
            }
            return amp; // 0 for h not reachable as |c + n*m|, |n| <= 10
         }

         case 13: // Sync Sweep - hard-sync spectral envelope, moving sinc lobe
         {
            const float N = Lerp(1.0f, 16.0f, t); // sync ratio, slave:master
            const float x = ((float)h - N) / N;
            const float lobe = (fabsf(x) < 1e-6f) ? 1.0f : sinf((float)M_PI * x) / ((float)M_PI * x);
            return (1.0f / (float)h) * fabsf(lobe);
         }

         case 14: // Drawbar - Hammond tonewheel harmonic set, hollow -> full registration
         {
            static const int   kHarm[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
            static const float kLo[]   = { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.0f,  0.0f  };
            static const float kHi[]   = { 1.0f, 0.85f, 0.7f, 0.65f, 0.55f, 0.5f, 0.4f, 0.35f, 0.3f };
            for (int i = 0; i < 9; i++)
               if (kHarm[i] == h)
                  return Lerp(kLo[i], kHi[i], t);
            return 0.0f; // every h other than the nine drawbar harmonics
         }

         case 15: // Reso Saw - CZ-style resonant sawtooth, resonance narrowing in place
         {
            const float width = Lerp(10.0f, 1.0f, t); // wide/soft -> narrow/piercing
            const float peakH = 16.0f;                // fixed resonance position
            return SawAmp(h) + 2.5f * GaussPeak(h, peakH, width);
         }

         case 16: // Marimba - struck-bar modal ratios (1, 4, 10), hard -> soft mallet
         {
            if (h == 1)  return 1.0f;
            if (h == 4)  return Lerp(0.55f, 0.15f, t);
            if (h == 10) return Lerp(0.35f, 0.05f, t);
            return 0.0f; // every h other than 1, 4, 10
         }

         case 17: // Tine EP - fundamental + 2nd + bell-like 7th, bright -> mellow
         {
            if (h == 1) return 1.0f;
            if (h == 2) return Lerp(0.5f, 0.2f, t);
            if (h == 7) return Lerp(0.35f, 0.03f, t);
            return 0.0f; // every h other than 1, 2, 7
         }

         case 18: // Prime Comb - prime-indexed partials, admission ceiling grows with t
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

         case 19: // Walsh Square - Walsh/Hadamard square-wave pairs, more pairs admitted as t rises
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

         case 20: // Static - per-frame independent noise draw, white -> pink tilt
         {
            const int frameIdx = (int)roundf(t * (float)(kFrames - 1));
            Lcg g(0x8B2A7C31u + (uint32_t)frameIdx * 2246822519u + (uint32_t)h * 3266489917u);
            const float p = Lerp(0.2f, 1.4f, t); // spectral tilt exponent
            return g.Unit() / powf((float)h, p);
         }

         case 21: // Vowel Path - five cardinal vowels a-e-i-o-u, real formant data at a 110 Hz voice
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

         case 22: // Reso Tri - CZ resonant triangle with sweeping resonant formant peak
         {
            const float peak = Lerp(2.0f, 18.0f, t);
            const float width = Lerp(6.0f, 1.2f, t);
            return TriAmp(h) + 2.0f * GaussPeak(h, peak, width);
         }

         case 23: // Dirty Analog - vintage oscillator with sub-octave warmth & even harmonic leakage
         {
            const float sub = (h == 1) ? 1.0f : ((h == 2) ? Lerp(0.3f, 0.7f, t) : 0.0f);
            const float saw = SawAmp(h) * (1.0f + 0.35f * sinf(2.0f * (float)M_PI * (float)h * 0.125f * t));
            const float evenLeak = (!(h & 1)) ? (0.25f * t / (float)h) : 0.0f;
            return sub + saw + evenLeak;
         }

         case 24: // Multi Saw - detuned 3-saw spectral comb beating
         {
            const float detuneSpread = Lerp(0.02f, 0.28f, t);
            const float comb = fabsf(cosf((float)M_PI * (float)h * detuneSpread)) *
                               fabsf(cosf(0.5f * (float)M_PI * (float)h * detuneSpread));
            return SawAmp(h) * (0.4f + 0.6f * comb);
         }

         case 25: // Transistor Clip - overdriven diode clipping, progressive even/odd harmonic expansion
         {
            const float drive = Lerp(1.0f, 6.0f, t);
            const float p = Lerp(1.8f, 0.65f, t);
            const float odd = (h & 1) ? powf((float)h, -p) : 0.0f;
            const float even = (!(h & 1)) ? (0.35f * t * powf((float)h, -p - 0.4f)) : 0.0f;
            return (odd + even) / sqrtf(drive);
         }

         case 26: // Chebyshev - successive polynomial harmonic saturation modes (T1 -> T8)
         {
            const float order = Lerp(1.0f, 8.0f, t);
            const float dist = fabsf((float)h - order);
            if (dist < 3.0f)
               return expf(-0.8f * dist * dist) + 0.15f / (float)h;
            return 0.15f / (float)h;
         }

         case 27: // Golden Ratio - phi-spaced partial dispersion (phi = 1.618034)
         {
            static const int kPhiHarmonics[] = { 1, 2, 3, 4, 7, 11, 18, 29, 47, 76, 123, 199, 322 };
            for (int i = 0; i < (int)(sizeof(kPhiHarmonics) / sizeof(kPhiHarmonics[0])); i++)
            {
               if (kPhiHarmonics[i] == h)
                  return powf(1.0f / (float)(i + 1), Lerp(1.4f, 0.6f, t));
            }
            return 0.0f;
         }

         case 28: // Fibonacci - partials restricted to the Fibonacci sequence
         {
            static const int kFib[] = { 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377 };
            const float maxAllowed = Lerp(8.0f, 377.0f, t);
            for (int i = 0; i < (int)(sizeof(kFib) / sizeof(kFib[0])); i++)
            {
               if (kFib[i] == h && (float)h <= maxAllowed)
                  return 1.0f / sqrtf((float)h);
            }
            return 0.0f;
         }

         case 29: // Cathedral Pipe - pipe organ flue chiff & harmonic brilliance
         {
            static const int kPipeH[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
            static const float kSoft[]  = { 1.0f, 0.6f, 0.1f, 0.3f, 0.0f, 0.1f, 0.0f, 0.05f };
            static const float kTutti[] = { 1.0f, 0.85f, 0.7f, 0.75f, 0.5f, 0.6f, 0.4f, 0.45f };
            for (int i = 0; i < 8; i++)
            {
               if (kPipeH[i] == h)
                  return Lerp(kSoft[i], kTutti[i], t);
            }
            return 0.0f;
         }

         case 30: // Staircase - octave-quantized harmonic step blocks
         {
            int oct = 0;
            while ((1 << (oct + 1)) <= h && oct < 9) oct++;
            const float octAmp = powf(0.65f, (float)oct);
            const float stair = octAmp;
            const float smooth = 1.0f / (float)h;
            return Lerp(stair, smooth, t);
         }

         case 31: // Dual Octave - stacked octave partials 1, 2, 4, 8, 16, 32, 64, 128, 256
         {
            if ((h & (h - 1)) != 0)
               return 0.0f;
            int p = 0;
            int v = h;
            while (v > 1) { v >>= 1; p++; }
            const float decay = Lerp(1.5f, 0.4f, t);
            return powf(0.5f, (float)p * decay);
         }

         case 32: // FM Clang - FM Bessel carrier:modulator = 1:3.5
         {
            constexpr int c = 2, m = 7;
            const float beta = Lerp(0.5f, 7.5f, t);
            auto bessel = [beta](int n) -> float {
               const int k = std::abs(n);
               const float j = BesselJ(k, beta);
               return (n >= 0 || (k & 1) == 0) ? j : -j;
            };
            float amp = 0.0f;
            for (int n = -8; n <= 8; n++)
            {
               const int pos2 = c + n * m;
               const int h_cand = std::abs(pos2) / 2;
               if ((std::abs(pos2) % 2 == 0) && h_cand == h && h > 0)
                  amp += (pos2 >= 0) ? bessel(n) : -bessel(n);
            }
            return fabsf(amp) + (h == 1 ? 0.2f : 0.0f);
         }

         case 33: // FM Metal - inharmonic FM with irrational ratio ~1:1.414
         {
            const float beta = Lerp(0.8f, 6.0f, t);
            float amp = 0.0f;
            for (int n = -8; n <= 8; n++)
            {
               const float fCenter = fabsf(1.0f + (float)n * 1.41421356f);
               const int k = std::abs(n);
               const float j = BesselJ(k, beta);
               amp += fabsf(j) * GaussPeak(h, fCenter, 0.85f);
            }
            return amp;
         }

         case 34: // FM Sweep - 1:1 FM index beta 0 -> 10 sweep
         {
            const float beta = Lerp(0.0f, 10.0f, t);
            if (beta < 1e-4f)
               return (h == 1) ? 1.0f : 0.0f;
            const float j1 = BesselJ(h - 1, beta);
            const float j2 = ((h + 1) & 1) ? -BesselJ(h + 1, beta) : BesselJ(h + 1, beta);
            return fabsf(j1 - j2);
         }

         case 35: // Modern Squelch - aggressive digital resonance peak over odd harmonics
         {
            const float center = Lerp(2.0f, 28.0f, t);
            const float oddBase = (h & 1) ? (1.0f / (float)h) : 0.0f;
            const float peak = 3.0f * GaussPeak(h, center, 1.8f);
            return oddBase + peak;
         }

         case 36: // Bitcrush Morph - harmonic envelope of amplitude quantization
         {
            const float bits = Lerp(8.0f, 1.5f, t);
            const float aliasDecay = Lerp(2.0f, 0.5f, t);
            const float base = 1.0f / powf((float)h, aliasDecay);
            const float mirrorLobe = fabsf(sinf((float)M_PI * (float)h / bits));
            return base * (0.4f + 0.6f * mirrorLobe);
         }

         case 37: // Phase Fold - wavefolded cosine series
         {
            const float drive = Lerp(1.0f, 8.0f, t);
            return (1.0f / sqrtf((float)h)) * GaussPeak(h, drive * 2.2f, drive * 1.5f) + 0.1f / (float)h;
         }

         case 38: // Mirror Peak - dual symmetric spectral lobes moving toward each other
         {
            const float c1 = Lerp(2.0f, 20.0f, t);
            const float c2 = Lerp(40.0f, 22.0f, t);
            return (1.0f / sqrtf((float)h)) * (GaussPeak(h, c1, 2.5f) + GaussPeak(h, c2, 2.5f) + 0.1f);
         }

         case 39: // Sync Phase - slave hard-sync phase reset harmonics
         {
            const float syncRatio = Lerp(1.5f, 9.5f, t);
            const float sincArg = (float)h / syncRatio;
            const float sinc = (sincArg < 1e-4f) ? 1.0f : fabsf(sinf((float)M_PI * sincArg) / ((float)M_PI * sincArg));
            return (1.0f / (float)h) * sinc;
         }

         case 40: // Ring Mod - sum/diff sidebands around carrier
         {
            const float fMod = Lerp(2.2f, 11.8f, t);
            return GaussPeak(h, fabsf(1.0f - fMod), 1.2f) + GaussPeak(h, 1.0f + fMod, 1.2f) + 0.05f / (float)h;
         }

         case 41: // Digital Crunch - PPG-style quantized harmonic steps & high-frequency mirror grit
         {
            const int stepH = (h / 4) * 4 + 1;
            const float p = Lerp(1.8f, 0.5f, t);
            return (h == stepH || h == stepH + 1) ? powf((float)h, -p) : (0.15f * powf((float)h, -p));
         }

         case 42: // Modern Talk - iconic Nord G2 / Massive "Yoi-Yaa" vocal formant trajectory
         {
            struct V { float f1, f2, f3; };
            static const V kPath[] = {
               { 320.0f,  920.0f, 2400.0f }, // "Y"
               { 450.0f,  750.0f, 2200.0f }, // "o"
               { 300.0f, 2200.0f, 3000.0f }, // "i"
               { 820.0f, 1400.0f, 2500.0f }, // "Yaa"
            };
            const float f0 = 130.0f;
            const float seg = t * 3.0f;
            const int i = std::min(2, (int)seg);
            const float f = seg - (float)i;
            const float F1 = Lerp(kPath[i].f1, kPath[i + 1].f1, f);
            const float F2 = Lerp(kPath[i].f2, kPath[i + 1].f2, f);
            const float F3 = Lerp(kPath[i].f3, kPath[i + 1].f3, f);
            const float body = 1.0f / (float)h;
            return body * (GaussPeak(h, F1 / f0, 1.6f) * 1.2f +
                           GaussPeak(h, F2 / f0, 2.2f) * 0.9f +
                           GaussPeak(h, F3 / f0, 3.0f) * 0.5f);
         }

         case 43: // Choir - multi-voice choir formant blend (Ooh -> Aah)
         {
            const float F1 = Lerp(320.0f, 750.0f, t);
            const float F2 = Lerp(850.0f, 1300.0f, t);
            const float F3 = Lerp(2400.0f, 2700.0f, t);
            const float f0 = 120.0f;
            const float body = 1.0f / powf((float)h, 0.85f);
            return body * (GaussPeak(h, F1 / f0, 2.5f) + 0.7f * GaussPeak(h, F2 / f0, 3.5f) +
                           0.4f * GaussPeak(h, F3 / f0, 4.5f));
         }

         case 44: // Monster Growl - deep throat sub-formant & rasp peak
         {
            const float subPeak = Lerp(1.0f, 3.0f, t);
            const float raspPeak = Lerp(12.0f, 28.0f, t);
            return (1.0f / sqrtf((float)h)) * (GaussPeak(h, subPeak, 1.2f) + 0.8f * GaussPeak(h, raspPeak, 3.5f) + 0.1f / (float)h);
         }

         case 45: // Nasal Throat - sharp dual-pole vocal tract resonance
         {
            const float pole1 = Lerp(3.0f, 7.0f, t);
            const float pole2 = Lerp(14.0f, 24.0f, t);
            return (1.0f / (float)h) * (2.0f * GaussPeak(h, pole1, 0.9f) + 1.5f * GaussPeak(h, pole2, 1.4f));
         }

         case 46: // Whisper Formant - diffuse airy vowel formant envelope
         {
            const float center = Lerp(8.0f, 32.0f, t);
            return (1.0f / (float)h) * (GaussPeak(h, center, 6.0f) + GaussPeak(h, center * 1.8f, 8.0f));
         }

         case 47: // Soprano Vowels - high register female vocal formants
         {
            const float F1 = Lerp(450.0f, 880.0f, t);
            const float F2 = Lerp(1800.0f, 2400.0f, t);
            const float F3 = Lerp(3000.0f, 3800.0f, t);
            const float f0 = 240.0f;
            return (1.0f / (float)h) * (GaussPeak(h, F1 / f0, 1.8f) + 0.75f * GaussPeak(h, F2 / f0, 2.5f) +
                                        0.5f * GaussPeak(h, F3 / f0, 3.5f));
         }

         case 48: // Talk Box Wah - mouth opening dynamic sweep (closed "oo" -> open "wah")
         {
            const float mouthOpen = Lerp(2.0f, 16.0f, t);
            const float q = Lerp(1.2f, 3.5f, t);
            const float throat = (h & 1) ? (1.0f / (float)h) : (0.3f / (float)h);
            return throat * (1.0f + q * GaussPeak(h, mouthOpen, 1.8f));
         }

         case 49: // Vibraphone - struck aluminum bar modal series (1, 3.98, 9.25)
         {
            if (h == 1) return 1.0f;
            if (h == 4) return Lerp(0.6f, 0.2f, t);
            if (h == 9) return Lerp(0.35f, 0.05f, t);
            if (h == 16) return Lerp(0.15f, 0.0f, t);
            return 0.0f;
         }

         case 50: // Plucked String - modal string harmonics, pluck brightness decay
         {
            const float decay = Lerp(0.8f, 2.2f, t);
            return powf((float)h, -decay);
         }

         case 51: // Church Chime - tubular bell modal series (1, 2.76, 5.40, 8.93, 13.34)
         {
            static const float kModes[] = { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f };
            float amp = 0.0f;
            for (int i = 0; i < 6; i++)
            {
               const float modeDecay = powf(0.7f, (float)i * Lerp(1.5f, 0.6f, t));
               amp += modeDecay * GaussPeak(h, kModes[i], 0.75f);
            }
            return amp;
         }

         case 52: // Acoustic Reed - woodwind reed odd/even balance sweep
         {
            const float odd = (h & 1) ? (1.0f / (float)h) : 0.0f;
            const float even = (!(h & 1)) ? (Lerp(0.05f, 0.7f, t) / (float)h) : 0.0f;
            return odd + even;
         }

         case 53: // Brass Lead - lip reed brass shockwave non-linear harmonic buildup
         {
            const float p = Lerp(2.2f, 0.85f, t);
            return powf((float)h, -p);
         }

         case 54: // Slap Bass - deep fundamental + string slap click peak at harmonics 16..24
         {
            const float body = (h == 1) ? 1.0f : ((h <= 4) ? 0.5f / (float)h : 0.15f / (float)h);
            const float click = Lerp(0.1f, 1.8f, t) * GaussPeak(h, 18.0f, 4.0f);
            return body + click;
         }

         case 55: // Kalimba - thumb piano metal tines (modes: 1, 6.25, 17.5) + body
         {
            if (h == 1) return 1.0f;
            if (h == 6) return Lerp(0.45f, 0.1f, t);
            if (h == 18) return Lerp(0.25f, 0.02f, t);
            return 0.02f / (float)h;
         }

         case 56: // Cosmic Drone - dense subharmonic cluster drone
         {
            const float c1 = Lerp(1.0f, 6.0f, t);
            const float c2 = Lerp(4.0f, 14.0f, t);
            return (1.0f / sqrtf((float)h)) * (GaussPeak(h, c1, 1.5f) + 0.6f * GaussPeak(h, c2, 2.5f) + 0.1f);
         }

         case 57: // Glitch Shard - sparse randomized prime harmonic spikes
         {
            Lcg g(0x738A4B1Du + (uint32_t)h * 1337u + (uint32_t)(t * 7.0f) * 65537u);
            const float rnd = g.Unit();
            return (rnd > 0.75f) ? (rnd / sqrtf((float)h)) : (0.02f / (float)h);
         }

         case 58: // Granular Sieve - Gaussian windowed spectral sieve roving across series
         {
            const float pos = Lerp(2.0f, 48.0f, t);
            const float width = Lerp(3.0f, 12.0f, t);
            return (1.0f / sqrtf((float)h)) * GaussPeak(h, pos, width);
         }

         case 59: // Void Abyss - ultra-dark spectral falloff with moving sub rumble
         {
            const float p = Lerp(2.8f, 1.6f, t);
            const float subPeak = GaussPeak(h, Lerp(1.0f, 4.0f, t), 1.0f);
            return powf((float)h, -p) + 0.5f * subPeak;
         }

         case 60: // High Glass Reso - resonant crystal ring mode series
         {
            const float ringH = Lerp(12.0f, 48.0f, t);
            return (1.0f / (float)h) * (0.1f + 3.0f * GaussPeak(h, ringH, 1.2f));
         }

         case 61: // Overdrive Saw - multi-stage waveshaped saw harmonic spread
         {
            const float p = Lerp(1.6f, 0.4f, t);
            return (1.0f / powf((float)h, p)) * (1.0f + 0.3f * cosf((float)M_PI * (float)h * 0.25f));
         }

         case 62: // Shimmer Cloud - high octave ethereal cluster
         {
            const float center = Lerp(16.0f, 64.0f, t);
            return (1.0f / sqrtf((float)h)) * (GaussPeak(h, center, 8.0f) + 0.5f * GaussPeak(h, center * 2.0f, 12.0f) + 0.05f);
         }

         case 63: // Nebula - multi-phase evolving ambient texture
         {
            const float c1 = Lerp(3.0f, 18.0f, t);
            const float c2 = Lerp(12.0f, 36.0f, t);
            const float c3 = Lerp(24.0f, 72.0f, t);
            return (1.0f / sqrtf((float)h)) * (GaussPeak(h, c1, 2.0f) + 0.7f * GaussPeak(h, c2, 3.5f) + 0.4f * GaussPeak(h, c3, 6.0f));
         }

         default: return 0.0f; // bad index -> silence, never a stored patch's saved table
      }
   }

   // Integer phase offset (in sine-table steps) for harmonic h.
   int HarmonicPhase(int table, int h)
   {
      if (table != 9 && table != 11 && table != 20 &&
          table != 46 && table != 56 && table != 57 && table != 62 && table != 63)
         return 0;
      Lcg g(0x9E3779B9u + (uint32_t)(table * 7919 + h));
      return (int)(g.Unit() * (float)kFrameSize) & (kFrameSize - 1);
   }

   const char* const kTableNames[kNumTables] = {
      // 0..21 (Existing - preserved indices & names)
      "Basic Shapes", "Harmonics",     "Odd Only",       "Formant",        "Pulse",
      "Vocal",        "Bell",          "Saturate",       "Comb",           "Drift",
      "Sub",          "Glass",         "FM Bell",        "Sync Sweep",     "Drawbar",
      "Reso Saw",     "Marimba",       "Tine EP",        "Prime Comb",     "Walsh Square",
      "Static",       "Vowel Path",

      // 22..63 (New categorized tables)
      "Reso Tri",     "Dirty Analog",  "Multi Saw",      "Transistor Clip",
      "Chebyshev",    "Golden Ratio",  "Fibonacci",      "Cathedral Pipe", "Staircase",      "Dual Octave",
      "FM Clang",     "FM Metal",      "FM Sweep",       "Modern Squelch", "Bitcrush Morph", "Phase Fold",
      "Mirror Peak",  "Sync Phase",    "Ring Mod",       "Digital Crunch",
      "Modern Talk",  "Choir",         "Monster Growl",  "Nasal Throat",   "Whisper Formant","Soprano Vowels", "Talk Box Wah",
      "Vibraphone",   "Plucked String","Church Chime",   "Acoustic Reed",  "Brass Lead",     "Slap Bass",      "Kalimba",
      "Cosmic Drone", "Glitch Shard",  "Granular Sieve", "Void Abyss",     "High Glass Reso","Overdrive Saw",  "Shimmer Cloud",
      "Nebula",
   };

   const Category kTableCategories[kNumTables] = {
      // 0..21
      Category::Analog,    // 0: Basic Shapes
      Category::Harmonic,  // 1: Harmonics
      Category::Harmonic,  // 2: Odd Only
      Category::Vocal,     // 3: Formant
      Category::Analog,    // 4: Pulse
      Category::Vocal,     // 5: Vocal
      Category::Acoustic,  // 6: Bell
      Category::Analog,    // 7: Saturate
      Category::Spectral,  // 8: Comb
      Category::Spectral,  // 9: Drift
      Category::Analog,    // 10: Sub
      Category::Harmonic,  // 11: Glass
      Category::Digital,   // 12: FM Bell
      Category::Analog,    // 13: Sync Sweep
      Category::Harmonic,  // 14: Drawbar
      Category::Analog,    // 15: Reso Saw
      Category::Acoustic,  // 16: Marimba
      Category::Acoustic,  // 17: Tine EP
      Category::Harmonic,  // 18: Prime Comb
      Category::Digital,   // 19: Walsh Square
      Category::Spectral,  // 20: Static
      Category::Vocal,     // 21: Vowel Path

      // 22..25: Analog
      Category::Analog,    // 22: Reso Tri
      Category::Analog,    // 23: Dirty Analog
      Category::Analog,    // 24: Multi Saw
      Category::Analog,    // 25: Transistor Clip

      // 26..31: Harmonic
      Category::Harmonic,  // 26: Chebyshev
      Category::Harmonic,  // 27: Golden Ratio
      Category::Harmonic,  // 28: Fibonacci
      Category::Harmonic,  // 29: Cathedral Pipe
      Category::Harmonic,  // 30: Staircase
      Category::Harmonic,  // 31: Dual Octave

      // 32..41: Digital
      Category::Digital,   // 32: FM Clang
      Category::Digital,   // 33: FM Metal
      Category::Digital,   // 34: FM Sweep
      Category::Digital,   // 35: Modern Squelch
      Category::Digital,   // 36: Bitcrush Morph
      Category::Digital,   // 37: Phase Fold
      Category::Digital,   // 38: Mirror Peak
      Category::Digital,   // 39: Sync Phase
      Category::Digital,   // 40: Ring Mod
      Category::Digital,   // 41: Digital Crunch

      // 42..48: Vocal
      Category::Vocal,     // 42: Modern Talk
      Category::Vocal,     // 43: Choir
      Category::Vocal,     // 44: Monster Growl
      Category::Vocal,     // 45: Nasal Throat
      Category::Vocal,     // 46: Whisper Formant
      Category::Vocal,     // 47: Soprano Vowels
      Category::Vocal,     // 48: Talk Box Wah

      // 49..55: Acoustic
      Category::Acoustic,  // 49: Vibraphone
      Category::Acoustic,  // 50: Plucked String
      Category::Acoustic,  // 51: Church Chime
      Category::Acoustic,  // 52: Acoustic Reed
      Category::Acoustic,  // 53: Brass Lead
      Category::Acoustic,  // 54: Slap Bass
      Category::Acoustic,  // 55: Kalimba

      // 56..63: Spectral
      Category::Spectral,  // 56: Cosmic Drone
      Category::Spectral,  // 57: Glitch Shard
      Category::Spectral,  // 58: Granular Sieve
      Category::Spectral,  // 59: Void Abyss
      Category::Spectral,  // 60: High Glass Reso
      Category::Spectral,  // 61: Overdrive Saw
      Category::Spectral,  // 62: Shimmer Cloud
      Category::Spectral,  // 63: Nebula
   };

   const char* const kCategoryNames[(int)Category::Count] = {
      "Analog",
      "Harmonic",
      "Digital",
      "Vocal",
      "Acoustic",
      "Spectral"
   };

   // ------------------------------------------------------------- the bank
   struct Bank
   {
      // [table][frame][mip] -> kFrameSize floats, flattened.
      std::vector<float> data;
      bool built = false;

      size_t Offset(int table, int frame, int mip) const
      {
         return ((size_t)table * kFrames * kMipLevels + (size_t)frame * kMipLevels + (size_t)mip) *
                kFrameSize;
      }
   };

   Bank& TheBank()
   {
      static Bank bank;
      return bank;
   }

   void BuildBank(Bank& bank)
   {
      // One sine period, sampled at the frame size. Harmonic h at sample i is
      // then sine[(h * i) & mask] exactly - no sinf in the inner loop, and no
      // phase error accumulating across 512 harmonics.
      float sine[kFrameSize];
      for (int i = 0; i < kFrameSize; i++)
         sine[i] = sinf(2.0f * (float)M_PI * (float)i / (float)kFrameSize);

      bank.data.assign((size_t)kNumTables * kFrames * kMipLevels * kFrameSize, 0.0f);

      // Harmonic ceilings per mip, coarsest last: level kMipLevels-1 holds
      // just the fundamental, level 0 the full 512.
      int ceilings[kMipLevels];
      for (int L = 0; L < kMipLevels; L++)
         ceilings[L] = kMaxHarmonic >> L;

      std::vector<float> accum(kFrameSize);
      for (int table = 0; table < kNumTables; table++)
      {
         for (int frame = 0; frame < kFrames; frame++)
         {
            const float t = kFrames > 1 ? (float)frame / (float)(kFrames - 1) : 0.0f;

            // Build coarsest-first and keep accumulating: each harmonic is
            // synthesised exactly once across the whole pyramid (512 * 1024
            // multiply-adds per frame) rather than once per level, which is
            // the difference between a ~100 ms build and a multi-second one.
            std::fill(accum.begin(), accum.end(), 0.0f);
            int nextHarmonic = 1;
            for (int L = kMipLevels - 1; L >= 0; L--)
            {
               for (int h = nextHarmonic; h <= ceilings[L]; h++)
               {
                  const float amp = HarmonicAmp(table, t, h);
                  if (amp == 0.0f)
                     continue;
                  const int ph = HarmonicPhase(table, h);
                  for (int i = 0; i < kFrameSize; i++)
                     accum[i] += amp * sine[(h * i + ph) & (kFrameSize - 1)];
               }
               nextHarmonic = ceilings[L] + 1;
               std::copy(accum.begin(), accum.end(), bank.data.begin() + bank.Offset(table, frame, L));
            }

            // Normalise every mip of this frame by the *full-bandwidth* peak,
            // not its own: normalising each level separately would make the
            // oscillator jump in level as pitch crosses a mip boundary.
            float peak = 0.0f;
            for (int i = 0; i < kFrameSize; i++)
               peak = std::max(peak, fabsf(accum[i]));
            const float norm = peak > 1e-6f ? 0.98f / peak : 0.0f;
            for (int L = 0; L < kMipLevels; L++)
            {
               float* dst = bank.data.data() + bank.Offset(table, frame, L);
               for (int i = 0; i < kFrameSize; i++)
                  dst[i] *= norm;
            }
         }
      }

      bank.built = true;
   }
}

namespace Wavetable
{
   void EnsureBuilt()
   {
      Bank& bank = TheBank();
      if (!bank.built)
         BuildBank(bank);
   }

   int NumTables() { return kNumTables; }

   const char* TableName(int table)
   {
      return kTableNames[std::clamp(table, 0, kNumTables - 1)];
   }

   Category TableCategory(int table)
   {
      return kTableCategories[std::clamp(table, 0, kNumTables - 1)];
   }

   const char* CategoryName(Category cat)
   {
      const int idx = std::clamp((int)cat, 0, (int)Category::Count - 1);
      return kCategoryNames[idx];
   }

   const char* TableCategoryName(int table)
   {
      return CategoryName(TableCategory(table));
   }

   const float* Frame(int table, int frame, int mip)
   {
      // Deliberately does NOT call EnsureBuilt: this runs on the audio thread
      // and the build allocates. WavetableNode's constructor builds the bank
      // on the main thread before any AudioNode of its can exist; the silence
      // fallback below covers the impossible case rather than reading a null.
      Bank& bank = TheBank();
      if (!bank.built)
      {
         static const float kSilence[kFrameSize] = {};
         return kSilence;
      }
      return bank.data.data() + bank.Offset(std::clamp(table, 0, kNumTables - 1),
                                            std::clamp(frame, 0, kFrames - 1),
                                            std::clamp(mip, 0, kMipLevels - 1));
   }
}

