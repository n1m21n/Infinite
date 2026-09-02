#include "SlicerNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "audio/WavWriter.h"
#include "audio/dsp/SlicerDsp.h"
#include "core/AudioDecodeCache.h"
#include "core/AudioTopologyRequest.h"
#include "platform/Platform.h"
#include "Transport.h"

namespace
{
   // Mailbox param ids. Slice boundaries deliberately do NOT travel here -
   // a smoothed loop boundary is meaningless, so they go through the plain
   // atomic table below (same reasoning as SamplerNode's start/end).
   constexpr int kPitchParam = 0;
   constexpr int kFinetuneParam = 1;
   constexpr int kSpeedParam = 2;
   constexpr int kVolumeParam = 3;
   constexpr int kDecayParam = 4;

   // Hidden constants - not user-visible params (see SlicerNode.h's header
   // comment and the design spec).
   constexpr float kFadeInMs = 2.0f;
   constexpr float kFadeOutMs = 3.0f;
   constexpr float kStealFadeMs = 2.0f;
   constexpr int kNumNoteVoices = 8;  // polyphony, steal oldest
   constexpr int kNumGhosts = 8;      // stolen voices finishing their crossfade
   constexpr int kSelfVoice = kNumNoteVoices; // the audition lane's own slot
   constexpr int kNumVoiceSlots = kNumNoteVoices + 1;

   constexpr int kMaxRecordSeconds = 30;
   constexpr int kMaxRecordSampleRate = 192000;

   // -60 dB at `decay`: exp(-t/tau) = 1e-3 at t = decay -> tau = decay/6.9.
   // The spec's 4.6 figure is the -40 dB constant and is what this node
   // uses, matching the "decay" length users hear as the tail.
   constexpr float kDecayTauDivisor = 4.6f;

   inline float RaisedCosine(float x01)
   {
      const float x = std::clamp(x01, 0.0f, 1.0f);
      return 0.5f * (1.0f - std::cos(3.14159265358979323846f * x));
   }
}

// Denominator, and whether it is a triplet, for each entry of the division
// dropdown. Shared with main.cpp's body, which draws the same names.
const char* const kSlicerDivisionNames[kSlicerNumDivisions] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
const float kSlicerDivisionDenoms[kSlicerNumDivisions] = { 4.0f, 8.0f, 8.0f, 16.0f, 16.0f, 32.0f };
const bool kSlicerDivisionTriplet[kSlicerNumDivisions] = { false, false, true, false, true, false };

// ------------------------------------------------------------- audio thread
class AudioSlicerNode : public AudioNode
{
public:
   AudioSlicerNode()
   {
      for (int i = 0; i <= SlicerNode::kMaxSlices; i++)
         mSliceStart[i].store(i == 0 ? 0.0f : 1.0f, std::memory_order_relaxed);
      mSliceCount.store(0, std::memory_order_relaxed);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      // Every mailbox param gets an immediate seed or the first block ramps
      // up from zero (SamplerNode.cpp:101-108's rule).
      mMailbox.SetImmediate(kPitchParam, mPitch.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kFinetuneParam, mFinetune.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kSpeedParam, mSpeed.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kVolumeParam, mVolume.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kDecayParam, mDecay.load(std::memory_order_relaxed));

      if (mRecordBuffer.empty())
         mRecordBuffer.resize((size_t)kMaxRecordSeconds * kMaxRecordSampleRate);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   // Main thread only.
   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }
   void DrainRetired() { mSampleSlot.DrainRetired(); }

   void PushParams(float pitch, float finetune, float speed, float volume, float decayMs)
   {
      mPitch.store(pitch, std::memory_order_relaxed);
      mFinetune.store(finetune, std::memory_order_relaxed);
      mSpeed.store(speed, std::memory_order_relaxed);
      mVolume.store(volume, std::memory_order_relaxed);
      mDecay.store(decayMs, std::memory_order_relaxed);
      mMailbox.Push(kPitchParam, pitch);
      mMailbox.Push(kFinetuneParam, finetune);
      mMailbox.Push(kSpeedParam, speed);
      mMailbox.Push(kVolumeParam, volume);
      mMailbox.Push(kDecayParam, decayMs);
   }

   // Main thread. `starts` holds `count` ascending 0..1 fractions; the count
   // is stored last (release) so the audio thread never sees a count that
   // outruns the fractions behind it.
   void PushSlices(const float* starts, int count)
   {
      const int n = std::clamp(count, 0, SlicerNode::kMaxSlices);
      for (int i = 0; i < n; i++)
         mSliceStart[i].store(std::clamp(starts[i], 0.0f, 1.0f), std::memory_order_relaxed);
      mSliceStart[n].store(1.0f, std::memory_order_relaxed);
      mSliceCount.store(n, std::memory_order_release);
   }

   void TriggerPreviewFromMainThread(int sliceIndex)
   {
      mPreviewSlice.store(sliceIndex, std::memory_order_release);
   }
   void RequestStopFromMainThread() { mStopRequested.store(true, std::memory_order_release); }
   bool IsPlaying() const { return mIsPlaying.load(std::memory_order_relaxed); }

   void GetVisualSnapshot(SlicerVoiceSnapshot& out)
   {
      const int rIdx = mVisualReadIdx.load(std::memory_order_acquire);
      out = mVisualSnapshots[rIdx];
   }

   void SetRecording(bool on)
   {
      if (on)
         mRecordWritePos.store(0, std::memory_order_relaxed);
      mRecording.store(on, std::memory_order_release);
   }
   int RecordedFrames() const { return mRecordWritePos.load(std::memory_order_acquire); }
   const float* RecordBufferData() const { return mRecordBuffer.data(); }
   double RecordSampleRate() const { return mSampleRate; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      // Adopt a newly loaded buffer only at the top of the block, never
      // mid-block: a voice already reading the active buffer must finish this
      // callback against a consistent one.
      if (mSampleSlot.SwapIn())
      {
         mActiveBuffer = mSampleSlot.Active();
         for (int v = 0; v < kNumVoiceSlots; v++)
            mVoices[v].active = false;
         for (int g = 0; g < kNumGhosts; g++)
            mGhosts[g].active = false;
      }

      // Slot 1, not 0 - slot 0 is the note pin in the shared pin index space.
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
      {
         mIsPlaying.store(false, std::memory_order_relaxed);
         PublishSnapshot();
         return;
      }

      if (mStopRequested.exchange(false, std::memory_order_acq_rel))
         BeginFadeOut(mVoices[kSelfVoice]);

      const int previewSlice = mPreviewSlice.exchange(-2, std::memory_order_acq_rel);
      if (previewSlice >= -1)
         StartVoice(mVoices[kSelfVoice], previewSlice, 1.0f, -1, -1);

      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (mNoteInbox != nullptr)
         numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);

      const int numFrames = mActiveBuffer->numFrames;
      const double srRatio = (mActiveBuffer->sampleRate > 0.0)
                                ? (mActiveBuffer->sampleRate / mSampleRate)
                                : 1.0;
      const float fadeOutSamples = std::max(1.0f, kFadeOutMs * 0.001f * (float)mSampleRate);

      for (int i = 0; i < buffer.numFrames; i++)
      {
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            const NoteEvent& e = evts[evtIdx];
            if (e.isNoteOn)
               NoteOn(e.note, e.velocity, e.voiceId);
            else if (!e.bendUpdate)
               NoteOff(e.voiceId);
            evtIdx++;
         }

         const float pitchSemis = mMailbox.SmoothedValue(kPitchParam) +
                                  mMailbox.SmoothedValue(kFinetuneParam) / 100.0f;
         const float speed = mMailbox.SmoothedValue(kSpeedParam);
         const float volume = mMailbox.SmoothedValue(kVolumeParam);
         const double rate = std::pow(2.0, (double)pitchSemis / 12.0) * (double)speed * srRatio;

         float sample = 0.0f;

         for (int v = 0; v < kNumVoiceSlots; v++)
         {
            Voice& vo = mVoices[v];
            if (!vo.active)
               continue;

            float g = vo.velocity;
            if (vo.fadeInLeft > 0)
            {
               g *= RaisedCosine(1.0f - (float)vo.fadeInLeft / (float)vo.fadeInTotal);
               vo.fadeInLeft--;
            }
            if (!vo.infinite)
               g *= std::exp(-(float)vo.elapsed / vo.tau);
            if (vo.fadeOutLeft >= 0)
            {
               g *= RaisedCosine((float)vo.fadeOutLeft / (float)vo.fadeOutTotal);
               vo.fadeOutLeft--;
               if (vo.fadeOutLeft < 0)
               {
                  vo.active = false;
                  continue;
               }
            }

            sample += ReadSample(*mActiveBuffer, vo.pos) * g;
            vo.lastGain = g;
            vo.pos += rate;
            vo.elapsed += 1.0f / (float)mSampleRate;

            if (vo.fadeOutLeft < 0)
            {
               // An infinite-decay slice stops at its own next boundary. A
               // decaying one is deliberately allowed to read PAST that
               // boundary (at speed < 1 the tail would otherwise truncate
               // audibly) and only stops at the buffer's end or when the
               // envelope has run out.
               const double stopPos = vo.infinite ? vo.endPos : (double)(numFrames - 1);
               if (vo.pos + fadeOutSamples * rate >= stopPos)
                  BeginFadeOut(vo);
               else if (!vo.infinite && std::exp(-(float)vo.elapsed / vo.tau) < 1.0e-4f)
                  BeginFadeOut(vo);
            }
         }

         for (int gI = 0; gI < kNumGhosts; gI++)
         {
            Ghost& gh = mGhosts[gI];
            if (!gh.active)
               continue;
            const float g = gh.gain * RaisedCosine((float)gh.left / (float)gh.total);
            sample += ReadSample(*mActiveBuffer, gh.pos) * g;
            gh.pos += rate;
            gh.left--;
            if (gh.left <= 0 || gh.pos < 0.0 || gh.pos >= (double)numFrames)
               gh.active = false;
         }

         const float out = sample * volume;
         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = out;
      }

      bool selfActive = mVoices[kSelfVoice].active;
      mIsPlaying.store(selfActive, std::memory_order_relaxed);
      PublishSnapshot();
   }

private:
   struct Voice
   {
      bool active = false;
      int note = -1;
      int slice = 0;
      int voiceId = -1;
      double pos = 0.0;
      double endPos = 0.0;
      float velocity = 1.0f;
      float elapsed = 0.0f;
      float tau = 1.0f;
      bool infinite = true;
      int fadeInLeft = 0;
      int fadeInTotal = 1;
      int fadeOutLeft = -1;
      int fadeOutTotal = 1;
      float lastGain = 0.0f;
      unsigned long long order = 0;
   };

   struct Ghost
   {
      bool active = false;
      double pos = 0.0;
      float gain = 0.0f;
      int left = 0;
      int total = 1;
   };

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

   void BeginFadeOut(Voice& v)
   {
      if (!v.active || v.fadeOutLeft >= 0)
         return;
      v.fadeOutTotal = std::max(1, (int)(kFadeOutMs * 0.001f * (float)mSampleRate));
      v.fadeOutLeft = v.fadeOutTotal;
   }

   void SpawnGhost(const Voice& v)
   {
      for (int i = 0; i < kNumGhosts; i++)
      {
         if (mGhosts[i].active)
            continue;
         mGhosts[i].active = true;
         mGhosts[i].pos = v.pos;
         mGhosts[i].gain = v.lastGain;
         mGhosts[i].total = std::max(1, (int)(kStealFadeMs * 0.001f * (float)mSampleRate));
         mGhosts[i].left = mGhosts[i].total;
         return;
      }
      // All ghost slots busy: the steal simply cuts, which at 2 ms of
      // simultaneous crossfades is inaudible anyway.
   }

   // sliceIndex < 0 plays the whole buffer (the audition lane's default).
   void StartVoice(Voice& v, int sliceIndex, float velocity, int note, int voiceId)
   {
      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0)
         return;

      if (v.active)
         SpawnGhost(v);

      const int count = mSliceCount.load(std::memory_order_acquire);
      float startFrac = 0.0f;
      float endFrac = 1.0f;
      if (sliceIndex >= 0 && count > 0)
      {
         const int idx = std::min(sliceIndex, count - 1);
         startFrac = mSliceStart[idx].load(std::memory_order_relaxed);
         endFrac = (idx + 1 < count) ? mSliceStart[idx + 1].load(std::memory_order_relaxed) : 1.0f;
      }
      if (endFrac <= startFrac)
         endFrac = 1.0f;

      const int numFrames = mActiveBuffer->numFrames;
      const float decayMs = mMailbox.SmoothedValue(kDecayParam);

      v.active = true;
      v.note = note;
      v.slice = std::max(0, sliceIndex);
      v.voiceId = voiceId;
      v.pos = (double)startFrac * numFrames;
      v.endPos = (double)endFrac * numFrames;
      v.velocity = std::clamp(velocity, 0.0f, 1.0f);
      v.elapsed = 0.0f;
      v.infinite = decayMs >= SlicerNode::kDecayInfinite;
      v.tau = std::max(1.0e-4f, (decayMs * 0.001f) / kDecayTauDivisor);
      v.fadeInTotal = std::max(1, (int)(kFadeInMs * 0.001f * (float)mSampleRate));
      v.fadeInLeft = v.fadeInTotal;
      v.fadeOutLeft = -1;
      v.lastGain = 0.0f;
      v.order = ++mVoiceOrder;
   }

   void NoteOn(int note, float velocity, int voiceId)
   {
      const int count = mSliceCount.load(std::memory_order_acquire);
      const int slice = note - SlicerNode::kBaseNote;
      // Out of range is silence: no wrap, no clamp, no fall back to slice 0.
      if (slice < 0 || slice >= count)
         return;

      int target = -1;
      for (int v = 0; v < kNumNoteVoices; v++)
      {
         if (!mVoices[v].active)
         {
            target = v;
            break;
         }
      }
      if (target < 0)
      {
         unsigned long long oldest = ~0ull;
         for (int v = 0; v < kNumNoteVoices; v++)
         {
            if (mVoices[v].order < oldest)
            {
               oldest = mVoices[v].order;
               target = v;
            }
         }
      }
      if (target < 0)
         return;
      StartVoice(mVoices[target], slice, velocity, note, voiceId);
   }

   void NoteOff(int voiceId)
   {
      // A slicer is one-shot by design: a slice plays its own length (or its
      // decay) regardless of how long the key is held, which is what every
      // hardware slicer does. Note-off is therefore only honoured for an
      // infinite-decay voice, where nothing else would ever stop it early.
      for (int v = 0; v < kNumNoteVoices; v++)
      {
         if (mVoices[v].active && mVoices[v].voiceId == voiceId && mVoices[v].infinite)
            BeginFadeOut(mVoices[v]);
      }
   }

   void PublishSnapshot()
   {
      const int frames = (mActiveBuffer != nullptr) ? std::max(1, mActiveBuffer->numFrames) : 1;
      const int wIdx = (mVisualWriteIdx.load(std::memory_order_relaxed) + 1) % 3;
      SlicerVoiceSnapshot& snap = mVisualSnapshots[wIdx];
      int n = 0;
      for (int v = 0; v < kNumVoiceSlots && n < SlicerVoiceSnapshot::kMaxVisualVoices; v++)
      {
         if (!mVoices[v].active)
            continue;
         snap.voices[n].position = (float)(mVoices[v].pos / (double)frames);
         snap.voices[n].amp = std::clamp(mVoices[v].lastGain, 0.0f, 1.0f);
         snap.voices[n].slice = mVoices[v].slice;
         n++;
      }
      snap.count = n;
      mVisualWriteIdx.store(wIdx, std::memory_order_release);
      mVisualReadIdx.store(wIdx, std::memory_order_release);
   }

   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   Voice mVoices[kNumVoiceSlots];
   Ghost mGhosts[kNumGhosts];
   unsigned long long mVoiceOrder = 0;

   Platform::SampleBuffer* mActiveBuffer = nullptr;
   SampleSlot mSampleSlot;

   std::atomic<float> mSliceStart[SlicerNode::kMaxSlices + 1];
   std::atomic<int> mSliceCount { 0 };

   std::atomic<float> mPitch { 0.0f };
   std::atomic<float> mFinetune { 0.0f };
   std::atomic<float> mSpeed { 1.0f };
   std::atomic<float> mVolume { 0.8f };
   std::atomic<float> mDecay { 5000.0f };

   std::atomic<int> mPreviewSlice { -2 };
   std::atomic<bool> mStopRequested { false };
   std::atomic<bool> mIsPlaying { false };

   std::vector<float> mRecordBuffer;
   std::atomic<int> mRecordWritePos { 0 };
   std::atomic<bool> mRecording { false };

   SlicerVoiceSnapshot mVisualSnapshots[3];
   std::atomic<int> mVisualWriteIdx { 0 };
   std::atomic<int> mVisualReadIdx { 0 };
};

// -------------------------------------------------------------- main thread
SlicerNode::SlicerNode() = default;

SlicerNode::~SlicerNode()
{
   mAbort.store(true, std::memory_order_release);
   if (mWorkerThread.joinable())
      mWorkerThread.join();
}

AudioNode* SlicerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSlicerNode>();
   return mAudioNode.get();
}

void SlicerNode::VisitParams(ParamVisitor& v)
{
   v.Text("path", mFilePath);
   v.Int("sliceBy", sliceBy);
   v.Int("onsets", onsets);
   v.Int("division", division);
   v.Float("sensitivity", sensitivity);
   v.Float("pitch", pitch);
   v.Float("finetune", finetune);
   v.Float("speed", speed);
   v.Float("decay", decay);
   v.Float("volume", volume);
   // The detected (and possibly hand-dragged) markers, so a save/load or a
   // copy/paste doesn't have to re-run analysis - and, more importantly, so
   // manual marker edits survive at all.
   v.Text("slices", mSliceBlob);
}

void SlicerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSlicerNode>();

   sliceBy = std::clamp(sliceBy, 0, 1);
   onsets = std::clamp(onsets, 1, kMaxSlices);
   division = std::clamp(division, 0, kSlicerNumDivisions - 1);

   mAudioNode->PushParams(pitch, finetune, speed, volume, decay);
   mAudioNode->DrainRetired();

   if (mResultReady.load(std::memory_order_acquire))
   {
      JoinWorkerIfDone();
      mResultReady.store(false, std::memory_order_relaxed);
      mCandidateFrac = std::move(mPendingResult.frac);
      mCandidateStrength = std::move(mPendingResult.strength);
      mPendingResult.frac.clear();
      mPendingResult.strength.clear();
      mSliceBlob = SerializeSlices();
      mSlicesDirty = true;
   }

   const float tempo = Transport::Instance().Tempo();
   if (mSlicesDirty || sliceBy != mLastRebuiltSliceBy || onsets != mLastRebuiltOnsets ||
       division != mLastRebuiltDivision || mSourceFrames != mLastRebuiltSourceFrames ||
       (sliceBy == 1 && tempo != mLastRebuiltTempo))
   {
      RebuildSlices();
      mSlicesDirty = false;
      mLastRebuiltSliceBy = sliceBy;
      mLastRebuiltOnsets = onsets;
      mLastRebuiltDivision = division;
      mLastRebuiltTempo = tempo;
      mLastRebuiltSourceFrames = mSourceFrames;
      PushSlicesToAudio();
   }

   mAudioNode->GetVisualSnapshot(mLatestVisualSnapshot);
   mIsPlaying = mAudioNode->IsPlaying();

   // Only `sensitivity` relaunches the detector. `onsets`, `slice by` and
   // `division` are handled entirely by RebuildSlices above.
   if (!mWorking.load(std::memory_order_relaxed) && !mSourceMono.empty())
   {
      const DispatchSnapshot current { sensitivity };
      if (!(current == mLastDispatched))
      {
         if (++mCooldownFrames >= 3)
         {
            mCooldownFrames = 0;
            LaunchJob(Job::ReProcess);
         }
      }
      else
      {
         mCooldownFrames = 0;
      }
   }
}

void SlicerNode::RebuildSlices()
{
   mSliceFrac.clear();
   if (mSourceFrames <= 0)
      return;

   if (sliceBy == 1)
   {
      // Grid: the global transport tempo, never a guessed file BPM.
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      double sliceLen = (60.0 / bpm) * (4.0 / (double)kSlicerDivisionDenoms[division]);
      if (kSlicerDivisionTriplet[division])
         sliceLen *= 2.0 / 3.0;
      const double total = (double)mSourceFrames / std::max(1.0, mSourceSR);
      if (sliceLen <= 1.0e-6 || total <= 0.0)
      {
         mSliceFrac.push_back(0.0f);
         return;
      }
      const int count = std::clamp((int)std::ceil(total / sliceLen), 1, kMaxSlices);
      for (int i = 0; i < count; i++)
         mSliceFrac.push_back((float)std::clamp((double)i * sliceLen / total, 0.0, 1.0));
      return;
   }

   if (mCandidateFrac.empty())
   {
      mSliceFrac.push_back(0.0f);
      return;
   }

   // Onsets: keep the top N by peak strength, then re-sort ascending by time.
   // The forced onset at 0 carries a sentinel strength so it always survives.
   struct Item
   {
      float frac;
      float strength;
   };
   std::vector<Item> items;
   items.reserve(mCandidateFrac.size());
   for (size_t i = 0; i < mCandidateFrac.size(); i++)
   {
      const float s = (i < mCandidateStrength.size()) ? mCandidateStrength[i] : 0.0f;
      items.push_back({ mCandidateFrac[i], s });
   }

   const int keep = std::clamp(onsets, 1, kMaxSlices);
   if ((int)items.size() > keep)
   {
      std::stable_sort(items.begin(), items.end(),
                       [](const Item& a, const Item& b) { return a.strength > b.strength; });
      items.resize((size_t)keep);
   }
   std::stable_sort(items.begin(), items.end(),
                    [](const Item& a, const Item& b) { return a.frac < b.frac; });

   for (const Item& it : items)
      mSliceFrac.push_back(std::clamp(it.frac, 0.0f, 1.0f));
   if (mSliceFrac.empty() || mSliceFrac.front() > 0.0f)
      mSliceFrac.insert(mSliceFrac.begin(), 0.0f);
   if ((int)mSliceFrac.size() > kMaxSlices)
      mSliceFrac.resize(kMaxSlices);
}

void SlicerNode::PushSlicesToAudio()
{
   if (!mAudioNode)
      return;
   mAudioNode->PushSlices(mSliceFrac.empty() ? nullptr : mSliceFrac.data(), (int)mSliceFrac.size());
}

std::string SlicerNode::SerializeSlices() const
{
   std::ostringstream os;
   for (size_t i = 0; i < mCandidateFrac.size(); i++)
   {
      if (i > 0)
         os << ' ';
      const float s = (i < mCandidateStrength.size()) ? mCandidateStrength[i] : 0.0f;
      char buf[64];
      snprintf(buf, sizeof(buf), "%.6f:%.4f", mCandidateFrac[i], s);
      os << buf;
   }
   return os.str();
}

void SlicerNode::DeserializeSlices(const std::string& blob)
{
   mCandidateFrac.clear();
   mCandidateStrength.clear();
   std::istringstream is(blob);
   std::string token;
   while (is >> token)
   {
      const size_t colon = token.find(':');
      const float frac = (float)atof(token.substr(0, colon).c_str());
      const float strength =
         (colon == std::string::npos) ? 1.0f : (float)atof(token.substr(colon + 1).c_str());
      mCandidateFrac.push_back(std::clamp(frac, 0.0f, 1.0f));
      mCandidateStrength.push_back(strength);
   }
   if (mCandidateFrac.empty())
      return;
   if (mCandidateFrac.front() > 0.0f)
   {
      mCandidateFrac.insert(mCandidateFrac.begin(), 0.0f);
      mCandidateStrength.insert(mCandidateStrength.begin(), SlicerDsp::kForcedOnsetStrength);
   }
   else
   {
      mCandidateStrength[0] = SlicerDsp::kForcedOnsetStrength;
   }
}

void SlicerNode::MoveSliceMarker(int index, float frac)
{
   if (!MarkersAreEditable())
      return;
   if (index <= 0 || index >= (int)mSliceFrac.size())
      return;

   const float lo = mSliceFrac[index - 1] + 0.001f;
   const float hi = (index + 1 < (int)mSliceFrac.size()) ? mSliceFrac[index + 1] - 0.001f : 0.999f;
   if (hi <= lo)
      return;
   const float oldFrac = mSliceFrac[index];
   const float newFrac = std::clamp(frac, lo, hi);
   mSliceFrac[index] = newFrac;

   // Move the candidate the marker came from too, so the edit survives an
   // `onsets` change (which re-prunes from the candidate list) and a save.
   int best = -1;
   float bestDist = 1.0e9f;
   for (size_t i = 0; i < mCandidateFrac.size(); i++)
   {
      const float d = std::fabs(mCandidateFrac[i] - oldFrac);
      if (d < bestDist)
      {
         bestDist = d;
         best = (int)i;
      }
   }
   if (best > 0 && bestDist < 0.02f)
   {
      mCandidateFrac[best] = newFrac;
      std::vector<size_t> order(mCandidateFrac.size());
      for (size_t i = 0; i < order.size(); i++)
         order[i] = i;
      std::stable_sort(order.begin(), order.end(),
                       [&](size_t a, size_t b) { return mCandidateFrac[a] < mCandidateFrac[b]; });
      std::vector<float> f, s;
      f.reserve(order.size());
      s.reserve(order.size());
      for (size_t i : order)
      {
         f.push_back(mCandidateFrac[i]);
         s.push_back(i < mCandidateStrength.size() ? mCandidateStrength[i] : 0.0f);
      }
      mCandidateFrac.swap(f);
      mCandidateStrength.swap(s);
   }
   mSliceBlob = SerializeSlices();
   PushSlicesToAudio();
}

void SlicerNode::TriggerSlicePreview(int index)
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSlicerNode>();
   mAudioNode->TriggerPreviewFromMainThread(index);
}

void SlicerNode::StopPreview()
{
   if (mAudioNode)
      mAudioNode->RequestStopFromMainThread();
}

void SlicerNode::StartRecording()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSlicerNode>();
   mRecording = true;
   mAudioNode->SetRecording(true);
   mStatus = "recording...";
   AudioTopologyRequest::Request();
}

void SlicerNode::StopRecording()
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

   const std::string wavPath = AudioRecordings::GenerateFilePath("slicer");
   AudioRecordings::WriteWav(wavPath, data, frames, sr, 1);

   auto* decoded = new Platform::SampleBuffer();
   decoded->channels = 1;
   decoded->numFrames = frames;
   decoded->sampleRate = sr;
   decoded->channelData.assign(data, data + frames);

   FinishBuffer(decoded, "recorded audio", wavPath, "recorded");
}

bool SlicerNode::LoadFile(const std::string& path)
{
   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (!AudioDecodeCache::DecodeCached(path, *decoded, error))
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

// Shared tail of LoadFile()/StopRecording(): builds the waveform cache, keeps
// a main-thread mono copy for the analyser, hands the buffer to the audio
// thread, and kicks off onset detection. Takes ownership of `decoded`.
void SlicerNode::FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                              const std::string& filePath, const std::string& status)
{
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

   mSourceFrames = decoded->numFrames;
   mSourceSR = decoded->sampleRate > 0.0 ? decoded->sampleRate : 44100.0;
   mSourceMono.assign(decoded->channelData.begin(),
                      decoded->channelData.begin() + decoded->numFrames);

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSlicerNode>();
   mAudioNode->PushBuffer(decoded);

   mFilePath = filePath;
   mFileName = fileName;
   mStatus = status;

   mCandidateFrac.clear();
   mCandidateStrength.clear();
   mSliceBlob.clear();
   mSlicesDirty = true;

   LaunchJob(Job::NewSource);
}

void SlicerNode::ReSlice()
{
   if (mSourceMono.empty())
      return;
   LaunchJob(Job::NewSource);
}

void SlicerNode::ReloadFromPath()
{
   if (mFilePath.empty())
      return;

   // FinishBuffer resets the marker blob and relaunches analysis; every one
   // of these is restored verbatim afterwards, the same save/restore dance
   // SamplerNode::ReloadFromPath does for start/end/position/decay (and for
   // the same reason: copy/paste routes through here and would otherwise
   // drop the pasted values).
   const std::string savedBlob = mSliceBlob;
   const int savedSliceBy = sliceBy;
   const int savedOnsets = onsets;
   const int savedDivision = division;
   const float savedSensitivity = sensitivity;
   const float savedPitch = pitch;
   const float savedFinetune = finetune;
   const float savedSpeed = speed;
   const float savedDecay = decay;
   const float savedVolume = volume;

   LoadFile(mFilePath);

   sliceBy = savedSliceBy;
   onsets = savedOnsets;
   division = savedDivision;
   sensitivity = savedSensitivity;
   pitch = savedPitch;
   finetune = savedFinetune;
   speed = savedSpeed;
   decay = savedDecay;
   volume = savedVolume;

   if (!savedBlob.empty())
   {
      // Markers were saved (possibly hand-edited) - abandon the analysis
      // FinishBuffer just launched and use them instead.
      mAbort.store(true, std::memory_order_release);
      if (mWorkerThread.joinable())
         mWorkerThread.join();
      mWorking.store(false, std::memory_order_release);
      mResultReady.store(false, std::memory_order_release);
      mCurrentJob = Job::None;

      mSliceBlob = savedBlob;
      DeserializeSlices(savedBlob);
      mLastDispatched = { sensitivity };
      mSlicesDirty = true;
   }
}

void SlicerNode::LaunchJob(Job job)
{
   if (mWorkerThread.joinable())
   {
      mAbort.store(true, std::memory_order_release);
      mWorkerThread.join();
   }
   if (mSourceMono.empty())
      return;

   mCurrentJob = job;
   mWorking.store(true, std::memory_order_release);
   mAbort.store(false, std::memory_order_release);
   mResultReady.store(false, std::memory_order_release);
   mStatus = "analyzing...";

   SlicerDsp::Params params;
   params.sensitivity = sensitivity;
   params.maxSlices = kMaxSlices;
   mLastDispatched = { sensitivity };

   const std::vector<float> monoCopy = mSourceMono;
   const double sr = mSourceSR;

   mWorkerThread = std::thread([this, monoCopy, sr, params]() {
      std::vector<int> frames;
      std::vector<float> strengths;
      SlicerDsp::Detect(monoCopy.data(), (int)monoCopy.size(), sr, params, frames, strengths, &mAbort);

      if (!mAbort.load(std::memory_order_relaxed))
      {
         PendingResult res;
         const float len = (float)std::max<size_t>(1, monoCopy.size());
         res.frac.reserve(frames.size());
         for (int f : frames)
            res.frac.push_back(std::clamp((float)f / len, 0.0f, 1.0f));
         res.strength = std::move(strengths);
         mPendingResult = std::move(res);
         mResultReady.store(true, std::memory_order_release);
      }
      mWorking.store(false, std::memory_order_release);
   });
}

void SlicerNode::JoinWorkerIfDone()
{
   if (mWorkerThread.joinable() && !mWorking.load(std::memory_order_acquire))
   {
      mWorkerThread.join();
      mCurrentJob = Job::None;
      if (!mFileName.empty())
         mStatus = "loaded";
   }
}
