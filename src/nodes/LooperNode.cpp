#include "LooperNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "core/Transport.h"
#include "platform/Platform.h"

namespace
{
   enum Command
   {
      kCmdRecOn = 1,
      kCmdRecOff,
      kCmdPlayOn,
      kCmdPlayOff,
      kCmdDubOn,
      kCmdDubOff,
      kCmdClear
   };

   // Preallocated once per instance: 120 s of stereo at 48 kHz (~46 MB).
   // At 96 kHz the same buffer holds 60 s. Allocating here, not in
   // PrepareToPlay, means the audio thread can never observe a reallocation.
   constexpr int kCapacityFrames = 48000 * 120;
   constexpr int kDeclickFrames = 256;
}

class AudioLooperNode : public AudioNode
{
public:
   AudioLooperNode()
   {
      mBuffer[0].assign((size_t)kCapacityFrames, 0.0f);
      mBuffer[1].assign((size_t)kCapacityFrames, 0.0f);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
   }

   // ---- main thread -----------------------------------------------------
   void PushCommand(int cmd)
   {
      const uint32_t tail = mCmdTail.load(std::memory_order_relaxed);
      const uint32_t next = (tail + 1) % kCmdCapacity;
      if (next == mCmdHead.load(std::memory_order_acquire))
         return; // full: a burst of 64 unread presses, drop the newest
      mCmds[tail] = cmd;
      mCmdTail.store(next, std::memory_order_release);
   }

   void PushParams(const LooperNode& n)
   {
      mLengthMode.store(n.lengthMode, std::memory_order_relaxed);
      mBars.store(std::clamp(n.bars, 1, 32), std::memory_order_relaxed);
      mSubDivision.store(std::clamp(n.subDivision, 0, 3), std::memory_order_relaxed);
      mSyncStart.store(n.syncStart, std::memory_order_relaxed);
      mLoop.store(n.loop, std::memory_order_relaxed);
      mDirection.store(std::clamp(n.direction, 0, 2), std::memory_order_relaxed);
      mThru.store(std::clamp(n.thru, 0.0f, 2.0f), std::memory_order_relaxed);
      mLevelParam.store(std::clamp(n.level, 0.0f, 2.0f), std::memory_order_relaxed);
      const double autoFrames = n.autoLatency ? (double)Platform::AudioRoundTripLatencyFrames() : 0.0;
      const double frames = autoFrames + (double)std::clamp(n.latencyOffsetMs, -100.0f, 300.0f) * 0.001 * mSampleRate;
      mLatency.store((int)std::clamp(frames, 0.0, mSampleRate), std::memory_order_relaxed);
   }

   int LatencyFrames() const { return mLatency.load(std::memory_order_relaxed); }

   int PublishedState() const { return mPubState.load(std::memory_order_relaxed); }
   float PublishedLengthSec() const { return mPubLength.load(std::memory_order_relaxed); }
   float PublishedRecordedSec() const { return mPubRecorded.load(std::memory_order_relaxed); }
   float PublishedPos01() const { return mPubPos.load(std::memory_order_relaxed); }
   float PublishedPeak() const { return mPubPeak.load(std::memory_order_relaxed); }
   double SampleRate() const { return mSampleRate; }

   // ---- audio thread ----------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      const int numFrames = output.numFrames;
      const int direction = mDirection.load(std::memory_order_relaxed);
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const float thru = mThru.load(std::memory_order_relaxed);
      const float level = mLevelParam.load(std::memory_order_relaxed);

      // Commands, in order.
      int armedOffset = -1;
      for (;;)
      {
         const uint32_t head = mCmdHead.load(std::memory_order_relaxed);
         if (head == mCmdTail.load(std::memory_order_acquire))
            break;
         const int cmd = mCmds[head];
         mCmdHead.store((head + 1) % kCmdCapacity, std::memory_order_release);
         ApplyCommand(cmd, direction);
      }

      // An armed take starts on the next grid line inside this block, if any.
      if (mState == LooperNode::kArmed)
         armedOffset = ArmedStartOffset(numFrames);

      float peak = 0.0f;
      for (int i = 0; i < numFrames; i++)
      {
         float inL = 0.0f, inR = 0.0f;
         if (in != nullptr && in->numChannels > 0)
         {
            inL = in->channels[0][i];
            inR = in->numChannels > 1 ? in->channels[1][i] : inL;
         }
         float outL = inL * thru;
         float outR = inR * thru;

         if (mState == LooperNode::kArmed && armedOffset >= 0 && i >= armedOffset)
            BeginTake();

         if (mState == LooperNode::kRecording)
         {
            // Latency compensation: the first mSkip input frames belong to
            // before the take (they left the performer before the grid line).
            if (mSkip > 0)
               mSkip--;
            else if (mLength < kCapacityFrames)
            {
               mBuffer[0][(size_t)mLength] = inL;
               mBuffer[1][(size_t)mLength] = inR;
               mLength++;
            }
            if ((mTarget > 0 && mLength >= mTarget) || mLength >= kCapacityFrames)
               FinishTake(direction);
         }
         else if ((mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing) && mLength > 0)
         {
            const size_t idx = (size_t)std::clamp(mPos, 0, mLength - 1);
            outL += mBuffer[0][idx] * level;
            outR += mBuffer[1][idx] * level;
            if (mState == LooperNode::kOverdubbing)
            {
               // Same compensation: this input was played against the loop
               // position `latency` frames ago.
               size_t w = idx;
               if (direction == LooperNode::kForward && mLength > 0)
                  w = (size_t)(((int)idx - mTakeLatency % mLength + mLength) % mLength);
               mBuffer[0][w] += inL;
               mBuffer[1][w] += inR;
            }
            Advance(direction, loop);
         }

         if (output.numChannels > 0)
            output.channels[0][i] = outL;
         if (output.numChannels > 1)
            output.channels[1][i] = outR;
         for (int ch = 2; ch < output.numChannels; ch++)
            output.channels[ch][i] = outL;
         peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
      }

      mPubState.store(mState, std::memory_order_relaxed);
      const bool hasLoop = mState != LooperNode::kRecording && mState != LooperNode::kArmed && mLength > 0;
      mPubLength.store(hasLoop ? (float)(mLength / mSampleRate) : 0.0f, std::memory_order_relaxed);
      mPubRecorded.store(mState == LooperNode::kRecording ? (float)(mLength / mSampleRate) : 0.0f,
                         std::memory_order_relaxed);
      mPubPos.store(hasLoop ? (float)mPos / (float)std::max(1, mLength) : 0.0f, std::memory_order_relaxed);
      mPubPeak.store(peak, std::memory_order_relaxed);
   }

private:
   static constexpr int kCmdCapacity = 64;

   int TargetFrames() const
   {
      const int mode = mLengthMode.load(std::memory_order_relaxed);
      if (mode == LooperNode::kLengthFree)
         return 0;
      const double beatsPerBar = Transport::Instance().BeatsPerBar();
      const double bpm = std::max(1.0f, Transport::Instance().Tempo());
      double beats = beatsPerBar * (double)mBars.load(std::memory_order_relaxed);
      if (mode == LooperNode::kLengthSubBar)
      {
         static const double kFraction[4] = { 0.5, 0.25, 0.125, 0.0625 };
         beats = beatsPerBar * kFraction[std::clamp(mSubDivision.load(std::memory_order_relaxed), 0, 3)];
      }
      const double frames = beats * (60.0 / bpm) * mSampleRate;
      return (int)std::clamp(frames, 1.0, (double)kCapacityFrames);
   }

   double GridBeats() const
   {
      const double beatsPerBar = Transport::Instance().BeatsPerBar();
      if (mLengthMode.load(std::memory_order_relaxed) == LooperNode::kLengthSubBar)
      {
         static const double kFraction[4] = { 0.5, 0.25, 0.125, 0.0625 };
         return beatsPerBar * kFraction[std::clamp(mSubDivision.load(std::memory_order_relaxed), 0, 3)];
      }
      return beatsPerBar;
   }

   // Sample offset in this block where the next grid line falls, or -1.
   int ArmedStartOffset(int numFrames) const
   {
      if (!Transport::Instance().IsPlaying())
         return 0; // nothing to sync to: start now
      const double bpm = std::max(1.0f, Transport::Instance().Tempo());
      const double beatsPerSample = bpm / 60.0 / mSampleRate;
      const double grid = std::max(1e-6, GridBeats());
      const double now = Transport::Instance().Beats();
      const double end = now + beatsPerSample * (double)numFrames;
      const double next = std::ceil(now / grid - 1e-9) * grid;
      if (next >= end)
         return -1;
      return std::clamp((int)((next - now) / beatsPerSample), 0, std::max(0, numFrames - 1));
   }

   void StartPosition(int direction)
   {
      mPing = 1;
      mPos = (direction == LooperNode::kReverse) ? std::max(0, mLength - 1) : 0;
   }

   void FinishTake(int direction)
   {
      if (mLength <= 0)
      {
         mState = LooperNode::kIdle;
         return;
      }
      // Short fades at both ends so the loop seam does not click.
      const int fade = std::min(kDeclickFrames, mLength / 4);
      for (int i = 0; i < fade; i++)
      {
         const float g = (float)i / (float)std::max(1, fade);
         for (int ch = 0; ch < 2; ch++)
         {
            mBuffer[ch][(size_t)i] *= g;
            mBuffer[ch][(size_t)(mLength - 1 - i)] *= g;
         }
      }
      mState = LooperNode::kPlaying;
      StartPosition(direction);
      // The take ended `latency` frames after its musical end, so the loop is
      // already that far into its next pass.
      if (mTakeLatency > 0 && mLength > 0)
      {
         const int shift = mTakeLatency % mLength;
         if (direction == LooperNode::kReverse)
            mPos = std::max(0, mLength - 1 - shift);
         else
            mPos = shift;
      }
   }

   void BeginTake()
   {
      mState = LooperNode::kRecording;
      mLength = 0;
      mTakeLatency = LatencyFrames();
      mSkip = mTakeLatency;
   }

   void Advance(int direction, bool loop)
   {
      if (direction == LooperNode::kReverse)
      {
         if (--mPos < 0)
         {
            if (loop)
               mPos = mLength - 1;
            else
               Stop(direction);
         }
      }
      else if (direction == LooperNode::kPingPong)
      {
         mPos += mPing;
         if (mPos >= mLength)
         {
            mPing = -1;
            mPos = std::max(0, mLength - 2);
         }
         else if (mPos < 0)
         {
            mPing = 1;
            mPos = std::min(1, mLength - 1);
            if (!loop)
               Stop(direction);
         }
      }
      else
      {
         if (++mPos >= mLength)
         {
            if (loop)
               mPos = 0;
            else
               Stop(direction);
         }
      }
   }

   void Stop(int direction)
   {
      mState = LooperNode::kStopped;
      StartPosition(direction);
   }

   void ApplyCommand(int cmd, int direction)
   {
      switch (cmd)
      {
         case kCmdRecOn:
            if (mState == LooperNode::kRecording || mState == LooperNode::kArmed)
               break;
            mLength = 0;
            mPos = 0;
            mTarget = TargetFrames();
            if (mTarget > 0 && mSyncStart.load(std::memory_order_relaxed))
               mState = LooperNode::kArmed;
            else
               BeginTake();
            break;
         case kCmdRecOff:
            // Free take: keep recording for the latency window so the tail
            // that is still in flight lands in the loop, then close it.
            if (mState == LooperNode::kRecording && mTarget == 0 && mTakeLatency > 0)
               mTarget = std::max(1, mLength + mSkip + mTakeLatency);
            else if (mState == LooperNode::kRecording)
               FinishTake(direction);
            else if (mState == LooperNode::kArmed)
               mState = LooperNode::kIdle;
            break;
         case kCmdPlayOn:
            if (mLength > 0 && (mState == LooperNode::kIdle || mState == LooperNode::kStopped))
            {
               mState = LooperNode::kPlaying;
               StartPosition(direction);
            }
            break;
         case kCmdPlayOff:
            if (mState == LooperNode::kRecording)
            {
               FinishTake(direction);
               Stop(direction);
            }
            else if (mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing)
               Stop(direction);
            else if (mState == LooperNode::kArmed)
               mState = LooperNode::kIdle;
            break;
         case kCmdDubOn:
            if (mState == LooperNode::kPlaying)
               mState = LooperNode::kOverdubbing;
            else if (mLength > 0 && (mState == LooperNode::kIdle || mState == LooperNode::kStopped))
            {
               mState = LooperNode::kOverdubbing;
               StartPosition(direction);
            }
            break;
         case kCmdDubOff:
            if (mState == LooperNode::kOverdubbing)
               mState = LooperNode::kPlaying;
            break;
         case kCmdClear:
            mState = LooperNode::kIdle;
            mLength = 0;
            mPos = 0;
            mTarget = 0;
            break;
         default:
            break;
      }
   }

   std::vector<float> mBuffer[2];
   double mSampleRate = 48000.0;

   // Audio-thread state.
   int mState = LooperNode::kIdle;
   int mLength = 0; // frames in the loop (or recorded so far)
   int mTarget = 0; // fixed-length take, 0 = free
   int mPos = 0;
   int mPing = 1;
   int mSkip = 0;        // input frames still to drop before the take starts
   int mTakeLatency = 0; // compensation used by the current loop

   // Main -> audio.
   int mCmds[kCmdCapacity] = {};
   std::atomic<uint32_t> mCmdHead { 0 };
   std::atomic<uint32_t> mCmdTail { 0 };
   std::atomic<int> mLengthMode { LooperNode::kLengthBars };
   std::atomic<int> mBars { 1 };
   std::atomic<int> mSubDivision { 1 };
   std::atomic<bool> mSyncStart { true };
   std::atomic<bool> mLoop { true };
   std::atomic<int> mDirection { LooperNode::kForward };
   std::atomic<float> mThru { 1.0f };
   std::atomic<float> mLevelParam { 1.0f };
   std::atomic<int> mLatency { 0 };

   // Audio -> main.
   std::atomic<int> mPubState { LooperNode::kIdle };
   std::atomic<float> mPubLength { 0.0f };
   std::atomic<float> mPubRecorded { 0.0f };
   std::atomic<float> mPubPos { 0.0f };
   std::atomic<float> mPubPeak { 0.0f };
};

LooperNode::LooperNode() : mAudioNode(std::make_unique<AudioLooperNode>()) {}
LooperNode::~LooperNode() = default;

AudioNode* LooperNode::GetAudioNode()
{
   return mAudioNode.get();
}

double LooperNode::MaxSeconds()
{
   return (double)kCapacityFrames / 48000.0;
}

const char* LooperNode::StateName(int state)
{
   switch (state)
   {
      case kArmed: return "armed";
      case kRecording: return "recording";
      case kPlaying: return "playing";
      case kOverdubbing: return "overdub";
      case kStopped: return "stopped";
      default: return "empty";
   }
}

void LooperNode::SetRecord(bool on) { mAudioNode->PushCommand(on ? kCmdRecOn : kCmdRecOff); }
void LooperNode::SetPlay(bool on) { mAudioNode->PushCommand(on ? kCmdPlayOn : kCmdPlayOff); }
void LooperNode::SetOverdub(bool on) { mAudioNode->PushCommand(on ? kCmdDubOn : kCmdDubOff); }
void LooperNode::Clear() { mAudioNode->PushCommand(kCmdClear); }

void LooperNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   mAudioNode->PushParams(*this);
   mState = mAudioNode->PublishedState();
   mLengthSec = mAudioNode->PublishedLengthSec();
   mRecordedSec = mAudioNode->PublishedRecordedSec();
   mPos01 = mAudioNode->PublishedPos01();
   mLevel = mAudioNode->PublishedPeak();
   mCompMs = (float)(1000.0 * mAudioNode->LatencyFrames() / std::max(1.0, mAudioNode->SampleRate()));
}

void LooperNode::VisitParams(ParamVisitor& v)
{
   v.Int("lengthMode", lengthMode);
   v.Int("bars", bars);
   v.Int("subDivision", subDivision);
   v.Bool("syncStart", syncStart);
   v.Bool("loop", loop);
   v.Int("direction", direction);
   v.Float("thru", thru);
   v.Float("level", level);
   v.Bool("autoLatency", autoLatency);
   v.Float("latencyOffsetMs", latencyOffsetMs);
}
