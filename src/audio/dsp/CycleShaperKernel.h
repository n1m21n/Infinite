#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/dsp/AnalogPrimitives.h"

// Cycle Shaper: replaces each wavecycle of the input with an idealized
// geometric waveform (Sine, Square, Triangle) of the same period and peak.
//
// How. Every upward zero crossing of the input is located to a fraction of
// a sample (linear interpolation between the samples either side). The time
// between two crossings is the cycle's period, and the cycle's peak is its
// amplitude. A phase-continuous oscillator plays the chosen shape at that
// period and amplitude, phase-locked to the crossings: at each crossing the
// oscillator's phase error against the crossing is measured and half of it
// is corrected over the next cycle, so the output stays in step with the
// input instead of drifting or jumping. The amplitude ramps to each new
// cycle's peak across that cycle. Both estimates use a quintic through six
// samples centred on the event (the crossing's root, the crest's extreme),
// which needs two samples of look-ahead - reported as latency, and the dry
// path is delayed to match. At short periods the nearest sample wanders by
// several percent from cycle to cycle, and a three-sample quadratic still
// left ~-50 dB of jitter sidebands on a 5 kHz sine; the quintic is ~25 dB
// cleaner.
//
// The shapes come from band-limited tables (harmonic series, one mip level
// per octave of harmonic ceiling, crossfaded continuously), read at the mip
// that keeps every harmonic under Nyquist for the current period, so the
// square and triangle do not alias. `smooth` widens the edges further: it is
// the transition length in samples (a square with H harmonics rises in about
// period / 2H), so `smooth` = N caps the harmonics at the count that gives an
// N-sample edge. 0 or 1 is the sharpest edge the sample rate allows.
//
// Cycles quieter than `threshold`, inputs with no crossings (DC, sub-audio
// below ~23 Hz at 48 kHz) and silence fade (5 ms) to the dry input.
//
// What the previous version did, and why it changed: it rendered each cycle
// as a whole-sample buffer of the previous cycle's length, restarted at
// every crossing. A period that is not a whole number of samples alternates
// lengths (e.g. 239/240), so playback was truncated or ran out a sample
// early - a gap or a jump every cycle - and the waveforms were not
// band-limited. ALIASTEST read -10 dB of non-harmonic energy on a plain
// sine. It also played one cycle late while reporting zero latency; the
// phase-locked oscillator itself has none (the 2 samples now reported are
// the estimator's look-ahead).
class AudioEffectNode;

class CycleShaperKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 2;
   static constexpr int kMaxCycleSamples = 2048;
   static constexpr int kMaxTail = 32; // `smooth` range, samples
   static constexpr int kTableSize = 1024;
   static constexpr int kMips = 10; // harmonic ceilings 512, 256, ... 1

   struct ChannelState
   {
      float hist[6] = {}; // hist[3] is the sample being processed, hist[4..5] the look-ahead
      double period = 0.0; // smoothed across cycles
      double sinceCrossing = 0.0; // time of the current sample after the last upward crossing
      bool haveCrossing = false;
      float cyclePeak = 0.0f;

      double phase = 0.0; // 0..1, 0 = upward crossing
      double inc = 0.0;   // cycles per sample
      bool locked = false;
      float amp = 0.0f, ampStep = 0.0f;
      int ampSteps = 0;
      float gate = 0.0f, gateTarget = 0.0f;

      float lastCycleLen = 100.0f;
      float lastCyclePeak = 0.5f;

      void Reset() { *this = ChannelState(); }
   };

   CycleShaperKernel() { Tables(); } // builds the shared tables on the main thread

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mGateCoeff = (float)std::exp(-1.0 / (0.005 * sampleRate));
      Reset();
   }

   void Reset() override
   {
      for (auto& ch : mChannels)
         ch.Reset();
   }

   // Discrete / per-cycle threshold parameters legitimately need no per-sample ramp:
   // threshold is a per-cycle gate, waveform and smooth are discrete integer counts.
   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   static constexpr int kLookAhead = 2;
   int LatencySamples() const override { return kLookAhead; }

   float LastCycleLength(int ch) const { return mChannels[ch % kMaxChannels].lastCycleLen; }
   float LastCyclePeak(int ch) const { return mChannels[ch % kMaxChannels].lastCyclePeak; }

   // Band-limited square and triangle, unit fundamental-normalised exactly
   // like the naive shapes they replace (square +-1, triangle peak 1 at
   // phase 0.25), plus one guard sample for interpolation.
   struct ShapeTables
   {
      float square[kMips][kTableSize + 1];
      float triangle[kMips][kTableSize + 1];
   };
   static const ShapeTables& Tables()
   {
      static const ShapeTables* tables = Build();
      return *tables;
   }

private:
   static ShapeTables* Build()
   {
      auto* t = new ShapeTables();
      for (int mip = 0; mip < kMips; mip++)
      {
         const int ceiling = (kTableSize / 2) >> mip;
         for (int i = 0; i <= kTableSize; i++)
         {
            const double ph = 2.0 * M_PI * (double)i / (double)kTableSize;
            double sq = 0.0, tri = 0.0;
            for (int h = 1; h <= ceiling; h += 2)
            {
               sq += std::sin(h * ph) / h;
               tri += (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0) * std::sin(h * ph) / ((double)h * h);
            }
            t->square[mip][i] = (float)(sq * 4.0 / M_PI);
            t->triangle[mip][i] = (float)(tri * 8.0 / (M_PI * M_PI));
         }
      }
      return t;
   }

   double mSampleRate = 44100.0;
   float mGateCoeff = 0.995f;
   std::atomic<int> mWaveform { 0 };
   std::atomic<float> mThresholdLinear { 0.0158489f }; // -36 dBFS
   std::atomic<int> mSmoothSamples { 8 };
   std::atomic<int> mAnalog { 0 };

   ChannelState mChannels[kMaxChannels];
};
