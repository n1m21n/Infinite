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
#include "platform/Platform.h"

namespace
{
   constexpr int kPitchParam = 0;
   constexpr int kFinetuneParam = 1;
   constexpr int kSpeedParam = 2;
   constexpr int kVolumeParam = 3;
   constexpr int kStartParam = 4;
   constexpr int kEndParam = 5;
   constexpr int kLoopParam = 6;     // pushed as 0.0/1.0, no smoothing needed but the mailbox has no per-param opt-out
   constexpr int kReverseParam = 7;  // ditto
   constexpr int kPingpongParam = 8; // ditto

   constexpr int kMaxVoices = 8;
   constexpr int kFreeRunningNote = 60; // sample's own recorded pitch plays back at rate 1.0

   // Recording capacity: 30s mono at a generous upper-bound sample rate.
   // Preallocated once in PrepareToPlay (main thread, before the audio
   // callback ever runs) so Record itself never allocates on the audio
   // thread - it only ever writes into already-owned memory.
   constexpr int kMaxRecordSeconds = 30;
   constexpr int kMaxRecordSampleRate = 192000;

   float NoteToRate(int note, float pitchSemis)
   {
      return powf(2.0f, ((float)(note - kFreeRunningNote) + pitchSemis) / 12.0f);
   }

   // Tiny SPSC ring of raw pointers: the audio thread retires a superseded
   // Platform::SampleBuffer* here instead of deleting it directly (deleting
   // on the audio thread would free memory mid-callback, one of the
   // audio-thread prohibitions). The main thread drains and deletes them.
   // Same index discipline as MeterRing/NoteEventQueue. A full ring silently
   // drops the retire (the buffer leaks) rather than overwriting a slot the
   // consumer hasn't read - loads are rare enough that this never triggers
   // in practice, and leaking one buffer beats a double free.
   class BufferRetireRing
   {
   public:
      static constexpr int kCapacity = 8;

      // Audio thread only.
      void Retire(Platform::SampleBuffer* buf)
      {
         const size_t tail = mTail.load(std::memory_order_relaxed);
         const size_t head = mHead.load(std::memory_order_acquire);
         const size_t next = (tail + 1) % kCapacity;
         if (next == head)
            return; // full - drop rather than overwrite an unread slot
         mEntries[tail] = buf;
         mTail.store(next, std::memory_order_release);
      }

      // Main thread only.
      Platform::SampleBuffer* Drain()
      {
         const size_t head = mHead.load(std::memory_order_relaxed);
         const size_t tail = mTail.load(std::memory_order_acquire);
         if (head == tail)
            return nullptr;
         Platform::SampleBuffer* out = mEntries[head];
         mHead.store((head + 1) % kCapacity, std::memory_order_release);
         return out;
      }

   private:
      Platform::SampleBuffer* mEntries[kCapacity] = {};
      std::atomic<size_t> mHead { 0 };
      std::atomic<size_t> mTail { 0 };
   };
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
      mVoiceDir.assign(kMaxVoices, 1);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mMailbox.SetImmediate(kPitchParam, mPitch.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kFinetuneParam, mFinetune.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kSpeedParam, mSpeed.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kVolumeParam, mVolume.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kStartParam, mStart.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kEndParam, mEnd.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kLoopParam, mLoop.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
      mMailbox.SetImmediate(kReverseParam, mReverse.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
      mMailbox.SetImmediate(kPingpongParam, mPingpong.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
      // No envelope shaping to speak of - fast fixed attack/release just
      // enough to avoid a click on trigger/steal, not a musical parameter.
      mVoices.SetSampleRate(sampleRate);
      mVoices.SetADSR(2.0f, 0.0f, 1.0f, 15.0f);

      if (mRecordBuffer.empty())
         mRecordBuffer.resize((size_t)kMaxRecordSeconds * kMaxRecordSampleRate);
   }

   void SetNoteInbox(NoteEventQueue* inbox) override { mNoteInbox = inbox; }

   // Main thread only. Hands over ownership of a freshly decoded/recorded
   // buffer; the previously active one (if any) is retired through
   // mRetireRing rather than freed here.
   void PushBuffer(Platform::SampleBuffer* buf)
   {
      Platform::SampleBuffer* old = mPendingBuffer.exchange(buf, std::memory_order_acq_rel);
      if (old != nullptr)
         delete old; // never overtook by the audio thread - see DrainRetired()
   }

   // Main thread only, called once per frame from CookIfNeeded.
   void DrainRetired()
   {
      while (Platform::SampleBuffer* b = mRetireRing.Drain())
         delete b;
   }

   void PushParams(float pitch, float finetune, float speed, float volume, float start, float end, bool loop,
                    bool reverse, bool pingpong)
   {
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
      mMailbox.Push(kStartParam, start);
      mMailbox.Push(kEndParam, end);
      mMailbox.Push(kLoopParam, loop ? 1.0f : 0.0f);
      mMailbox.Push(kReverseParam, reverse ? 1.0f : 0.0f);
      mMailbox.Push(kPingpongParam, pingpong ? 1.0f : 0.0f);
   }

   MeterRing& PlayheadRing() { return mPlayheadRing; }

   // Main thread. Auditions the loaded sample from `frac` right away,
   // independent of the note graph - the last-write-wins atomic exchange
   // below means a rapid double-click just retargets the same pending
   // trigger rather than queuing two, which is the right behaviour for a
   // "play from here" gesture.
   void TriggerPreviewFromMainThread(float frac) { mPreviewFrac.store(frac, std::memory_order_release); }

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
      // mid-block, so a voice already reading mActiveBuffer this callback
      // finishes against a consistent buffer.
      if (Platform::SampleBuffer* fresh = mPendingBuffer.exchange(nullptr, std::memory_order_acq_rel))
      {
         if (mActiveBuffer != nullptr)
            mRetireRing.Retire(mActiveBuffer);
         mActiveBuffer = fresh;
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

      const bool noteDriven = mNoteInbox != nullptr;

      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (noteDriven)
         numEvts = mNoteInbox->Pop(evts, 64);
      else if (!mFreeRunningStarted)
      {
         // Same convention as Wavetable/Oscillator: patched with no note
         // cable, it just sounds - one permanently-open voice at note 60.
         TriggerVoice(kFreeRunningNote, 1.0f, -1.0f);
         mFreeRunningStarted = true;
      }

      // A pending manual preview (click-the-waveform) always wins the same
      // block it arrives in, note-driven or not - it's an explicit "play
      // from here" the user just asked for.
      const float previewFrac = mPreviewFrac.exchange(-1.0f, std::memory_order_acq_rel);
      if (previewFrac >= 0.0f)
         TriggerVoice(kFreeRunningNote, 1.0f, previewFrac);

      float playheadOut = -1.0f;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            if (evts[evtIdx].isNoteOn)
               TriggerVoice(evts[evtIdx].note, evts[evtIdx].velocity, -1.0f);
            else
               mVoices.NoteOff(evts[evtIdx].note);
            evtIdx++;
         }

         const float pitchSemis = mMailbox.SmoothedValue(kPitchParam) + mMailbox.SmoothedValue(kFinetuneParam) / 100.0f;
         const float speed = mMailbox.SmoothedValue(kSpeedParam);
         const float volume = mMailbox.SmoothedValue(kVolumeParam);
         const float startFrac = mMailbox.SmoothedValue(kStartParam);
         const float endFrac = std::max(startFrac + 0.001f, mMailbox.SmoothedValue(kEndParam));
         const bool loop = mMailbox.SmoothedValue(kLoopParam) > 0.5f;
         const bool pingpong = mMailbox.SmoothedValue(kPingpongParam) > 0.5f;
         const double startPos = (double)startFrac * mActiveBuffer->numFrames;
         const double endPos = (double)endFrac * mActiveBuffer->numFrames;
         const float speedSign = speed < 0.0f ? -1.0f : 1.0f;

         float sampleL = 0.0f, sampleR = 0.0f;

         for (int v = 0; v < mVoices.NumVoices(); v++)
         {
            if (!mVoices.IsVoiceActive(v))
               continue;

            const float rate = NoteToRate(mVoices.NoteAt(v), pitchSemis) * std::fabs(speed);
            const float dirSign = (float)mVoiceDir[v] * speedSign;
            const float env = mVoices.EnvelopeAt(v).Process();
            const float s = ReadSample(*mActiveBuffer, mVoicePos[v]) * env * mVoices.VelocityAt(v);

            sampleL += s;
            sampleR += s; // mono-summed voice, panned centre - no per-voice pan control in this minimal node

            mVoicePos[v] += rate * dirSign;

            const bool hitEnd = dirSign > 0.0f && mVoicePos[v] >= endPos;
            const bool hitStart = dirSign < 0.0f && mVoicePos[v] <= startPos;
            if (hitEnd || hitStart)
            {
               if (loop && pingpong)
               {
                  mVoiceDir[v] = -mVoiceDir[v];
                  mVoicePos[v] = hitEnd ? endPos : startPos;
               }
               else if (loop)
               {
                  mVoicePos[v] = hitEnd ? startPos : endPos;
               }
               else
               {
                  mVoices.NoteOff(mVoiceNote[v]);
               }
            }

            if (v == mLastTriggeredVoice)
               playheadOut = (float)(mVoicePos[v] / std::max(1, mActiveBuffer->numFrames));
         }

         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = (ch == 0 ? sampleL : sampleR) * volume;
      }

      if (playheadOut >= 0.0f)
         mPlayheadRing.Write(&playheadOut, 1);
   }

private:
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

   // overrideStartFrac >= 0 forces the trigger position (a manual preview
   // click); < 0 uses the configured start/end/reverse range like an
   // ordinary note-on.
   void TriggerVoice(int note, float velocity, float overrideStartFrac)
   {
      const bool reverseOn = mMailbox.SmoothedValue(kReverseParam) > 0.5f;
      const float speed = mMailbox.SmoothedValue(kSpeedParam);
      const float startFrac = mMailbox.SmoothedValue(kStartParam);
      const float endFrac = std::max(startFrac + 0.001f, mMailbox.SmoothedValue(kEndParam));

      const int idx = mVoices.NoteOn(note, velocity);
      const int baseDir = reverseOn ? -1 : 1;
      mVoiceDir[idx] = baseDir;
      const float initialDirSign = (float)baseDir * (speed < 0.0f ? -1.0f : 1.0f);

      float frac;
      if (overrideStartFrac >= 0.0f)
         frac = overrideStartFrac;
      else
         frac = initialDirSign < 0.0f ? endFrac : startFrac;

      mVoicePos[idx] = mActiveBuffer != nullptr ? (double)frac * mActiveBuffer->numFrames : 0.0;
      mVoiceNote[idx] = note;
      mLastTriggeredVoice = idx;
   }

   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;
   MeterRing mPlayheadRing;
   NoteEventQueue* mNoteInbox = nullptr;

   VoiceAllocator mVoices;
   std::vector<double> mVoicePos;
   std::vector<int> mVoiceNote;
   std::vector<int> mVoiceDir; // +1 forward, -1 backward; ping-pong flips this at each edge
   int mLastTriggeredVoice = -1;
   bool mFreeRunningStarted = false;
   std::atomic<float> mPreviewFrac { -1.0f };

   Platform::SampleBuffer* mActiveBuffer = nullptr;
   std::atomic<Platform::SampleBuffer*> mPendingBuffer { nullptr };
   BufferRetireRing mRetireRing;

   std::atomic<float> mPitch { 0.0f };
   std::atomic<float> mFinetune { 0.0f };
   std::atomic<float> mSpeed { 1.0f };
   std::atomic<float> mVolume { 0.8f };
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
   mAudioNode->PushParams(pitch, finetune, speed, volume, start, end, loop, reverse, pingpong);
   mAudioNode->DrainRetired();

   float playhead = 0.0f;
   if (mAudioNode->PlayheadRing().Read(&playhead, 1) > 0)
      mPlayhead = playhead;
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

void SamplerNode::StartRecording()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSamplerNode>();
   mRecording = true;
   mAudioNode->SetRecording(true);
   mStatus = "recording...";
}

void SamplerNode::StopRecording()
{
   if (!mRecording || !mAudioNode)
      return;
   mRecording = false;
   mAudioNode->SetRecording(false);

   const int frames = mAudioNode->RecordedFrames();
   if (frames <= 0)
   {
      mStatus = "recording was empty";
      return;
   }

   auto* decoded = new Platform::SampleBuffer();
   decoded->channels = 1;
   decoded->numFrames = frames;
   decoded->sampleRate = mAudioNode->RecordSampleRate();
   decoded->channelData.assign(mAudioNode->RecordBufferData(), mAudioNode->RecordBufferData() + frames);

   FinishBuffer(decoded, "recorded audio", "", "recorded");
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
}

void SamplerNode::ReloadFromPath()
{
   if (!mFilePath.empty())
      LoadFile(mFilePath);
}
