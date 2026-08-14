#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Stutter's kernel - each tempo-synced (or free-ms) cycle records the first
// 1/`steps` fraction of live audio, then loops that captured grain for the
// rest of the cycle (one repeat per step) before recording fresh audio next
// cycle. This is the standard "beat repeat" / glitch-loop topology (e.g.
// Ableton's Beat Repeat at 100% chance, one-slice mode) - distinct from
// Delay (which reads a continuously advancing tap) by looping a *fixed short
// slice* in place rather than re-reading a moving position further back in
// time. Each channel gets its own grain buffer so the stereo image survives
// the loop.
//
// `steps` is an explicit, discrete repeat count rather than a free-floating
// grain fraction - "1/8 repeats 8 times" only reads cleanly if the count is
// always a whole number the UI's step grid can show one square per repeat
// for. `gateMask` mutes individual repeats within the cycle (a square in
// that grid clicked off), independent of which repeat is muted - the
// record-phase repeat (index 0) included, so muting it silences the first
// hit while still capturing it for any later, un-muted repeat to play.
class AudioEffectNode;

class StutterKernel : public IEffectKernel
{
public:
   static constexpr float kMaxPeriodSeconds = 4.0f;
   // Bitmask width for gateMask - 16 repeats covers every rate down to
   // 1/16 at a 4/4 bar, comfortably an exact int in a float (no precision
   // loss pushing it through ParamMailbox/EffectDef's float param table).
   static constexpr int kMaxGateSteps = 16;

   enum ParamSlot
   {
      kFreeMs = 0,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const int maxSamples = (int)std::ceil(kMaxPeriodSeconds * sampleRate) + 8;
      mGrainL.assign((size_t)maxSamples, 0.0f);
      mGrainR.assign((size_t)maxSamples, 0.0f);
      Reset();
   }

   void Reset() override
   {
      mCyclePos = 0;
      mCurrentChunkSamples = 1;
      std::fill(mGrainL.begin(), mGrainL.end(), 0.0f);
      std::fill(mGrainR.begin(), mGrainR.end(), 0.0f);
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mSync { 1 };
   std::atomic<int> mRateDiv { MusicTime::kEighth };
   std::atomic<int> mSteps { 8 };
   std::atomic<int> mGateMask { (1 << kMaxGateSteps) - 1 };

   std::vector<float> mGrainL, mGrainR;
   int mCyclePos = 0;
   int mCurrentChunkSamples = 1; // latched at the start of each cycle's record phase
};
