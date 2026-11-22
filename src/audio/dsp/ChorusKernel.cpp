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
   const double invSr = 1.0 / mSampleRate;
   const float msToSamples = 0.001f * (float)mSampleRate;
   const float invTaps = 1.0f / (float)taps;
   const float syncRateHz = sync
      ? (float)MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, std::max(1.0, (double)Transport::Instance().Tempo()))
      : 0.0f;
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr float kPi = 3.14159265358979323846f;
   constexpr float kSqrt3Over2 = 0.86602540378443864676f;

   float lastSpread = -1.0f, spreadCos = 1.0f, spreadSin = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      float delayMs = mMailbox.SmoothedValue(kDelayMs);
      if (!mDelaySlewInit)
      {
         mDelaySlew.SetImmediate(delayMs);
         mDelaySlewInit = true;
      }
      delayMs = mDelaySlew.Process(delayMs);
      const float spread = std::clamp(mMailbox.SmoothedValue(kSpread), 0.0f, 1.0f);
      const float depthMs = mMailbox.SmoothedValue(kDepthMs);
      const float feedback = std::clamp(mMailbox.SmoothedValue(kFeedback), 0.0f, 0.9f);
      const float rateHz = sync ? syncRateHz : std::max(0.0f, mMailbox.SmoothedValue(kRateHz));

      if (spread != lastSpread)
      {
         lastSpread = spread;
         const float ang = kPi * spread;
         spreadCos = cosf(ang);
         spreadSin = sinf(ang);
      }

      mPhase += (double)rateHz * invSr;
      if (mPhase >= 1.0)
         mPhase -= floor(mPhase);

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;

      float wetL = 0.0f, wetR = 0.0f;
      if (analog)
      {
         const float lfoDrift = mDriftLfo.Advance(std::max(0.1f, rateHz), 0.15f, 0.04f, mSampleRate) * 0.1f;
         const float baseAng = kTwoPi * ((float)mPhase + lfoDrift);
         const float sL = sinf(baseAng);
         const float cL = cosf(baseAng);
         const float sR = sL * spreadCos + cL * spreadSin;
         const float cR = cL * spreadCos - sL * spreadSin;

         if (taps == 2)
         {
            wetL = mLineL.Read(std::clamp(delayMs + depthMs * sL, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineL.Read(std::clamp(delayMs - depthMs * sL, 0.5f, lineCapacityMs) * msToSamples);
            wetR = mLineR.Read(std::clamp(delayMs + depthMs * sR, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineR.Read(std::clamp(delayMs - depthMs * sR, 0.5f, lineCapacityMs) * msToSamples);
         }
         else
         {
            const float sL1 = -0.5f * sL + kSqrt3Over2 * cL;
            const float sL2 = -0.5f * sL - kSqrt3Over2 * cL;
            const float sR1 = -0.5f * sR + kSqrt3Over2 * cR;
            const float sR2 = -0.5f * sR - kSqrt3Over2 * cR;
            wetL = mLineL.Read(std::clamp(delayMs + depthMs * sL, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineL.Read(std::clamp(delayMs + depthMs * sL1, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineL.Read(std::clamp(delayMs + depthMs * sL2, 0.5f, lineCapacityMs) * msToSamples);
            wetR = mLineR.Read(std::clamp(delayMs + depthMs * sR, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineR.Read(std::clamp(delayMs + depthMs * sR1, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineR.Read(std::clamp(delayMs + depthMs * sR2, 0.5f, lineCapacityMs) * msToSamples);
         }
         wetL *= invTaps;
         wetR *= invTaps;

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
         const float baseAng = kTwoPi * (float)mPhase;
         const float sL = sinf(baseAng);
         const float cL = cosf(baseAng);
         const float sR = sL * spreadCos + cL * spreadSin;
         const float cR = cL * spreadCos - sL * spreadSin;

         if (taps == 2)
         {
            wetL = mLineL.Read(std::clamp(delayMs + depthMs * sL, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineL.Read(std::clamp(delayMs - depthMs * sL, 0.5f, lineCapacityMs) * msToSamples);
            wetR = mLineR.Read(std::clamp(delayMs + depthMs * sR, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineR.Read(std::clamp(delayMs - depthMs * sR, 0.5f, lineCapacityMs) * msToSamples);
         }
         else
         {
            const float sL1 = -0.5f * sL + kSqrt3Over2 * cL;
            const float sL2 = -0.5f * sL - kSqrt3Over2 * cL;
            const float sR1 = -0.5f * sR + kSqrt3Over2 * cR;
            const float sR2 = -0.5f * sR - kSqrt3Over2 * cR;
            wetL = mLineL.Read(std::clamp(delayMs + depthMs * sL, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineL.Read(std::clamp(delayMs + depthMs * sL1, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineL.Read(std::clamp(delayMs + depthMs * sL2, 0.5f, lineCapacityMs) * msToSamples);
            wetR = mLineR.Read(std::clamp(delayMs + depthMs * sR, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineR.Read(std::clamp(delayMs + depthMs * sR1, 0.5f, lineCapacityMs) * msToSamples)
                 + mLineR.Read(std::clamp(delayMs + depthMs * sR2, 0.5f, lineCapacityMs) * msToSamples);
         }
         wetL *= invTaps;
         wetR *= invTaps;

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
