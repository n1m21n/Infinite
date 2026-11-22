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
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

void PhaserKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const int stages = mStageCount.load(std::memory_order_relaxed);
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;
   const double invSr = 1.0 / mSampleRate;
   const float srF = (float)mSampleRate;
   const float syncRateHz = sync
      ? (float)MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, std::max(1.0, (double)Transport::Instance().Tempo()))
      : 0.0f;
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr float kPi = 3.14159265358979323846f;

   float lastSpread = -1.0f, spreadCos = 1.0f, spreadSin = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float cutoffHz = mMailbox.SmoothedValue(kCutoffHz);
      const float rateHz = sync ? syncRateHz : std::max(0.0f, mMailbox.SmoothedValue(kRateHz));
      const float depth = std::clamp(mMailbox.SmoothedValue(kDepth), 0.0f, 1.0f);
      const float spread = std::clamp(mMailbox.SmoothedValue(kSpread), 0.0f, 1.0f);
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

      const float octaves = depth;

      if (analog)
      {
         const float lfoDrift = mDriftLfo.Advance(std::max(0.1f, rateHz), 0.15f, 0.04f, mSampleRate) * 0.04f;
         const float ang = kTwoPi * ((float)mPhase + lfoDrift);
         const float sL = sinf(ang);
         const float fcL = cutoffHz * exp2f(octaves * sL);
         const float aL = AllpassCoeff(fcL, srF);
         const float aR = (spread == 0.0f) ? aL : AllpassCoeff(cutoffHz * exp2f(octaves * (sL * spreadCos + cosf(ang) * spreadSin)), srF);

         float xL = AnalogDsp::AsymTanh(in.channels[0][i] + kFeedbackAnalog * mFbL, 0.06f);
         float xR = numChannels >= 2 ? AnalogDsp::AsymTanh(in.channels[1][i] + kFeedbackAnalog * mFbR, 0.06f) : xL;
         for (int s = 0; s < stages; s++)
         {
            xL = mStagesL[s].Process(xL, aL);
            xL = AnalogDsp::AsymTanh(xL, 0.04f);
            xR = mStagesR[s].Process(xR, aR);
            xR = AnalogDsp::AsymTanh(xR, 0.04f);
         }
         mFbL = xL;
         mFbR = xR;

         out.channels[0][i] = xL;
         if (numChannels >= 2)
            out.channels[1][i] = xR;
      }
      else
      {
         const float ang = kTwoPi * (float)mPhase;
         const float sL = sinf(ang);
         const float fcL = cutoffHz * exp2f(octaves * sL);
         const float aL = AllpassCoeff(fcL, srF);
         const float aR = (spread == 0.0f) ? aL : AllpassCoeff(cutoffHz * exp2f(octaves * (sL * spreadCos + cosf(ang) * spreadSin)), srF);

         // Delay-free feedback: the cascade's output is y = G u + S (G the
         // product of the stage coefficients, S what the stage states already
         // fix), and u = x + k y, so y = (G x + S) / (1 - k G) and the loop
         // needs no unit delay. A z^-1 in the loop is a different time at each
         // rate, so it moved the feedback's peaks (6 kHz read 1.4 dB apart
         // between 44.1 and 96 kHz); this is the analog loop at every rate.
         float gL = 1.0f, sL_acc = 0.0f, gR = 1.0f, sR_acc = 0.0f;
         for (int s = 0; s < stages; s++)
         {
            gL *= aL;
            sL_acc = aL * sL_acc + mStagesL[s].State(aL);
            gR *= aR;
            sR_acc = aR * sR_acc + mStagesR[s].State(aR);
         }
         const float inL = in.channels[0][i];
         const float inR = numChannels >= 2 ? in.channels[1][i] : in.channels[0][i];
         const float yL = (gL * inL + sL_acc) / (1.0f - kFeedbackDigital * gL);
         const float yR = (gR * inR + sR_acc) / (1.0f - kFeedbackDigital * gR);
         float xL = inL + kFeedbackDigital * yL;
         float xR = inR + kFeedbackDigital * yR;
         for (int s = 0; s < stages; s++)
         {
            xL = mStagesL[s].Process(xL, aL);
            xR = mStagesR[s].Process(xR, aR);
         }
         mFbL = xL;
         mFbR = xR;

         out.channels[0][i] = xL;
         if (numChannels >= 2)
            out.channels[1][i] = xR;
      }

      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}
