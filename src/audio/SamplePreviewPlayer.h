#pragma once

#include <algorithm>
#include <atomic>

#include "AudioBuffer.h"
#include "AudioVoice.h" // Envelope - reused for the few-ms fade in/out
#include "SampleSlot.h"
#include "platform/Platform.h"

// Single-voice, stereo-aware, one-shot file player for auditioning a sample
// from the search panel's Samples mode without touching the node graph at
// all - see local-prompts/05-sample-preview-in-search-panel.md. Owned by
// AudioEngine and mixed into the device buffer *after* RunTopology (see
// AudioEngine::Process), so it is unaffected by the graph, by bypass, and by
// the transport, is audible even with nothing patched to Audio Out, and
// survives a topology swap mid-preview untouched.
//
// Decoding (Platform::DecodeAudioFileToBuffer) is main-thread-only and
// happens in the UI before Play() is called. The decoded buffer crosses to
// the audio thread through the same SampleSlot handoff SamplerNode already
// uses: Push() here, SwapIn() at the top of ProcessBlock, retire (not
// delete) on the audio thread, DrainRetired() here from the main thread.
class SamplePreviewPlayer
{
public:
   void PrepareToPlay(double deviceSampleRate)
   {
      mDeviceSampleRate = deviceSampleRate > 0.0 ? deviceSampleRate : 44100.0;
      mEnvelope.SetSampleRate(mDeviceSampleRate);
      // Instant decay/sustain at 1.0 - this envelope exists only for the
      // click-free fade in/out, not for shaping the sample itself.
      mEnvelope.SetADSR(kFadeMs, 0.0f, 1.0f, kFadeMs);
   }

   // Main thread. Hands ownership of `decoded` to the audio thread; a
   // buffer already playing is superseded the moment the audio thread
   // adopts this one (see ProcessBlock's SwapIn branch) - no explicit stop
   // needed first, though the UI calls Stop() anyway when toggling off an
   // already-playing row.
   //
   // mStopRequested is cleared *before* the slot Push, not after: Push()
   // performs an acq_rel exchange that acts as a release fence for anything
   // sequenced before it on this thread, so the audio thread's SwapIn (an
   // acquire) is guaranteed to observe the cleared flag together with the
   // new buffer. Clearing it the other way round would leave a window where
   // a Stop() issued just before this Play() (the UI's own "stop the
   // previous row" call) could still be seen by the audio thread *after* it
   // has already adopted the fresh buffer, killing the new preview's
   // envelope on arrival.
   void Play(Platform::SampleBuffer* decoded)
   {
      mStopRequested.store(false, std::memory_order_relaxed);
      mSlot.Push(decoded);
   }

   // Main thread.
   void Stop() { mStopRequested.store(true, std::memory_order_release); }

   bool IsPlaying() const { return mPlayingPublished.load(std::memory_order_relaxed); }
   float PositionSeconds() const { return mPositionSecondsPublished.load(std::memory_order_relaxed); }

   // Main thread, once per frame from wherever the app already pumps
   // per-frame audio housekeeping (main.cpp's per-frame loop, next to
   // PollAudioRecovery()).
   void DrainRetired() { mSlot.DrainRetired(); }

   // Audio thread. Adds into `out` - never overwrites - so the preview mixes
   // on top of whatever the graph itself produced this block.
   void ProcessBlock(AudioBuffer& out)
   {
      if (mSlot.SwapIn())
      {
         mActiveBuffer = mSlot.Active();
         mPos = 0.0;
         mEndReleaseTriggered = false;
         mEnvelope.NoteOn();
      }

      if (mStopRequested.exchange(false, std::memory_order_acq_rel))
         mEnvelope.NoteOff();

      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0)
      {
         mPlayingPublished.store(false, std::memory_order_relaxed);
         return;
      }

      // Simple linear-interpolation sample-rate conversion against the
      // device rate - the source advances `rate` source-frames per device
      // frame, exactly the ratio SamplerNode::ReadSample's playhead would
      // need if it tracked the file's native rate; ReadSample below is the
      // same interpolation lifted from there, extended to read a specific
      // channel plane for stereo files.
      const double rate = (mActiveBuffer->sampleRate > 0.0)
                              ? mActiveBuffer->sampleRate / mDeviceSampleRate
                              : 1.0;
      const int numSourceFrames = mActiveBuffer->numFrames;
      const int numSourceChannels = std::max(1, mActiveBuffer->channels);

      bool stillActive = mEnvelope.IsActive();
      int framesProcessed = 0;
      for (; framesProcessed < out.numFrames && stillActive; framesProcessed++)
      {
         if (!mEndReleaseTriggered && mPos >= (double)(numSourceFrames - 1))
         {
            // Reached end of file: release the envelope's few-ms tail
            // rather than stopping dead, then fall silent once it
            // completes. Latched so this fires exactly once - calling
            // NoteOff() every sample thereafter would keep re-arming the
            // release from a lower and lower level each time, never
            // actually finishing.
            mEnvelope.NoteOff();
            mEndReleaseTriggered = true;
         }

         const float env = mEnvelope.Process() * kPreviewGain;
         if (!mEnvelope.IsActive() && env <= 0.0f)
         {
            stillActive = false;
            break;
         }

         for (int ch = 0; ch < out.numChannels; ch++)
         {
            const int srcCh = std::min(ch, numSourceChannels - 1);
            out.channels[ch][framesProcessed] += ReadSample(*mActiveBuffer, mPos, srcCh, numSourceFrames) * env;
         }

         mPos += rate;
      }

      const bool finished = !mEnvelope.IsActive();
      mPositionSecondsPublished.store(
         mActiveBuffer->sampleRate > 0.0 ? (float)(mPos / mActiveBuffer->sampleRate) : 0.0f,
         std::memory_order_relaxed);

      if (finished)
      {
         mActiveBuffer = nullptr;
         mPos = 0.0;
      }

      mPlayingPublished.store(!finished, std::memory_order_relaxed);
   }

private:
   // A few ms, not zero - a preview browser stops previews constantly (every
   // row click cuts the last one) and a hard edit click on every one of them
   // is exactly the "clicks constantly" failure mode this exists to avoid.
   static constexpr float kFadeMs = 5.0f;
   // Attenuated so a hot sample previewed over a running patch doesn't clip
   // against it - this is a preview, not the mix.
   static constexpr float kPreviewGain = 0.7f;

   // Lifted from AudioSamplerNode::ReadSample (SamplerNode.cpp), extended
   // with a channel offset into the buffer's planar layout (channel 0's
   // numFrames samples, then channel 1's) since a preview must be
   // stereo-aware where the note-lane sampler mono-sums.
   static float ReadSample(const Platform::SampleBuffer& buf, double pos, int channel, int numFrames)
   {
      const int base = channel * numFrames;
      const int i0 = (int)pos;
      if (i0 < 0 || i0 >= numFrames - 1)
         return (i0 >= 0 && i0 < numFrames) ? buf.channelData[base + i0] : 0.0f;
      const float frac = (float)(pos - i0);
      const float a = buf.channelData[base + i0];
      const float b = buf.channelData[base + i0 + 1];
      return a + (b - a) * frac;
   }

   SampleSlot mSlot;
   Platform::SampleBuffer* mActiveBuffer = nullptr; // audio thread only
   double mPos = 0.0;                               // audio thread only, frames in the source buffer's own rate
   double mDeviceSampleRate = 44100.0;               // audio thread only, set from PrepareToPlay before Start()
   Envelope mEnvelope;                                // audio thread only
   bool mEndReleaseTriggered = false;                  // audio thread only

   std::atomic<bool> mStopRequested { false };
   std::atomic<bool> mPlayingPublished { false };
   std::atomic<float> mPositionSecondsPublished { 0.0f };
};
