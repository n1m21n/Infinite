#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Frequency Shifter's kernel - a true single-sideband (SSB) frequency shifter:
// moves every frequency component by a fixed Hz offset rather than multiplying
// by a ratio (Pitch Shifter) or producing dual sum/difference sidebands (Ring Mod).
//
// Primary reference for the Hilbert transform allpass cascade:
//   Olli Niemitalo, "Hilbert transformer using 2nd-order allpass filters",
//   musicdsp.org archive. Two parallel cascades of four 2nd-order allpass
//   sections with a 90-degree phase difference across the audio band.
//
// Single-sideband modulation:
//   out(t) = x(t) * cos(2*pi*f*t) - x_hat(t) * sin(2*pi*f*t)
// where x_hat is the Hilbert transform (quadrature phase) of x.
// Signed f shifts up when positive and down when negative.
//
// Note on Path B: Path B includes a crucial 1-sample delay prior to/within its
// allpass cascade so that Path A and Path B remain exactly 90 degrees apart.
//
// Feedback: Output is fed back into the input through a gain, capped at 0.95,
// and soft-clipped via DspMath::FastTanh to produce the classic Bode / barber-pole
// rising/falling spiral effect stably.
class AudioEffectNode;

struct Allpass2ndOrder
{
   float x1 = 0.0f, x2 = 0.0f;
   float y1 = 0.0f, y2 = 0.0f;

   void Reset()
   {
      x1 = x2 = 0.0f;
      y1 = y2 = 0.0f;
   }

   // Recurrence relation for 2nd-order allpass: y[n] = a^2 * (x[n] + y[n-2]) - x[n-2]
   inline float Process(float x, float aSq)
   {
      const float y = aSq * (x + y2) - x2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      return y;
   }
};

class FrequencyShifterKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;

   enum ParamSlot
   {
      kShiftHz = 0,
      kFeedback,
      kSpreadHz,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      Reset();
   }

   void Reset() override
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         for (int s = 0; s < 4; s++)
         {
            mStagesA[ch][s].Reset();
            mStagesB[ch][s].Reset();
         }
         mDelayB[ch] = 0.0f;
         mPhase[ch] = 0.0;
         mLastOut[ch] = 0.0f;
      }
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   // 1 sample discrete pipeline delay from Path B's structural unit-delay.
   int LatencySamples() const override { return 1; }

private:
   // Olli Niemitalo coefficient squares (a^2):
   // Path A: 0.6923877778065, 0.9360654322959, 0.9882295226860, 0.9987488452737
   // Path B: 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278
   static constexpr float kCoeffsASq[4] = {
      0.4794008343717222f,
      0.8762184935408794f,
      0.9765975895786483f,
      0.9974992560371661f
   };

   static constexpr float kCoeffsBSq[4] = {
      0.1617584983637118f,
      0.7330289323145458f,
      0.9453497003058869f,
      0.9905991568222474f
   };

   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   Allpass2ndOrder mStagesA[kMaxChannels][4];
   Allpass2ndOrder mStagesB[kMaxChannels][4];
   float mDelayB[kMaxChannels] = { 0.0f };
   double mPhase[kMaxChannels] = { 0.0 };
   float mLastOut[kMaxChannels] = { 0.0f };
};
