#include "StutterKernel.h"

#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

void StutterKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mSync.store(node.Param("sync") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mRateDiv.store(std::clamp((int)(node.Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1),
                  std::memory_order_relaxed);
   mMailbox.Push(kFreeMs, node.Param("timeMs"));
   mMailbox.Push(kChunkFrac, node.Param("chunk"));
}

void StutterKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
   const int grainCapacity = (int)mGrainL.size();

   for (int i = 0; i < out.numFrames; i++)
   {
      const float freeMs = mMailbox.SmoothedValue(kFreeMs);
      const float chunkFrac = std::clamp(mMailbox.SmoothedValue(kChunkFrac), 0.02f, 1.0f);

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

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;

      float outL, outR;
      if (mCyclePos < mCurrentChunkSamples)
      {
         // Record phase: pass live audio through and capture it into the
         // grain buffer for the loop phase that follows.
         mGrainL[(size_t)mCyclePos] = inL;
         mGrainR[(size_t)mCyclePos] = inR;
         outL = inL;
         outR = inR;
      }
      else
      {
         // Loop phase: replay the captured grain on repeat.
         const int idx = mCyclePos % mCurrentChunkSamples;
         outL = mGrainL[(size_t)idx];
         outR = mGrainR[(size_t)idx];
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
