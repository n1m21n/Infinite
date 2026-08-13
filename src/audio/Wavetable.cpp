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

         default: // 11 Glass - only square-numbered partials, very bright
         {
            const int r = (int)(sqrtf((float)h) + 0.5f);
            if (r * r != h)
               return 0.0f;
            return powf(1.0f / (float)r, Lerp(1.8f, 0.7f, t));
         }
      }
   }

   // Integer phase offset (in sine-table steps) for harmonic h. Zero phase
   // gives the textbook waveform shapes; the two "scattered" tables randomise
   // it, which flattens their peak amplitude and gives them a diffuse,
   // non-impulsive character no amount of amplitude shaping reproduces.
   int HarmonicPhase(int table, int h)
   {
      if (table != 9 && table != 11)
         return 0;
      Lcg g(0x9E3779B9u + (uint32_t)(table * 7919 + h));
      return (int)(g.Unit() * (float)kFrameSize) & (kFrameSize - 1);
   }

   const char* const kTableNames[kNumTables] = {
      "Basic Shapes", "Harmonics", "Odd Only", "Formant", "Pulse",   "Vocal",
      "Bell",         "Saturate",  "Comb",     "Drift",   "Sub",     "Glass",
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
