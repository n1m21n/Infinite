#include "AudioEffectNode.h"
#include "audio/dsp/ConvolutionKernel.h"
#include "audio/dsp/ShapeResonatorKernel.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"

// The runtime every EffectDef's kernel is wrapped in - the concrete home for
// §0.5's shared conventions (mix, denormal guard) so no individual kernel has
// to reimplement them. Latency is reported straight from the kernel
// (LatencySamples below), and the dry half of the mix is delayed by that same
// amount: PDC aligns this node's whole output with its siblings, so an
// undelayed dry copy would land early against the wet one - a comb for the
// Limiter's 1.5 ms look-ahead, a ~46 ms slapback for the 2048-point spectral
// effects - at any mix below 100%.
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
   explicit AudioEffectRuntime(const EffectDef& def) : mKernel(def.makeKernel()), mLinearMix(def.linearMix)
   {
      static constexpr double kFallbackSampleRate = 48000.0;
      mKernel->PrepareToPlay(kFallbackSampleRate, kAudioMaxBlockFrames);
      mPreparedRate.store(kFallbackSampleRate, std::memory_order_relaxed);
      ResizeBuffers(kAudioMaxBlockFrames);
   }

   void PrepareToPlay(double sampleRate, int maxBlockSize) override
   {
      mMixSmoother.SetTimeConstant(0.005f, sampleRate);
      mMixSmoother.SetImmediate(mMixTarget.load(std::memory_order_relaxed));
      mKernel->PrepareToPlay(sampleRate, maxBlockSize);
      mPreparedRate.store(sampleRate, std::memory_order_relaxed);
      ResizeBuffers(maxBlockSize);
   }

   // The rate the kernel was last prepared at - the one its coefficients must
   // be computed for. Not AudioEngine::SampleRate(): with no device open
   // (offline render, self-tests) that reads 0 while the kernel still runs.
   double PreparedRate() const { return mPreparedRate.load(std::memory_order_relaxed); }

   void Reset() override
   {
      mKernel->Reset();
      std::fill(mDryStorage.begin(), mDryStorage.end(), 0.0f);
      mDryPos = 0;
   }

   // The dry path's delay this block: the kernel's current latency, which a
   // mode-switching kernel may change at any time (never above the ring).

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

      const int dryDelay = std::clamp(mKernel->LatencySamples(), 0, mDryLength - 1);

      AudioBuffer wet;
      wet.channels = mWetChannels;
      wet.numChannels = numChannels;
      wet.numFrames = output.numFrames;

      mKernel->ProcessBlock(in, sidechainPtr, wet);

      // A kernel that aligns its own dry signal (IEffectKernel::ProcessDry)
      // replaces the delay ring below.
      AudioBuffer kernelDry;
      kernelDry.channels = mDryChannels;
      kernelDry.numChannels = numChannels;
      kernelDry.numFrames = output.numFrames;
      const bool hasKernelDry = mKernel->ProcessDry(in, kernelDry);

      // A NaN or inf that reaches a recursive kernel (filter state, a delay
      // line, a reverb tail, an oversampler) never leaves: every later block
      // reads it back, so one bad input block would silence this chain until
      // the patch is reloaded. Clear the kernel and drop its block instead;
      // the dry path still carries what came in. Every kernel's Reset() only
      // zero-fills. Checked after ProcessDry, whose own filters (Drive's dry
      // oversampler) hold state too.
      bool finite = true;
      for (int ch = 0; ch < numChannels && finite; ch++)
         for (int i = 0; i < output.numFrames; i++)
            if (!std::isfinite(wet.channels[ch][i]) || (hasKernelDry && !std::isfinite(mDryChannels[ch][i])))
            {
               finite = false;
               break;
            }
      if (!finite)
      {
         mKernel->Reset();
         for (int ch = 0; ch < numChannels; ch++)
         {
            std::fill(wet.channels[ch], wet.channels[ch] + wet.numFrames, 0.0f);
            if (hasKernelDry)
               std::copy(in.channels[ch], in.channels[ch] + output.numFrames, mDryChannels[ch]);
         }
      }

      float peak = 0.0f;
      float monoBuf[kAudioMaxBlockFrames];
      for (int i = 0; i < output.numFrames; i++)
      {
         const float m = std::clamp(mMixSmoother.Process(mMixTarget.load(std::memory_order_relaxed)), 0.0f, 1.0f);
         // Equal-power crossfade (§0.5) for a wet signal that is decorrelated
         // from the dry (reverb, delay, pitch, spectral) - so a mix of 0.5
         // doesn't read as quieter than either extreme. A linear one for a wet
         // signal that is still essentially the dry one, processed (dynamics,
         // drive, filtering) - two near-identical signals summed at
         // cos + sin = 1.414 would make the half-way setting +3 dB louder.
         float dryGain, wetGain;
         if (mLinearMix)
         {
            dryGain = 1.0f - m;
            wetGain = m;
         }
         else
         {
            const float angle = m * (float)M_PI * 0.5f;
            dryGain = cosf(angle);
            wetGain = sinf(angle);
         }
         float monoSum = 0.0f;
         for (int ch = 0; ch < numChannels; ch++)
         {
            float dry = hasKernelDry ? mDryChannels[ch][i] : in.channels[ch][i];
            if (!hasKernelDry && mDryLength > 1)
            {
               float* ring = &mDryStorage[(size_t)ch * mDryLength];
               ring[mDryPos] = dry;
               int readPos = mDryPos - dryDelay;
               if (readPos < 0)
                  readPos += mDryLength;
               dry = ring[readPos];
            }
            const float v = dry * dryGain + wet.channels[ch][i] * wetGain;
            output.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
            monoSum += v;
         }
         if (mDryLength > 1 && ++mDryPos >= mDryLength)
            mDryPos = 0;
         monoBuf[i] = numChannels > 0 ? monoSum / (float)numChannels : 0.0f;
      }
      mMeter.Write(&peak, 1);
      // Post-mix mono signal for the EQ visualizer's live spectrum trace -
      // same lock-free MeterRing, same full-rate write AudioColorRampNode
      // already does for its own FFT (AudioColorRampNode.cpp), just tapped
      // here instead of via a dedicated sink node.
      mSpectrumRing.Write(monoBuf, output.numFrames);
   }

   // Main thread only.
   void PushMix(float mixValue) { mMixTarget.store(std::clamp(mixValue, 0.0f, 1.0f), std::memory_order_relaxed); }

   IEffectKernel& Kernel() { return *mKernel; }
   MeterRing& Meter() { return mMeter; }
   int LatencySamples() const override { return mKernel->LatencySamples(); }
   MeterRing* KernelExtraMeter() { return mKernel->ExtraMeter(); }
   // Main thread only.
   int ReadSpectrumSamples(float* out, int maxCount) { return mSpectrumRing.Read(out, maxCount); }

private:
   static constexpr int kMaxChannels = 8;

   void ResizeBuffers(int maxBlockSize)
   {
      mMaxBlockSize = std::max(1, maxBlockSize);
      mWetStorage.assign((size_t)mMaxBlockSize * kMaxChannels, 0.0f);
      mSilence.assign((size_t)mMaxBlockSize, 0.0f);
      mDryBlockStorage.assign((size_t)mMaxBlockSize * kMaxChannels, 0.0f);
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         mWetChannels[ch] = mWetStorage.data() + (size_t)ch * mMaxBlockSize;
         mDryChannels[ch] = mDryBlockStorage.data() + (size_t)ch * mMaxBlockSize;
      }
      // The kernel has just been prepared, so its latency for this rate is
      // known. Sized here, on the main thread, never in ProcessBlock.
      // One slot more than the longest delay: the ring holds the sample just
      // written plus MaxLatencySamples() of history.
      mDryLength = std::max(0, mKernel->MaxLatencySamples()) + 1;
      mDryStorage.assign((size_t)mDryLength * kMaxChannels, 0.0f);
      mDryPos = 0;
   }

   std::unique_ptr<IEffectKernel> mKernel;
   const bool mLinearMix;
   DspMath::OnePole mMixSmoother;
   std::atomic<float> mMixTarget { 1.0f };
   std::atomic<double> mPreparedRate { 0.0 };

   std::vector<float> mWetStorage;
   float* mWetChannels[kMaxChannels] = {};
   std::vector<float> mDryBlockStorage; // one block of kernel-aligned dry, see IEffectKernel::ProcessDry
   float* mDryChannels[kMaxChannels] = {};
   std::vector<float> mSilence;
   int mMaxBlockSize = 0;

   // Dry-path delay line, one ring of mDryLength samples per channel; read
   // `LatencySamples()` behind the write.
   std::vector<float> mDryStorage;
   int mDryLength = 0;
   int mDryPos = 0;

   MeterRing mMeter;
   MeterRing mSpectrumRing;
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

bool AudioEffectNode::KernelPreparing() const
{
   return mAudioNode && mAudioNode->Kernel().Preparing();
}

void AudioEffectNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioEffectRuntime>(mDef);

   // EQ has no mix control on its body (the "output" section was removed
   // entirely) - always push fully wet regardless of whatever value happens
   // to be stored in `mix` (e.g. carried over from an older save), rather
   // than leaving it silently blended with no UI to see or fix it.
   mAudioNode->PushMix(mDef.forceFullyWet ? 1.0f : mix);
   // A kernel that derives run-time coefficients (Audio Filter's tan()) needs
   // the rate it actually runs at: the one it was prepared at (the device's,
   // or the offline render's when no device is open).
   const double sampleRate = mAudioNode->PreparedRate();
   mAudioNode->Kernel().PushParams(*this, sampleRate > 0.0 ? sampleRate : 48000.0);

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
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
   // A patch file, OSC or RPC set stores any finite float; the knobs bind
   // only the UI. Kernels trust the def's range (Stereo's width scales the
   // side signal by it), so hold every param to it, as the knobs do.
   mix = std::clamp(mix, 0.0f, 1.0f);
   for (size_t i = 0; i < mDef.params.size(); i++)
      mParamValues[i] = std::clamp(mParamValues[i], mDef.params[i].minVal, mDef.params[i].maxVal);
   if (mDef.hasFile)
      v.Text("file", filePath);
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

int AudioEffectNode::ReadSpectrumSamples(float* out, int maxCount)
{
   return mAudioNode ? mAudioNode->ReadSpectrumSamples(out, maxCount) : 0;
}

int AudioEffectNode::ReadModeFrequencies(float* out, int maxCount, bool* solving)
{
   if (solving)
      *solving = false;
   if (!mAudioNode)
      return 0;
   auto* k = dynamic_cast<ShapeResonatorKernel*>(&mAudioNode->Kernel());
   if (!k)
      return 0;
   if (solving)
      *solving = k->Solving();
   return k->ModeFrequencies(out, maxCount);
}

int AudioEffectNode::ReadImpulseEnvelope(float* outDb, int maxCount, float* seconds, bool* building, bool* usingFile,
                                         const std::string** fileError)
{
   static const std::string kNone;
   if (building)
      *building = false;
   if (usingFile)
      *usingFile = false;
   if (fileError)
      *fileError = &kNone;
   if (!mAudioNode)
      return 0;
   auto* k = dynamic_cast<ConvolutionKernel*>(&mAudioNode->Kernel());
   if (!k)
      return 0;
   if (building)
      *building = k->Building();
   if (usingFile)
      *usingFile = k->UsingFile();
   if (fileError)
      *fileError = &k->FileError();
   return k->Envelope(outDb, maxCount, seconds);
}
