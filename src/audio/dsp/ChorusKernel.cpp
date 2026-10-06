#include "ChorusKernel.h"

#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

void ChorusKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kDelayMs, node.Param("delay"));
   mMailbox.Push(kSpread, node.Param("spread"));
   mMailbox.Push(kDepthMs, node.Param("depth"));
   mMailbox.Push(kRateHz, node.Param("rate"));
   mMailbox.Push(kFeedback, node.Param("feedback"));
   mTaps.store(node.Param("taps") >= 2.5f ? 3 : 2, std::memory_order_relaxed);
   mSync.store(node.Param("sync") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mRateDiv.store(std::clamp((int)(node.Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1),
                  std::memory_order_relaxed);
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

void ChorusKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const int taps = mTaps.load(std::memory_order_relaxed);
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;
   const float lineCapacityMs = kMaxDelayMs - 4.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float delayMs = mMailbox.SmoothedValue(kDelayMs);
      const float spread = std::clamp(mMailbox.SmoothedValue(kSpread), 0.0f, 1.0f);
      const float depthMs = mMailbox.SmoothedValue(kDepthMs);
      const float feedback = std::clamp(mMailbox.SmoothedValue(kFeedback), 0.0f, 0.9f);

      float rateHz;
      if (sync)
      {
         const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
         rateHz = (float)MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
      }
      else
         rateHz = std::max(0.0f, mMailbox.SmoothedValue(kRateHz));

      mPhase += rateHz / mSampleRate;
      if (mPhase >= 1.0)
         mPhase -= floor(mPhase);

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;

      float wetL = 0.0f, wetR = 0.0f;
      if (analog)
      {
         const float lfoDrift = mDriftLfo.Advance(std::max(0.1f, rateHz), 0.15f, 0.04f, mSampleRate) * 0.1f;

         for (int k = 0; k < taps; k++)
         {
            const float voiceOffset = (float)k / (float)taps;
            const float lPhase = (float)mPhase + voiceOffset + lfoDrift;
            const float rPhase = (float)mPhase + voiceOffset + spread * 0.5f + lfoDrift;

            const float lMs = std::clamp(delayMs + depthMs * sinf(2.0f * (float)M_PI * lPhase), 0.5f, lineCapacityMs);
            const float rMs = std::clamp(delayMs + depthMs * sinf(2.0f * (float)M_PI * rPhase), 0.5f, lineCapacityMs);

            wetL += mLineL.Read(lMs * 0.001f * (float)mSampleRate);
            wetR += mLineR.Read(rMs * 0.001f * (float)mSampleRate);
         }
         wetL /= (float)taps;
         wetR /= (float)taps;

         // BBD compander and bucket saturation
         wetL = AnalogDsp::AsymTanh(wetL, 0.10f);
         wetR = AnalogDsp::AsymTanh(wetR, 0.10f);

         // 4th-order 7.5kHz lowpass reconstruction filter
         wetL = mFilterL[1].Process(mFilterL[0].Process(wetL).low).low;
         wetR = mFilterR[1].Process(mFilterR[0].Process(wetR).low).low;

         const float writeL = AnalogDsp::AsymTanh(inL + wetL * feedback, 0.12f);
         const float writeR = AnalogDsp::AsymTanh(inR + wetR * feedback, 0.12f);
         mLineL.Write(writeL);
         mLineR.Write(writeR);
      }
      else
      {
         for (int k = 0; k < taps; k++)
         {
            const float voiceOffset = (float)k / (float)taps;
            const float lPhase = (float)mPhase + voiceOffset;
            const float rPhase = (float)mPhase + voiceOffset + spread * 0.5f;

            const float lMs = std::clamp(delayMs + depthMs * sinf(2.0f * (float)M_PI * lPhase), 0.5f, lineCapacityMs);
            const float rMs = std::clamp(delayMs + depthMs * sinf(2.0f * (float)M_PI * rPhase), 0.5f, lineCapacityMs);

            wetL += mLineL.Read(lMs * 0.001f * (float)mSampleRate);
            wetR += mLineR.Read(rMs * 0.001f * (float)mSampleRate);
         }
         wetL /= (float)taps;
         wetR /= (float)taps;

         mLineL.Write(inL + wetL * feedback);
         mLineR.Write(inR + wetR * feedback);
      }

      out.channels[0][i] = wetL;
      if (numChannels >= 2)
         out.channels[1][i] = wetR;
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}
