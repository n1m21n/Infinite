#include "VideoSourceNode.h"

#include "platform/OpenGLHeaders.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "Transport.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/SampleSlot.h"
#include "core/AudioTopologyRequest.h"
#include "core/Modulation.h"

class VideoAudioNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mDeviceSampleRate.store(sampleRate > 0.0 ? sampleRate : 44100.0,
                              std::memory_order_relaxed);
   }

   void PushBuffer(Platform::SampleBuffer* buffer) { mSampleSlot.Push(buffer); }
   void DrainRetired() { mSampleSlot.DrainRetired(); }

   void PushParams(bool enabled, float gain, float rate, bool shouldLoop,
                   float trimIn, float trimOut)
   {
      mEnabled.store(enabled, std::memory_order_relaxed);
      mGain.store(std::clamp(gain, 0.0f, 2.0f), std::memory_order_relaxed);
      mRate.store(std::clamp(rate, -4.0f, 4.0f), std::memory_order_relaxed);
      mLoop.store(shouldLoop, std::memory_order_relaxed);
      mTrimIn.store(std::clamp(trimIn, 0.0f, 1.0f), std::memory_order_relaxed);
      mTrimOut.store(std::clamp(trimOut, 0.0f, 1.0f), std::memory_order_relaxed);
   }

   void SetPlaying(bool playing) { mPlaying.store(playing, std::memory_order_relaxed); }
   void RequestSeek(float normalized)
   {
      mSeekNormalized.store(std::clamp(normalized, 0.0f, 1.0f), std::memory_order_relaxed);
      mSeekSerial.fetch_add(1, std::memory_order_release);
   }

   double PositionSeconds() const
   {
      return mPositionSeconds.load(std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      if (mSampleSlot.SwapIn())
      {
         mActiveBuffer = mSampleSlot.Active();
         mPositionFrames = 0.0;
         mAppliedSeekSerial = 0;
      }

      for (int channel = 0; channel < output.numChannels; ++channel)
         std::fill(output.channels[channel], output.channels[channel] + output.numFrames, 0.0f);

      Platform::SampleBuffer* buffer = mActiveBuffer;
      if (buffer == nullptr || buffer->numFrames <= 0 || buffer->channels <= 0 ||
          buffer->sampleRate <= 0.0)
      {
         mPositionSeconds.store(0.0, std::memory_order_relaxed);
         return;
      }

      const uint64_t seekSerial = mSeekSerial.load(std::memory_order_acquire);
      if (seekSerial != mAppliedSeekSerial)
      {
         mPositionFrames = (double)mSeekNormalized.load(std::memory_order_relaxed) *
                           (double)std::max(0, buffer->numFrames - 1);
         mAppliedSeekSerial = seekSerial;
      }

      const double trimIn = (double)std::clamp(mTrimIn.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const double trimOut = (double)std::clamp(mTrimOut.load(std::memory_order_relaxed),
                                                (float)trimIn, 1.0f);
      const double firstFrame = trimIn * (double)std::max(0, buffer->numFrames - 1);
      const double lastFrame = std::max(firstFrame + 1.0,
                                        trimOut * (double)std::max(0, buffer->numFrames - 1));
      const double rate = (double)mRate.load(std::memory_order_relaxed);
      const double deviceRate = mDeviceSampleRate.load(std::memory_order_relaxed);
      const double step = rate * buffer->sampleRate / std::max(1.0, deviceRate);
      const bool playing = mPlaying.load(std::memory_order_relaxed) && std::fabs(step) > 1.0e-9;
      const bool shouldLoop = mLoop.load(std::memory_order_relaxed);
      const bool enabled = mEnabled.load(std::memory_order_relaxed);
      const float gain = mGain.load(std::memory_order_relaxed);

      if (mPositionFrames < firstFrame || mPositionFrames >= lastFrame)
         mPositionFrames = rate < 0.0 ? std::max(firstFrame, lastFrame - 1.0) : firstFrame;

      for (int frame = 0; frame < output.numFrames; ++frame)
      {
         if (!playing)
            continue;

         if (mPositionFrames < firstFrame || mPositionFrames >= lastFrame)
         {
            if (shouldLoop)
               mPositionFrames = step < 0.0 ? std::max(firstFrame, lastFrame - 1.0) : firstFrame;
            else
            {
               mPositionFrames = step < 0.0 ? firstFrame : std::max(firstFrame, lastFrame - 1.0);
               mPlaying.store(false, std::memory_order_relaxed);
               break;
            }
         }

         if (enabled)
         {
            for (int channel = 0; channel < output.numChannels; ++channel)
            {
               const int sourceChannel = std::min(channel, buffer->channels - 1);
               output.channels[channel][frame] = ReadSample(*buffer, sourceChannel, mPositionFrames) * gain;
            }
         }
         mPositionFrames += step;
      }

      const double published = std::clamp(mPositionFrames, firstFrame,
                                           std::max(firstFrame, lastFrame - 1.0));
      mPositionSeconds.store(published / buffer->sampleRate, std::memory_order_relaxed);
   }

private:
   static float ReadSample(const Platform::SampleBuffer& buffer, int channel, double position)
   {
      const int i0 = std::clamp((int)position, 0, buffer.numFrames - 1);
      const int i1 = std::min(i0 + 1, buffer.numFrames - 1);
      const float fraction = (float)(position - std::floor(position));
      const size_t offset = (size_t)channel * (size_t)buffer.numFrames;
      const float a = buffer.channelData[offset + (size_t)i0];
      const float b = buffer.channelData[offset + (size_t)i1];
      return a + (b - a) * fraction;
   }

   SampleSlot mSampleSlot;
   Platform::SampleBuffer* mActiveBuffer = nullptr;
   double mPositionFrames = 0.0;
   uint64_t mAppliedSeekSerial = 0;

   std::atomic<double> mDeviceSampleRate { 44100.0 };
   std::atomic<double> mPositionSeconds { 0.0 };
   std::atomic<float> mGain { 1.0f };
   std::atomic<float> mRate { 1.0f };
   std::atomic<float> mTrimIn { 0.0f };
   std::atomic<float> mTrimOut { 1.0f };
   std::atomic<float> mSeekNormalized { 0.0f };
   std::atomic<uint64_t> mSeekSerial { 0 };
   std::atomic<bool> mEnabled { true };
   std::atomic<bool> mLoop { true };
   std::atomic<bool> mPlaying { false };
};

VideoSourceNode::VideoSourceNode()
   : mAudioNode(std::make_unique<VideoAudioNode>())
{
}

VideoSourceNode::~VideoSourceNode()
{
   if (mVideo != nullptr)
      Platform::VideoClose(mVideo);
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

AudioNode* VideoSourceNode::GetAudioNode()
{
   return mAudioNode.get();
}

IModulator** VideoSourceNode::ModulatorInputSlot(int slot)
{
   return slot >= 0 && slot < kCueCount ? &mCueInputs[slot] : nullptr;
}

const char* VideoSourceNode::InputLabel(int slot) const
{
   static const char* labels[kCueCount] = { "cue 1", "cue 2", "cue 3", "cue 4" };
   return slot >= 0 && slot < kCueCount ? labels[slot] : nullptr;
}

void VideoSourceNode::EnsurePlaceholder()
{
   if (mTex != 0)
      return;

   const int kSize = 256;
   std::vector<unsigned char> pixels(kSize * kSize * 4);
   for (int y = 0; y < kSize; y++)
   {
      for (int x = 0; x < kSize; x++)
      {
         const bool checker = ((x / 32) + (y / 32)) % 2 == 0;
         const unsigned char v = checker ? 70 : 40;
         const int i = (y * kSize + x) * 4;
         pixels[i + 0] = v;
         pixels[i + 1] = (unsigned char)(v + 10);
         pixels[i + 2] = (unsigned char)(v + 30);
         pixels[i + 3] = 255;
      }
   }

   glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mWidth = kSize;
   mHeight = kSize;
   mHasPlaceholder = true;
}

bool VideoSourceNode::Open(const std::string& path)
{
   if (path.empty())
      return false;

   std::string error;
   Platform::VideoHandle* handle = Platform::VideoOpen(path, error);
   if (handle == nullptr)
   {
      mLastError = error;
      return false;
   }

   if (mVideo != nullptr)
      Platform::VideoClose(mVideo);
   mVideo = handle;
   mDuration = Platform::VideoDuration(mVideo);
   mLoadedPath = path;
   mLastError.clear();
   mHasPlaceholder = false;
   mPosition = 0.0;
   mLastTransportSeconds = Transport::Instance().Seconds();
   mWasTransportPlaying = Transport::Instance().IsPlaying();

   auto* decoded = new Platform::SampleBuffer();
   std::string audioError;
   if (Platform::DecodeAudioFileToBuffer(path, *decoded, audioError))
   {
      mAudioLoaded = decoded->numFrames > 0 && decoded->channels > 0;
      mAudioError.clear();
   }
   else
   {
      delete decoded;
      decoded = new Platform::SampleBuffer();
      mAudioLoaded = false;
      mAudioError = audioError.empty() ? "video has no decodable audio track" : audioError;
   }

   mAudioNode->PushBuffer(decoded);
   PushAudioParams();
   mAudioNode->SetPlaying(mWasTransportPlaying);
   Restart();
   AudioTopologyRequest::Request();
   return true;
}

bool VideoSourceNode::OpenViaDialog()
{
   const std::string path = Platform::OpenVideoDialog();
   if (path.empty())
      return false;
   return Open(path);
}

unsigned int VideoSourceNode::GetOutputTexture()
{
   EnsurePlaceholder();
   return mTex;
}

void VideoSourceNode::PushAudioParams()
{
   trimStart = std::clamp(trimStart, 0.0f, 1.0f);
   trimEnd = std::clamp(trimEnd, trimStart, 1.0f);
   volume = std::clamp(volume, 0.0f, 2.0f);
   // Migrate old patches where reverse playback was encoded as a negative
   // speed. The UI now exposes direction explicitly and keeps speed positive.
   if (speed < 0.0f)
   {
      speed = std::fabs(speed);
      reverse = true;
   }
   speed = std::clamp(speed, 0.05f, 4.0f);
   mAudioNode->PushParams(audioEnabled, volume, EffectiveRate(), loop, trimStart, trimEnd);
}

void VideoSourceNode::Restart()
{
   const float target = EffectiveRate() < 0.0f ? std::max(trimStart, trimEnd - 0.00001f) : trimStart;
   SeekNormalized(target);
}

void VideoSourceNode::SeekNormalized(float normalized)
{
   const float clamped = std::clamp(normalized, trimStart, trimEnd);
   mPosition = mDuration > 0.0 ? (double)clamped * mDuration : 0.0;
   mAudioNode->RequestSeek(clamped);
}

void VideoSourceNode::SetCue(int index, float normalized)
{
   if (index < 0 || index >= kCueCount)
      return;
   mCueNormalized[index] = std::clamp(normalized, trimStart, trimEnd);
}

void VideoSourceNode::SetNextCue(float normalized)
{
   for (int i = 0; i < kCueCount; ++i)
   {
      if (mCueNormalized[i] < 0.0f)
      {
         SetCue(i, normalized);
         return;
      }
   }

   int nearest = 0;
   float nearestDistance = std::fabs(mCueNormalized[0] - normalized);
   for (int i = 1; i < kCueCount; ++i)
   {
      const float distance = std::fabs(mCueNormalized[i] - normalized);
      if (distance < nearestDistance)
      {
         nearest = i;
         nearestDistance = distance;
      }
   }
   SetCue(nearest, normalized);
}

void VideoSourceNode::ClearCue(int index)
{
   if (index >= 0 && index < kCueCount)
      mCueNormalized[index] = -1.0f;
}

void VideoSourceNode::TriggerCue(int index)
{
   if (index >= 0 && index < kCueCount && mCueNormalized[index] >= 0.0f)
      SeekNormalized(mCueNormalized[index]);
}

float VideoSourceNode::CueNormalized(int index) const
{
   return index >= 0 && index < kCueCount ? mCueNormalized[index] : -1.0f;
}

double VideoSourceNode::CueSeconds(int index) const
{
   const float cue = CueNormalized(index);
   return cue >= 0.0f ? (double)cue * mDuration : -1.0;
}

void VideoSourceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   EnsurePlaceholder();
   PushAudioParams();
   mAudioNode->DrainRetired();

   for (int i = 0; i < kCueCount; ++i)
   {
      const float value = mCueInputs[i] != nullptr ? mCueInputs[i]->Value01() : 0.0f;
      if (value > 0.0f && mCueInputPrevious[i] <= 0.0f)
         TriggerCue(i);
      mCueInputPrevious[i] = value;
   }

   const bool transportPlaying = Transport::Instance().IsPlaying();
   if (transportPlaying != mWasTransportPlaying)
   {
      mAudioNode->SetPlaying(transportPlaying);
      mWasTransportPlaying = transportPlaying;
   }

   const double now = Transport::Instance().Seconds();
   double delta = now - mLastTransportSeconds;
   if (delta < 0.0)
      delta = 0.0;
   mLastTransportSeconds = now;

   if (mVideo == nullptr)
      return;

   if (mAudioLoaded)
   {
      mPosition = mAudioNode->PositionSeconds();
   }
   else if (transportPlaying)
   {
      const double rate = (double)EffectiveRate();
      mPosition += delta * rate;
      const double trimInSeconds = (double)trimStart * mDuration;
      const double trimOutSeconds = (double)trimEnd * mDuration;
      if (mPosition >= trimOutSeconds || mPosition < trimInSeconds)
      {
         if (loop)
            mPosition = rate < 0.0 ? std::max(trimInSeconds, trimOutSeconds - 0.00001) : trimInSeconds;
         else
            mPosition = std::clamp(mPosition, trimInSeconds, trimOutSeconds);
      }
   }

   if (Platform::VideoFrameAt(mVideo, mPosition, mFrame) && !mFrame.empty())
   {
      const int w = Platform::VideoWidth(mVideo);
      const int h = Platform::VideoHeight(mVideo);
      if (w > 0 && h > 0)
      {
         glBindTexture(GL_TEXTURE_2D, mTex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mFrame.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         mWidth = w;
         mHeight = h;
         mHasPlaceholder = false;
      }
   }
}
