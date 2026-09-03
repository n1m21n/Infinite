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

      float channelPeak[MixerNode::kMaxSlots] = {};
      float peak = 0.0f;
      for (int i = 0; i < output.numFrames; i++)
      {
         float linear[MixerNode::kMaxSlots];
         for (int s = 0; s < activeChannels; s++)
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
      mWriter.Append(scratch, n / 2);
}

void AudioOutputNode::VisitParams(ParamVisitor& v)
{
   v.Int("formatIndex", formatIndex);
   v.Text("recordDirectory", recordDirectory);
}

bool AudioOutputNode::StartRecording(const std::string& path)
{
   if (IsRecording())
      return false;
   const double sampleRate = AudioEngine::Instance().SampleRate();
   if (sampleRate <= 0.0)
      return false;
   if (!mWriter.Open(path, sampleRate, 2))
      return false;
   mOpenSampleRate = sampleRate;
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
