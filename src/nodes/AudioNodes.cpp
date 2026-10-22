#include "AudioNodes.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioBuffer.h"
#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "platform/Platform.h"

namespace
{
   constexpr int kGainDbParam = 0;
}

// ----------------------------------------------------------------------- Gain
class AudioGainNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kGainDbParam, mGainDb.load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      float peak = 0.0f;
      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float linear = DspMath::DbToLinear(mMailbox.SmoothedValue(kGainDbParam));
         for (int ch = 0; ch < buffer.numChannels; ch++)
         {
            const float s = (in != nullptr) ? in->channels[ch][i] : 0.0f;
            const float v = s * linear;
            buffer.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      mMeter.Write(&peak, 1);
   }

   // Main thread only.
   void PushParams(float gainDb)
   {
      mGainDb.store(gainDb, std::memory_order_relaxed);
      mMailbox.Push(kGainDbParam, gainDb);
   }

   MeterRing& Meter() { return mMeter; }

private:
   ParamMailbox mMailbox;
   MeterRing mMeter;
   std::atomic<float> mGainDb { 0.0f };
};

GainNode::GainNode() = default;
GainNode::~GainNode() = default;

void GainNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGainNode>();
   mAudioNode->PushParams(gainDb);

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
}

void GainNode::VisitParams(ParamVisitor& v)
{
   v.Float("gainDb", gainDb);
}

AudioNode* GainNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGainNode>();
   return mAudioNode.get();
}

// ---------------------------------------------------------------------- Mixer
class AudioMixerNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      for (int s = 0; s < MixerNode::kMaxSlots; s++)
         mMailbox.SetImmediate(s, mGainDb[s].load(std::memory_order_relaxed));
      mPanPrimed = false;
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int activeChannels = std::clamp(mNumChannels.load(std::memory_order_relaxed), 0, (int)MixerNode::kMaxSlots);
      const int slots = std::min(numInputs, activeChannels);

      bool anySolo = false;
      for (int s = 0; s < activeChannels; s++)
      {
         if (mSolo[s].load(std::memory_order_relaxed) != 0)
         {
            anySolo = true;
            break;
         }
      }

      // Pan, mute and solo are read once per block, not smoothed per sample.
      float panL[MixerNode::kMaxSlots] = {}, panR[MixerNode::kMaxSlots] = {};
      for (int s = 0; s < activeChannels; s++)
      {
         const bool isMuted = mMute[s].load(std::memory_order_relaxed) != 0;
         const bool isSolo = mSolo[s].load(std::memory_order_relaxed) != 0;
         if (isMuted || (anySolo && !isSolo))
         {
            panL[s] = panR[s] = 0.0f;
            continue;
         }
         DspMath::EqualPowerPan(mPan[s].load(std::memory_order_relaxed), panL[s], panR[s]);
         // EqualPowerPan is 0.707/0.707 at centre; scaled back to unity so a
         // centred, unity-gain channel passes through at exactly its input
         // level rather than 3 dB down.
         panL[s] *= (float)M_SQRT2;
         panR[s] *= (float)M_SQRT2;
      }

      // Pan, mute and solo change the gain pair at block rate; ramping from
      // last block's pair across this one turns a mute, solo or pan jump
      // from a step into a 10 ms fade. Steady state is untouched.
      if (!mPanPrimed)
      {
         for (int s = 0; s < MixerNode::kMaxSlots; s++)
         {
            mPrevPanL[s] = panL[s];
            mPrevPanR[s] = panR[s];
         }
         mPanPrimed = true;
      }
      const float invFrames = 1.0f / (float)std::max(1, output.numFrames);

      float channelPeak[MixerNode::kMaxSlots] = {};
      float peak = 0.0f;
      for (int i = 0; i < output.numFrames; i++)
      {
         float linear[MixerNode::kMaxSlots];
         float gL[MixerNode::kMaxSlots], gR[MixerNode::kMaxSlots];
         const float t = (float)(i + 1) * invFrames;
         for (int s = 0; s < activeChannels; s++)
         {
            linear[s] = DspMath::DbToLinear(mMailbox.SmoothedValue(s));
            gL[s] = mPrevPanL[s] + (panL[s] - mPrevPanL[s]) * t;
            gR[s] = mPrevPanR[s] + (panR[s] - mPrevPanR[s]) * t;
         }

         for (int ch = 0; ch < output.numChannels; ch++)
         {
            // Channel 0 is left, 1 is right; anything beyond a stereo pair
            // gets the left law rather than being silently dropped.
            const float law = (ch == 1) ? 1.0f : 0.0f;
            float sum = 0.0f;
            for (int s = 0; s < slots; s++)
            {
               if (inputs[s] == nullptr)
                  continue;
               const float v = inputs[s]->channels[ch][i] * linear[s] *
                               (law > 0.5f ? gR[s] : gL[s]);
               sum += v;
               channelPeak[s] = std::max(channelPeak[s], std::fabs(v));
            }
            output.channels[ch][i] = sum;
            peak = std::max(peak, std::fabs(sum));
         }
      }

      for (int s = 0; s < MixerNode::kMaxSlots; s++)
      {
         mPrevPanL[s] = panL[s];
         mPrevPanR[s] = panR[s];
      }
      mMeter.Write(&peak, 1);
      for (int s = 0; s < MixerNode::kMaxSlots; s++)
         mChannelPeak[s].store(s < activeChannels ? channelPeak[s] : 0.0f, std::memory_order_relaxed);
   }

   // Main thread only.
   void PushParams(const MixerNode& n)
   {
      mNumChannels.store(n.numChannels, std::memory_order_relaxed);
      for (int s = 0; s < MixerNode::kMaxSlots; s++)
      {
         mGainDb[s].store(n.gainDb[s], std::memory_order_relaxed);
         mMailbox.Push(s, n.gainDb[s]);
         mPan[s].store(n.pan[s], std::memory_order_relaxed);
         mMute[s].store(n.mute[s] ? 1 : 0, std::memory_order_relaxed);
         mSolo[s].store(n.solo[s] ? 1 : 0, std::memory_order_relaxed);
      }
   }

   MeterRing& Meter() { return mMeter; }
   float ChannelPeak(int slot) const { return mChannelPeak[slot].load(std::memory_order_relaxed); }

private:
   ParamMailbox mMailbox;
   MeterRing mMeter;
   float mPrevPanL[MixerNode::kMaxSlots] = {}, mPrevPanR[MixerNode::kMaxSlots] = {}; // audio thread only
   bool mPanPrimed = false;
   std::atomic<int> mNumChannels { 8 };
   std::atomic<float> mGainDb[MixerNode::kMaxSlots] {};
   std::atomic<float> mPan[MixerNode::kMaxSlots] {};
   std::atomic<int> mMute[MixerNode::kMaxSlots] {};
   std::atomic<int> mSolo[MixerNode::kMaxSlots] {};
   std::atomic<float> mChannelPeak[MixerNode::kMaxSlots] {};
};

MixerNode::MixerNode() = default;
MixerNode::~MixerNode() = default;

void MixerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMixerNode>();
   mAudioNode->PushParams(*this);

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
   for (int s = 0; s < kMaxSlots; s++)
      mChannelLevel[s] = mAudioNode->ChannelPeak(s);
}

void MixerNode::VisitParams(ParamVisitor& v)
{
   v.Int("channels", numChannels);
   numChannels = std::clamp(numChannels, 0, kMaxSlots); // loaded unchecked; indexes inputs[]
   static const char* kGainNames[kMaxSlots] = {
      "gain0", "gain1", "gain2", "gain3", "gain4", "gain5",
      "gain6", "gain7", "gain8", "gain9", "gain10", "gain11"
   };
   static const char* kPanNames[kMaxSlots] = {
      "pan0", "pan1", "pan2", "pan3", "pan4", "pan5",
      "pan6", "pan7", "pan8", "pan9", "pan10", "pan11"
   };
   static const char* kMuteNames[kMaxSlots] = {
      "mute0", "mute1", "mute2", "mute3", "mute4", "mute5",
      "mute6", "mute7", "mute8", "mute9", "mute10", "mute11"
   };
   static const char* kSoloNames[kMaxSlots] = {
      "solo0", "solo1", "solo2", "solo3", "solo4", "solo5",
      "solo6", "solo7", "solo8", "solo9", "solo10", "solo11"
   };
   for (int i = 0; i < kMaxSlots; i++)
   {
      v.Float(kGainNames[i], gainDb[i]);
      v.Float(kPanNames[i], pan[i]);
      v.Bool(kMuteNames[i], mute[i]);
      v.Bool(kSoloNames[i], solo[i]);
   }
}

AudioNode* MixerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMixerNode>();
   return mAudioNode.get();
}

// ------------------------------------------------------------------- Blend
namespace
{
   constexpr int kBlendParam = 0;
}

class AudioBlendNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kBlendParam, mBlend.load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* a = (numInputs > 0) ? inputs[0] : nullptr;
      const AudioBuffer* b = (numInputs > 1) ? inputs[1] : nullptr;

      float peak = 0.0f;
      for (int i = 0; i < output.numFrames; i++)
      {
         const float blend = mMailbox.SmoothedValue(kBlendParam);
         float gainA, gainB;
         DspMath::EqualPowerPan(blend * 2.0f - 1.0f, gainA, gainB);

         for (int ch = 0; ch < output.numChannels; ch++)
         {
            const float sa = (a != nullptr) ? a->channels[ch][i] : 0.0f;
            const float sb = (b != nullptr) ? b->channels[ch][i] : 0.0f;
            const float v = sa * gainA + sb * gainB;
            output.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      mMeter.Write(&peak, 1);
   }

   // Main thread only.
   void PushParams(float blend)
   {
      mBlend.store(blend, std::memory_order_relaxed);
      mMailbox.Push(kBlendParam, blend);
   }

   MeterRing& Meter() { return mMeter; }

private:
   ParamMailbox mMailbox;
   MeterRing mMeter;
   std::atomic<float> mBlend { 0.5f };
};

BlendAudioNode::BlendAudioNode() = default;
BlendAudioNode::~BlendAudioNode() = default;

void BlendAudioNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBlendNode>();
   mAudioNode->PushParams(blend);

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
}

void BlendAudioNode::VisitParams(ParamVisitor& v)
{
   v.Float("blend", blend);
}

AudioNode* BlendAudioNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBlendNode>();
   return mAudioNode.get();
}

// ------------------------------------------------------------------- Splitter
class AudioSplitterNode : public AudioNode
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kSplits = SplitterNode::kMaxBands - 1;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      for (int s = 0; s < kSplits; s++)
         mHz[s] = mTargetHz[s].load(std::memory_order_relaxed);
      Reset();
   }

   void Reset() override
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         for (int s = 0; s < kSplits; s++)
            for (DspMath::TptSvf& f : mSplit[ch][s])
               f.Reset();
         for (DspMath::TptSvf& f : mAllpass[ch])
            f.Reset();
      }
      mActiveBands = mBands.load(std::memory_order_relaxed);
      SetCutoffs();
   }

   int AudioOutputCount() const override { return SplitterNode::kMaxBands; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      AudioBuffer* outs[1] = { &output };
      ProcessBlockMulti(inputs, numInputs, outs, 1);
   }

   void ProcessBlockMulti(const AudioBuffer* const* inputs, int numInputs,
                          AudioBuffer* const* outputs, int numOutputs) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      const int bands = mBands.load(std::memory_order_relaxed);
      if (bands != mActiveBands)
      {
         // A band count change re-wires the pins, so it is an edit, not a
         // gesture: start the new tree from silence rather than reuse states
         // that belonged to a different split.
         Reset();
         mActiveBands = bands;
      }
      AudioBuffer* out[SplitterNode::kMaxBands] = {};
      for (int o = 0; o < numOutputs && o < SplitterNode::kMaxBands; o++)
         out[o] = outputs[o];
      const AudioBuffer* ref = nullptr;
      for (AudioBuffer* o : out)
         if (o != nullptr) { ref = o; break; }
      if (ref == nullptr)
         return;

      if (bands <= 1)
      {
         if (out[0] != nullptr)
         {
            AudioBuffer& output = *out[0];
            for (int ch = 0; ch < output.numChannels; ch++)
            {
               for (int i = 0; i < output.numFrames; i++)
                  output.channels[ch][i] = (in != nullptr) ? in->channels[ch][i] : 0.0f;
            }
         }
         ZeroFrom(out, 1);
         return;
      }

      SmoothCutoffs(ref->numFrames);
      const float k = 1.41421356f; // Butterworth damping, 1/Q
      const int numCh = std::min(ref->numChannels, kMaxChannels);
      for (int ch = 0; ch < numCh; ch++)
      {
         DspMath::TptSvf* x1 = mSplit[ch][0];
         DspMath::TptSvf* x2 = mSplit[ch][1];
         DspMath::TptSvf* x3 = mSplit[ch][2];
         for (int i = 0; i < ref->numFrames; i++)
         {
            const float x = (in != nullptr && ch < in->numChannels) ? in->channels[ch][i] : 0.0f;
            float band[SplitterNode::kMaxBands] = {};
            // Tree: split at 1, the high side at 2, its high side at 3.
            float lo, hi;
            Lr4(x1, x, lo, hi);
            band[0] = lo;
            if (bands == 2)
               band[1] = hi;
            else
            {
               float lo2, hi2;
               Lr4(x2, hi, lo2, hi2);
               band[0] = Allpass(mAllpass[ch][0], band[0], k); // phase of split 2
               band[1] = lo2;
               if (bands == 3)
                  band[2] = hi2;
               else
               {
                  float lo3, hi3;
                  Lr4(x3, hi2, lo3, hi3);
                  band[0] = Allpass(mAllpass[ch][1], band[0], k); // phase of split 3
                  band[1] = Allpass(mAllpass[ch][2], band[1], k);
                  band[2] = lo3;
                  band[3] = hi3;
               }
            }
            for (int b = 0; b < bands; b++)
               if (out[b] != nullptr && ch < out[b]->numChannels)
                  out[b]->channels[ch][i] = band[b];
         }
      }
      for (int b = 0; b < SplitterNode::kMaxBands; b++)
      {
         if (out[b] == nullptr)
            continue;
         for (int ch = (b < bands ? numCh : 0); ch < out[b]->numChannels; ch++)
            std::fill(out[b]->channels[ch], out[b]->channels[ch] + out[b]->numFrames, 0.0f);
      }
   }

   // Main thread only.
   void PushParams(const SplitterNode& n)
   {
      mBands.store(n.Bands(), std::memory_order_relaxed);
      mTargetHz[0].store(n.split1, std::memory_order_relaxed);
      mTargetHz[1].store(n.split2, std::memory_order_relaxed);
      mTargetHz[2].store(n.split3, std::memory_order_relaxed);
   }

private:
   // LR4: low = LP(LP(x)), high = HP(HP(x)), sharing the first section.
   static void Lr4(DspMath::TptSvf* f, float x, float& lo, float& hi)
   {
      const DspMath::TptSvf::Outputs a = f[0].Process(x);
      lo = f[1].Process(a.low).low;
      hi = f[2].Process(a.high).high;
   }

   // LP^2 + HP^2 of the LR4 above: x - 2k band.
   static float Allpass(DspMath::TptSvf& f, float x, float k)
   {
      return x - 2.0f * k * f.Process(x).band;
   }

   static void ZeroFrom(AudioBuffer* const* out, int first)
   {
      for (int b = first; b < SplitterNode::kMaxBands; b++)
      {
         if (out[b] == nullptr)
            continue;
         for (int ch = 0; ch < out[b]->numChannels; ch++)
            std::fill(out[b]->channels[ch], out[b]->channels[ch] + out[b]->numFrames, 0.0f);
      }
   }

   // Splits in order, a third of an octave apart at least, below Nyquist.
   void OrderedHz(float hz[kSplits]) const
   {
      const float top = (float)(0.45 * mSampleRate);
      float floorHz = 20.0f;
      for (int s = 0; s < kSplits; s++)
      {
         hz[s] = std::clamp(mHz[s], floorHz, std::max(floorHz, top));
         floorHz = hz[s] * 1.26f;
      }
   }

   void SetCutoffs()
   {
      float hz[kSplits];
      OrderedHz(hz);
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         for (int s = 0; s < kSplits; s++)
            for (DspMath::TptSvf& f : mSplit[ch][s])
            {
               f.SetSampleRate(mSampleRate);
               f.SetCutoff(hz[s], 0.70710678f);
            }
         const float apHz[3] = { hz[1], hz[2], hz[2] };
         for (int a = 0; a < 3; a++)
         {
            mAllpass[ch][a].SetSampleRate(mSampleRate);
            mAllpass[ch][a].SetCutoff(apHz[a], 0.70710678f);
         }
      }
   }

   // Per block, in octaves, ~20 ms: a split knob sweeps instead of stepping.
   void SmoothCutoffs(int frames)
   {
      const float a = 1.0f - std::exp(-(float)frames / (0.02f * (float)mSampleRate));
      bool moved = false;
      for (int s = 0; s < kSplits; s++)
      {
         const float target = std::clamp(mTargetHz[s].load(std::memory_order_relaxed), 20.0f, 20000.0f);
         if (target == mHz[s])
            continue;
         const float next = mHz[s] * std::pow(target / mHz[s], a);
         mHz[s] = std::fabs(next - target) < 0.01f ? target : next;
         moved = true;
      }
      if (moved)
         SetCutoffs();
   }

   std::atomic<int> mBands { 1 };
   std::atomic<float> mTargetHz[kSplits] { { 250.0f }, { 2500.0f }, { 8000.0f } };
   double mSampleRate = 48000.0;
   int mActiveBands = 1;
   float mHz[kSplits] = { 250.0f, 2500.0f, 8000.0f };
   DspMath::TptSvf mSplit[kMaxChannels][kSplits][3];
   DspMath::TptSvf mAllpass[kMaxChannels][3]; // band 1 at split 2, band 1 at split 3, band 2 at split 3
};

SplitterNode::SplitterNode() = default;
SplitterNode::~SplitterNode() = default;

void SplitterNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSplitterNode>();
   mAudioNode->PushParams(*this);
}

void SplitterNode::VisitParams(ParamVisitor& v)
{
   v.Int("bands", bands);
   v.Float("split1", split1);
   v.Float("split2", split2);
   v.Float("split3", split3);
}

AudioNode* SplitterNode::GetAudioNode()
{
   if (!mAudioNode)
   {
      mAudioNode = std::make_unique<AudioSplitterNode>();
      mAudioNode->PushParams(*this);
   }
   return mAudioNode.get();
}

// ----------------------------------------------------------------------- In
class AudioCaptureNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kGainDbParam, mGainDb.load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
   {
      // Platform::AudioInputCaptureRead is lock-free and zero-fills on
      // underrun, so this is safe to call unconditionally even before the
      // tap has produced its first block (or if mic permission was denied).
      //
      // Captured into a private scratch pair rather than straight into
      // `buffer`: on a mono output buffer both entries of a
      // buffer.channels-derived array point at the same memory, so the
      // right-channel read overwrote the left one it had just filled and
      // drained ring 0 into nothing. Scratch also keeps the gain loop below
      // reading source samples rather than samples it has already scaled.
      const int frames = std::min(buffer.numFrames, kMaxCaptureFrames);
      float* chans[2] = { mScratch[0], mScratch[1] };
      const int mode = mChannelMode.load(std::memory_order_relaxed);
      const int channelOffset = (mode <= 0) ? 0 : (mode - 1);
      const bool isMono = (mode > 0);
      const int captured = Platform::AudioInputCaptureRead(chans, frames, 2, mReaderCursor, channelOffset, isMono);

      float peak = 0.0f;
      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float linear = DspMath::DbToLinear(mMailbox.SmoothedValue(kGainDbParam));
         for (int ch = 0; ch < buffer.numChannels; ch++)
         {
            const float s = (captured > 0 && i < frames) ? mScratch[std::min(ch, 1)][i] : 0.0f;
            const float v = s * linear;
            buffer.channels[ch][i] = v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      mMeter.Write(&peak, 1);
   }

   // Main thread only.
   void PushParams(float gainDb, int channelMode)
   {
      mGainDb.store(gainDb, std::memory_order_relaxed);
      mChannelMode.store(channelMode, std::memory_order_relaxed);
      mMailbox.Push(kGainDbParam, gainDb);
   }

   MeterRing& Meter() { return mMeter; }

private:
   static constexpr int kMaxCaptureFrames = kAudioMaxBlockFrames;

   ParamMailbox mMailbox;
   MeterRing mMeter;
   std::atomic<float> mGainDb { 0.0f };
   std::atomic<int> mChannelMode { 0 };
   uint64_t mReaderCursor = 0;
   float mScratch[2][kMaxCaptureFrames] = {};
};

AudioInputNode::AudioInputNode() { Platform::AudioInputCaptureAddRef(); }

AudioInputNode::~AudioInputNode() { Platform::AudioInputCaptureRemoveRef(); }

void AudioInputNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioCaptureNode>();
   mAudioNode->PushParams(gainDb, channelMode);

   if (deviceId != 0)
      Platform::AudioInputCaptureSetDevice((uint32_t)deviceId);

   // The pump's error used to be collected into a local and dropped on the
   // floor, so every reason the tap can fail to install - permission denied,
   // permission not answered yet, no input device - looked identical to
   // "listening, but the room is quiet". Keep it for the body's status line.
   std::string error;
   Platform::AudioInputCapturePump(error); // no-op once the tap is already live
   mStatus = Platform::AudioInputCaptureIsRunning() ? std::string() : error;

   float peak = 0.0f;
   if (mAudioNode->Meter().ReadLatest(peak))
      mLevel = peak;
}

void AudioInputNode::VisitParams(ParamVisitor& v)
{
   v.Float("gainDb", gainDb);
   v.Int("channelMode", channelMode);
   v.Int("deviceId", deviceId);
   v.Text("deviceName", deviceName);
}

AudioNode* AudioInputNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioCaptureNode>();
   return mAudioNode.get();
}

// ------------------------------------------------------------------- Output

AudioOutputNode::~AudioOutputNode()
{
   StopRecording(); // never leave a WAV with unpatched chunk sizes
}

void AudioOutputNode::CookIfNeeded(int /*frameId*/)
{
   if (!mWriter.IsOpen())
      return;
   // Interleaved stereo, matching AudioEngine::RunTopology's capture write.
   float scratch[4096];
   int n;
   while ((n = mCaptureRing.Read(scratch, 4096)) > 0)
   {
      for (int i = 0; i < n; i++)
         mTakePeak = std::max(mTakePeak, std::fabs(scratch[i]));
      mWriter.Append(scratch, n / 2);
   }
}

void AudioOutputNode::VisitParams(ParamVisitor& v)
{
   v.Int("formatIndex", formatIndex);
   v.Text("recordDirectory", recordDirectory);
   v.Bool("live", live);
}

bool AudioOutputNode::StartRecording(const std::string& path, double sampleRate)
{
   if (IsRecording())
      return false;
   if (sampleRate <= 0.0)
      sampleRate = AudioEngine::Instance().SampleRate();
   if (sampleRate <= 0.0)
      return false;
   if (!mWriter.Open(path, sampleRate, 2))
      return false;
   mOpenSampleRate = sampleRate;
   mTakePeak = 0.0f;
   mCaptureRing.overflowCount.store(0, std::memory_order_relaxed);
   mCaptureRing.enabled.store(true, std::memory_order_relaxed);
   return true;
}

void AudioOutputNode::StopRecording()
{
   mCaptureRing.enabled.store(false, std::memory_order_relaxed);
   // Drain whatever the ring still has queued before closing, so the last
   // fraction of a second isn't silently dropped.
   CookIfNeeded(0);
   mWriter.Close();
}
