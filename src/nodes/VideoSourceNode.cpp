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
   }

   // Main thread only.
   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }
   void DrainRetired() { mSampleSlot.DrainRetired(); }
   void PublishPosition(double seconds) { mPublishedPositionSeconds.store(seconds, std::memory_order_relaxed); }
   void PushParams(bool audioEnabled, float volume, float speed, bool loop)
   {
      mAudioEnabled = audioEnabled;
      mVolume = volume;
      mSpeed = speed;
      mLoop = loop;
      mMailbox.Push(kAudioEnabledParam, audioEnabled ? 1.0f : 0.0f);
      mMailbox.Push(kVolumeParam, volume);
      mMailbox.Push(kSpeedParam, speed);
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

      if (isLooping && mActiveBuffer->numFrames > 0)
      {
         cursor = std::fmod(cursor, (double)mActiveBuffer->numFrames);
         if (cursor < 0.0)
            cursor += (double)mActiveBuffer->numFrames;
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
   mPosition = 0.0;
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

   // Reverse folds into a signed rate the audio half reads too, so its own
   // read cursor runs backwards in step with the picture.
   const float effectiveSpeed = speed * (reverse ? -1.0f : 1.0f);
   if (mAudioNode)
   {
      mAudioNode->PushParams(audioEnabled, volume, effectiveSpeed, loop);
      mAudioNode->DrainRetired();
   }

   if (mVideo == nullptr)
      return;

   // Learn the true duration for containers whose header declares none (on
   // Windows MF_PD_DURATION comes back empty for some files, which used to
   // leave mDuration at 0 - and a 0 duration silently disabled both the loop
   // wrap and the total-time readout). VideoObservedEndSeconds fills in once
   // the clip has been decoded through to its end at least once.
   if (mDuration <= 0.0)
   {
      const double observed = Platform::VideoObservedEndSeconds(mVideo);
      if (observed > 0.0)
         mDuration = observed;
   }

   // Active playback window from the trim points. trimOut <= 0 means "to the
   // end"; when the end is not yet known (unknown duration) outSec stays 0 and
   // the wrap below falls back to the decoder's end-of-stream signal.
   const double inSec = std::max(0.0, (double)trimIn);
   double outSec = 0.0;
   if (trimOut > 0.0f && (double)trimOut > inSec)
      outSec = (double)trimOut;
   else if (mDuration > 0.0)
      outSec = mDuration;
   if (outSec > 0.0 && mDuration > 0.0)
      outSec = std::min(outSec, mDuration);

   // Advance by the transport's own delta so pausing holds the frame and the
   // speed control retimes playback without touching wall time.
   const double now = Transport::Instance().Seconds();
   double delta = now - mLastTransportSeconds;
   if (delta < 0.0)
      delta = 0.0;
   mLastTransportSeconds = now;

   if (scrub)
   {
      // Manual positioning: the transport is ignored and the frame follows the
      // scrub control directly.
      const double hi = (outSec > inSec) ? outSec
                                         : (mDuration > 0.0 ? mDuration : (double)scrubSeconds);
      mPosition = std::clamp((double)scrubSeconds, inSec, std::max(inSec, hi));
   }
   else
   {
      mPosition += delta * (double)effectiveSpeed;

      if (outSec > inSec)
      {
         const double span = outSec - inSec;
         if (loop)
         {
            double rel = std::fmod(mPosition - inSec, span);
            if (rel < 0.0)
               rel += span; // fmod keeps the sign of the dividend
            mPosition = inSec + rel;
         }
         else
         {
            mPosition = std::clamp(mPosition, inSec, outSec);
         }
      }
      else
      {
         // End not yet known: hold the floor at trimIn, and wrap on the
         // decoder's real end-of-stream when looping forward. Reverse-looping
         // with an unknown end has nothing to wrap to, so it just holds at in.
         if (mPosition < inSec)
            mPosition = inSec;
         if (loop && effectiveSpeed >= 0.0f && Platform::VideoAtEnd(mVideo))
            mPosition = inSec;
         mPosition = std::max(mPosition, 0.0);
      }
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
