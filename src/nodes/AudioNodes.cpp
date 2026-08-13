#include "AudioNodes.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"

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
   if (mAudioNode->Meter().Read(&peak, 1) > 0)
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
      for (int s = 0; s < MixerNode::kSlots; s++)
         mMailbox.SetImmediate(s, mGainDb[s].load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int slots = std::min(numInputs, (int)MixerNode::kSlots);

      // Pan and mute are read once per block, not smoothed per sample. Mute is
      // a state, not a level - smoothing it would make it a fade with no
      // control over its length - and pan moves are gestural, not audio-rate.
      float panL[MixerNode::kSlots], panR[MixerNode::kSlots];
      for (int s = 0; s < MixerNode::kSlots; s++)
      {
         if (mMute[s].load(std::memory_order_relaxed) != 0)
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

      float channelPeak[MixerNode::kSlots] = {};
      float peak = 0.0f;
      for (int i = 0; i < output.numFrames; i++)
      {
         float linear[MixerNode::kSlots];
         for (int s = 0; s < MixerNode::kSlots; s++)
            linear[s] = DspMath::DbToLinear(mMailbox.SmoothedValue(s));

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
                               (law > 0.5f ? panR[s] : panL[s]);
               sum += v;
               channelPeak[s] = std::max(channelPeak[s], std::fabs(v));
            }
            output.channels[ch][i] = sum;
            peak = std::max(peak, std::fabs(sum));
         }
      }

      mMeter.Write(&peak, 1);
      for (int s = 0; s < MixerNode::kSlots; s++)
         mChannelPeak[s].store(channelPeak[s], std::memory_order_relaxed);
   }

   // Main thread only.
   void PushParams(const MixerNode& n)
   {
      for (int s = 0; s < MixerNode::kSlots; s++)
      {
         mGainDb[s].store(n.gainDb[s], std::memory_order_relaxed);
         mMailbox.Push(s, n.gainDb[s]);
         mPan[s].store(n.pan[s], std::memory_order_relaxed);
         mMute[s].store(n.mute[s] ? 1 : 0, std::memory_order_relaxed);
      }
   }

   MeterRing& Meter() { return mMeter; }
   float ChannelPeak(int slot) const { return mChannelPeak[slot].load(std::memory_order_relaxed); }

private:
   ParamMailbox mMailbox;
   MeterRing mMeter;
   std::atomic<float> mGainDb[MixerNode::kSlots] {};
   std::atomic<float> mPan[MixerNode::kSlots] {};
   std::atomic<int> mMute[MixerNode::kSlots] {};
   std::atomic<float> mChannelPeak[MixerNode::kSlots] {};
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
   if (mAudioNode->Meter().Read(&peak, 1) > 0)
      mLevel = peak;
   for (int s = 0; s < kSlots; s++)
      mChannelLevel[s] = mAudioNode->ChannelPeak(s);
}

void MixerNode::VisitParams(ParamVisitor& v)
{
   static const char* kGainNames[kSlots] = {
      "gain0", "gain1", "gain2", "gain3", "gain4", "gain5", "gain6", "gain7"
   };
   static const char* kPanNames[kSlots] = {
      "pan0", "pan1", "pan2", "pan3", "pan4", "pan5", "pan6", "pan7"
   };
   static const char* kMuteNames[kSlots] = {
      "mute0", "mute1", "mute2", "mute3", "mute4", "mute5", "mute6", "mute7"
   };
   for (int i = 0; i < kSlots; i++)
   {
      v.Float(kGainNames[i], gainDb[i]);
      v.Float(kPanNames[i], pan[i]);
      v.Bool(kMuteNames[i], mute[i]);
   }
}

AudioNode* MixerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMixerNode>();
   return mAudioNode.get();
}

// ------------------------------------------------------------------- Splitter
class AudioSplitterNode : public AudioNode
{
public:
   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      for (int ch = 0; ch < output.numChannels; ch++)
      {
         for (int i = 0; i < output.numFrames; i++)
            output.channels[ch][i] = (in != nullptr) ? in->channels[ch][i] : 0.0f;
      }
   }
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
}

AudioNode* SplitterNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSplitterNode>();
   return mAudioNode.get();
}
