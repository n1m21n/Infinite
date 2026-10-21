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
   mMailbox.Push(kWow, node.Param("wow"));
   mMailbox.Push(kFlutter, node.Param("flutter"));
   mMailbox.Push(kSaturation, node.Param("saturation"));
   mMailbox.Push(kDiffusion, node.Param("diffusion"));
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

   // What is written back into a clean line. Feedback above 100% grows the
   // line by that factor every pass, so with nothing in the loop it ran away
   // without bound (to inf, then NaN, after a few minutes of self-
   // oscillation) while SoftClipOutput hid it at +6 dBFS. A soft ceiling,
   // bit-identical below the knee, makes it settle near 0 dBFS instead -
   // the analog path's AsymTanh already does this job there.
   inline float LineCeiling(float x) { return DspMath::SoftCeiling(x, 0.9f); }

   // Tape saturation in the loop: the drive goes in and comes back out, and
   // the bias grows with it, so a hot tape rounds the peaks and also takes
   // up to ~3 dB off each pass. Saturation 0 is the analog curve this mode
   // always had.
   inline float TapeSaturate(float x, float saturation)
   {
      const float drive = 1.0f + 7.0f * saturation; // up to +18 dB
      return AnalogDsp::AsymTanh(x * drive, 0.15f + 0.45f * saturation) / drive;
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
      float wow = 0.0f, flutter = 0.0f, saturation = 0.0f, diffusion = 0.0f;
      if (analog)
      {
         wow = std::clamp(mMailbox.SmoothedValue(kWow), 0.0f, 1.0f);
         flutter = std::clamp(mMailbox.SmoothedValue(kFlutter), 0.0f, 1.0f);
         saturation = std::clamp(mMailbox.SmoothedValue(kSaturation), 0.0f, 1.0f);
         diffusion = std::clamp(mMailbox.SmoothedValue(kDiffusion), 0.0f, 1.0f);
         if (!mDelaySmootherAnalogInit)
         {
            mDelaySmootherAnalog.SetImmediate(targetSamples);
            mDelaySmootherAnalogInit = true;
         }
         const float rawDelaySamples = mDelaySmootherAnalog.Process(targetSamples);
         // Wow 0.5 is the 2.5 ms this mode always swung by; flutter adds up
         // to 0.25 ms at the capstan's ~6.5 Hz.
         const float wowMod = mWowFlutter.Advance(0.5f, 0.4f, 4.2f, mSampleRate) * wow * 0.005f;
         const float flutterMod = mFlutter.Advance(6.5f, 0.2f, 0.7f, mSampleRate) * flutter * 0.00025f;
         const float modSamples = (wowMod + flutterMod) * (float)mSampleRate;
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
      // The diffused tap is read early by the chain's length, so the loop
      // through it is still `delaySamples` long.
      const float loopSamples = std::max(1.0f, delaySamples - (float)mDiffuseSamples);

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
         // Diffusion fades the plain tap into one smeared by the allpass
         // chain; the chain always runs so turning it up never clicks.
         auto diffuse = [diffusion](DelayAllpass* chain, float plain, float early) {
            for (int k = 0; k < kDiffuseStages; k++)
               early = chain[k].Process(early, kDiffuseGain);
            return plain + diffusion * (early - plain);
         };
         if (bounce)
         {
            const float outA = mLineA.Read(delaySamples);
            const float outB = mLineB.Read(delaySamples);
            const float tapA = diffuse(mDiffuseA, outA, mLineA.Read(loopSamples));
            const float tapB = diffuse(mDiffuseB, outB, mLineB.Read(loopSamples));
            float fbA = mFilterA.Process(tapA * feedbackFrac, tone, (float)mSampleRate);
            float fbB = mFilterB.Process(tapB * feedbackFrac, tone, (float)mSampleRate);
            fbA = TapeSaturate(mAnalogLpA.Process(mAnalogHpA.Process(fbA)), saturation);
            fbB = TapeSaturate(mAnalogLpB.Process(mAnalogHpB.Process(fbB)), saturation);
            mLineA.Write(inMono + fbB);
            mLineB.Write(fbA);
            wetL = SoftClipOutput(outA);
            wetR = SoftClipOutput(outB);
         }
         else
         {
            const float delayed = mLine.Read(delaySamples);
            const float tap = diffuse(mDiffuse, delayed, mLine.Read(loopSamples));
            float fb = mLineFilter.Process(tap * feedbackFrac, tone, (float)mSampleRate);
            fb = TapeSaturate(mAnalogLp.Process(mAnalogHp.Process(fb)), saturation);
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
            mLineA.Write(LineCeiling(inMono + fbB));
            mLineB.Write(LineCeiling(fbA));
            wetL = SoftClipOutput(outA);
            wetR = SoftClipOutput(outB);
         }
         else
         {
            const float delayed = mLine.Read(delaySamples);
            const float fb = mLineFilter.Process(delayed * feedbackFrac, tone, (float)mSampleRate);
            mLine.Write(LineCeiling(inMono + fb));
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
