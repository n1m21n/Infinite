#include "FlangerKernel.h"

#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

void FlangerKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kDelayMs, node.Param("delay"));
   mMailbox.Push(kDepthMs, node.Param("depth"));
   mMailbox.Push(kRateHz, node.Param("rate"));
   mMailbox.Push(kFeedback, node.Param("feedback"));
   mSync.store(node.Param("sync") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mRateDiv.store(std::clamp((int)(node.Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1),
                  std::memory_order_relaxed);
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

void FlangerKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const float lineCapacityMs = kMaxDelayMs - 2.0f;
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float delayMs = mMailbox.SmoothedValue(kDelayMs);
      const float depthMs = mMailbox.SmoothedValue(kDepthMs);
      float rateHz;
      if (sync)
      {
         const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
         rateHz = (float)MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
      }
      else
         rateHz = std::max(0.0f, mMailbox.SmoothedValue(kRateHz));
      const float feedback = std::clamp(mMailbox.SmoothedValue(kFeedback), -0.95f, 0.95f);

      mPhase += rateHz / mSampleRate;
      if (mPhase >= 1.0)
         mPhase -= floor(mPhase);

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;

      if (analog)
      {
         const float lfoDrift = mDriftLfo.Advance(std::max(0.1f, rateHz), 0.15f, 0.05f, mSampleRate) * 0.05f;
         const float lMs = std::clamp(delayMs + depthMs * sinf(2.0f * (float)M_PI * ((float)mPhase + lfoDrift)), 0.2f, lineCapacityMs);
         const float rMs = std::clamp(
            delayMs + depthMs * sinf(2.0f * (float)M_PI * ((float)mPhase + 0.25f + lfoDrift)), 0.2f, lineCapacityMs);

         float delayedL = mLineL.Read(lMs * 0.001f * (float)mSampleRate);
         float delayedR = mLineR.Read(rMs * 0.001f * (float)mSampleRate);

         const float fbL = AnalogDsp::AsymTanh(delayedL * feedback, 0.15f);
         const float fbR = AnalogDsp::AsymTanh(delayedR * feedback, 0.15f);
         mLineL.Write(inL + fbL);
         mLineR.Write(inR + fbR);

         // 4th-order 9kHz lowpass reconstruction filter
         delayedL = mFilterL[1].Process(mFilterL[0].Process(delayedL).low).low;
         delayedR = mFilterR[1].Process(mFilterR[0].Process(delayedR).low).low;

         out.channels[0][i] = delayedL;
         if (numChannels >= 2)
            out.channels[1][i] = delayedR;
      }
      else
      {
         const float lMs = std::clamp(delayMs + depthMs * sinf(2.0f * (float)M_PI * (float)mPhase), 0.2f, lineCapacityMs);
         const float rMs = std::clamp(
            delayMs + depthMs * sinf(2.0f * (float)M_PI * ((float)mPhase + 0.25f)), 0.2f, lineCapacityMs);

         const float delayedL = mLineL.Read(lMs * 0.001f * (float)mSampleRate);
         const float delayedR = mLineR.Read(rMs * 0.001f * (float)mSampleRate);

         mLineL.Write(inL + delayedL * feedback);
         mLineR.Write(inR + delayedR * feedback);

         out.channels[0][i] = delayedL;
         if (numChannels >= 2)
            out.channels[1][i] = delayedR;
      }

      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}
