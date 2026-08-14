#include "StutterKernel.h"

#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

void StutterKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mSync.store(node.Param("sync") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mRateDiv.store(std::clamp((int)(node.Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1),
                  std::memory_order_relaxed);
   mSteps.store(std::clamp((int)(node.Param("steps") + 0.5f), 2, kMaxGateSteps), std::memory_order_relaxed);
   mGateMask.store(std::clamp((int)(node.Param("gateMask") + 0.5f), 0, (1 << kMaxGateSteps) - 1),
                   std::memory_order_relaxed);
   mMailbox.Push(kFreeMs, node.Param("timeMs"));
}

void StutterKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
   const int steps = mSteps.load(std::memory_order_relaxed);
   const int gateMask = mGateMask.load(std::memory_order_relaxed);
   const float chunkFrac = 1.0f / (float)steps;
   const int grainCapacity = (int)mGrainL.size();

   for (int i = 0; i < out.numFrames; i++)
   {
      const float freeMs = mMailbox.SmoothedValue(kFreeMs);

      float periodSeconds;
      if (sync)
      {
         const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
         periodSeconds = (float)(MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv) * 60.0 / bpm);
      }
      else
         periodSeconds = freeMs * 0.001f;

      const int periodSamples =
         std::clamp((int)(periodSeconds * (float)mSampleRate), 2, grainCapacity);

      if (mCyclePos == 0)
         mCurrentChunkSamples = std::clamp((int)(chunkFrac * (float)periodSamples), 1, periodSamples);

      // Which repeat-in-cycle this sample falls in - index 0 is the
      // record-phase repeat itself, so gateMask can mute it too.
      const int stepIdx = std::clamp(mCyclePos / std::max(1, mCurrentChunkSamples), 0, kMaxGateSteps - 1);
      const bool audible = (gateMask & (1 << stepIdx)) != 0;

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;

      float outL, outR;
      if (mCyclePos < mCurrentChunkSamples)
      {
         // Record phase: capture live audio into the grain buffer for the
         // loop phase that follows - unconditionally, so a later un-muted
         // repeat can still play it even if this repeat itself is muted.
         mGrainL[(size_t)mCyclePos] = inL;
         mGrainR[(size_t)mCyclePos] = inR;
         outL = audible ? inL : 0.0f;
         outR = audible ? inR : 0.0f;
      }
      else
      {
         // Loop phase: replay the captured grain on repeat.
         const int idx = mCyclePos % mCurrentChunkSamples;
         outL = audible ? mGrainL[(size_t)idx] : 0.0f;
         outR = audible ? mGrainR[(size_t)idx] : 0.0f;
      }

      out.channels[0][i] = outL;
      if (numChannels >= 2)
         out.channels[1][i] = outR;
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;

      mCyclePos++;
      if (mCyclePos >= periodSamples)
         mCyclePos = 0;
   }
}
