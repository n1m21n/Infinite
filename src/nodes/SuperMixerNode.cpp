#include "SuperMixerNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "audio/AudioNode.h"
#include "audio/DspMath.h"

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
      if (sampleRate > 0.0 && sampleRate != mSampleRate)
      {
         mSampleRate = sampleRate;
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
      mMaster.store(DspMath::DbToLinear(std::clamp(n.masterDb, -60.0f, 12.0f)), std::memory_order_relaxed);
   }

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

      const float master = mMaster.load(std::memory_order_relaxed);
      float peak = 0.0f;
      for (int ch = 0; ch < output.numChannels; ch++)
      {
         float* o = output.channels[ch];
         if (ch >= 2)
            std::copy(output.channels[0], output.channels[0] + frames, o);
         for (int i = 0; i < frames; i++)
         {
            o[i] *= master;
            peak = std::max(peak, std::fabs(o[i]));
         }
      }
      mPeak.store(peak, std::memory_order_relaxed);
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
   std::atomic<float> mMaster { 1.0f };
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

void SuperMixerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   mAudioNode->PushParams(*this);
   mLevel = mAudioNode->Peak();
   for (int c = 0; c < kChannels; c++)
      mChannelLevel[c] = mAudioNode->ChannelPeak(c);
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
}
