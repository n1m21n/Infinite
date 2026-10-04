#include "SamplerNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "platform/Platform.h"
#include "Transport.h"
#include "core/AudioTopologyRequest.h"
#include "core/AudioDecodeCache.h"
#include "audio/WavWriter.h"

namespace
{
   constexpr int kPitchParam = 0;
   constexpr int kFinetuneParam = 1;
   constexpr int kSpeedParam = 2;
   constexpr int kVolumeParam = 3;
   // start/end travel via the plain mStart/mEnd atomics instead - see ProcessBlock/TriggerVoice.
   constexpr int kLoopParam = 4;     // pushed as 0.0/1.0, no smoothing needed but the mailbox has no per-param opt-out
   constexpr int kPingpongParam = 5; // ditto
   constexpr int kFadeInParam = 6;   // fade-in ms at the start of every pass (from upstream)
   constexpr int kFadeOutParam = 7;  // fade-out ms at the end of every pass
   // reverse travels via the plain mReverse atomic instead - see ProcessBlock/TriggerVoice.
   // Turbo 0.49: Arrangement Timeline per-clip pitch, semitones added on top of
   // pitch/finetune. Written by the audio thread itself (SetClipPitchOverride runs
   // in the engine's lookahead right before ProcessBlock), held to 0 otherwise.
   constexpr int kClipPitchParam = 8;

   // Turbo 0.49 (upstream): 16 note voices (was 8). Still allocated once in the
   // constructor; SamplerNode::kMaxVoicePositions mirrors it for the UI snapshot.
   constexpr int kMaxVoices = 16;
   static_assert(kMaxVoices == SamplerNode::kMaxVoicePositions, "keep the voice snapshot in step");
   constexpr int kReferenceNote = 60; // sample's own recorded pitch plays back at rate 1.0

   // Recording capacity: 30s mono at a generous upper-bound sample rate.
   // Preallocated once in PrepareToPlay (main thread, before the audio
   // callback ever runs) so Record itself never allocates on the audio
   // thread - it only ever writes into already-owned memory.
   constexpr int kMaxRecordSeconds = 30;
   constexpr int kMaxRecordSampleRate = 192000;

   float NoteToRate(int note, float pitchSemis)
   {
      return powf(2.0f, ((float)(note - kReferenceNote) + pitchSemis) / 12.0f);
   }

   // Shared advance+edge-handling for one voice's playback position - used
   // both by the note lane's polyphonic voices and the self lane's single
   // dedicated voice, so loop/reverse/ping-pong behaviour can't drift
   // between them. `dir` is already updated for the current block (reverse
   // toggling outside a ping-pong bounce) by the caller before this runs.
   // Returns true if the voice hit a non-looping edge and should release.
   // Per-pass fade gain (from upstream, replaces the loop crossfade). A pass
   // is one traversal of start..end in the direction of travel: the fade-in
   // ramps from the edge playback enters at, the fade-out to the edge it
   // leaves by, so a loop dips at its seam instead of clicking and a one-shot
   // opens and closes softly. Output-time ms, converted to source frames via
   // `rate`, scaled down together if they would not fit. A voice that starts
   // mid-range gets no fade-in. Both 0 = exactly 1.0.
   float FadeGain(double pos, double startPos, double endPos, float dirSign, float fadeInMs, float fadeOutMs,
                  float rate, double sampleRate)
   {
      if ((fadeInMs <= 0.0f && fadeOutMs <= 0.0f) || sampleRate <= 0.0)
         return 1.0f;
      const double perMs = 0.001 * sampleRate * (double)rate;
      double inF = std::max(0.0, (double)fadeInMs) * perMs;
      double outF = std::max(0.0, (double)fadeOutMs) * perMs;
      const double len = std::max(1.0, endPos - startPos);
      if (inF + outF > len)
      {
         const double k = len / (inF + outF);
         inF *= k;
         outF *= k;
      }
      const double fromEntry = (dirSign >= 0.0f) ? (pos - startPos) : (endPos - pos);
      const double toExit = (dirSign >= 0.0f) ? (endPos - pos) : (pos - startPos);
      float g = 1.0f;
      if (inF > 0.0)
         g *= (float)std::clamp(fromEntry / inF, 0.0, 1.0);
      if (outF > 0.0)
         g *= (float)std::clamp(toExit / outF, 0.0, 1.0);
      return g;
   }

   bool AdvanceVoicePosition(double& pos, int& dir, bool loop, bool pingpong, float rate, float speedSign,
                              double startPos, double endPos)
   {
      const float dirSign = (float)dir * speedSign;
      pos += rate * dirSign;

      const bool hitEnd = dirSign > 0.0f && pos >= endPos;
      const bool hitStart = dirSign < 0.0f && pos <= startPos;
      bool shouldRelease = false;
      if (hitEnd || hitStart)
      {
         if (loop && pingpong)
         {
            dir = -dir;
            pos = hitEnd ? endPos : startPos;
         }
         else if (loop)
         {
            pos = hitEnd ? startPos : endPos;
         }
         else
         {
            shouldRelease = true;
         }
      }

      // One-shot voices keep advancing through their release (the caller
      // handles NoteOff) and a handle drag can move the range under a voice
      // already in flight - clamp unconditionally rather than only in the
      // loop/ping-pong branches above.
      pos = std::clamp(pos, startPos, endPos);
      return shouldRelease;
   }
}

// ------------------------------------------------------------- audio thread
class AudioSamplerNode : public AudioNode
{
public:
   AudioSamplerNode()
      : mVoices(kMaxVoices)
   {
      mVoicePos.assign(kMaxVoices, 0.0);
      mVoiceNote.assign(kMaxVoices, -1);
      mVoiceId.assign(kMaxVoices, 0);
      mVoiceDir.assign(kMaxVoices, 1);
      mVoiceBend.assign(kMaxVoices, 0.0f);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kPitchParam, mPitch.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kFinetuneParam, mFinetune.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kSpeedParam, mSpeed.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kVolumeParam, mVolume.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kLoopParam, mLoop.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
      mMailbox.SetImmediate(kPingpongParam, mPingpong.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
      mMailbox.SetImmediate(kFadeInParam, mFadeIn.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kFadeOutParam, mFadeOut.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kClipPitchParam, 0.0f);
      // No envelope shaping to speak of - fast fixed attack/release just
      // enough to avoid a click on trigger/steal, not a musical parameter.
      mVoices.SetSampleRate(sampleRate);
      mVoices.SetADSR(2.0f, 0.0f, 1.0f, 15.0f);
      mSelfEnv.SetSampleRate(sampleRate);
      mSelfEnv.SetADSR(2.0f, 0.0f, 1.0f, 15.0f);
      mLastDecaySec = -1.0f; // re-apply the decay param on the next block
      mPosParamSeen = false;

      if (mRecordBuffer.empty())
         mRecordBuffer.resize((size_t)kMaxRecordSeconds * kMaxRecordSampleRate);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   // Main thread only. Hands over ownership of a freshly decoded/recorded
   // buffer; the previously active one (if any) is retired through
   // mSampleSlot rather than freed here.
   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }

   // Main thread only, called once per frame from CookIfNeeded.
   void DrainRetired() { mSampleSlot.DrainRetired(); }

   void PushParams(float pitch, float finetune, float speed, float volume, float start, float end, float position,
                   float decay, float fadeIn, float fadeOut, bool loop, bool reverse, bool pingpong)
   {
      mPosition.store(position, std::memory_order_relaxed);
      mDecay.store(decay, std::memory_order_relaxed);
      mPitch.store(pitch, std::memory_order_relaxed);
      mFinetune.store(finetune, std::memory_order_relaxed);
      mSpeed.store(speed, std::memory_order_relaxed);
      mVolume.store(volume, std::memory_order_relaxed);
      mStart.store(start, std::memory_order_relaxed);
      mEnd.store(end, std::memory_order_relaxed);
      mLoop.store(loop, std::memory_order_relaxed);
      mReverse.store(reverse, std::memory_order_relaxed);
      mPingpong.store(pingpong, std::memory_order_relaxed);
      mMailbox.Push(kPitchParam, pitch);
      mMailbox.Push(kFinetuneParam, finetune);
      mMailbox.Push(kSpeedParam, speed);
      mMailbox.Push(kVolumeParam, volume);
      mMailbox.Push(kLoopParam, loop ? 1.0f : 0.0f);
      mMailbox.Push(kPingpongParam, pingpong ? 1.0f : 0.0f);
      mMailbox.Push(kFadeInParam, fadeIn);
      mMailbox.Push(kFadeOutParam, fadeOut);
      mFadeIn.store(fadeIn, std::memory_order_relaxed);
      mFadeOut.store(fadeOut, std::memory_order_relaxed);
   }

   MeterRing& PlayheadRing() { return mPlayheadRing; }
   // VoicePositions (Turbo 0.49): slots 0..kMaxVoices-1 note voices, kMaxVoices the self voice.
   const PlayCursorSet<kMaxVoices + 1>& Cursors() const { return mCursors; }

   // Turbo 0.49 (upstream port): Arrangement Timeline per-clip pitch. Audio thread,
   // called by the engine's lookahead every block a clip of this node is under the
   // playhead. Additive (clip pitch 0 = the node's own pitch, so old patches and
   // clips play unchanged; upstream replaces the knob instead). Valid for that
   // block only: a block without the call (gap, stop, clip removed) eases back to 0.
   void SetClipPitchOverride(float semitones) override
   {
      mClipPitch = std::isfinite(semitones) ? std::clamp(semitones, -24.0f, 24.0f) : 0.0f;
      mClipPitchFresh = true;
   }

   // Main thread. Auditions the loaded sample from `frac` right away,
   // independent of the note graph and the transport - the last-write-wins
   // atomic exchange below means a rapid double-click just retargets the
   // same pending trigger rather than queuing two, which is the right
   // behaviour for a "play from here" gesture.
   void TriggerPreviewFromMainThread(float frac) { mPreviewFrac.store(frac, std::memory_order_release); }

   // Main thread. Silences the self lane's dedicated voice on the next
   // block, whoever currently owns it - the Stop half of the audition
   // button.
   void RequestStopFromMainThread() { mStopRequested.store(true, std::memory_order_release); }

   // Main thread. Whether the self lane's dedicated voice is still
   // sounding, published once per block from ProcessBlock.
   bool IsPlaying() const { return mIsPlaying.load(std::memory_order_relaxed); }

   // Main thread. Whether any note-lane voice is sounding, and how many.
   bool NotesSounding() const { return mNotesSounding.load(std::memory_order_relaxed); }
   int ActiveNoteCount() const { return mActiveNoteCount.load(std::memory_order_relaxed); }

   // Main thread. Whether the self voice's current owner is the audition
   // control rather than the transport.
   bool SelfOwnedByUser() const { return mSelfOwnedByUserPublished.load(std::memory_order_relaxed); }

   // Main thread. Recording is a plain state flip - see the class comment
   // on kMaxRecordSeconds for why this never allocates.
   void SetRecording(bool on)
   {
      if (on)
         mRecordWritePos.store(0, std::memory_order_relaxed);
      mRecording.store(on, std::memory_order_release);
   }
   // Main thread, after SetRecording(false): how many frames were captured,
   // and the buffer to read them from. Safe to read up to this count - the
   // audio thread publishes it with release ordering only after the sample
   // at that index is written (see ProcessBlock).
   int RecordedFrames() const { return mRecordWritePos.load(std::memory_order_acquire); }
   const float* RecordBufferData() const { return mRecordBuffer.data(); }
   double RecordSampleRate() const { return mSampleRate; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      // Adopt a newly loaded buffer, if any, at the top of the block - never
      // mid-block, so a voice already reading the active buffer this
      // callback finishes against a consistent buffer.
      if (mSampleSlot.SwapIn())
      {
         mActiveBuffer = mSampleSlot.Active();
         // A newly loaded/recorded buffer should audition on its own when
         // free-running, the way SamplerNode.h's class comment describes -
         // without this a one-shot free-running node that already finished
         // playing would stay permanently silent after loading a different
         // file.
         if (Transport::Instance().IsPlaying() && mNoteInbox == nullptr)
            TriggerSelfVoice(-1.0f, SelfOwner::Transport);
      }

      // Slot 1, not 0 - slot 0 is the note pin's slot in the shared pin
      // index space (see SamplerNode.h's AudioInputSlot override); this
      // node's actual audio input always lands one slot past it.
      const AudioBuffer* recordSrc = (numInputs > 1) ? inputs[1] : nullptr;
      if (mRecording.load(std::memory_order_relaxed) && recordSrc != nullptr)
      {
         int pos = mRecordWritePos.load(std::memory_order_relaxed);
         const int cap = (int)mRecordBuffer.size();
         for (int i = 0; i < recordSrc->numFrames && pos < cap; i++, pos++)
            mRecordBuffer[pos] = recordSrc->channels[0][i];
         mRecordWritePos.store(pos, std::memory_order_release);
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      // Turbo 0.49: clip pitch lives for one block (see SetClipPitchOverride).
      mMailbox.Push(kClipPitchParam, mClipPitchFresh ? mClipPitch : 0.0f);
      mClipPitchFresh = false;

      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0)
      {
         for (int v = 0; v <= kMaxVoices; v++)
            mCursors.Idle(v);
         return;
      }

      // Turbo 0.48 (upstream port): note voices decay to silence over `decay` seconds.
      // 0 keeps the old held envelope (fixed 2 ms attack, full sustain, 15 ms release).
      const float decaySec = mDecay.load(std::memory_order_relaxed);
      if (decaySec != mLastDecaySec)
      {
         mLastDecaySec = decaySec;
         if (decaySec > 0.0f)
         {
            const float decayMs = std::max(10.0f, decaySec * 1000.0f);
            mVoices.SetADSR(2.0f, decayMs, 0.0f, decayMs);
         }
         else
            mVoices.SetADSR(2.0f, 0.0f, 1.0f, 15.0f);
      }

      // Turbo 0.48 (upstream port): moving `position` (knob drag or modulation) scrubs a
      // sounding self voice to the new start point. Only a change of the raw param
      // counts: start/end moving (e.g. an LFO on start) must not reset the playhead.
      {
         const float rawPos = mPosition.load(std::memory_order_relaxed);
         if (mPosParamSeen && std::fabs(rawPos - mLastPosParam) > 1e-4f && mSelfEnv.IsActive())
         {
            const float curStartFrac = mStart.load(std::memory_order_relaxed);
            const float curEndFrac = std::max(curStartFrac + 0.001f, mEnd.load(std::memory_order_relaxed));
            mSelfPos = (double)std::clamp(rawPos, curStartFrac, curEndFrac) * mActiveBuffer->numFrames;
         }
         mLastPosParam = rawPos;
         mPosParamSeen = true;
      }

      // The audition button's Stop always releases the self voice, whoever
      // currently owns it.
      if (mStopRequested.exchange(false, std::memory_order_acq_rel))
      {
         mSelfEnv.NoteOff();
         mSelfOwner = SelfOwner::None;
      }

      const bool noteDriven = mNoteInbox != nullptr;
      const bool transportPlaying = Transport::Instance().IsPlaying();

      // Spacebar stops every sound this node is making: a transport falling
      // edge releases every note-lane voice, and the self voice too if the
      // transport (not the audition control) is the one holding it. A
      // rising edge retriggers the self voice from `start` when no note
      // cable is connected - the auto/free-run case.
      if (transportPlaying && !mTransportWasPlaying)
      {
         if (!noteDriven)
            TriggerSelfVoice(-1.0f, SelfOwner::Transport);
      }
      else if (!transportPlaying && mTransportWasPlaying)
      {
         for (int v = 0; v < mVoices.NumVoices(); v++)
            mVoices.EnvelopeAt(v).NoteOff();
         if (mSelfOwner == SelfOwner::Transport)
         {
            mSelfEnv.NoteOff();
            mSelfOwner = SelfOwner::None;
         }
      }
      mTransportWasPlaying = transportPlaying;

      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (noteDriven)
         numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);

      // A pending manual preview (click-the-waveform, or the node's own
      // audition button) always wins the same block it arrives in,
      // note-driven or not - it's an explicit "play from here" the user
      // just asked for, and it takes ownership of the self voice.
      const float previewFrac = mPreviewFrac.exchange(-1.0f, std::memory_order_acq_rel);
      if (previewFrac >= 0.0f)
         TriggerSelfVoice(previewFrac, SelfOwner::User);

      for (int i = 0; i < buffer.numFrames; i++)
      {
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            if (evts[evtIdx].isNoteOn)
               TriggerVoice(evts[evtIdx].note, evts[evtIdx].velocity, -1.0f, evts[evtIdx].voiceId,
                            evts[evtIdx].bendSemitones);
            else if (evts[evtIdx].bendUpdate)
               BendUpdate(evts[evtIdx].voiceId, evts[evtIdx].bendSemitones);
            else
               mVoices.NoteOff(evts[evtIdx].voiceId);
            evtIdx++;
         }

         const float pitchSemis = mMailbox.SmoothedValue(kPitchParam) + mMailbox.SmoothedValue(kFinetuneParam) / 100.0f +
                                  mMailbox.SmoothedValue(kClipPitchParam);
         const float speed = mMailbox.SmoothedValue(kSpeedParam);
         const float volume = mMailbox.SmoothedValue(kVolumeParam);
         const float startFrac = mStart.load(std::memory_order_relaxed);
         const float endFrac = std::max(startFrac + 0.001f, mEnd.load(std::memory_order_relaxed));
         const bool loop = mMailbox.SmoothedValue(kLoopParam) > 0.5f;
         const bool pingpong = mMailbox.SmoothedValue(kPingpongParam) > 0.5f;
         const bool reverseOn = mReverse.load(std::memory_order_relaxed);
         const double startPos = (double)startFrac * mActiveBuffer->numFrames;
         const double endPos = (double)endFrac * mActiveBuffer->numFrames;
         const float speedSign = speed < 0.0f ? -1.0f : 1.0f;
         const float fadeInMs = mMailbox.SmoothedValue(kFadeInParam);
         const float fadeOutMs = mMailbox.SmoothedValue(kFadeOutParam);

         float sampleL = 0.0f, sampleR = 0.0f;

         for (int v = 0; v < mVoices.NumVoices(); v++)
         {
            if (!mVoices.IsVoiceActive(v))
               continue;

            // Outside a ping-pong bounce, direction tracks the live reverse
            // toggle every sample rather than only at trigger time - without
            // this, flipping "rev" mid-playback did nothing until the next
            // retrigger, and turning "p-p" off after a bounce left a voice
            // stuck playing backward forever (nothing else ever re-synced
            // it). Once a bounce is in progress, the edge-hit logic below
            // owns direction until the next edge.
            if (!pingpong)
               mVoiceDir[v] = reverseOn ? -1 : 1;

            const float rate = NoteToRate(mVoices.NoteAt(v), pitchSemis + mVoiceBend[v]) * std::fabs(speed);
            const float env = mVoices.EnvelopeAt(v).Process();
            // Turbo 0.48: decay > 0 (sustain 0) has faded the voice out, free it
            // without waiting for a note-off.
            if (mVoices.EnvelopeAt(v).InSilentSustain())
            {
               mVoices.EnvelopeAt(v).ForceIdle();
               continue;
            }
            const float s = ReadSample(*mActiveBuffer, mVoicePos[v]) *
                            FadeGain(mVoicePos[v], startPos, endPos, (float)mVoiceDir[v] * speedSign, fadeInMs,
                                     fadeOutMs, rate, mSampleRate) *
                            env * mVoices.VelocityAt(v);

            sampleL += s;
            sampleR += s; // mono-summed voice, panned centre - no per-voice pan control in this minimal node

            if (AdvanceVoicePosition(mVoicePos[v], mVoiceDir[v], loop, pingpong, rate, speedSign, startPos, endPos))
               mVoices.NoteOff(mVoiceId[v]);
         }

         if (mSelfEnv.IsActive())
         {
            if (!pingpong)
               mSelfDir = reverseOn ? -1 : 1;

            const float rate = NoteToRate(kReferenceNote, pitchSemis) * std::fabs(speed);
            const float env = mSelfEnv.Process();
            const float s = ReadSample(*mActiveBuffer, mSelfPos) *
                            FadeGain(mSelfPos, startPos, endPos, (float)mSelfDir * speedSign, fadeInMs, fadeOutMs,
                                     rate, mSampleRate) *
                            env;

            sampleL += s;
            sampleR += s;

            if (AdvanceVoicePosition(mSelfPos, mSelfDir, loop, pingpong, rate, speedSign, startPos, endPos))
               mSelfEnv.NoteOff();
         }

         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = (ch == 0 ? sampleL : sampleR) * volume;
      }

      // The playhead follows the self voice while it's sounding (it's the
      // one thing a single waveform view can usefully track), else the most
      // recently triggered note voice, else parks on the start marker rather
      // than freezing wherever it last was.
      float playheadOut = -1.0f;
      if (mSelfEnv.IsActive())
         playheadOut = (float)(mSelfPos / std::max(1, mActiveBuffer->numFrames));
      else if (mLastTriggeredVoice >= 0 && mVoices.IsVoiceActive(mLastTriggeredVoice))
         playheadOut = (float)(mVoicePos[mLastTriggeredVoice] / std::max(1, mActiveBuffer->numFrames));
      if (playheadOut < 0.0f)
         playheadOut = mStart.load(std::memory_order_relaxed);

      mPlayheadRing.Write(&playheadOut, 1);

      // ---- VoicePositions (Turbo 0.49): publish every voice's position ----
      {
         const double frames = (double)std::max(1, mActiveBuffer->numFrames);
         for (int v = 0; v < kMaxVoices; v++)
         {
            if (v < mVoices.NumVoices() && mVoices.IsVoiceActive(v))
               mCursors.Publish(v, (float)(mVoicePos[v] / frames));
            else
               mCursors.Idle(v);
         }
         if (mSelfEnv.IsActive())
            mCursors.Publish(kMaxVoices, (float)(mSelfPos / frames));
         else
            mCursors.Idle(kMaxVoices);
      }

      mIsPlaying.store(mSelfEnv.IsActive(), std::memory_order_relaxed);
      mSelfOwnedByUserPublished.store(mSelfOwner == SelfOwner::User, std::memory_order_relaxed);

      bool anyNoteActive = false;
      int activeNoteCount = 0;
      for (int v = 0; v < mVoices.NumVoices(); v++)
      {
         if (mVoices.IsVoiceActive(v))
         {
            anyNoteActive = true;
            activeNoteCount++;
         }
      }
      mNotesSounding.store(anyNoteActive, std::memory_order_relaxed);
      mActiveNoteCount.store(activeNoteCount, std::memory_order_relaxed);
   }

private:
   // The self lane: one dedicated voice outside VoiceAllocator, shared by
   // transport-driven free-run and the audition button/waveform click so
   // neither can steal or retune a note-lane voice (see the class comment in
   // SamplerNode.h). Whichever triggered it last owns it, and only that
   // owner's stop condition releases it - see ProcessBlock.
   enum class SelfOwner
   {
      None,
      Transport,
      User
   };

   // ReadSample takes a fractional frame position and linearly interpolates
   // between the two nearest frames - enough for "basic playback", not a
   // resampling-quality claim.
   static float ReadSample(const Platform::SampleBuffer& buf, double pos)
   {
      const int i0 = (int)pos;
      if (i0 < 0 || i0 >= buf.numFrames - 1)
         return (i0 >= 0 && i0 < buf.numFrames) ? buf.channelData[i0] : 0.0f;
      const float frac = (float)(pos - i0);
      const float a = buf.channelData[i0];
      const float b = buf.channelData[i0 + 1];
      return a + (b - a) * frac;
   }

   // Turbo 0.48 (upstream port): where an ordinary trigger starts. `position` (clamped
   // into the range) is the start point; at 0 (or anywhere at/below start) a reversed
   // voice starts from the end as before, so old patches play unchanged.
   float StartFracFor(float initialDirSign, float startFrac, float endFrac) const
   {
      const float posFrac = std::clamp(mPosition.load(std::memory_order_relaxed), startFrac, endFrac);
      if (initialDirSign < 0.0f)
         return (posFrac <= startFrac) ? endFrac : posFrac;
      return posFrac;
   }

   // overrideStartFrac >= 0 forces the trigger position; < 0 uses the
   // configured start/end/reverse range like an ordinary note-on. Note-only:
   // always allocates through VoiceAllocator's normal polyphonic round-robin.
   void TriggerVoice(int note, float velocity, float overrideStartFrac, int voiceId, float bendSemitones)
   {
      const bool reverseOn = mReverse.load(std::memory_order_relaxed);
      const float speed = mMailbox.SmoothedValue(kSpeedParam);
      const float startFrac = mStart.load(std::memory_order_relaxed);
      const float endFrac = std::max(startFrac + 0.001f, mEnd.load(std::memory_order_relaxed));

      const int idx = mVoices.NoteOn(note, velocity, voiceId);
      const int baseDir = reverseOn ? -1 : 1;
      mVoiceDir[idx] = baseDir;
      const float initialDirSign = (float)baseDir * (speed < 0.0f ? -1.0f : 1.0f);

      float frac;
      if (overrideStartFrac >= 0.0f)
         frac = std::clamp(overrideStartFrac, startFrac, endFrac);
      else
         frac = StartFracFor(initialDirSign, startFrac, endFrac);

      mVoicePos[idx] = mActiveBuffer != nullptr ? (double)frac * mActiveBuffer->numFrames : 0.0;
      mVoiceNote[idx] = note;
      mVoiceId[idx] = voiceId;
      mVoiceBend[idx] = bendSemitones;
      mLastTriggeredVoice = idx;
   }

   // Slides a voice already sounding, without retriggering it - same reason
   // WavetableNode's BendUpdate exists (see its comment): a real note-on
   // here would restart the voice's envelope and playback position, which a
   // bend wheel must never do.
   void BendUpdate(int voiceId, float bendSemitones)
   {
      for (int v = 0; v < mVoices.NumVoices(); v++)
      {
         if (mVoices.IsVoiceActive(v) && mVoiceId[v] == voiceId)
            mVoiceBend[v] = bendSemitones;
      }
   }

   // Triggers the self lane's single dedicated voice - both the transport's
   // free-run auto-trigger and the audition button/waveform click go through
   // here, distinguished only by `owner`. Always retriggers in place
   // (monophonic - rapid waveform clicks must not stack copies), same as
   // note-on above but writing mSelfPos/mSelfDir/mSelfEnv instead of a
   // VoiceAllocator slot.
   void TriggerSelfVoice(float overrideStartFrac, SelfOwner owner)
   {
      const bool reverseOn = mReverse.load(std::memory_order_relaxed);
      const float speed = mMailbox.SmoothedValue(kSpeedParam);
      const float startFrac = mStart.load(std::memory_order_relaxed);
      const float endFrac = std::max(startFrac + 0.001f, mEnd.load(std::memory_order_relaxed));

      const int baseDir = reverseOn ? -1 : 1;
      mSelfDir = baseDir;
      const float initialDirSign = (float)baseDir * (speed < 0.0f ? -1.0f : 1.0f);

      float frac;
      if (overrideStartFrac >= 0.0f)
         frac = std::clamp(overrideStartFrac, startFrac, endFrac); // manual preview click - never start outside the range
      else
         frac = StartFracFor(initialDirSign, startFrac, endFrac);

      mSelfPos = mActiveBuffer != nullptr ? (double)frac * mActiveBuffer->numFrames : 0.0;
      mSelfEnv.NoteOn();
      mSelfOwner = owner;
   }

   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;
   MeterRing mPlayheadRing;
   PlayCursorSet<kMaxVoices + 1> mCursors; // VoicePositions (Turbo 0.49)
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   VoiceAllocator mVoices;
   std::vector<double> mVoicePos;
   std::vector<int> mVoiceNote;
   std::vector<int> mVoiceId;
   std::vector<int> mVoiceDir; // +1 forward, -1 backward; ping-pong flips this at each edge
   std::vector<float> mVoiceBend; // live bendSemitones from the note chain (Pitch Bend), updated in place on bendUpdate
   int mLastTriggeredVoice = -1;

   // The self lane's dedicated voice state - see SelfOwner's declaration
   // above for what owns it and when.
   Envelope mSelfEnv;
   double mSelfPos = 0.0;
   int mSelfDir = 1;
   SelfOwner mSelfOwner = SelfOwner::None;
   bool mTransportWasPlaying = false;
   std::atomic<bool> mSelfOwnedByUserPublished { false };

   std::atomic<float> mPreviewFrac { -1.0f };
   std::atomic<bool> mStopRequested { false };
   std::atomic<bool> mIsPlaying { false };
   std::atomic<bool> mNotesSounding { false };
   std::atomic<int> mActiveNoteCount { 0 };

   Platform::SampleBuffer* mActiveBuffer = nullptr;
   SampleSlot mSampleSlot;

   std::atomic<float> mPitch { 0.0f };
   std::atomic<float> mFinetune { 0.0f };
   std::atomic<float> mSpeed { 1.0f };
   std::atomic<float> mVolume { 0.8f };
   std::atomic<float> mFadeIn { 3.0f };
   std::atomic<float> mFadeOut { 3.0f };
   std::atomic<float> mPosition { 0.0f };
   std::atomic<float> mDecay { 0.0f };
   float mLastDecaySec = -1.0f; // audio thread
   float mClipPitch = 0.0f;      // audio thread (Turbo 0.49)
   bool mClipPitchFresh = false; // audio thread
   float mLastPosParam = 0.0f; // audio thread, raw `position` of the last block
   bool mPosParamSeen = false; // audio thread
   std::atomic<float> mStart { 0.0f };
   std::atomic<float> mEnd { 1.0f };
   std::atomic<bool> mLoop { false };
   std::atomic<bool> mReverse { false };
   std::atomic<bool> mPingpong { false };

   std::vector<float> mRecordBuffer;      // preallocated once in PrepareToPlay
   std::atomic<int> mRecordWritePos { 0 };
   std::atomic<bool> mRecording { false };
};

SamplerNode::SamplerNode() = default;
SamplerNode::~SamplerNode() = default;

void SamplerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSamplerNode>();
   // Turbo 0.48: `position` goes raw; the audio thread clamps it into the range
   // (with end >= start + 0.001) and scrubs only when the raw value changes.
   mAudioNode->PushParams(pitch, finetune, speed, volume, start, end, position, decay, fadeIn,
                          fadeOut, loop, reverse, pingpong);
   mAudioNode->DrainRetired();

   float playhead = 0.0f;
   if (mAudioNode->PlayheadRing().ReadLatest(playhead))
      mPlayhead = playhead;
   mIsPlaying = mAudioNode->IsPlaying();
   mNotesSounding = mAudioNode->NotesSounding();
   mActiveNoteCount = mAudioNode->ActiveNoteCount();
   mSelfOwnedByUser = mAudioNode->SelfOwnedByUser();
}

void SamplerNode::VisitParams(ParamVisitor& v)
{
   v.Text("path", mFilePath);
   v.Float("pitch", pitch);
   v.Float("finetune", finetune);
   v.Float("speed", speed);
   v.Float("start", start);
   v.Float("end", end);
   v.Float("volume", volume);
   v.Float("fadeIn", fadeIn);   // the old loop "xfade" key is ignored on load
   v.Float("fadeOut", fadeOut);
   v.Bool("loop", loop);
   v.Bool("reverse", reverse);
   v.Bool("pingpong", pingpong);
   v.Float("position", position); // Turbo 0.48: appended, save-compat
   v.Float("decay", decay);
}

AudioNode* SamplerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSamplerNode>();
   return mAudioNode.get();
}

// ---- VoicePositions (Turbo 0.49) ----
int SamplerNode::VoicePositions(float* out, int max) const
{
   if (!mAudioNode || out == nullptr || max <= 0)
      return 0;
   return mAudioNode->Cursors().Collect(out, max);
}
// ---- end VoicePositions (Turbo 0.49) ----

void SamplerNode::TriggerPreview(float frac)
{
   if (!mAudioNode)
      return;
   mAudioNode->TriggerPreviewFromMainThread(std::clamp(frac, 0.0f, 1.0f));
}

void SamplerNode::StopPreview()
{
   if (mAudioNode)
      mAudioNode->RequestStopFromMainThread();
}

void SamplerNode::StartRecording()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSamplerNode>();
   mRecording = true;
   mAudioNode->SetRecording(true);
   mStatus = "recording...";
   // RequiresAudioProcessing() now answers true - a Sampler with no path to
   // an Audio Out and no note input still needs an AudioTopologyEntry so
   // ProcessBlock (and the record branch inside it) actually runs.
   AudioTopologyRequest::Request();
}

void SamplerNode::StopRecording()
{
   if (!mRecording || !mAudioNode)
      return;
   mRecording = false;
   mAudioNode->SetRecording(false);
   AudioTopologyRequest::Request();

   const int frames = mAudioNode->RecordedFrames();
   if (frames <= 0)
   {
      mStatus = "recording was empty";
      return;
   }

   const double sr = mAudioNode->RecordSampleRate();
   const float* data = mAudioNode->RecordBufferData();

   const std::string wavPath = AudioRecordings::GenerateFilePath("sampler");
   AudioRecordings::WriteWav(wavPath, data, frames, sr, 1);

   auto* decoded = new Platform::SampleBuffer();
   decoded->channels = 1;
   decoded->numFrames = frames;
   decoded->sampleRate = sr;
   decoded->channelData.assign(data, data + frames);

   FinishBuffer(decoded, "recorded audio", wavPath, "recorded");
}

bool SamplerNode::LoadFile(const std::string& path)
{
   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   // Turbo 0.49 (upstream): shared decode cache, so a reload (undo, paste, patch
   // load) or several Samplers on one file decode it once.
   if (!AudioDecodeCache::DecodeCached(path, *decoded, error))
   {
      delete decoded;
      mStatus = error.empty() ? "failed to load" : error;
      return false;
   }

   const size_t slash = path.find_last_of("/\\");
   const std::string fileName = (slash == std::string::npos) ? path : path.substr(slash + 1);
   FinishBuffer(decoded, fileName, path, "loaded");
   return true;
}

void SamplerNode::FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                                const std::string& filePath, const std::string& status)
{
   // Decimated min/max waveform for the visualizer - built once here on the
   // main thread from channel 0 only (a stereo file's L/R rarely differ
   // enough to matter for a shape overview).
   waveformCacheCount = std::min(kWaveformCacheSize, decoded->numFrames);
   if (waveformCacheCount > 0)
   {
      const int framesPerBucket = std::max(1, decoded->numFrames / waveformCacheCount);
      for (int b = 0; b < waveformCacheCount; b++)
      {
         float mn = 0.0f, mx = 0.0f;
         const int bucketStart = b * framesPerBucket;
         const int bucketEnd = std::min(decoded->numFrames, bucketStart + framesPerBucket);
         for (int i = bucketStart; i < bucketEnd; i++)
         {
            mn = std::min(mn, decoded->channelData[i]);
            mx = std::max(mx, decoded->channelData[i]);
         }
         waveformMin[b] = mn;
         waveformMax[b] = mx;
      }
   }

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSamplerNode>();
   // Turbo 0.49: full-resolution peaks for the waveform view (all channels).
   peaks.BuildFrom(*decoded);

   mAudioNode->PushBuffer(decoded);

   mFilePath = filePath;
   mFileName = fileName;
   mStatus = status;
   // A fresh buffer has neither been scrubbed nor range-trimmed yet.
   start = 0.0f;
   end = 1.0f;
   position = 0.0f;
}

void SamplerNode::ReloadFromPath()
{
   if (!mFilePath.empty())
   {
      float savedStart = start;
      float savedEnd = end;
      const float savedPos = position;
      LoadFile(mFilePath);
      start = savedStart;
      end = savedEnd;
      position = savedPos; // restored verbatim; CookIfNeeded clamps it into [start, end]
   }
}
