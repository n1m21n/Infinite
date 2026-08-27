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
   // reverse travels via the plain mReverse atomic instead - see ProcessBlock/TriggerVoice.

   constexpr int kMaxVoices = 16;
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
      // No envelope shaping to speak of - fast fixed attack/release just
      // enough to avoid a click on trigger/steal, not a musical parameter.
      mVoices.SetSampleRate(sampleRate);
      mVoices.SetADSR(2.0f, 0.0f, 1.0f, 15.0f);
      mSelfEnv.SetSampleRate(sampleRate);
      mSelfEnv.SetADSR(2.0f, 0.0f, 1.0f, 15.0f);

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
                    float decay, bool loop, bool reverse, bool pingpong)
   {
      mPitch.store(pitch, std::memory_order_relaxed);
      mFinetune.store(finetune, std::memory_order_relaxed);
      mSpeed.store(speed, std::memory_order_relaxed);
      mVolume.store(volume, std::memory_order_relaxed);
      mStart.store(start, std::memory_order_relaxed);
      mEnd.store(end, std::memory_order_relaxed);
      mPosition.store(position, std::memory_order_relaxed);
      mDecay.store(decay, std::memory_order_relaxed);
      mLoop.store(loop, std::memory_order_relaxed);
      mReverse.store(reverse, std::memory_order_relaxed);
      mPingpong.store(pingpong, std::memory_order_relaxed);
      mMailbox.Push(kPitchParam, pitch);
      mMailbox.Push(kFinetuneParam, finetune);
      mMailbox.Push(kSpeedParam, speed);
      mMailbox.Push(kVolumeParam, volume);
      mMailbox.Push(kLoopParam, loop ? 1.0f : 0.0f);
      mMailbox.Push(kPingpongParam, pingpong ? 1.0f : 0.0f);
   }

   MeterRing& PlayheadRing() { return mPlayheadRing; }

   void GetVisualSnapshot(SamplerVoiceSnapshot& out)
   {
      const int rIdx = mVisualReadIdx.load(std::memory_order_acquire);
      out = mVisualSnapshots[rIdx];
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

      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0)
         return;

      const float decaySec = mDecay.load(std::memory_order_relaxed);
      if (decaySec != mLastDecaySec)
      {
         mLastDecaySec = decaySec;
         const float decayMs = std::max(10.0f, decaySec * 1000.0f);
         mVoices.SetADSR(2.0f, decayMs, 0.0f, decayMs);
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

      if (!noteDriven && mWasNoteDriven && transportPlaying && !mSelfEnv.IsActive())
      {
         TriggerSelfVoice(-1.0f, SelfOwner::Transport);
      }
      mWasNoteDriven = noteDriven;

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

      // Detect manual position scrub/change (slider drag or param modulation)
      const float curStartFrac = mStart.load(std::memory_order_relaxed);
      const float curEndFrac = std::max(curStartFrac + 0.001f, mEnd.load(std::memory_order_relaxed));
      const float currentPosParam = std::clamp(mPosition.load(std::memory_order_relaxed), curStartFrac, curEndFrac);
      if (mLastPosParam >= 0.0f && fabsf(currentPosParam - mLastPosParam) > 1e-4f)
      {
         if (mActiveBuffer != nullptr)
            mSelfPos = (double)currentPosParam * mActiveBuffer->numFrames;
      }
      mLastPosParam = currentPosParam;

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

         const float pitchSemis = mMailbox.SmoothedValue(kPitchParam) + mMailbox.SmoothedValue(kFinetuneParam) / 100.0f;
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
            const float s = ReadSample(*mActiveBuffer, mVoicePos[v]) * env * mVoices.VelocityAt(v);

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
            const float s = ReadSample(*mActiveBuffer, mSelfPos) * env;

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

      // Snapshot active voice playheads for multi-note polyphonic UI rendering
      const int sampleFrames = mActiveBuffer ? mActiveBuffer->numFrames : 1;
      const int wIdx = (mVisualWriteIdx.load(std::memory_order_relaxed) + 1) % 3;
      SamplerVoiceSnapshot& snap = mVisualSnapshots[wIdx];
      snap.selfActive = mSelfEnv.IsActive();
      snap.selfPos = snap.selfActive ? (float)(mSelfPos / std::max(1, sampleFrames)) : -1.0f;
      snap.selfAmp = snap.selfActive ? mSelfEnv.Level() : 0.0f;
      int snapCount = 0;
      for (int v = 0; v < mVoices.NumVoices() && snapCount < SamplerVoiceSnapshot::kMaxVisualVoices; v++)
      {
         if (!mVoices.IsVoiceActive(v))
            continue;
         snap.voices[snapCount].position = (float)(mVoicePos[v] / std::max(1, sampleFrames));
         snap.voices[snapCount].amp = std::clamp(mVoices.EnvelopeAt(v).Level() * mVoices.VelocityAt(v), 0.0f, 1.0f);
         snap.voices[snapCount].note = mVoices.NoteAt(v);
         snapCount++;
      }
      snap.count = snapCount;
      mVisualWriteIdx.store(wIdx, std::memory_order_release);
      mVisualReadIdx.store(wIdx, std::memory_order_release);
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
      {
         const float posFrac = std::clamp(mPosition.load(std::memory_order_relaxed), startFrac, endFrac);
         if (initialDirSign < 0.0f)
            frac = (posFrac <= startFrac) ? endFrac : posFrac;
         else
            frac = posFrac;
      }

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
      {
         const float posFrac = std::clamp(mPosition.load(std::memory_order_relaxed), startFrac, endFrac);
         if (initialDirSign < 0.0f)
            frac = (posFrac <= startFrac) ? endFrac : posFrac;
         else
            frac = posFrac;
      }

      mSelfPos = mActiveBuffer != nullptr ? (double)frac * mActiveBuffer->numFrames : 0.0;
      mLastPosParam = frac;
      mSelfEnv.NoteOn();
      mSelfOwner = owner;
   }

   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;
   MeterRing mPlayheadRing;
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
   bool mWasNoteDriven = false;
   float mLastPosParam = -1.0f;
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
   std::atomic<float> mStart { 0.0f };
   std::atomic<float> mEnd { 1.0f };
   std::atomic<float> mPosition { 0.0f };
   std::atomic<float> mDecay { 2.0f };
   float mLastDecaySec = -1.0f;
   std::atomic<bool> mLoop { false };
   std::atomic<bool> mReverse { false };
   std::atomic<bool> mPingpong { false };

   std::vector<float> mRecordBuffer;      // preallocated once in PrepareToPlay
   std::atomic<int> mRecordWritePos { 0 };
   std::atomic<bool> mRecording { false };

   SamplerVoiceSnapshot mVisualSnapshots[3];
   std::atomic<int> mVisualWriteIdx { 0 };
   std::atomic<int> mVisualReadIdx { 0 };
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
   position = std::clamp(position, start, end);
   mAudioNode->PushParams(pitch, finetune, speed, volume, start, end, position, decay, loop, reverse, pingpong);
   mAudioNode->DrainRetired();

   float playhead = 0.0f;
   if (mAudioNode->PlayheadRing().ReadLatest(playhead))
      mPlayhead = playhead;
   mAudioNode->GetVisualSnapshot(mLatestVisualSnapshot);
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
   v.Float("position", position);
   v.Float("decay", decay);
   v.Float("volume", volume);
   v.Bool("loop", loop);
   v.Bool("reverse", reverse);
   v.Bool("pingpong", pingpong);
}

AudioNode* SamplerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSamplerNode>();
   return mAudioNode.get();
}

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
   if (!Platform::DecodeAudioFileToBuffer(path, *decoded, error))
   {
      delete decoded;
      mStatus = error.empty() ? "failed to load" : error;
      return false;
   }

   const size_t slash = path.find_last_of('/');
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
      float savedPos = position;
      float savedDecay = decay;
      LoadFile(mFilePath);
      start = savedStart;
      end = savedEnd;
      // Restored verbatim, like start/end above - CookIfNeeded clamps position
      // into [start, end] every frame anyway, and clamping here instead made
      // copy/paste (CopyParams -> ReloadDerivedState) drop the saved value.
      position = savedPos;
      decay = savedDecay;
   }
}
