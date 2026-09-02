#include "VideoSourceNode.h"

#include "gl3.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <cmath>

#include "Transport.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"

namespace
{
   constexpr int kAudioEnabledParam = 0;
   constexpr int kVolumeParam = 1;
   constexpr int kSpeedParam = 2;
   constexpr int kTrimStartParam = 3;
   constexpr int kTrimEndParam = 4;

   // Frames of drift between the free-running read position and a freshly
   // published one beyond which this is treated as a seek/loop-wrap rather
   // than ordinary playback, and the read position snaps instead of ramping.
   constexpr double kDiscontinuitySeconds = 0.08;
}

// ------------------------------------------------------------- audio thread
//
// Position is a free-running read cursor advanced every block by `speed`
// (source frames per audio-thread sample, so pitch tracks speed exactly like
// tape/VCR playback), phase-locked with the Transport-driven timeline.
class VideoAudioNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kAudioEnabledParam, mAudioEnabled ? 1.0f : 0.0f);
      mMailbox.SetImmediate(kVolumeParam, mVolume);
      mMailbox.SetImmediate(kSpeedParam, mSpeed);
      mMailbox.SetImmediate(kTrimStartParam, mTrimStart);
      mMailbox.SetImmediate(kTrimEndParam, mTrimEnd);
   }

   // Main thread only.
   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }
   void DrainRetired() { mSampleSlot.DrainRetired(); }
   void PublishPosition(double seconds) { mPublishedPositionSeconds.store(seconds, std::memory_order_relaxed); }
   void PushParams(bool audioEnabled, float volume, float speed, bool loop, float trimStart, float trimEnd)
   {
      mAudioEnabled = audioEnabled;
      mVolume = volume;
      mSpeed = speed;
      mLoop = loop;
      mTrimStart = trimStart;
      mTrimEnd = trimEnd;
      mMailbox.Push(kAudioEnabledParam, audioEnabled ? 1.0f : 0.0f);
      mMailbox.Push(kVolumeParam, volume);
      mMailbox.Push(kSpeedParam, speed);
      mMailbox.Push(kTrimStartParam, trimStart);
      mMailbox.Push(kTrimEndParam, trimEnd);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
   {
      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      // Adopt a newly decoded buffer at the top of the block, never mid-block.
      if (mSampleSlot.SwapIn())
      {
         mActiveBuffer = mSampleSlot.Active();
         mHavePosition = false;
      }

      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0 || mActiveBuffer->channels <= 0)
         return;

      if (mMailbox.SmoothedValue(kAudioEnabledParam) <= 0.5f || !Transport::Instance().IsPlaying())
         return;

      const double sr = mActiveBuffer->sampleRate > 0.0 ? mActiveBuffer->sampleRate : 48000.0;
      const double publishedSeconds = mPublishedPositionSeconds.load(std::memory_order_relaxed);
      const double publishedFrame = std::clamp(publishedSeconds * sr, 0.0, (double)(mActiveBuffer->numFrames - 1));

      if (!mHavePosition)
      {
         mFreeRunFrame = publishedFrame;
         mHavePosition = true;
      }

      const double discontinuityFrames = 0.25 * sr; // snap on genuine seek or loop wrap (> 250ms)
      if (std::fabs(publishedFrame - mFreeRunFrame) > discontinuityFrames)
      {
         mFreeRunFrame = publishedFrame;
      }

      const double mailboxSr = mMailbox.SampleRate() > 0.0 ? mMailbox.SampleRate() : sr;

      // Mirror VideoSourceNode::CookIfNeeded's effective-trim-window
      // computation (in frames rather than seconds) so the free-running
      // cursor wraps at the same in/out points as the video's own position,
      // rather than relying solely on the discontinuity snap above - which
      // is too coarse (250ms) to keep short trim windows in sync.
      const double totalFrames = (double)mActiveBuffer->numFrames;
      const float trimStart = mMailbox.SmoothedValue(kTrimStartParam);
      const float trimEnd = mMailbox.SmoothedValue(kTrimEndParam);
      double effStartFrame = std::clamp((double)trimStart * sr, 0.0, totalFrames > 0.0 ? totalFrames : (double)trimStart * sr);
      double effEndFrame = (trimEnd <= 0.0f || (double)trimEnd * sr > totalFrames) ? totalFrames : (double)trimEnd * sr;
      if (totalFrames > 0.0 && effEndFrame - effStartFrame < 0.001 * sr)
      {
         effStartFrame = 0.0;
         effEndFrame = totalFrames;
      }
      const double trimRangeFrames = effEndFrame - effStartFrame;

      double cursor = mFreeRunFrame;
      const bool isLooping = mLoop;
      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float speed = mMailbox.SmoothedValue(kSpeedParam);
         const float volume = mMailbox.SmoothedValue(kVolumeParam);
         const double stepPerSample = (double)speed * sr / mailboxSr;
         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = ReadSample(*mActiveBuffer, ch, cursor, isLooping) * volume;
         cursor += stepPerSample;
      }

      if (isLooping && trimRangeFrames > 0.0)
      {
         cursor = effStartFrame + std::fmod(cursor - effStartFrame, trimRangeFrames);
         if (cursor < effStartFrame)
            cursor += trimRangeFrames;
      }
      mFreeRunFrame = cursor;
   }

private:
   // Linear interpolation between frames of `channel`, with loop wrap support.
   static float ReadSample(const Platform::SampleBuffer& buf, int channel, double posFrames, bool loop)
   {
      if (buf.numFrames <= 0)
         return 0.0f;
      if (loop)
      {
         posFrames = std::fmod(posFrames, (double)buf.numFrames);
         if (posFrames < 0.0)
            posFrames += (double)buf.numFrames;
      }
      else if (posFrames < 0.0 || posFrames >= buf.numFrames)
      {
         return 0.0f;
      }
      const int ch = std::min(channel, buf.channels - 1);
      const float* data = buf.channelData.data() + (size_t)ch * (size_t)buf.numFrames;
      const int i0 = (int)std::floor(posFrames);
      const int i1 = (i0 + 1 < buf.numFrames) ? i0 + 1 : (loop ? 0 : i0);
      const float frac = (float)(posFrames - i0);
      return data[i0] + (data[i1] - data[i0]) * frac;
   }

   SampleSlot mSampleSlot;
   Platform::SampleBuffer* mActiveBuffer = nullptr;
   ParamMailbox mMailbox;
   bool mAudioEnabled = true;
   float mVolume = 1.0f;
   float mSpeed = 1.0f;
   bool mLoop = true;
   float mTrimStart = 0.0f;
   float mTrimEnd = 0.0f;
   std::atomic<double> mPublishedPositionSeconds { 0.0 };
   double mFreeRunFrame = 0.0;
   bool mHavePosition = false;
};

// --------------------------------------------------------------- main thread

VideoSourceNode::VideoSourceNode() = default;
VideoSourceNode::~VideoSourceNode()
{
   if (mVideo != nullptr)
      Platform::VideoClose(mVideo);
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

void VideoSourceNode::EnsurePlaceholder()
{
   if (mTex != 0)
      return;

   // No GL context in headless test/sweep runs (AUDIOPARAMSWEEPTEST runs
   // before glfwInit) - this node became reachable from there the moment it
   // implemented IAudioSource, alongside every other GL call in this file.
   // See the identical guard in WaveTerrainNode::RenderPreview.
   if (glfwGetCurrentContext() == nullptr)
      return;

   const int kSize = 256;
   std::vector<unsigned char> pixels(kSize * kSize * 4);
   for (int y = 0; y < kSize; y++)
   {
      for (int x = 0; x < kSize; x++)
      {
         bool checker = ((x / 32) + (y / 32)) % 2 == 0;
         unsigned char v = checker ? 70 : 40;
         int i = (y * kSize + x) * 4;
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

void VideoSourceNode::LoadAudioTrack(const std::string& path)
{
   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (!Platform::DecodeVideoAudioTrackToBuffer(path, *decoded, error))
   {
      delete decoded;
      mAudioLoaded = false;
      mAudioError = error;
      return;
   }

   mAudioLoaded = true;
   mAudioError.clear();
   if (!mAudioNode)
      mAudioNode = std::make_unique<VideoAudioNode>();
   mAudioNode->PushBuffer(decoded);
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
   mPosition = trimStart;
   mLastTransportSeconds = Transport::Instance().Seconds();

   LoadAudioTrack(path);
   return true;
}

bool VideoSourceNode::OpenViaDialog()
{
   std::string path = Platform::OpenVideoDialog();
   if (path.empty())
      return false; // cancelled
   return Open(path);
}

unsigned int VideoSourceNode::GetOutputTexture()
{
   EnsurePlaceholder();
   return mTex;
}

AudioNode* VideoSourceNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<VideoAudioNode>();
   return mAudioNode.get();
}

void VideoSourceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   EnsurePlaceholder();

   if (mAudioNode)
   {
      mAudioNode->PushParams(audioEnabled, volume, speed, loop, trimStart, trimEnd);
      mAudioNode->DrainRetired();
   }

   if (mVideo == nullptr)
      return;

   // Advance by the transport's own delta so pausing holds the frame and the
   // speed control retimes playback without touching wall time.
   const double now = Transport::Instance().Seconds();
   double delta = now - mLastTransportSeconds;
   if (delta < 0.0)
      delta = 0.0;
   mLastTransportSeconds = now;
   mPosition += delta * (double)speed;

   // Effective trim window for this cook. Guarded against degenerate/inverted
   // ranges since trimStart/trimEnd are modulatable and can be driven to any
   // value at runtime.
   double effStart = std::clamp((double)trimStart, 0.0, mDuration > 0.0 ? mDuration : (double)trimStart);
   double effEnd = (trimEnd <= 0.0f || (double)trimEnd > mDuration) ? mDuration : (double)trimEnd;
   if (mDuration > 0.0 && effEnd - effStart < 0.001)
   {
      // Degenerate/inverted trim window - fall back to full clip rather than
      // divide-by-near-zero in fmod below.
      effStart = 0.0;
      effEnd = mDuration;
   }

   if (mDuration > 0.0)
   {
      double range = effEnd - effStart;
      if (loop)
      {
         mPosition = effStart + std::fmod(mPosition - effStart, range);
         if (mPosition < effStart)
            mPosition += range; // fmod keeps the sign of the dividend
      }
      else
      {
         mPosition = std::clamp(mPosition, effStart, effEnd);
      }
   }
   else
   {
      mPosition = std::max(mPosition, (double)trimStart);
   }

   // Published for the audio half to read - see VideoAudioNode's class
   // comment. Written every cook, whether or not this node currently has an
   // audio-consuming cable, so a cable patched in later starts in sync
   // rather than from a stale position.
   if (mAudioNode)
      mAudioNode->PublishPosition(mPosition);

   if (glfwGetCurrentContext() != nullptr &&
       Platform::VideoFrameAt(mVideo, mPosition, mFrame) && !mFrame.empty())
   {
      const int w = Platform::VideoWidth(mVideo);
      const int h = Platform::VideoHeight(mVideo);
      if (w > 0 && h > 0)
      {
         glBindTexture(GL_TEXTURE_2D, mTex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         if (mTexWidth != w || mTexHeight != h || mHasPlaceholder)
         {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mFrame.data());
            mTexWidth = w;
            mTexHeight = h;
         }
         else
         {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, mFrame.data());
         }
         glBindTexture(GL_TEXTURE_2D, 0);
         mWidth = w;
         mHeight = h;
         mHasPlaceholder = false;
         mFrameUpdateCount++;
      }
   }
}
