#include "AudioEffectNode.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"

// The runtime every EffectDef's kernel is wrapped in - the concrete home for
// §0.5's shared conventions (mix, denormal guard) so no individual kernel has
// to reimplement them. Latency is reported straight from the kernel
// (LatencySamples below); nothing to wrap there.
class AudioEffectRuntime : public AudioNode
{
public:
   // Prepared here too, not just in PrepareToPlay: RebuildAudioTopology only
   // calls PrepareToPlay when a real device is open (sampleRate > 0), but
   // ProcessOffline (video export, and every offline self-test) will still
   // run whatever topology is published regardless of device state. Without
   // this, mSilence/mWetStorage and the kernel's own internal buffers
   // (DelayKernel's line, ReverbKernel's FDN, ...) stay default-empty and
   // the very first block dereferences a null vector<float>::data() - see
   // AUDIOTEARDOWNSWEEPTEST, which runs with no device open at all. The real
   // PrepareToPlay (with the device's actual sample rate) always follows
   // once a topology rebuild sees sampleRate > 0, so this fallback rate only
   // matters for the window before that, or when no device ever opens.
   explicit AudioEffectRuntime(const EffectDef& def) : mKernel(def.makeKernel())
   {
      static constexpr double kFallbackSampleRate = 48000.0;
      mKernel->PrepareToPlay(kFallbackSampleRate, kAudioMaxBlockFrames);
      ResizeBuffers(kAudioMaxBlockFrames);
   }

   void PrepareToPlay(double sampleRate, int maxBlockSize) override
   {
      mMixSmoother.SetTimeConstant(0.005f, sampleRate);
      mMixSmoother.SetImmediate(mMixTarget.load(std::memory_order_relaxed));
      mKernel->PrepareToPlay(sampleRate, maxBlockSize);
      ResizeBuffers(maxBlockSize);
   }

   void Reset() override { mKernel->Reset(); }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* inPtr = (numInputs > 0) ? inputs[0] : nullptr;
      const int numChannels = std::min(output.numChannels, kMaxChannels);

      float* silenceChannels[kMaxChannels];
      AudioBuffer silence;
      if (inPtr == nullptr)
      {
         for (int ch = 0; ch < numChannels; ch++)
            silenceChannels[ch] = mSilence.data();
         silence.channels = silenceChannels;
         silence.numChannels = numChannels;
         silence.numFrames = output.numFrames;
      }
      const AudioBuffer& in = (inPtr != nullptr) ? *inPtr : silence;
      // Slot 1 (sidechain) is only present at all for a hasSidechain kernel
      // (Dynamics); every other kernel ignores this and ProcessBlock's
      // numInputs never exceeds 1 for them, per AudioEffectNode's
      // AudioInputSlot(1) - see §0.5.
      const AudioBuffer* sidechainPtr = (numInputs > 1 && inputs[1] != nullptr) ? inputs[1] : nullptr;

      AudioBuffer wet;
      wet.channels = mWetChannels;
      wet.numChannels = numChannels;
      wet.numFrames = output.numFrames;

      mKernel->ProcessBlock(in, sidechainPtr, wet);

      float peak = 0.0f;
      for (int i = 0; i < output.numFrames; i++)
      {
         const float m = std::clamp(mMixSmoother.Process(mMixTarget.load(std::memory_order_relaxed)), 0.0f, 1.0f);
         // Equal-power crossfade (§0.5), not linear - so a mix of 0.5 doesn't
         // read as quieter than either extreme.
         const float angle = m * (float)M_PI * 0.5f;
         const float dryGain = cosf(angle);
         const float wetGain = sinf(angle);
         for (int ch = 0; ch < numChannels; ch++)
         {
            const float v = in.channels[ch][i] * dryGain + wet.channels[ch][i] * wetGain;
            output.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      mMeter.Write(&peak, 1);
   }

   // Main thread only.
   void PushMix(float mixValue) { mMixTarget.store(std::clamp(mixValue, 0.0f, 1.0f), std::memory_order_relaxed); }

   IEffectKernel& Kernel() { return *mKernel; }
   MeterRing& Meter() { return mMeter; }
   int LatencySamples() const { return mKernel->LatencySamples(); }
   MeterRing* KernelExtraMeter() { return mKernel->ExtraMeter(); }

private:
   static constexpr int kMaxChannels = 8;

   void ResizeBuffers(int maxBlockSize)
   {
      mMaxBlockSize = std::max(1, maxBlockSize);
      mWetStorage.assign((size_t)mMaxBlockSize * kMaxChannels, 0.0f);
      mSilence.assign((size_t)mMaxBlockSize, 0.0f);
      for (int ch = 0; ch < kMaxChannels; ch++)
         mWetChannels[ch] = mWetStorage.data() + (size_t)ch * mMaxBlockSize;
   }

   std::unique_ptr<IEffectKernel> mKernel;
   DspMath::OnePole mMixSmoother;
   std::atomic<float> mMixTarget { 1.0f };

   std::vector<float> mWetStorage;
   float* mWetChannels[kMaxChannels] = {};
   std::vector<float> mSilence;
   int mMaxBlockSize = 0;

   MeterRing mMeter;
};

AudioEffectNode::AudioEffectNode(const EffectDef& def) : mix(def.defaultMix), mDef(def)
{
   mParamValues.resize(def.params.size());
   for (size_t i = 0; i < def.params.size(); i++)
      mParamValues[i] = def.params[i].defaultVal;
}

AudioEffectNode::~AudioEffectNode() = default;

int AudioEffectNode::ParamIndex(const char* name) const
{
   for (size_t i = 0; i < mDef.params.size(); i++)
      if (mDef.params[i].name == name)
         return (int)i;
   return -1;
}

void AudioEffectNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioEffectRuntime>(mDef);

   mAudioNode->PushMix(mix);
   // A kernel that derives run-time coefficients (Audio Filter's tan()) needs
   // the real device rate; falls back to a sane default when no device is
   // open yet so a patch built before pressing play still shows a sensible
   // response curve.
   const double sampleRate = AudioEngine::Instance().SampleRate();
   mAudioNode->Kernel().PushParams(*this, sampleRate > 0.0 ? sampleRate : 44100.0);

   float peak = 0.0f;
   if (mAudioNode->Meter().Read(&peak, 1) > 0)
      mLevel = peak;

   if (MeterRing* extra = mAudioNode->KernelExtraMeter())
   {
      float buf[kMaxExtraMeterValues];
      const int n = extra->Read(buf, kMaxExtraMeterValues);
      for (int i = 0; i < n; i++)
         mExtraMeterValues[i] = buf[i];
   }
}

void AudioEffectNode::VisitParams(ParamVisitor& v)
{
   v.Float("mix", mix);
   for (size_t i = 0; i < mDef.params.size(); i++)
      v.Float(mDef.params[i].name.c_str(), mParamValues[i]);
}

AudioNode* AudioEffectNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioEffectRuntime>(mDef);
   return mAudioNode.get();
}

int AudioEffectNode::LatencySamples() const
{
   return mAudioNode ? mAudioNode->LatencySamples() : 0;
}
