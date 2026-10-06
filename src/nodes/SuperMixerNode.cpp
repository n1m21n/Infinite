#include "SuperMixerNode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MasterChain.h"

namespace
{
   constexpr double kLowShelfHz = 120.0;
   constexpr double kHighShelfHz = 8000.0;
   constexpr double kShelfQ = 0.707;
   constexpr double kMidQ = 0.9;
}

class AudioSuperMixerNode : public AudioNode
{
public:
   static constexpr int N = SuperMixerNode::kChannels;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMasterChain.Prepare(sampleRate);
      if (sampleRate > 0.0 && sampleRate != mSampleRate)
      {
         mSampleRate = sampleRate;
         mLatRate.store(sampleRate, std::memory_order_relaxed);
         for (int c = 0; c < N; c++)
            mEqDirty[c] = true;
      }
   }

   void PushParams(const SuperMixerNode& n)
   {
      for (int c = 0; c < N; c++)
      {
         mGain[c].store(DspMath::DbToLinear(std::clamp(n.gainDb[c], -60.0f, 12.0f)) *
                           (n.gainDb[c] <= -59.9f ? 0.0f : 1.0f),
                        std::memory_order_relaxed);
         mPan[c].store(std::clamp(n.pan[c], -1.0f, 1.0f), std::memory_order_relaxed);
         mTrim[c].store(DspMath::DbToLinear(std::clamp(n.trimDb[c], -24.0f, 24.0f)), std::memory_order_relaxed);
         mMute[c].store(n.mute[c], std::memory_order_relaxed);
         mSolo[c].store(n.solo[c], std::memory_order_relaxed);
         mLow[c].store(std::clamp(n.eqLow[c], -15.0f, 15.0f), std::memory_order_relaxed);
         mMid[c].store(std::clamp(n.eqMid[c], -15.0f, 15.0f), std::memory_order_relaxed);
         mMidFreq[c].store(std::clamp(n.eqMidFreq[c], 200.0f, 8000.0f), std::memory_order_relaxed);
         mHigh[c].store(std::clamp(n.eqHigh[c], -15.0f, 15.0f), std::memory_order_relaxed);
      }
      // Turbo 0.50: the master bus (fader included) runs in MasterChain.
      using namespace MasterDsp;
      MasterChain& m = mMasterChain;
      // Turbo 0.51: -inf at the bottom of the throw, like the strips above.
      m.SetParam(kFaderGain, DspMath::DbToLinear(std::clamp(n.masterDb, -60.0f, 12.0f)) *
                                (n.masterDb <= -59.9f ? 0.0f : 1.0f));
      m.SetParam(kBalance, std::clamp(n.masterPan, -1.0f, 1.0f));
      m.SetParam(kMute, n.masterMute ? 1.0f : 0.0f);
      m.SetParam(kEqOn, n.fxEqOn ? 1.0f : 0.0f);
      m.SetParam(kEqLowHz, n.fxEqLowHz);
      m.SetParam(kEqLowDb, n.fxEqLowDb);
      m.SetParam(kEqMidHz, n.fxEqMidHz);
      m.SetParam(kEqMidDb, n.fxEqMidDb);
      m.SetParam(kEqMidQ, n.fxEqMidQ);
      m.SetParam(kEqHighHz, n.fxEqHighHz);
      m.SetParam(kEqHighDb, n.fxEqHighDb);
      m.SetParam(kCompOn, n.fxCompOn ? 1.0f : 0.0f);
      m.SetParam(kCompThreshDb, n.fxCompThreshDb);
      m.SetParam(kCompRatio, n.fxCompRatio);
      m.SetParam(kCompAttackMs, n.fxCompAttackMs);
      m.SetParam(kCompReleaseMs, n.fxCompReleaseMs);
      m.SetParam(kCompMakeupDb, n.fxCompMakeupDb);
      m.SetParam(kWidthOn, n.fxWidthOn ? 1.0f : 0.0f);
      m.SetParam(kWidth, n.fxWidth);
      m.SetParam(kSatOn, n.fxSatOn ? 1.0f : 0.0f);
      m.SetParam(kSatDriveDb, n.fxSatDriveDb);
      m.SetParam(kSatMix, n.fxSatMix);
      m.SetParam(kSatOutDb, n.fxSatOutDb);
      m.SetParam(kLimOn, n.fxLimOn ? 1.0f : 0.0f);
      m.SetParam(kLimCeilingDb, n.fxLimCeilingDb);
      m.SetParam(kLimReleaseMs, n.fxLimReleaseMs);
      // Turbo 0.50: limiter v2.
      const float look = n.LimLookaheadApplied();
      m.SetParam(kLimLookaheadMs, look);
      m.SetParam(kLimLink, std::clamp(n.fxLimLink, 0.0f, 100.0f) * 0.01f);
      m.SetParam(kLimTruePeak, n.fxLimTruePeak ? 1.0f : 0.0f);
      m.SetParam(kLimAutoRelease, n.fxLimAutoRelease ? 1.0f : 0.0f);
      m.SetParam(kLimStyle, (float)std::clamp(n.fxLimStyle, 0, MasterDsp::MasterChain::kLimStyleCount - 1));
      mLatOn.store(n.fxLimOn, std::memory_order_relaxed);
      mLatMs.store(look, std::memory_order_relaxed);
   }

   // Turbo 0.50: the limiter's lookahead while it is switched in, 0 while
   // it is out (bypassed means no delay at all). Read by RebuildAudioTopology
   // and the per-frame latency watch, main thread only. The audio thread
   // follows the switch with a 2 ms duck, so delay compensation and the
   // actual delay disagree for at most a few ms around a toggle.
   int LatencySamples() const override
   {
      if (!mLatOn.load(std::memory_order_relaxed))
         return 0;
      return MasterDsp::MasterChain::LimiterLatencySamples(mLatMs.load(std::memory_order_relaxed),
                                                          mLatRate.load(std::memory_order_relaxed));
   }
   bool LatencyMayChange() const override { return true; }
   double LatencyRate() const { return mLatRate.load(std::memory_order_relaxed); }

   MasterDsp::MasterChain& Master() { return mMasterChain; }

   float Peak() const { return mPeak.load(std::memory_order_relaxed); }
   float ChannelPeak(int c) const { return mChannelPeak[c].load(std::memory_order_relaxed); }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int frames = output.numFrames;
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + frames, 0.0f);

      bool anySolo = false;
      for (int c = 0; c < N; c++)
         anySolo = anySolo || mSolo[c].load(std::memory_order_relaxed);

      const float invFrames = frames > 0 ? 1.0f / (float)frames : 0.0f;
      for (int c = 0; c < N; c++)
      {
         const AudioBuffer* in = (c < numInputs) ? inputs[c] : nullptr;
         const bool audible = !mMute[c].load(std::memory_order_relaxed) &&
                              (!anySolo || mSolo[c].load(std::memory_order_relaxed));
         float target = audible ? mGain[c].load(std::memory_order_relaxed) : 0.0f;
         if (in == nullptr || in->numChannels <= 0)
         {
            mCurrentGain[c] = target;
            mChannelPeak[c].store(0.0f, std::memory_order_relaxed);
            continue;
         }
         UpdateEq(c);

         float panL = 1.0f, panR = 1.0f;
         DspMath::EqualPowerPan(mPan[c].load(std::memory_order_relaxed), panL, panR);
         panL *= (float)M_SQRT2;
         panR *= (float)M_SQRT2;

         const float start = mCurrentGain[c];
         const float delta = (target - start) * invFrames;
         const float* srcL = in->channels[0];
         const float* srcR = in->numChannels > 1 ? in->channels[1] : srcL;
         const float trimTarget = mTrim[c].load(std::memory_order_relaxed);
         const float trimStart = mCurrentTrim[c];
         const float trimDelta = (trimTarget - trimStart) * invFrames;
         float peak = 0.0f;
         for (int i = 0; i < frames; i++)
         {
            const float g = start + delta * (float)(i + 1);
            const float t = trimStart + trimDelta * (float)(i + 1);
            float l = srcL[i] * t;
            float r = srcR[i] * t;
            if (mEqActive[c])
            {
               l = mHighF[c][0].Process(mMidF[c][0].Process(mLowF[c][0].Process(l)));
               r = mHighF[c][1].Process(mMidF[c][1].Process(mLowF[c][1].Process(r)));
            }
            l *= g * panL;
            r *= g * panR;
            if (output.numChannels > 0)
               output.channels[0][i] += l;
            if (output.numChannels > 1)
               output.channels[1][i] += r;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
         }
         mCurrentGain[c] = target;
         mCurrentTrim[c] = trimTarget;
         mChannelPeak[c].store(peak, std::memory_order_relaxed);
      }

      // Turbo 0.50: master chain (FX, fader, balance, limiter, mute,
      // meters) on the stereo pair; extra channels mirror the left one, as
      // before.
      if (output.numChannels > 0)
      {
         mMasterChain.Process(output.channels[0], output.numChannels > 1 ? output.channels[1] : nullptr, frames);
         for (int ch = 2; ch < output.numChannels; ch++)
            std::copy(output.channels[0], output.channels[0] + frames, output.channels[ch]);
      }
      mPeak.store(mMasterChain.BlockPeak(), std::memory_order_relaxed);
   }

private:
   void UpdateEq(int c)
   {
      const float low = mLow[c].load(std::memory_order_relaxed);
      const float mid = mMid[c].load(std::memory_order_relaxed);
      const float midHz = mMidFreq[c].load(std::memory_order_relaxed);
      const float high = mHigh[c].load(std::memory_order_relaxed);
      if (!mEqDirty[c] && low == mLastLow[c] && mid == mLastMid[c] && midHz == mLastMidHz[c] && high == mLastHigh[c])
         return;
      const bool wasActive = mEqActive[c];
      mEqActive[c] = std::fabs(low) > 0.01f || std::fabs(mid) > 0.01f || std::fabs(high) > 0.01f;
      for (int s = 0; s < 2; s++)
      {
         mLowF[c][s].SetLowShelf(kLowShelfHz, kShelfQ, low, mSampleRate);
         mMidF[c][s].SetPeaking(midHz, kMidQ, mid, mSampleRate);
         mHighF[c][s].SetHighShelf(kHighShelfHz, kShelfQ, high, mSampleRate);
         if (!wasActive && mEqActive[c])
         {
            mLowF[c][s].Reset();
            mMidF[c][s].Reset();
            mHighF[c][s].Reset();
         }
      }
      mLastLow[c] = low;
      mLastMid[c] = mid;
      mLastMidHz[c] = midHz;
      mLastHigh[c] = high;
      mEqDirty[c] = false;
   }

   double mSampleRate = 48000.0;
   DspMath::Biquad mLowF[N][2];
   DspMath::Biquad mMidF[N][2];
   DspMath::Biquad mHighF[N][2];
   bool mEqActive[N] = {};
   bool mEqDirty[N] = { true, true, true, true, true, true, true, true,
                        true, true, true, true, true, true, true, true };
   float mLastLow[N] = {}, mLastMid[N] = {}, mLastMidHz[N] = {}, mLastHigh[N] = {};
   float mCurrentGain[N] = {};
   float mCurrentTrim[N] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

   std::atomic<float> mGain[N] {};
   std::atomic<float> mPan[N] {};
   std::atomic<float> mTrim[N] {};
   std::atomic<bool> mMute[N] {};
   std::atomic<bool> mSolo[N] {};
   std::atomic<float> mLow[N] {};
   std::atomic<float> mMid[N] {};
   std::atomic<float> mMidFreq[N] {};
   std::atomic<float> mHigh[N] {};
   MasterDsp::MasterChain mMasterChain;
   std::atomic<bool> mLatOn { false };
   std::atomic<float> mLatMs { 1.5f };
   std::atomic<double> mLatRate { 48000.0 };
   std::atomic<float> mPeak { 0.0f };
   std::atomic<float> mChannelPeak[N] {};
};

SuperMixerNode::SuperMixerNode() : mAudioNode(std::make_unique<AudioSuperMixerNode>())
{
   for (int c = 0; c < kChannels; c++)
   {
      trimDb[c] = 0.0f;
      gainDb[c] = 0.0f;
      pan[c] = 0.0f;
      mute[c] = false;
      solo[c] = false;
      eqLow[c] = 0.0f;
      eqMid[c] = 0.0f;
      eqMidFreq[c] = 1000.0f;
      eqHigh[c] = 0.0f;
   }
   mAudioNode->PushParams(*this);
}

SuperMixerNode::~SuperMixerNode() = default;

AudioNode* SuperMixerNode::GetAudioNode()
{
   return mAudioNode.get();
}

float SuperMixerNode::LimLookaheadApplied() const
{
   if (mLookApplied >= 0.0f)
      return mLookApplied;
   return std::round(std::clamp(fxLimLookaheadMs, 0.5f, 5.0f) * 10.0f) * 0.1f;
}

float SuperMixerNode::LimLatencyMs() const
{
   const double sr = mAudioNode->LatencyRate();
   return (float)(1000.0 * MasterDsp::MasterChain::LimiterLatencySamples(LimLookaheadApplied(), sr) / sr);
}

void SuperMixerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

   // Turbo 0.50: the lookahead changes the delay (a 2 ms duck on the audio
   // thread, maybe a PDC rebuild), so it is quantised to 0.1 ms and applied
   // at most every 0.25 s: a single change lands at once, a knob drag or a
   // modulator lands 4 times a second and once more when it rests.
   const float look = std::round(std::clamp(fxLimLookaheadMs, 0.5f, 5.0f) * 10.0f) * 0.1f;
   if (look != mLookPending)
   {
      mLookPending = look;
      mLookPendingSince = now;
   }
   if (mLookApplied < 0.0f ||
       (look != mLookApplied && (now - mLookPendingSince >= 0.25 || now - mLookAppliedAt >= 0.25)))
   {
      mLookApplied = look;
      mLookAppliedAt = now;
   }

   mAudioNode->PushParams(*this);
   mLevel = mAudioNode->Peak();
   for (int c = 0; c < kChannels; c++)
      mChannelLevel[c] = mAudioNode->ChannelPeak(c);

   // Turbo 0.50: master meter ballistics (main thread).
   const float dt = mLastMeterTime < 0.0 ? 0.0f : (float)std::clamp(now - mLastMeterTime, 0.0, 0.25);
   mLastMeterTime = now;
   MasterDsp::MasterChain& m = mAudioNode->Master();
   auto toDb = [](float lin) { return lin > 1e-6f ? DspMath::LinearToDb(lin) : -120.0f; };
   for (int ch = 0; ch < 2; ch++)
   {
      const float pk = toDb(m.TakePeak(ch));
      mMeter.peakDb[ch] = std::max(pk, mMeter.peakDb[ch] - 24.0f * dt);
      if (pk >= mMeter.holdDb[ch])
      {
         mMeter.holdDb[ch] = pk;
         mMeter.holdAge[ch] = 0.0f;
      }
      else
      {
         mMeter.holdAge[ch] += dt;
         if (mMeter.holdAge[ch] > 1.5f)
            mMeter.holdDb[ch] = std::max(mMeter.peakDb[ch], mMeter.holdDb[ch] - 12.0f * dt);
      }
      mMeter.rmsDb[ch] = toDb(m.Rms(ch));
      if (m.TakeClip(ch))
         mMeter.clip[ch] = true;
   }
   mMeter.compGrDb = std::max(m.TakeCompGrDb(), mMeter.compGrDb - 20.0f * dt);
   const float limGr = m.TakeLimGrDb();
   mMeter.limGrDb = std::max(limGr, mMeter.limGrDb - 20.0f * dt);
   mMeter.limClamps += m.TakeLimClamps();
   // GR history strip: one column per ~33 ms, holding that step's max.
   mMeter.grHistAcc = std::max(mMeter.grHistAcc, limGr);
   mMeter.grHistTime += dt;
   if (mMeter.grHistTime >= 1.0f / 30.0f)
   {
      mMeter.grHistTime = std::fmod(mMeter.grHistTime, 1.0f / 30.0f);
      mMeter.grHist[mMeter.grHistPos] = mMeter.grHistAcc;
      mMeter.grHistPos = (mMeter.grHistPos + 1) % MasterMeter::kGrHist;
      mMeter.grHistAcc = 0.0f;
   }
}

void SuperMixerNode::VisitParams(ParamVisitor& v)
{
   char key[24];
   for (int c = 0; c < kChannels; c++)
   {
      snprintf(key, sizeof(key), "trim%d", c);
      v.Float(key, trimDb[c]);
      snprintf(key, sizeof(key), "gain%d", c);
      v.Float(key, gainDb[c]);
      snprintf(key, sizeof(key), "pan%d", c);
      v.Float(key, pan[c]);
      snprintf(key, sizeof(key), "mute%d", c);
      v.Bool(key, mute[c]);
      snprintf(key, sizeof(key), "solo%d", c);
      v.Bool(key, solo[c]);
      snprintf(key, sizeof(key), "eqLow%d", c);
      v.Float(key, eqLow[c]);
      snprintf(key, sizeof(key), "eqMid%d", c);
      v.Float(key, eqMid[c]);
      snprintf(key, sizeof(key), "eqMidFreq%d", c);
      v.Float(key, eqMidFreq[c]);
      snprintf(key, sizeof(key), "eqHigh%d", c);
      v.Float(key, eqHigh[c]);
   }
   v.Float("master", masterDb);

   // Turbo 0.50: master bus, appended so older patches load unchanged.
   v.Float("masterPan", masterPan);
   v.Bool("masterMute", masterMute);
   v.Bool("fxOpen", fxOpen);
   v.Bool("fxEqOn", fxEqOn);
   v.Float("fxEqLowHz", fxEqLowHz);
   v.Float("fxEqLowDb", fxEqLowDb);
   v.Float("fxEqMidHz", fxEqMidHz);
   v.Float("fxEqMidDb", fxEqMidDb);
   v.Float("fxEqMidQ", fxEqMidQ);
   v.Float("fxEqHighHz", fxEqHighHz);
   v.Float("fxEqHighDb", fxEqHighDb);
   v.Bool("fxCompOn", fxCompOn);
   v.Float("fxCompThreshDb", fxCompThreshDb);
   v.Float("fxCompRatio", fxCompRatio);
   v.Float("fxCompAttackMs", fxCompAttackMs);
   v.Float("fxCompReleaseMs", fxCompReleaseMs);
   v.Float("fxCompMakeupDb", fxCompMakeupDb);
   v.Bool("fxWidthOn", fxWidthOn);
   v.Float("fxWidth", fxWidth);
   v.Bool("fxSatOn", fxSatOn);
   v.Float("fxSatDriveDb", fxSatDriveDb);
   v.Float("fxSatMix", fxSatMix);
   v.Float("fxSatOutDb", fxSatOutDb);
   v.Bool("fxLimOn", fxLimOn);
   v.Float("fxLimCeilingDb", fxLimCeilingDb);
   v.Float("fxLimReleaseMs", fxLimReleaseMs);
   // Turbo 0.50: limiter v2, appended.
   v.Float("fxLimLookaheadMs", fxLimLookaheadMs);
   v.Float("fxLimLink", fxLimLink);
   v.Bool("fxLimTruePeak", fxLimTruePeak);
   v.Bool("fxLimAutoRelease", fxLimAutoRelease);
   v.Int("fxLimStyle", fxLimStyle);
}
