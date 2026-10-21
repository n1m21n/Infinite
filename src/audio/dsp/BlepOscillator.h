#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Band-limited oscillator with windowed-sinc step (BLEP) and ramp (BLAMP)
// corrections, for the analog synth's oscillators.
//
// The naive waveform is computed from a phase accumulator; every
// discontinuity in its value (saw wrap, pulse edges, a hard-sync reset) and
// in its slope (triangle peaks, the kinks pulse-width warping puts into the
// other shapes) is located at its exact fractional time inside the sample,
// and the difference between a band-limited step/ramp and the ideal one is
// added to the samples around it (Stilson & Smith, "Alias-Free Digital
// Synthesis of Classic Analog Waveforms", ICMC 1996; Esqueda, Valimaki &
// Bilbao, "Rounding Corners with BLAMP", DAFx-16). The kernel is a
// Kaiser-windowed (beta 9) sinc, kKernelHalf (12) samples either side, cut
// at 0.9 of Nyquist (0.8 for NarrowOscillator, see Band); it is
// linear-phase, so the output is delayed by kKernelHalf samples (kLatency)
// and the corrections land on both sides of each edge. Work is per discontinuity, not per sample.
//
// It replaces DspMath::PolyBlepOsc's 2-point polynomial step (and its naive
// triangle) in the synth: that left -34 dB of aliasing on a 1.75 kHz saw.
// Hard sync is done properly here - the slave is stepped to the instant the
// master wrapped, reset, and stepped on, with the jump band-limited - where
// the old code snapped the slave's phase to 0 at the sample boundary.
//
// Waveforms and pulse-width behaviour match PolyBlepOsc exactly (same
// naive shapes, same WarpPhase), so a patch keeps its timbre.
//
// Real-time: tables are built once (EnsureTables(band), main thread, before
// any audio-thread use of that band); Step() touches only the object's own
// fixed arrays.
namespace BlepOsc
{
   constexpr int kKernelHalf = 12;
   constexpr int kLatency = kKernelHalf;
   constexpr int kTaps = 2 * kKernelHalf;
   constexpr int kTableRes = 2048; // rows per sample of fractional position

   enum Waveform
   {
      kSine = 0,
      kTriangle,
      kSaw,
      kSquare,
   };

   // Residuals laid out by fractional position: row r holds the correction
   // for every tap of an event r / kTableRes of a sample past an integer
   // time, so one event reads two adjacent, contiguous rows. Resolution
   // matters more than kernel length here: at 128 rows per sample the
   // linear-interpolation error alone left a -67 dB line at every folded
   // harmonic (about -30 dB in total on a 1.75 kHz saw).
   struct Tables
   {
      float step[kTableRes + 1][kTaps]; // band-limited step minus the ideal step
      float ramp[kTableRes + 1][kTaps]; // band-limited ramp minus the ideal ramp
   };

   // Kernel cutoffs, as fractions of Nyquist. Same length (so the same
   // kLatency) either way; only where the band edge sits differs.
   //  kBandWide (0.90): flat to ~18 kHz at 48 kHz, leaves some content in
   //    20-24 kHz. Right for an output that goes straight out (Wavetable,
   //    whose PDC is kLatency).
   //  kBandNarrow (0.80): -0.4 dB at 16 kHz, -2.7 dB at 18 kHz, -52 dB at
   //    24 kHz. For an oscillator that feeds a nonlinearity (the Analog
   //    synth's drive and ladder): the 20-24 kHz content of the wide kernel
   //    intermodulates in the tanh stages and folds into the audible band,
   //    which no oversampling after the oscillator can remove (Analog went
   //    from -64 to -86 dB non-harmonic at 4.2 kHz with this plus 4x).
   enum Band
   {
      kBandWide = 0,
      kBandNarrow,
   };

   inline const Tables* BuildTables(double cutoff)
   {
      // (kept at its original indentation: this was EnsureTables' lambda)
      {
         auto* t = new Tables();
         const double kCutoff = cutoff; // fraction of Nyquist
         constexpr double kBeta = 9.0;  // Kaiser window
         constexpr int kFine = 4;         // integration points per table step
         const double dx = 1.0 / ((double)kTableRes * kFine);
         const int points = (int)std::lround(2.0 * kKernelHalf / dx) + 1;
         auto i0 = [](double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; k++)
            {
               term *= (x / (2.0 * k)) * (x / (2.0 * k));
               sum += term;
            }
            return sum;
         };
         // Cumulative step (integral of the kernel) and ramp (integral of
         // the step) on a fine grid over [-K, K], trapezoidal.
         std::vector<double> stepC((size_t)points), rampC((size_t)points);
         std::vector<double> h((size_t)points);
         for (int i = 0; i < points; i++)
         {
            const double x = -kKernelHalf + i * dx;
            const double a = M_PI * kCutoff * x;
            const double sinc = (std::fabs(a) < 1e-12) ? 1.0 : std::sin(a) / a;
            const double r = x / kKernelHalf;
            h[(size_t)i] = sinc * i0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(kBeta);
         }
         double total = 0.0;
         for (int i = 1; i < points; i++)
            total += 0.5 * (h[(size_t)i - 1] + h[(size_t)i]) * dx;
         stepC[0] = rampC[0] = 0.0;
         for (int i = 1; i < points; i++)
         {
            stepC[(size_t)i] = stepC[(size_t)i - 1] + 0.5 * (h[(size_t)i - 1] + h[(size_t)i]) * dx / total;
            rampC[(size_t)i] = rampC[(size_t)i - 1] + 0.5 * (stepC[(size_t)i - 1] + stepC[(size_t)i]) * dx;
         }
         auto sampleAt = [&](const std::vector<double>& c, double x) {
            const double pos = (x + kKernelHalf) / dx;
            const int i = std::clamp((int)pos, 0, points - 2);
            const double f = pos - i;
            return c[(size_t)i] + (c[(size_t)i + 1] - c[(size_t)i]) * f;
         };
         for (int r = 0; r <= kTableRes; r++)
         {
            // Row r is an event frac = r / kTableRes of a sample after the
            // integer `whole`, with frac in (0, 1] (AddEvent). Tap j is
            // sample whole - (K - 1) + j, at distance x = j - K + 1 - frac.
            // Taps j < K are samples before the event and j >= K after it,
            // for every frac in (0, 1] - so the ideal step/ramp each tap is
            // corrected against is fixed per tap, and interpolating between
            // two rows never straddles the step itself. (Deciding by the sign
            // of x instead put the x = 0 tap on the wrong side for events a
            // hair after a sample time: a full-height glitch, rare on a saw,
            // regular under hard sync.)
            const double frac = (double)r / kTableRes;
            for (int j = 0; j < kTaps; j++)
            {
               const double x = (double)(j - kKernelHalf + 1) - frac;
               const bool after = j >= kKernelHalf;
               const bool inside = x > -kKernelHalf && x < kKernelHalf;
               t->step[r][j] = inside ? (float)(sampleAt(stepC, x) - (after ? 1.0 : 0.0)) : 0.0f;
               t->ramp[r][j] = inside ? (float)(sampleAt(rampC, x) - (after ? x : 0.0)) : 0.0f;
            }
         }
         return t;
      }
   }

   inline const Tables& EnsureTables(Band band = kBandWide)
   {
      static const Tables* wide = BuildTables(0.90);
      if (band == kBandWide)
         return *wide;
      static const Tables* narrow = BuildTables(0.80);
      return *narrow;
   }

   // The correction machinery on its own, for any signal whose
   // discontinuities the caller can locate: push one naive sample per step,
   // add each step's events (a value jump and/or a slope change at a
   // fraction of the step) before pushing, and read back the band-limited
   // signal kLatency samples late. Used by Oscillator below and by the
   // Wavetable synth's phase warps (WavetableSynthCore.h).
   template <int B>
   class BasicCorrector
   {
   public:
      void Reset()
      {
         std::fill(mNaive, mNaive + kRing, 0.0f);
         std::fill(mCorr, mCorr + kRing, 0.0f);
         mTime = 0;
      }

      // The step being built runs from Time() to Time() + 1.
      long Time() const { return mTime; }

      // A step of `dv` and a slope change of `ds` (per sample) at fraction
      // `tau` in (0, 1] of the step being built.
      void AddEvent(double tau, double dv, double ds) { AddEventAt((double)mTime + tau, dv, ds); }

      // Same, at absolute time `t` in (Time(), Time() + 1].
      void AddEventAt(double t, double dv, double ds)
      {
         if (std::fabs(dv) < 1e-9 && std::fabs(ds) < 1e-12)
            return;
         const Tables& tb = EnsureTables((Band)B);
         // frac in (0, 1]: an event exactly on a sample time belongs to the
         // step that ends there (that sample already includes it).
         const double whole = std::ceil(t) - 1.0;
         const double pos = (t - whole) * kTableRes;
         const int r = std::min((int)pos, kTableRes - 1);
         const float f = (float)(pos - r);
         const long base = (long)whole - (kKernelHalf - 1);
         const float* s0 = tb.step[r];
         const float* s1 = tb.step[r + 1];
         const float* r0 = tb.ramp[r];
         const float* r1 = tb.ramp[r + 1];
         const float fdv = (float)dv, fds = (float)ds;
         for (int j = 0; j < kTaps; j++)
         {
            const float st = s0[j] + (s1[j] - s0[j]) * f;
            const float rp = r0[j] + (r1[j] - r0[j]) * f;
            mCorr[(unsigned)(base + j) & kMask] += fdv * st + fds * rp;
         }
      }

      // Ends the step: `naive` is the uncorrected value at its end. Returns
      // the corrected signal kLatency samples late.
      float Push(float naive)
      {
         mTime++;
         mNaive[(unsigned)mTime & kMask] = naive;
         const unsigned out = ((unsigned)mTime - kLatency) & kMask;
         const float y = mNaive[out] + mCorr[out];
         mCorr[out] = 0.0f;
         return y;
      }

   private:
      static constexpr int kRing = 32;
      static constexpr unsigned kMask = kRing - 1;
      float mNaive[kRing] = {};
      float mCorr[kRing] = {};
      long mTime = 0;
   };

   using Corrector = BasicCorrector<kBandWide>;

   template <int B>
   class BasicOscillator
   {
   public:
      double phase = 0.0; // 0..1
      double inc = 0.0;   // cycles per sample

      void Reset(double startPhase)
      {
         phase = startPhase - std::floor(startPhase);
         inc = 0.0;
         mCorr.Reset();
      }

      void SetFrequency(double hz, double sampleRate) { inc = (sampleRate > 0.0) ? hz / sampleRate : 0.0; }

      // The naive value at the current phase, without stepping - for use as
      // an FM source, which wants this sample's value with no delay.
      float Peek(int waveform, float pulseWidth) const { return Value(waveform, Pw(pulseWidth), phase); }

      // Would the next Step() wrap? If so, `when` is the fraction of the
      // sample (0, 1] at which it does - the instant a sync slave resets.
      bool WillWrap(double& when) const
      {
         if (inc <= 0.0 || phase + inc < 1.0)
            return false;
         when = (1.0 - phase) / inc;
         return true;
      }

      // Advances one sample and returns the band-limited output, kLatency
      // samples late. `syncAt` in (0, 1] resets the phase to 0 at that
      // fraction of the sample (hard sync); pass a negative value for none.
      float Step(int waveform, float pulseWidth, double syncAt = -1.0)
      {
         const double pw = Pw(pulseWidth);
         const double t0 = (double)mCorr.Time(); // this step runs from t0 to t0 + 1
         if (syncAt > 0.0 && syncAt <= 1.0)
         {
            Advance(waveform, pw, t0, syncAt);
            const double tSync = t0 + syncAt;
            AddEvent(tSync, Value(waveform, pw, 0.0) - Value(waveform, pw, phase),
                     (Slope(waveform, pw, 0.0, true) - Slope(waveform, pw, phase, false)) * inc);
            phase = 0.0;
            Advance(waveform, pw, tSync, 1.0 - syncAt);
         }
         else
         {
            Advance(waveform, pw, t0, 1.0);
         }

         return mCorr.Push(Value(waveform, pw, phase));
      }

   private:

      static double Pw(float pulseWidth)
      {
         return pulseWidth < 0.01f ? 0.01 : (pulseWidth > 0.99f ? 0.99 : (double)pulseWidth);
      }

      // PolyBlepOsc's WarpPhase: the shape's midpoint moves to pw.
      static double Warp(double p, double pw) { return p < pw ? (p / pw) * 0.5 : 0.5 + (p - pw) / (1.0 - pw) * 0.5; }
      static double WarpSlope(double p, double pw, bool after)
      {
         const bool firstHalf = after ? (p < pw) : (p <= pw && p > 0.0);
         return firstHalf ? 0.5 / pw : 0.5 / (1.0 - pw);
      }

      static float Value(int waveform, double pw, double p)
      {
         switch (waveform)
         {
            case kSine:
               return (float)std::sin(2.0 * M_PI * Warp(p, pw));
            case kTriangle:
               return (float)(1.0 - 4.0 * std::fabs(Warp(p, pw) - 0.5));
            case kSaw:
               return (float)(2.0 * Warp(p, pw) - 1.0);
            case kSquare:
            default:
               return p < pw ? 1.0f : -1.0f;
         }
      }

      // d(value)/d(phase), just after (`after`) or just before a point.
      static double Slope(int waveform, double pw, double p, bool after)
      {
         const double ws = WarpSlope(p, pw, after);
         switch (waveform)
         {
            case kSine:
               return 2.0 * M_PI * std::cos(2.0 * M_PI * Warp(p, pw)) * ws;
            case kTriangle:
            {
               const double w = Warp(p, pw);
               const bool rising = after ? (w < 0.5) : (w <= 0.5 && w > 0.0);
               return (rising ? 4.0 : -4.0) * ws;
            }
            case kSaw:
               return 2.0 * ws;
            case kSquare:
            default:
               return 0.0;
         }
      }

      // Moves the phase forward by `fraction` of a sample starting at time
      // `tStart`, adding a correction for every breakpoint (pw, wrap) it
      // passes.
      void Advance(int waveform, double pw, double tStart, double fraction)
      {
         double remaining = fraction * inc;
         double t = tStart;
         while (remaining > 0.0 && inc > 0.0)
         {
            const double next = (phase < pw) ? pw : 1.0;
            const double dist = next - phase;
            if (dist > remaining)
            {
               phase += remaining;
               return;
            }
            t += dist / inc;
            remaining -= dist;
            const double before = (next >= 1.0) ? 1.0 : next;
            const double after = (next >= 1.0) ? 0.0 : next;
            AddEvent(t, Value(waveform, pw, after) - Value(waveform, pw, before - 1e-12),
                     (Slope(waveform, pw, after, true) - Slope(waveform, pw, before, false)) * inc);
            phase = after;
         }
      }

      void AddEvent(double t, double dv, double ds) { mCorr.AddEventAt(t, dv, ds); }

      BasicCorrector<B> mCorr;
   };

   using Oscillator = BasicOscillator<kBandWide>;
   using NarrowOscillator = BasicOscillator<kBandNarrow>;
}
