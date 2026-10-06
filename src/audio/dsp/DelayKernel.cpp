#include "DelayKernel.h"

#include "audio/MusicTime.h"
#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

// Main thread only. bounce/sync/rateDiv are discrete selectors pushed as
// plain atomics; everything else is continuous and goes through the mailbox
// for per-sample smoothing.
void DelayKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   mBounce.store(node.Param("bounce") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mSync.store(node.Param("sync") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mRateDiv.store(std::clamp((int)(node.Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1),
                  std::memory_order_relaxed);
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);

   mMailbox.Push(kFeedbackPct, node.Param("feedback"));
   mMailbox.Push(kTone, node.Param("tone"));
   mMailbox.Push(kFreeMs, node.Param("timeMs"));
   mMailbox.Push(kPan, node.Param("pan"));
   mMailbox.Push(kDucking, node.Param("ducking"));
}

namespace
{
   // Soft ceiling on the *output* only, never on the feedback coefficient -
   // feedback above 100% is deliberate (it's how a delay self-oscillates),
   // so the coefficient itself is never clamped, only what comes out.
   inline float SoftClipOutput(float x)
   {
      return DspMath::FastTanh(x * 0.5f) * 2.0f;
   }
}

void DelayKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const bool bounce = mBounce.load(std::memory_order_relaxed) != 0;
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;
   const int lineCapacitySamples = (int)std::ceil(kMaxDelaySeconds * (float)mSampleRate) - 8;

   float blockDryPeak = 0.0f, blockWetPeak = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float feedbackFrac = mMailbox.SmoothedValue(kFeedbackPct) / 100.0f; // >1.0 is legal (self-oscillation)
      const float tone = mMailbox.SmoothedValue(kTone);
      const float freeMs = mMailbox.SmoothedValue(kFreeMs);
      const float pan = std::clamp(mMailbox.SmoothedValue(kPan), -1.0f, 1.0f);
      const float ducking = std::clamp(mMailbox.SmoothedValue(kDucking), 0.0f, 1.0f);

      // Time: tempo-synced RateDivision or a free millisecond value, run
      // through its own slow (20ms) smoother so a time change sweeps rather
      // than zippers.
      float baseSeconds;
      if (sync)
      {
         const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
         baseSeconds = (float)(MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv) * 60.0 / bpm);
      }
      else
      {
         baseSeconds = freeMs * 0.001f;
      }
      const float targetSamples = std::clamp(baseSeconds * (float)mSampleRate, 1.0f, (float)lineCapacitySamples);

      float delaySamples;
      if (analog)
      {
         if (!mDelaySmootherAnalogInit)
         {
            mDelaySmootherAnalog.SetImmediate(targetSamples);
            mDelaySmootherAnalogInit = true;
         }
         const float rawDelaySamples = mDelaySmootherAnalog.Process(targetSamples);
         const float wow = mWowFlutter.Advance(0.5f, 0.4f, 4.2f, mSampleRate);
         const float modSamples = wow * 0.0025f * (float)mSampleRate;
         delaySamples = std::clamp(rawDelaySamples + modSamples, 1.0f, (float)lineCapacitySamples);
      }
      else
      {
         if (!mDelaySmootherInit)
         {
            mDelaySmoother.SetImmediate(targetSamples);
            mDelaySmootherInit = true;
         }
         delaySamples = mDelaySmoother.Process(targetSamples);
      }

      const float inMono = numChannels >= 2 ? 0.5f * (in.channels[0][i] + in.channels[1][i]) : in.channels[0][i];

      // Duck: the dry input sidechains the wet signal. Asymmetric peak
      // follower (5ms attack / 150ms release), normalized against a nominal
      // -18 dBFS reference so a moderate dry signal produces a moderate duck.
      const float dryAbs = std::fabs(inMono);
      const float atkCoeff = expf(-1.0f / (0.005f * (float)mSampleRate));
      const float relCoeff = expf(-1.0f / (0.15f * (float)mSampleRate));
      const float duckCoeff = dryAbs > mDryEnv ? atkCoeff : relCoeff;
      mDryEnv = dryAbs + (mDryEnv - dryAbs) * duckCoeff;
      const float duckGain = 1.0f - ducking * std::clamp(mDryEnv * 8.0f, 0.0f, 1.0f);

      float wetL, wetR;
      if (analog)
      {
         if (bounce)
         {
            const float outA = mLineA.Read(delaySamples);
            const float outB = mLineB.Read(delaySamples);
            float fbA = mFilterA.Process(outA * feedbackFrac, tone, (float)mSampleRate);
            float fbB = mFilterB.Process(outB * feedbackFrac, tone, (float)mSampleRate);
            fbA = mAnalogLpA.Process(mAnalogHpA.Process(fbA));
            fbB = mAnalogLpB.Process(mAnalogHpB.Process(fbB));
            fbA = AnalogDsp::AsymTanh(fbA, 0.15f);
            fbB = AnalogDsp::AsymTanh(fbB, 0.15f);
            mLineA.Write(inMono + fbB);
            mLineB.Write(fbA);
            wetL = SoftClipOutput(outA);
            wetR = SoftClipOutput(outB);
         }
         else
         {
            const float delayed = mLine.Read(delaySamples);
            float fb = mLineFilter.Process(delayed * feedbackFrac, tone, (float)mSampleRate);
            fb = mAnalogLp.Process(mAnalogHp.Process(fb));
            fb = AnalogDsp::AsymTanh(fb, 0.15f);
            mLine.Write(inMono + fb);
            wetL = wetR = SoftClipOutput(delayed);
         }
      }
      else
      {
         if (bounce)
         {
            const float outA = mLineA.Read(delaySamples);
            const float outB = mLineB.Read(delaySamples);
            const float fbA = mFilterA.Process(outA * feedbackFrac, tone, (float)mSampleRate);
            const float fbB = mFilterB.Process(outB * feedbackFrac, tone, (float)mSampleRate);
            mLineA.Write(inMono + fbB);
            mLineB.Write(fbA);
            wetL = SoftClipOutput(outA);
            wetR = SoftClipOutput(outB);
         }
         else
         {
            const float delayed = mLine.Read(delaySamples);
            const float fb = mLineFilter.Process(delayed * feedbackFrac, tone, (float)mSampleRate);
            mLine.Write(inMono + fb);
            wetL = wetR = SoftClipOutput(delayed);
         }
      }

      // Pan: a balance law (not equal-power) so center stays unity gain on
      // both sides rather than dipping 3dB, since this scales an already-
      // computed stereo pair rather than spreading a single mono source.
      const float leftGain = 1.0f - std::max(0.0f, pan);
      const float rightGain = 1.0f - std::max(0.0f, -pan);

      out.channels[0][i] = wetL * leftGain * duckGain;
      if (numChannels >= 2)
         out.channels[1][i] = wetR * rightGain * duckGain;
      for (int ch = 2; ch < numChannels; ch++)
         out.channels[ch][i] = 0.0f;

      for (int ch = 0; ch < numChannels; ch++)
      {
         blockDryPeak = std::max(blockDryPeak, std::fabs(in.channels[ch][i]));
         blockWetPeak = std::max(blockWetPeak, std::fabs(out.channels[ch][i]));
      }
   }

   const float payload[2] = { blockDryPeak, blockWetPeak };
   mLevelMeter.Write(payload, 2);
}
