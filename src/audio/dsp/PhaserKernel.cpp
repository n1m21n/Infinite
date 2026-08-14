#include "PhaserKernel.h"

#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

void PhaserKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kCutoffHz, node.Param("cutoff"));
   mMailbox.Push(kRateHz, node.Param("rate"));
   mMailbox.Push(kDepth, node.Param("depth"));
   mMailbox.Push(kSpread, node.Param("spread"));
   mStageCount.store(std::clamp((int)(node.Param("order") + 0.5f) & ~1, 2, kMaxStages), std::memory_order_relaxed);
   mSync.store(node.Param("sync") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mRateDiv.store(std::clamp((int)(node.Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1),
                  std::memory_order_relaxed);
}

void PhaserKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const int stages = mStageCount.load(std::memory_order_relaxed);
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);

   for (int i = 0; i < out.numFrames; i++)
   {
      const float cutoffHz = mMailbox.SmoothedValue(kCutoffHz);
      float rateHz;
      if (sync)
      {
         const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
         rateHz = (float)MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
      }
      else
         rateHz = std::max(0.0f, mMailbox.SmoothedValue(kRateHz));
      const float depth = std::clamp(mMailbox.SmoothedValue(kDepth), 0.0f, 1.0f);
      const float spread = std::clamp(mMailbox.SmoothedValue(kSpread), 0.0f, 1.0f);

      mPhase += rateHz / mSampleRate;
      if (mPhase >= 1.0)
         mPhase -= floor(mPhase);

      // Sweep +/- one octave around `cutoffHz`, scaled by depth - an
      // exponential sweep (not linear) reads as musically even across the
      // whole range, the same reason Audio Filter's freq axis is log.
      const float octaves = depth * 1.0f;
      const float fcL = cutoffHz * powf(2.0f, octaves * sinf(2.0f * (float)M_PI * (float)mPhase));
      const float fcR = cutoffHz * powf(2.0f, octaves * sinf(2.0f * (float)M_PI * ((float)mPhase + spread * 0.5f)));
      const float aL = AllpassCoeff(fcL, (float)mSampleRate);
      const float aR = AllpassCoeff(fcR, (float)mSampleRate);

      float xL = in.channels[0][i];
      float xR = numChannels >= 2 ? in.channels[1][i] : xL;
      for (int s = 0; s < stages; s++)
      {
         xL = mStagesL[s].Process(xL, aL);
         xR = mStagesR[s].Process(xR, aR);
      }

      out.channels[0][i] = xL;
      if (numChannels >= 2)
         out.channels[1][i] = xR;
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}
