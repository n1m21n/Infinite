#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "AudioBuffer.h"
#include "SampleSlot.h"
#include "platform/Platform.h"

// The Sample studio's take, played on the global transport (SampleStudioPanel.cpp). The buffer is one pass of the
// take as heard (trimmed, pitched, reversed, faded) spanning `loopBeats` beats; the read position is the transport's
// beat position inside that span, so the take stays locked to the grid at any tempo (the panel renders it again at the new tempo; until then a tempo change varispeeds it).
// Silent while the transport is stopped. Loop off: one pass from where play started (or the last Restart), then
// Finished() turns true so the panel can stop the transport.
// Owned by AudioEngine and mixed after RunTopology like the preview player: it is not a node, so it never reaches
// an Audio Out recording. Buffers cross to the audio thread through an atomic pending slot and are freed on the main thread
// (DrainRetired), as SampleSlot does; this one keeps the old buffer for a 20 ms crossfade.
class StudioLoopPlayer
{
public:
   // Main thread. `buf` is planar stereo (or mono); an empty buffer silences the player.
   void Set(Platform::SampleBuffer* buf, double loopBeats, bool loop)
   {
      mLoopBeats.store(std::max(1.0 / 16.0, loopBeats), std::memory_order_relaxed);
      mLoop.store(loop, std::memory_order_relaxed);
      Platform::SampleBuffer* old = mPending.exchange(buf, std::memory_order_acq_rel);
      delete old; // never seen by the audio thread
   }
   void SetLoop(bool loop) { mLoop.store(loop, std::memory_order_relaxed); }
   void SetGain(float g) { mGain.store(g, std::memory_order_relaxed); }
   // Main thread: the one-shot pass starts again from the next block (a new take, or play pressed).
   void Restart()
   {
      mDone.store(false, std::memory_order_relaxed);
      mRestart.store(true, std::memory_order_relaxed);
   }
   // Main thread: loop off and the one pass has played out.
   bool Finished() const { return mDone.load(std::memory_order_relaxed); }

   // Main thread: where the playhead is, 0..1 of the take, or -1 when silent.
   float Phase() const { return mPhase.load(std::memory_order_relaxed); }
   // Main thread: the level of the last block (peak), for the panel's blob.
   float Level() const { return mLevel.load(std::memory_order_relaxed); }

   // Main thread, once a frame: frees buffers the audio thread is done with.
   void DrainRetired()
   {
      while (Platform::SampleBuffer* b = mRetired.Drain())
         delete b;
   }
   ~StudioLoopPlayer()
   {
      delete mActive;
      delete mPrev;
      delete mPending.exchange(nullptr);
      DrainRetired();
   }

   // Audio thread. Adds into `out`.
   void ProcessBlock(AudioBuffer& out, bool playing, double blockStartBeats, double beatsPerSample)
   {
      // A new buffer (an edit, a new take, a tempo change) crossfades in over the old one at the same place, so an
      // edit while it plays neither clicks nor drops out.
      if (Platform::SampleBuffer* fresh = mPending.exchange(nullptr, std::memory_order_acq_rel))
      {
         if (mPrev != nullptr)
            mRetired.Retire(mPrev);
         mPrev = mActive;
         mActive = fresh;
         mXfade = mPrev != nullptr && mPrev->numFrames > 1 ? 0.0f : 1.0f;
      }
      const Platform::SampleBuffer* b = mActive;
      if (playing && (!mWasPlaying || mRestart.exchange(false, std::memory_order_relaxed)))
      {
         mAnchor = blockStartBeats;
         mDone.store(false, std::memory_order_relaxed);
      }
      mWasPlaying = playing;
      if (b == nullptr || b->numFrames <= 1 || !playing || beatsPerSample <= 0.0)
      {
         mPhase.store(-1.0f, std::memory_order_relaxed);
         mLevel.store(0.0f, std::memory_order_relaxed);
         mGainNow = 0.0f;
         RetirePrev();
         return;
      }
      const double span = mLoopBeats.load(std::memory_order_relaxed);
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const float target = mGain.load(std::memory_order_relaxed);
      float peak = 0.0f;
      double u = -1.0;
      for (int i = 0; i < out.numFrames; i++)
      {
         const double beats = blockStartBeats + beatsPerSample * (double)i;
         const double rel = beats - mAnchor;
         if (beats < 0.0 || (!loop && (rel < 0.0 || rel >= span)))
         {
            if (!loop && rel >= span)
               mDone.store(true, std::memory_order_relaxed);
            u = -1.0;
            continue;
         }
         u = loop ? std::fmod(beats, span) / span : rel / span;
         // A short ramp on every gain change (start, stop, a new take) keeps edits click-free.
         mGainNow += (target - mGainNow) * 0.002f;
         if (mXfade < 1.0f)
            mXfade = std::min(1.0f, mXfade + kXfadeStep);
         for (int ch = 0; ch < out.numChannels; ch++)
         {
            float s = Read(*b, ch, u);
            if (mXfade < 1.0f)
               s = s * mXfade + Read(*mPrev, ch, u) * (1.0f - mXfade);
            s *= mGainNow;
            out.channels[ch][i] += s;
            peak = std::max(peak, std::fabs(s));
         }
      }
      if (mXfade >= 1.0f)
         RetirePrev();
      mPhase.store((float)u, std::memory_order_relaxed);
      mLevel.store(peak, std::memory_order_relaxed);
   }

private:
   static constexpr float kXfadeStep = 1.0f / 960.0f; // 20 ms at 48 kHz

   static float Read(const Platform::SampleBuffer& b, int ch, double u)
   {
      const int n = b.numFrames;
      const int base = std::min(ch, std::max(1, b.channels) - 1) * n;
      const double pos = u * (double)(n - 1);
      const int i0 = std::clamp((int)pos, 0, n - 1);
      const int i1 = std::min(i0 + 1, n - 1);
      const float frac = (float)(pos - (double)i0);
      const float a = b.channelData[(size_t)(base + i0)], c = b.channelData[(size_t)(base + i1)];
      return a + (c - a) * frac;
   }
   void RetirePrev()
   {
      if (mPrev != nullptr)
         mRetired.Retire(mPrev);
      mPrev = nullptr;
      mXfade = 1.0f;
   }

   std::atomic<Platform::SampleBuffer*> mPending { nullptr };
   Platform::SampleBuffer* mActive = nullptr; // audio thread
   Platform::SampleBuffer* mPrev = nullptr;   // audio thread: the buffer fading out
   float mXfade = 1.0f;                       // audio thread
   SampleSlotDetail::BufferRetireRing<Platform::SampleBuffer> mRetired;
   float mGainNow = 0.0f; // audio thread only
   double mAnchor = 0.0;  // audio thread only: beat where the one-shot pass started
   bool mWasPlaying = false;
   std::atomic<bool> mRestart { false };
   std::atomic<bool> mDone { false };
   std::atomic<double> mLoopBeats { 4.0 };
   std::atomic<bool> mLoop { true };
   std::atomic<float> mGain { 0.8f };
   std::atomic<float> mPhase { -1.0f };
   std::atomic<float> mLevel { 0.0f };
};
