#include "GrainMolderNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/MeterRing.h"
#include "audio/SampleSlot.h"
#include "platform/Platform.h"
#include "Transport.h"
#include "core/AudioTopologyRequest.h"

namespace
{
   constexpr int kMaxVoices = 16;
   constexpr int kReferenceNote = 60;

   constexpr int kMaxRecordSeconds = 30;
   constexpr int kMaxRecordSampleRate = 192000;

   float NoteToRate(int note, float pitchSemis)
   {
      return powf(2.0f, ((float)(note - kReferenceNote) + pitchSemis) / 12.0f);
   }

   float ReadSample(const Platform::SampleBuffer& buf, double pos, int channel = 0)
   {
      const float* chan = buf.channelData.data() + (size_t)channel * buf.numFrames;
      const int i0 = (int)pos;
      if (i0 < 0 || i0 >= buf.numFrames - 1)
         return (i0 >= 0 && i0 < buf.numFrames) ? chan[i0] : 0.0f;
      const float frac = (float)(pos - i0);
      const float a = chan[i0];
      const float b = chan[i0 + 1];
      return a + (b - a) * frac;
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

      pos = std::clamp(pos, startPos, endPos);
      return shouldRelease;
   }
}

// ------------------------------------------------------------- Audio Thread
class AudioGrainMolderNode : public AudioNode
{
public:
   AudioGrainMolderNode()
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

   SampleSlot& GetSampleSlot() { return mSampleSlot; }
   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }

   void TriggerPreview(float frac)
   {
      mPreviewFrac.store(frac, std::memory_order_release);
   }
   void StopPreview() { mStopRequested.store(true, std::memory_order_release); }

   void StartRecording()
   {
      mRecordWritePos.store(0, std::memory_order_release);
      mRecordingActive.store(true, std::memory_order_release);
   }
   void StopRecording() { mRecordingActive.store(false, std::memory_order_release); }
   int RecordedFrames() const { return mRecordWritePos.load(std::memory_order_acquire); }
   double RecordSampleRate() const { return mSampleRate; }
   const float* RecordBufferData() const { return mRecordBuffer.data(); }

   std::atomic<float> mLevel { 0.8f };
   std::atomic<float> mStart { 0.0f };
   std::atomic<float> mEnd { 1.0f };
   std::atomic<float> mPosition { 0.0f };
   std::atomic<float> mPitch { 0.0f };
   std::atomic<float> mDecay { 2.0f };
   std::atomic<bool> mLoop { false };
   std::atomic<bool> mReverse { false };
   std::atomic<bool> mPingpong { false };

   MeterRing& PlayheadRing() { return mPlayheadRing; }
   void GetVisualSnapshot(GrainMolderVoiceSnapshot& out)
   {
      const int rIdx = mVisualReadIdx.load(std::memory_order_acquire);
      out = mVisualSnapshots[rIdx];
   }

   bool IsPlaying() const { return mIsPlaying.load(std::memory_order_relaxed); }
   bool NotesSounding() const { return mNotesSounding.load(std::memory_order_relaxed); }
   int ActiveNoteCount() const { return mActiveNoteCount.load(std::memory_order_relaxed); }
   bool SelfOwnedByUser() const { return mSelfOwnedByUserPublished.load(std::memory_order_relaxed); }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      if (mSampleSlot.SwapIn())
      {
         mActiveBuffer = mSampleSlot.Active();
         if (Transport::Instance().IsPlaying() && mNoteInbox == nullptr)
            TriggerSelfVoice(-1.0f, SelfOwner::Transport);
      }

      const AudioBuffer* recordSrc = (numInputs > 1) ? inputs[1] : nullptr;
      if (mRecordingActive.load(std::memory_order_relaxed) && recordSrc != nullptr && recordSrc->numChannels > 0)
      {
         int pos = mRecordWritePos.load(std::memory_order_relaxed);
         const int cap = (int)mRecordBuffer.size();
         for (int i = 0; i < recordSrc->numFrames && pos < cap; i++, pos++)
            mRecordBuffer[pos] = recordSrc->channels[0][i];
         mRecordWritePos.store(pos, std::memory_order_release);
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      const float startFrac = mStart.load(std::memory_order_relaxed);

      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0)
      {
         mPlayheadRing.Write(&startFrac, 1);
         return;
      }

      const float decaySec = mDecay.load(std::memory_order_relaxed);
      if (decaySec != mLastDecaySec)
      {
         mLastDecaySec = decaySec;
         const float decayMs = std::max(10.0f, decaySec * 1000.0f);
         mVoices.SetADSR(2.0f, decayMs, 0.0f, decayMs);
      }

      if (mStopRequested.exchange(false, std::memory_order_acq_rel))
      {
         mSelfEnv.NoteOff();
         mSelfOwner = SelfOwner::None;
      }

      const bool noteDriven = mNoteInbox != nullptr;
      const bool transportPlaying = Transport::Instance().IsPlaying();

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

      const float previewFrac = mPreviewFrac.exchange(-1.0f, std::memory_order_acq_rel);
      if (previewFrac >= 0.0f)
         TriggerSelfVoice(previewFrac, SelfOwner::User);

      const float curStartFrac = mStart.load(std::memory_order_relaxed);
      const float curEndFrac = std::max(curStartFrac + 0.001f, mEnd.load(std::memory_order_relaxed));
      const float currentPosParam = std::clamp(mPosition.load(std::memory_order_relaxed), curStartFrac, curEndFrac);
      if (mLastPosParam >= 0.0f && fabsf(currentPosParam - mLastPosParam) > 1e-4f)
      {
         if (mActiveBuffer != nullptr)
            mSelfPos = (double)currentPosParam * mActiveBuffer->numFrames;
      }
      mLastPosParam = currentPosParam;

      const float endFrac = std::max(startFrac + 0.001f, mEnd.load(std::memory_order_relaxed));
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const bool pingpong = mPingpong.load(std::memory_order_relaxed);
      const bool reverseOn = mReverse.load(std::memory_order_relaxed);
      const double startPos = (double)startFrac * mActiveBuffer->numFrames;
      const double endPos = (double)endFrac * mActiveBuffer->numFrames;
      const float lvl = mLevel.load(std::memory_order_relaxed);
      const float pitchOffset = mPitch.load(std::memory_order_relaxed);
      const bool sourceStereo = mActiveBuffer->channels > 1;

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

         float sampleL = 0.0f, sampleR = 0.0f;

         for (int v = 0; v < mVoices.NumVoices(); v++)
         {
            if (!mVoices.IsVoiceActive(v))
               continue;

            if (!pingpong)
               mVoiceDir[v] = reverseOn ? -1 : 1;

            const float rate = NoteToRate(mVoices.NoteAt(v), pitchOffset + mVoiceBend[v]);
            const float env = mVoices.EnvelopeAt(v).Process();
            const float vel = mVoices.VelocityAt(v);

            const float sL = ReadSample(*mActiveBuffer, mVoicePos[v], 0) * env * vel;
            const float sR = sourceStereo ? ReadSample(*mActiveBuffer, mVoicePos[v], 1) * env * vel : sL;

            sampleL += sL;
            sampleR += sR;

            if (AdvanceVoicePosition(mVoicePos[v], mVoiceDir[v], loop, pingpong, rate, 1.0f, startPos, endPos))
               mVoices.NoteOff(mVoiceId[v]);
         }

         if (mSelfEnv.IsActive())
         {
            if (!pingpong)
               mSelfDir = reverseOn ? -1 : 1;

            const float rate = NoteToRate(kReferenceNote, pitchOffset);
            const float env = mSelfEnv.Process();
            const float sL = ReadSample(*mActiveBuffer, mSelfPos, 0) * env;
            const float sR = sourceStereo ? ReadSample(*mActiveBuffer, mSelfPos, 1) * env : sL;

            sampleL += sL;
            sampleR += sR;

            if (AdvanceVoicePosition(mSelfPos, mSelfDir, loop, pingpong, rate, 1.0f, startPos, endPos))
               mSelfEnv.NoteOff();
         }

         sampleL *= lvl;
         sampleR *= lvl;

         if (buffer.numChannels == 1)
            buffer.channels[0][i] = 0.5f * (sampleL + sampleR);
         else
         {
            if (buffer.numChannels > 0)
               buffer.channels[0][i] = sampleL;
            for (int ch = 1; ch < buffer.numChannels; ch++)
               buffer.channels[ch][i] = sampleR;
         }
      }

      float playheadOut = -1.0f;
      if (mSelfEnv.IsActive())
         playheadOut = (float)(mSelfPos / std::max(1, mActiveBuffer->numFrames));
      else if (mLastTriggeredVoice >= 0 && mVoices.IsVoiceActive(mLastTriggeredVoice))
         playheadOut = (float)(mVoicePos[mLastTriggeredVoice] / std::max(1, mActiveBuffer->numFrames));
      if (playheadOut < 0.0f)
         playheadOut = std::clamp(mPosition.load(std::memory_order_relaxed), startFrac, endFrac);

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

      const int sampleFrames = mActiveBuffer ? mActiveBuffer->numFrames : 1;
      const int wIdx = (mVisualWriteIdx.load(std::memory_order_relaxed) + 1) % 3;
      GrainMolderVoiceSnapshot& snap = mVisualSnapshots[wIdx];
      snap.selfActive = mSelfEnv.IsActive();
      snap.selfPos = snap.selfActive ? (float)(mSelfPos / std::max(1, sampleFrames)) : -1.0f;
      snap.selfAmp = snap.selfActive ? mSelfEnv.Level() : 0.0f;
      int snapCount = 0;
      for (int v = 0; v < mVoices.NumVoices() && snapCount < GrainMolderVoiceSnapshot::kMaxVisualVoices; v++)
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
   enum class SelfOwner
   {
      None,
      Transport,
      User
   };

   void TriggerVoice(int note, float velocity, float overrideStartFrac, int voiceId, float bendSemitones)
   {
      const bool reverseOn = mReverse.load(std::memory_order_relaxed);
      const float startFrac = mStart.load(std::memory_order_relaxed);
      const float endFrac = std::max(startFrac + 0.001f, mEnd.load(std::memory_order_relaxed));

      const int idx = mVoices.NoteOn(note, velocity, voiceId);
      const int baseDir = reverseOn ? -1 : 1;
      mVoiceDir[idx] = baseDir;

      float frac;
      if (overrideStartFrac >= 0.0f)
         frac = std::clamp(overrideStartFrac, startFrac, endFrac);
      else
      {
         const float posFrac = std::clamp(mPosition.load(std::memory_order_relaxed), startFrac, endFrac);
         if (baseDir < 0)
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

   void BendUpdate(int voiceId, float bendSemitones)
   {
      for (int v = 0; v < mVoices.NumVoices(); v++)
      {
         if (mVoices.IsVoiceActive(v) && mVoiceId[v] == voiceId)
            mVoiceBend[v] = bendSemitones;
      }
   }

   void TriggerSelfVoice(float overrideStartFrac, SelfOwner owner)
   {
      const bool reverseOn = mReverse.load(std::memory_order_relaxed);
      const float startFrac = mStart.load(std::memory_order_relaxed);
      const float endFrac = std::max(startFrac + 0.001f, mEnd.load(std::memory_order_relaxed));

      const int baseDir = reverseOn ? -1 : 1;
      mSelfDir = baseDir;

      float frac;
      if (overrideStartFrac >= 0.0f)
         frac = std::clamp(overrideStartFrac, startFrac, endFrac);
      else
      {
         const float posFrac = std::clamp(mPosition.load(std::memory_order_relaxed), startFrac, endFrac);
         if (baseDir < 0)
            frac = (posFrac <= startFrac) ? endFrac : posFrac;
         else
            frac = posFrac;
      }

      mSelfPos = mActiveBuffer != nullptr ? (double)frac * mActiveBuffer->numFrames : 0.0;
      mSelfEnv.NoteOn();
      mSelfOwner = owner;
   }

   double mSampleRate = 44100.0;
   SampleSlot mSampleSlot;
   Platform::SampleBuffer* mActiveBuffer = nullptr;

   VoiceAllocator mVoices;
   std::vector<double> mVoicePos;
   std::vector<int> mVoiceNote;
   std::vector<int> mVoiceId;
   std::vector<int> mVoiceDir;
   std::vector<float> mVoiceBend;
   int mLastTriggeredVoice = -1;

   Envelope mSelfEnv;
   double mSelfPos = 0.0;
   int mSelfDir = 1;
   SelfOwner mSelfOwner = SelfOwner::None;
   bool mTransportWasPlaying = false;
   bool mWasNoteDriven = false;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = 0;

   float mLastPosParam = -1.0f;
   float mLastDecaySec = 2.0f;

   std::atomic<float> mPreviewFrac { -1.0f };
   std::atomic<bool> mStopRequested { false };
   std::atomic<bool> mIsPlaying { false };
   std::atomic<bool> mNotesSounding { false };
   std::atomic<int> mActiveNoteCount { 0 };
   std::atomic<bool> mSelfOwnedByUserPublished { false };

   std::atomic<bool> mRecordingActive { false };
   std::atomic<int> mRecordWritePos { 0 };
   std::vector<float> mRecordBuffer;

   MeterRing mPlayheadRing;

   GrainMolderVoiceSnapshot mVisualSnapshots[3];
   std::atomic<int> mVisualWriteIdx { 0 };
   std::atomic<int> mVisualReadIdx { 0 };
};

// ------------------------------------------------------------- GrainMolderNode
GrainMolderNode::GrainMolderNode()
{
   mAudioNode = std::make_unique<AudioGrainMolderNode>();
}

GrainMolderNode::~GrainMolderNode()
{
   mAbort.store(true, std::memory_order_release);
   if (mWorkerThread.joinable())
      mWorkerThread.join();
}

AudioNode* GrainMolderNode::GetAudioNode()
{
   return mAudioNode.get();
}

void GrainMolderNode::VisitParams(ParamVisitor& v)
{
   v.Float("grain", grain);
   v.Float("amount", amount);
   v.Int("key", key);
   v.Bool("descending", descending);
   v.Int("seed", seed);
   v.Float("level", level);
   v.Float("pitch", pitch);
   v.Float("decay", decay);

   v.Float("start", start);
   v.Float("end", end);
   v.Float("position", position);
   v.Bool("loop", loop);
   v.Bool("reverse", reverse);
   v.Bool("pingpong", pingpong);
}

void GrainMolderNode::TriggerPreview(float frac)
{
   if (mAudioNode)
   {
      mAudioNode->TriggerPreview(frac);
      mIsPlaying = true;
      mSelfOwnedByUser = true;
   }
}

void GrainMolderNode::StopPreview()
{
   if (mAudioNode)
   {
      mAudioNode->StopPreview();
      mIsPlaying = false;
      mSelfOwnedByUser = false;
   }
}

void GrainMolderNode::StartRecording()
{
   if (mAudioNode)
   {
      mAudioNode->StartRecording();
      mRecording = true;
      mStatus = "recording...";
   }
}

void GrainMolderNode::StopRecording()
{
   if (mAudioNode && mRecording)
   {
      mAudioNode->StopRecording();
      mRecording = false;
      const int count = mAudioNode->RecordedFrames();
      const double sr = mAudioNode->RecordSampleRate();
      const float* data = mAudioNode->RecordBufferData();
      if (count > 0 && data)
      {
         std::vector<float> rec(data, data + count);
         LaunchJob(Job::NewSource, std::move(rec), sr);
      }
      else
      {
         mStatus = "recording empty";
      }
   }
}

bool GrainMolderNode::LoadFile(const std::string& path)
{
   if (path.empty())
      return false;

   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (!Platform::DecodeAudioFileToBuffer(path, *decoded, error))
   {
      delete decoded;
      mStatus = error.empty() ? "failed to load" : error;
      return false;
   }

   const size_t slash = path.find_last_of("/\\");
   mFileName = (slash == std::string::npos) ? path : path.substr(slash + 1);
   mFilePath = path;

   mSourceIsStereo = (decoded->channels > 1);
   mSourceSR = decoded->sampleRate;
   mSourceMono.assign(decoded->numFrames, 0.0f);

   const int channels = std::max(1, decoded->channels);
   if (mSourceIsStereo)
   {
      mSourceRight.assign(decoded->numFrames, 0.0f);
      const float* left = decoded->channelData.data();
      const float* right = decoded->channelData.data() + decoded->numFrames;
      for (int i = 0; i < decoded->numFrames; i++)
      {
         mSourceMono[i] = 0.5f * (left[i] + right[i]);
         mSourceRight[i] = right[i];
      }
   }
   else
   {
      mSourceRight.clear();
      const float* left = decoded->channelData.data();
      for (int i = 0; i < decoded->numFrames; i++)
         mSourceMono[i] = left[i];
   }

   delete decoded;

   LaunchJob(Job::NewSource);
   return true;
}

void GrainMolderNode::ReloadFromPath()
{
   if (!mFilePath.empty())
      LoadFile(mFilePath);
}

void GrainMolderNode::LaunchJob(Job job, std::vector<float> sourceOverride, double sourceOverrideSR)
{
   if (mWorkerThread.joinable())
   {
      mAbort.store(true, std::memory_order_release);
      mWorkerThread.join();
   }

   if (job == Job::NewSource && !sourceOverride.empty())
   {
      mSourceMono = std::move(sourceOverride);
      mSourceRight.clear();
      mSourceSR = sourceOverrideSR;
      mSourceIsStereo = false;
   }

   if (mSourceMono.empty())
      return;

   mCurrentJob = job;
   mWorking.store(true, std::memory_order_release);
   mAbort.store(false, std::memory_order_release);
   mResultReady.store(false, std::memory_order_release);

   mStatus = (job == Job::NewSource) ? "loading & molding..." : "molding...";

   const GrainMolderDsp::Params params {
      grain,
      amount,
      key,
      descending,
      (uint32_t)seed
   };

   mLastDispatched = { grain, amount, key, descending, seed };

   const std::vector<float> monoCopy = mSourceMono;
   const std::vector<float> rightCopy = mSourceRight;
   const double sr = mSourceSR;
   const bool isStereo = mSourceIsStereo;

   mWorkerThread = std::thread([this, monoCopy, rightCopy, sr, isStereo, params, job]() {
      PendingResult res;
      res.renderedSR = sr;
      res.isStereo = isStereo;
      res.isNewSource = (job == Job::NewSource);

      const float* rIn = (isStereo && !rightCopy.empty()) ? rightCopy.data() : nullptr;
      std::vector<float>* rOut = isStereo ? &res.renderedR : nullptr;

      GrainMolderDsp::Process(monoCopy.data(), (int)monoCopy.size(), sr, params,
                              res.renderedL, rOut, rIn, &mAbort);

      if (!mAbort.load(std::memory_order_relaxed))
      {
         mPendingResult = std::move(res);
         mResultReady.store(true, std::memory_order_release);
      }
      mWorking.store(false, std::memory_order_release);
   });
}

void GrainMolderNode::JoinWorkerIfDone()
{
   if (mWorkerThread.joinable() && !mWorking.load(std::memory_order_acquire))
   {
      mWorkerThread.join();
      mCurrentJob = Job::None;
   }
}

void GrainMolderNode::RebuildWaveformCache(const std::vector<float>& mono)
{
   if (mono.empty())
   {
      waveformCacheCount = 0;
      return;
   }

   const int n = (int)mono.size();
   waveformCacheCount = kWaveformCacheSize;
   for (int i = 0; i < kWaveformCacheSize; i++)
   {
      const int i0 = (int)((int64_t)i * n / kWaveformCacheSize);
      const int i1 = std::max(i0 + 1, (int)((int64_t)(i + 1) * n / kWaveformCacheSize));
      float mn = 1.0f;
      float mx = -1.0f;
      for (int k = i0; k < i1 && k < n; k++)
      {
         mn = std::min(mn, mono[k]);
         mx = std::max(mx, mono[k]);
      }
      waveformMin[i] = mn;
      waveformMax[i] = mx;
   }
}

void GrainMolderNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (mAudioNode)
   {
      mAudioNode->mLevel.store(level, std::memory_order_relaxed);
      mAudioNode->mStart.store(start, std::memory_order_relaxed);
      mAudioNode->mEnd.store(end, std::memory_order_relaxed);
      mAudioNode->mPosition.store(position, std::memory_order_relaxed);
      mAudioNode->mPitch.store(pitch, std::memory_order_relaxed);
      mAudioNode->mDecay.store(decay, std::memory_order_relaxed);
      mAudioNode->mLoop.store(loop, std::memory_order_relaxed);
      mAudioNode->mReverse.store(reverse, std::memory_order_relaxed);
      mAudioNode->mPingpong.store(pingpong, std::memory_order_relaxed);

      float ph = 0.0f;
      while (mAudioNode->PlayheadRing().Read(&ph, 1))
         mPlayhead = ph;

      mIsPlaying = mAudioNode->IsPlaying();
      mNotesSounding = mAudioNode->NotesSounding();
      mActiveNoteCount = mAudioNode->ActiveNoteCount();
      mSelfOwnedByUser = mAudioNode->SelfOwnedByUser();

      mAudioNode->GetVisualSnapshot(mLatestVisualSnapshot);
   }

   if (mResultReady.load(std::memory_order_acquire))
   {
      JoinWorkerIfDone();
      mResultReady.store(false, std::memory_order_relaxed);

      auto sampleBuf = std::make_unique<Platform::SampleBuffer>();
      sampleBuf->channels = mPendingResult.isStereo ? 2 : 1;
      sampleBuf->numFrames = (int)mPendingResult.renderedL.size();
      sampleBuf->sampleRate = mPendingResult.renderedSR;

      if (mPendingResult.isStereo)
      {
         sampleBuf->channelData.resize((size_t)sampleBuf->numFrames * 2);
         std::copy(mPendingResult.renderedL.begin(), mPendingResult.renderedL.end(), sampleBuf->channelData.begin());
         std::copy(mPendingResult.renderedR.begin(), mPendingResult.renderedR.end(), sampleBuf->channelData.begin() + sampleBuf->numFrames);
      }
      else
      {
         sampleBuf->channelData = std::move(mPendingResult.renderedL);
      }

      RebuildWaveformCache(mPendingResult.isStereo ? mPendingResult.renderedL : sampleBuf->channelData);
      const int numFrames = sampleBuf->numFrames;
      const double sampleRate = sampleBuf->sampleRate;
      if (mAudioNode)
         mAudioNode->PushBuffer(sampleBuf.release());

      char buf[128];
      snprintf(buf, sizeof(buf), "%.1fs - %d grains (%.0fms)",
               (double)numFrames / sampleRate,
               (int)((numFrames - (int)(grain * sampleRate / 1000.0)) / std::max(1, (int)(grain * sampleRate / 2000.0)) + 1),
               grain);
      mStatus = buf;
   }

   if (!mWorking.load(std::memory_order_relaxed) && !mSourceMono.empty())
   {
      const DispatchSnapshot current { grain, amount, key, descending, seed };
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
