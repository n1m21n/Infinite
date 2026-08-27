#pragma once

#include <cmath>
#include <cstdint>

// Header-only DSP building blocks shared by every audio node. Each piece is
// implemented directly from its primary reference (cited per-function below)
// rather than copied from any existing synth engine's source. Everything
// here is audio-thread-safe: no allocation, no locks, no syscalls - plain
// arithmetic and state structs only.
namespace DspMath
{
   // ---------------------------------------------------------------- OnePole
   // Exponential one-pole smoother: current moves toward target by a fixed
   // fraction each sample, coeff = exp(-1 / (timeConstantSec * sampleRate)).
   // This is the exact formula ParamMailbox::SmoothedValue used inline
   // before this header existed; factored out here so there's one copy.
   struct OnePole
   {
      float current = 0.0f;
      float coeff = 0.0f; // set via SetTimeConstant

      void SetTimeConstant(float timeConstantSec, double sampleRate)
      {
         coeff = (timeConstantSec > 0.0f && sampleRate > 0.0)
            ? expf(-1.0f / (timeConstantSec * (float)sampleRate))
            : 0.0f;
      }

      void SetImmediate(float value) { current = value; }

      float Process(float target)
      {
         current = target + (current - target) * coeff;
         return current;
      }
   };

   // ------------------------------------------------------------- dB <-> lin
   inline float LinearToDb(float lin)
   {
      return 20.0f * log10f(lin > 1e-9f ? lin : 1e-9f);
   }

   inline float DbToLinear(float db)
   {
      return powf(10.0f, db / 20.0f);
   }

   // ------------------------------------------------------------- Equal-power pan
   // Quarter-sine-wave law: pan in [-1, 1], 0 = center. Constant total power
   // across the pan range (unlike a linear crossfade).
   inline void EqualPowerPan(float pan, float& leftGain, float& rightGain)
   {
      const float clamped = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
      const float angle = (clamped + 1.0f) * (float)M_PI / 4.0f;
      leftGain = cosf(angle);
      rightGain = sinf(angle);
   }

   // -------------------------------------------------------- FlushDenormal
   // Flushes denormal / subnormal floating point numbers to zero to prevent
   // CPU performance stalls in recursive filter & delay feedback loops.
   inline float FlushDenormal(float x)
   {
      return std::fabs(x) < 1.0e-20f ? 0.0f : x;
   }

   // ------------------------------------------------------------- Fast tanh
   // Pade-style rational approximation, x*(27+x*x)/(27+9*x*x), clamped
   // outside +/-3 where the rational form stops tracking tanh (saturates to
   // +/-1 instead, which is the correct limit anyway). Common cheap
   // saturator approximation; max error versus std::tanh is small enough for
   // audio-rate soft clipping and much cheaper than the libm transcendental.
   inline float FastTanh(float x)
   {
      if (x < -3.0f)
         return -1.0f;
      if (x > 3.0f)
         return 1.0f;
      const float x2 = x * x;
      return x * (27.0f + x2) / (27.0f + 9.0f * x2);
   }

   // --------------------------------------------------------------- PolyBLEP
   // Band-limited sawtooth/square/sine/triangle via a phase-accumulator
   // oscillator with polynomial band-limited step (PolyBLEP) correction
   // applied at discontinuities, per Valimaki & Huovilainen's band-limited
   // oscillator algorithms (the standard PolyBLEP formulation used to
   // suppress aliasing at a waveform's hard edges without oversampling).
   //
   // Generation is split from advancing the phase accumulator (`Generate`
   // reads an explicit phase and has no side effects; `Advance` is the only
   // thing that moves `phase` forward) so a caller doing phase modulation -
   // Oscillator's 2-op FM section - can read the waveform at
   // `phase + instantaneous offset` without that offset ever leaking into
   // the persistent accumulator (which would detune the oscillator instead
   // of modulating it). Call `Generate` then `Advance` exactly once per
   // sample; `Advance` always uses the true, unoffset phase.
   enum Waveform
   {
      kWaveSine = 0,
      kWaveTriangle,
      kWaveSaw,
      kWaveSquare,
      kWavePulse,
   };

   struct PolyBlepOsc
   {
      double phase = 0.0;    // 0..1
      double phaseInc = 0.0; // cycles per sample

      void SetFrequency(double hz, double sampleRate)
      {
         phaseInc = (sampleRate > 0.0) ? hz / sampleRate : 0.0;
      }

      // Reads `atPhase` (normally `phase`, or `phase` plus a momentary
      // modulation offset) rather than the member directly - see the class
      // comment. `pulseWidth` only matters for kWaveSquare/kWavePulse.
      float Generate(int waveform, float pulseWidth, double atPhase) const
      {
         atPhase -= floor(atPhase); // wrap into [0, 1) - floor handles negative offsets too
         switch (waveform)
         {
            case kWaveSine:
               return (float)sin(2.0 * M_PI * atPhase);

            case kWaveTriangle:
               // Naive (non-BLEP) triangle: its harmonics already roll off
               // at -12 dB/octave, so residual aliasing is far less audible
               // than on saw/square - not worth a second integrator state
               // to correct, especially once phase can be offset per-sample
               // by FM (an integrator's running state can't be "read at an
               // offset" the way this stateless formula can).
               return 2.0f * (float)fabs(2.0 * (atPhase - floor(atPhase + 0.5))) - 1.0f;

            case kWaveSaw:
            {
               float out = (float)(2.0 * atPhase - 1.0);
               out -= BlepCorrection(atPhase, phaseInc);
               return out;
            }

            case kWaveSquare:
            case kWavePulse:
            default:
            {
               const double pw = pulseWidth < 0.01 ? 0.01 : (pulseWidth > 0.99 ? 0.99 : pulseWidth);
               float out = atPhase < pw ? 1.0f : -1.0f;
               out += BlepCorrection(atPhase, phaseInc);
               double shifted = atPhase - pw;
               if (shifted < 0.0)
                  shifted += 1.0;
               out -= BlepCorrection(shifted, phaseInc);
               return out;
            }
         }
      }

      float Generate(int waveform, float pulseWidth = 0.5f) const
      {
         return Generate(waveform, pulseWidth, phase);
      }

      void Advance()
      {
         phase += phaseInc;
         if (phase >= 1.0)
            phase -= floor(phase);
         else if (phase < 0.0)
            phase -= floor(phase);
      }

   private:
      // PolyBLEP: 2nd-order polynomial approximation of the band-limited
      // step, applied within one sample (dt = phaseInc) of a discontinuity.
      static float BlepCorrection(double t, double dt)
      {
         if (dt <= 0.0)
            return 0.0f;
         if (t < dt)
         {
            const double x = t / dt;
            return (float)(x + x - x * x - 1.0);
         }
         if (t > 1.0 - dt)
         {
            const double x = (t - 1.0) / dt;
            return (float)(x * x + x + x + 1.0);
         }
         return 0.0f;
      }
   };

   // ------------------------------------------------------------- Noise
   // Deterministic (seeded), allocation-free, audio-thread-safe generators.
   //
   // White: xorshift32 (Marsaglia, "Xorshift RNGs", 2003 - a standard
   // public-domain fast PRNG), mapped from its uint32 output to [-1, 1).
   //
   // Pink: Voss-McCartney (McCartney's discrete realisation of Voss's 1/f
   // process, widely described in DSP literature/the public
   // musicdsp/dsprelated write-ups): sum a bank of white generators, each
   // updated only when its bit of an incrementing counter flips, so lower
   // rows update (and therefore contribute energy) exponentially less often
   // - the sum approximates the ~3 dB/octave rolloff of pink noise. Also the
   // technique `docs/plans/audio/README.md` §2 names as an allowed
   // primary-literature algorithm.
   //
   // Brown/red: leaky integration of white noise (a first-order lowpass
   // random walk - standard textbook technique), leaked to stay bounded
   // instead of drifting off with a true unbounded random walk.
   struct WhiteNoise
   {
      uint32_t state = 0x9E3779B9u; // never 0 - xorshift's fixed point

      float Next()
      {
         state ^= state << 13;
         state ^= state >> 17;
         state ^= state << 5;
         return (float)(state) * (1.0f / 2147483648.0f) - 1.0f; // -> [-1, 1)
      }
   };

   struct PinkNoise
   {
      static constexpr int kRows = 7;
      WhiteNoise white;
      float rows[kRows] = {};
      uint32_t counter = 0;

      float Next()
      {
         const uint32_t prev = counter++;
         const uint32_t changed = prev ^ counter;
         float sum = white.Next(); // row 0 updates every sample
         for (int i = 0; i < kRows; i++)
         {
            if (changed & (1u << i))
               rows[i] = white.Next();
            sum += rows[i];
         }
         return sum / (float)(kRows + 1);
      }
   };

   struct BrownNoise
   {
      WhiteNoise white;
      float state = 0.0f;

      float Next()
      {
         state += white.Next() * 0.02f;
         state -= state * 0.001f; // leak: keeps the random walk bounded
         if (state > 1.0f) state = 1.0f;
         else if (state < -1.0f) state = -1.0f;
         return state * 3.5f; // the leaky walk sits well under unity; restore headroom
      }
   };

   // -------------------------------------------------------------- TPT SVF
   // Topology-preserving transform state-variable filter, per Vadim
   // Zavalishin's "The Art of VA Filter Design" (the trapezoidal-integrator
   // SVF, also known as the "cytomic" or "Zavalishin" SVF). Produces
   // low/high/band/notch simultaneously from one pair of state variables.
   struct TptSvf
   {
      float sampleRate = 44100.0f;
      float g = 0.0f;  // pre-warped cutoff coefficient
      float k = 1.0f;  // damping = 1/Q
      float s1 = 0.0f; // integrator states
      float s2 = 0.0f;

      void SetSampleRate(double sr) { sampleRate = (float)sr; }

      void SetCutoff(float hz, float q)
      {
         const float freq = hz < 1.0f ? 1.0f : hz;
         g = tanf((float)M_PI * freq / sampleRate);
         k = 1.0f / (q < 0.01f ? 0.01f : q);
      }

      void Reset()
      {
         s1 = 0.0f;
         s2 = 0.0f;
      }

      struct Outputs
      {
         float low, high, band, notch;
      };

      Outputs Process(float in)
      {
         const float a1 = 1.0f / (1.0f + g * (g + k));
         const float a2 = g * a1;
         const float band = a1 * s1 + a2 * (in - s2);
         const float low = s2 + g * band;
         const float high = in - low - k * band;
         const float notch = in - k * band;

         s1 = 2.0f * band - s1;
         s2 = 2.0f * low - s2;

         return { low, high, band, notch };
      }
   };

   // ---------------------------------------------------------------- Biquad
   // Robert Bristow-Johnson's Audio EQ Cookbook coefficients, direct form I.
   // One struct, per-type Set* functions compute b0..a2; Process runs the
   // difference equation. Direct form I (not II) - no coefficient
   // modulation happening yet, so the extra state of form II buys nothing
   // here.
   struct Biquad
   {
      float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
      float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

      void Reset()
      {
         x1 = x2 = y1 = y2 = 0.0f;
      }

      float Process(float in)
      {
         const float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
         x2 = x1;
         x1 = in;
         y2 = y1;
         y1 = out;
         return out;
      }

      void SetLowpass(double freq, double q, double sampleRate)
      {
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double a0 = 1.0 + alpha;
         const double B0 = (1.0 - cosw0) / 2.0;
         const double B1 = 1.0 - cosw0;
         const double B2 = B0;
         const double A1 = -2.0 * cosw0;
         const double A2 = 1.0 - alpha;
         Assign(B0, B1, B2, a0, A1, A2);
      }

      void SetHighpass(double freq, double q, double sampleRate)
      {
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double a0 = 1.0 + alpha;
         const double B0 = (1.0 + cosw0) / 2.0;
         const double B1 = -(1.0 + cosw0);
         const double B2 = B0;
         const double A1 = -2.0 * cosw0;
         const double A2 = 1.0 - alpha;
         Assign(B0, B1, B2, a0, A1, A2);
      }

      void SetBandpass(double freq, double q, double sampleRate)
      {
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double a0 = 1.0 + alpha;
         const double B0 = alpha;
         const double B1 = 0.0;
         const double B2 = -alpha;
         const double A1 = -2.0 * cosw0;
         const double A2 = 1.0 - alpha;
         Assign(B0, B1, B2, a0, A1, A2);
      }

      void SetNotch(double freq, double q, double sampleRate)
      {
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double a0 = 1.0 + alpha;
         const double B0 = 1.0;
         const double B1 = -2.0 * cosw0;
         const double B2 = 1.0;
         const double A1 = -2.0 * cosw0;
         const double A2 = 1.0 - alpha;
         Assign(B0, B1, B2, a0, A1, A2);
      }

      void SetAllpass(double freq, double q, double sampleRate)
      {
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double a0 = 1.0 + alpha;
         const double B0 = 1.0 - alpha;
         const double B1 = -2.0 * cosw0;
         const double B2 = 1.0 + alpha;
         const double A1 = -2.0 * cosw0;
         const double A2 = 1.0 - alpha;
         Assign(B0, B1, B2, a0, A1, A2);
      }

      void SetPeaking(double freq, double q, double gainDb, double sampleRate)
      {
         const double A = pow(10.0, gainDb / 40.0);
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double a0 = 1.0 + alpha / A;
         const double B0 = 1.0 + alpha * A;
         const double B1 = -2.0 * cosw0;
         const double B2 = 1.0 - alpha * A;
         const double A1 = -2.0 * cosw0;
         const double A2 = 1.0 - alpha / A;
         Assign(B0, B1, B2, a0, A1, A2);
      }

      void SetLowShelf(double freq, double q, double gainDb, double sampleRate)
      {
         const double A = pow(10.0, gainDb / 40.0);
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double sqrtA = sqrt(A);
         const double a0 = (A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
         const double B0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
         const double B1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
         const double B2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);
         const double A1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
         const double A2 = (A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;
         Assign(B0, B1, B2, a0, A1, A2);
      }

      void SetHighShelf(double freq, double q, double gainDb, double sampleRate)
      {
         const double A = pow(10.0, gainDb / 40.0);
         const double w0 = 2.0 * M_PI * freq / sampleRate;
         const double cosw0 = cos(w0), sinw0 = sin(w0);
         const double alpha = sinw0 / (2.0 * q);
         const double sqrtA = sqrt(A);
         const double a0 = (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
         const double B0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
         const double B1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
         const double B2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);
         const double A1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
         const double A2 = (A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;
         Assign(B0, B1, B2, a0, A1, A2);
      }

   private:
      void Assign(double B0, double B1, double B2, double a0, double A1, double A2)
      {
         b0 = (float)(B0 / a0);
         b1 = (float)(B1 / a0);
         b2 = (float)(B2 / a0);
         a1 = (float)(A1 / a0);
         a2 = (float)(A2 / a0);
      }
   };
}
