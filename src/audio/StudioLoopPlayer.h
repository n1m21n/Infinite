#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "AudioBuffer.h"
#include "SampleSlot.h"
#include "platform/Platform.h"

// The Sample studio's take, played on the global transport (SampleStudioPanel.cpp). The buffer is one pass of the
// take as heard (trimmed, pitched, reversed, faded) spanning `loopBeats` beats; the read position is the transport's
// beat position inside that span, so the take stays locked to the grid at any tempo (a tempo change varispeeds it).
// Silent while the transport is stopped. Loop off: one pass from where play started (or the last Restart), then
// Finished() turns true so the panel can stop the transport.
// Owned by AudioEngine and mixed after RunTopology like the preview player: it is not a node, so it never reaches
// an Audio Out recording. Buffers cross to the audio thread through SampleSlot (Push here, SwapIn there).
class StudioLoopPlayer
{
public:
   // Main thread. `buf` is planar stereo (or mono); an empty buffer silences the player.
   void Set(Platform::SampleBuffer* buf, double loopBeats, bool loop)
   {
      mLoopBeats.store(std::max(1.0 / 16.0, loopBeats), std::memory_order_relaxed);
      mLoop.store(loop, std::memory_order_relaxed);
      mSlot.Push(buf);
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

   void DrainRetired() { mSlot.DrainRetired(); }

   // Audio thread. Adds into `out`.
   void ProcessBlock(AudioBuffer& out, bool playing, double blockStartBeats, double beatsPerSample)
   {
      mSlot.SwapIn();
      const Platform::SampleBuffer* b = mSlot.Active();
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
         return;
      }
      const double span = mLoopBeats.load(std::memory_order_relaxed);
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const float target = mGain.load(std::memory_order_relaxed);
      const int n = b->numFrames;
      const int chans = std::max(1, b->channels);
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
         const double pos = u * (double)(n - 1);
         const int i0 = (int)pos;
         const float frac = (float)(pos - (double)i0);
         const int i1 = std::min(i0 + 1, n - 1);
         for (int ch = 0; ch < out.numChannels; ch++)
         {
            const int base = std::min(ch, chans - 1) * n;
            const float a = b->channelData[(size_t)(base + i0)], c = b->channelData[(size_t)(base + i1)];
            const float s = (a + (c - a) * frac) * mGainNow;
            out.channels[ch][i] += s;
            peak = std::max(peak, std::fabs(s));
         }
      }
      mPhase.store((float)u, std::memory_order_relaxed);
      mLevel.store(peak, std::memory_order_relaxed);
   }

private:
   SampleSlot mSlot;
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
