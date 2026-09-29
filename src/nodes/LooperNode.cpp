#include "LooperNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/SampleSlot.h"
#include "core/Transport.h"
#include "platform/Platform.h"

namespace
{
   // The longest take, at whatever rate the engine runs. The buffer is sized
   // when the node is prepared (never in the constructor, so an unused Looper
   // costs nothing) and capped so a 192 kHz device cannot ask for a
   // pathological allocation: 12M stereo frames is 96 MB at the absolute worst.
   constexpr double kMaxSeconds = LooperNode::MaxSeconds();
   constexpr int kMaxFrames = 12000000;
   constexpr int kMinTakeFrames = 32;
   constexpr int kDeclickFrames = 128;
   constexpr int kCmdCapacity = 32;

   struct LoopBuf
   {
      std::vector<float> ch[2];
      int capacity = 0;
      double sampleRate = 0.0;
   };
}

// ------------------------------------------------------------- audio thread
class AudioLooperNode : public AudioNode
{
public:
   // Main thread. Sized to the rate the engine is actually running at; a rate
   // change hands over a fresh buffer (the old loop is dropped - its content
   // was recorded at another rate).
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
      mSampleRate.store(sr, std::memory_order_relaxed);
      // Start the gain ramps at their targets, not at construction defaults.
      mThruNow = mThru.load(std::memory_order_relaxed);
      mLevelNow = mLevel.load(std::memory_order_relaxed);
      if (mBufRate == sr)
         return;
      auto* buf = new LoopBuf();
      buf->sampleRate = sr;
      buf->capacity = (int)std::min<double>(sr * kMaxSeconds, (double)kMaxFrames);
      buf->ch[0].assign((size_t)buf->capacity, 0.0f);
      buf->ch[1].assign((size_t)buf->capacity, 0.0f);
      mBufSlot.Push(buf);
      mBufRate = sr;
   }

   // ---- main thread ------------------------------------------------------
   void PushCommand(int button)
   {
      const uint32_t tail = mCmdTail.load(std::memory_order_relaxed);
      const uint32_t next = (tail + 1) % kCmdCapacity;
      if (next == mCmdHead.load(std::memory_order_acquire))
         return; // a burst of unread presses: drop the newest
      mCmds[tail] = button;
      mCmdTail.store(next, std::memory_order_release);
   }

   void PushParams(const LooperNode& n, int autoLatencyFrames)
   {
      mTake.store(std::clamp(n.take, 0, (int)MusicTime::kNumRateDivisions), std::memory_order_relaxed);
      mSync.store(n.syncStart, std::memory_order_relaxed);
      mThru.store(std::clamp(n.thru, 0.0f, 2.0f), std::memory_order_relaxed);
      mLevel.store(std::clamp(n.level, 0.0f, 2.0f), std::memory_order_relaxed);
      mAutoLatencyFrames.store(n.autoLatency ? std::max(0, autoLatencyFrames) : 0, std::memory_order_relaxed);
      mTrimMs.store(std::clamp(n.trimMs, -100.0f, 300.0f), std::memory_order_relaxed);
   }

   void DrainRetired() { mBufSlot.DrainRetired(); }

   int PublishedState() const { return mPubState.load(std::memory_order_relaxed); }
   float PublishedLoopSec() const { return mPubLoopSec.load(std::memory_order_relaxed); }
   float PublishedRecordedSec() const { return mPubRecSec.load(std::memory_order_relaxed); }
   float PublishedPos01() const { return mPubPos.load(std::memory_order_relaxed); }
   float PublishedPeak() const { return mPubPeak.exchange(0.0f, std::memory_order_relaxed); }
   double SampleRate() const { return mSampleRate.load(std::memory_order_relaxed); }
   int CompensationFrames() const { return mPubComp.load(std::memory_order_relaxed); }

   // ---- audio thread -----------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      const int numFrames = output.numFrames;
      const double sr = mSampleRate.load(std::memory_order_relaxed);

      if (mBufSlot.SwapIn())
      {
         // Fresh buffer (first prepare, or a rate change): nothing recorded in it.
         mBuf = mBufSlot.Active();
         mState = LooperNode::kEmpty;
         mLength = 0;
         mPos = 0;
      }

      const float thruTarget = mThru.load(std::memory_order_relaxed);
      const float levelTarget = mLevel.load(std::memory_order_relaxed);
      const double compFrames = (double)mAutoLatencyFrames.load(std::memory_order_relaxed) +
                                (double)mTrimMs.load(std::memory_order_relaxed) * 0.001 * sr;
      mComp = (int)std::clamp(compFrames, 0.0, sr);
      mPubComp.store(mComp, std::memory_order_relaxed);

      // Presses, in order.
      for (;;)
      {
         const uint32_t head = mCmdHead.load(std::memory_order_relaxed);
         if (head == mCmdTail.load(std::memory_order_acquire))
            break;
         const int cmd = mCmds[head];
         mCmdHead.store((head + 1) % kCmdCapacity, std::memory_order_release);
         if (cmd < 0)
            SeedTone(); // sweep rig only (LooperNode::SweepPrepare)
         else
            ApplyButton(cmd);
      }

      // An armed take starts on the first grid line inside this block.
      int armedOffset = -1;
      if (mState == LooperNode::kArmed)
         armedOffset = ArmedStartOffset(numFrames, sr);

      const float invN = 1.0f / (float)std::max(1, numFrames);
      float peak = 0.0f;
      for (int i = 0; i < numFrames; i++)
      {
         float inL = 0.0f, inR = 0.0f;
         if (in != nullptr && in->numChannels > 0)
         {
            inL = in->channels[0][i];
            inR = in->numChannels > 1 ? in->channels[1][i] : inL;
         }
         const float t = (float)(i + 1) * invN;
         const float thru = mThruNow + (thruTarget - mThruNow) * t;
         const float lvl = mLevelNow + (levelTarget - mLevelNow) * t;
         float outL = inL * thru;
         float outR = inR * thru;

         if (mState == LooperNode::kArmed && armedOffset >= 0 && i >= armedOffset)
            BeginTake();

         if (mBuf != nullptr)
         {
            if (mState == LooperNode::kRecording)
            {
               if (mSkip > 0)
                  mSkip--; // input from before the take's musical start
               else if (mLength < mBuf->capacity)
               {
                  mBuf->ch[0][(size_t)mLength] = inL;
                  mBuf->ch[1][(size_t)mLength] = inR;
                  mLength++;
               }
               if ((mTarget > 0 && mLength >= mTarget) || mLength >= mBuf->capacity)
                  FinishTake(LooperNode::kPlaying);
            }
            else if ((mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing) && mLength > 0)
            {
               const int idx = std::clamp(mPos, 0, mLength - 1);
               outL += mBuf->ch[0][(size_t)idx] * lvl;
               outR += mBuf->ch[1][(size_t)idx] * lvl;
               if (mState == LooperNode::kOverdubbing)
               {
                  // This input was played against the loop position `latency`
                  // frames ago.
                  const int w = ((idx - mTakeComp % mLength) + mLength) % mLength;
                  mBuf->ch[0][(size_t)w] = std::clamp(mBuf->ch[0][(size_t)w] + inL, -4.0f, 4.0f);
                  mBuf->ch[1][(size_t)w] = std::clamp(mBuf->ch[1][(size_t)w] + inR, -4.0f, 4.0f);
               }
               if (++mPos >= mLength)
                  mPos = 0;
            }
         }

         for (int ch = 0; ch < output.numChannels; ch++)
            output.channels[ch][i] = (ch & 1) ? outR : outL;
         peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
      }
      mThruNow = thruTarget;
      mLevelNow = levelTarget;

      const bool hasLoop = mLength > 0 && mState != LooperNode::kRecording && mState != LooperNode::kArmed;
      mPubState.store(mState, std::memory_order_relaxed);
      mPubLoopSec.store(hasLoop ? (float)((double)mLength / sr) : 0.0f, std::memory_order_relaxed);
      mPubRecSec.store(mState == LooperNode::kRecording ? (float)((double)mLength / sr) : 0.0f,
                       std::memory_order_relaxed);
      mPubPos.store(hasLoop ? (float)mPos / (float)std::max(1, mLength) : 0.0f, std::memory_order_relaxed);
      if (peak > mPubPeak.load(std::memory_order_relaxed))
         mPubPeak.store(peak, std::memory_order_relaxed);
   }

private:
   int TargetFrames(double sr) const
   {
      const int take = mTake.load(std::memory_order_relaxed);
      if (take <= 0)
         return 0; // free
      const double bpm = std::max(1.0f, Transport::Instance().Tempo());
      const double beats = MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(take - 1, 0, MusicTime::kNumRateDivisions - 1));
      const double frames = beats * (60.0 / bpm) * sr;
      const int cap = mBuf != nullptr ? mBuf->capacity : kMaxFrames;
      return (int)std::clamp(frames, (double)kMinTakeFrames, (double)cap);
   }

   // One grid step, in beats: the take's own division, but never longer than a
   // bar (a 4-bar take still starts on a bar line).
   double GridBeats() const
   {
      const double bar = std::max(1e-6, Transport::Instance().BeatsPerBar());
      const int take = mTake.load(std::memory_order_relaxed);
      if (take <= 0)
         return bar;
      const double beats = MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(take - 1, 0, MusicTime::kNumRateDivisions - 1));
      return std::max(1e-6, std::min(beats, bar));
   }

   // Frame in this block where the next grid line falls, or -1. Beats() is the
   // block's end position (AdvanceAudioClock ran before this node).
   int ArmedStartOffset(int numFrames, double sr) const
   {
      if (!Transport::Instance().IsPlaying())
         return 0; // nothing to sync to: start now
      const double bpm = std::max(1.0f, Transport::Instance().Tempo());
      const double beatsPerFrame = bpm / 60.0 / sr;
      const double end = Transport::Instance().Beats();
      const double start = end - beatsPerFrame * (double)numFrames;
      const double grid = GridBeats();
      const double next = std::ceil(start / grid - 1e-6) * grid;
      if (next >= end)
         return -1;
      return std::clamp((int)((next - start) / beatsPerFrame), 0, std::max(0, numFrames - 1));
   }

   void BeginTake()
   {
      mState = LooperNode::kRecording;
      mLength = 0;
      mPos = 0;
      mTakeComp = mComp;
      mSkip = mComp;
   }

   // Closes the take being recorded and moves on to `next` (Playing or
   // Overdubbing). A take too short to be anything is thrown away.
   void FinishTake(int next)
   {
      if (mBuf == nullptr || mLength < kMinTakeFrames)
      {
         mState = LooperNode::kEmpty;
         mLength = 0;
         return;
      }
      // Short fades at both ends so the seam does not click.
      const int fade = std::min(kDeclickFrames, mLength / 4);
      for (int i = 0; i < fade; i++)
      {
         const float g = (float)i / (float)std::max(1, fade);
         for (int ch = 0; ch < 2; ch++)
         {
            mBuf->ch[ch][(size_t)i] *= g;
            mBuf->ch[ch][(size_t)(mLength - 1 - i)] *= g;
         }
      }
      mState = next;
      // The take ended `comp` frames after its musical end, so the loop is
      // already that far into its next pass.
      mPos = mTakeComp % mLength;
   }

   // Fills the loop with one second of a tone and starts playing it, so the
   // AUDIOPARAMSWEEPTEST rig has a loop to observe level and thru against.
   void SeedTone()
   {
      if (mBuf == nullptr || mBuf->capacity < kMinTakeFrames)
         return;
      const double sr = mSampleRate.load(std::memory_order_relaxed);
      mLength = (int)std::min<double>(mBuf->capacity, sr);
      for (int i = 0; i < mLength; i++)
      {
         const float v = 0.5f * (float)std::sin(6.283185307179586 * 220.0 * (double)i / sr);
         mBuf->ch[0][(size_t)i] = v;
         mBuf->ch[1][(size_t)i] = v;
      }
      mPos = 0;
      mTakeComp = 0;
      mState = LooperNode::kPlaying;
   }

   void ApplyButton(int button)
   {
      switch (button)
      {
      case LooperNode::kRec:
         if (mState == LooperNode::kArmed)
            mState = mLength > 0 ? LooperNode::kStopped : LooperNode::kEmpty;
         else if (mState == LooperNode::kRecording)
         {
            // Free take: the input still in flight (`comp` frames) belongs to
            // the take, so keep recording until it has landed. A fixed take
            // pressed early just ends where it is.
            if (mTarget == 0 && mTakeComp > 0)
               mTarget = std::max(1, mLength + mTakeComp - mSkip);
            else
               FinishTake(LooperNode::kPlaying);
         }
         else if (mBuf != nullptr)
         {
            mTarget = TargetFrames(mSampleRate.load(std::memory_order_relaxed));
            if (mSync.load(std::memory_order_relaxed) && Transport::Instance().IsPlaying())
               mState = LooperNode::kArmed;
            else
               BeginTake();
         }
         break;
      case LooperNode::kPlay:
         if (mState == LooperNode::kRecording)
            FinishTake(LooperNode::kPlaying);
         else if (mState == LooperNode::kArmed)
            mState = mLength > 0 ? LooperNode::kStopped : LooperNode::kEmpty;
         else if (mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing)
            mState = LooperNode::kStopped;
         else if (mState == LooperNode::kStopped && mLength > 0)
         {
            mState = LooperNode::kPlaying;
            mPos = 0;
         }
         break;
      case LooperNode::kDub:
         if (mState == LooperNode::kRecording)
            FinishTake(LooperNode::kOverdubbing);
         else if (mState == LooperNode::kPlaying)
            mState = LooperNode::kOverdubbing;
         else if (mState == LooperNode::kOverdubbing)
            mState = LooperNode::kPlaying;
         else if (mState == LooperNode::kStopped && mLength > 0)
         {
            mState = LooperNode::kOverdubbing;
            mPos = 0;
         }
         break;
      case LooperNode::kClear:
         mState = LooperNode::kEmpty;
         mLength = 0;
         mPos = 0;
         mTarget = 0;
         break;
      default:
         break;
      }
   }

   SampleSlotT<LoopBuf> mBufSlot;
   double mBufRate = 0.0; // main thread: the rate the last pushed buffer was sized for
   std::atomic<double> mSampleRate { 48000.0 };

   // Audio-thread state.
   LoopBuf* mBuf = nullptr;
   int mState = LooperNode::kEmpty;
   int mLength = 0;   // frames in the loop, or recorded so far
   int mTarget = 0;   // fixed-length take, 0 = free
   int mPos = 0;
   int mSkip = 0;     // input frames still to drop before the take's start
   int mComp = 0;     // current compensation, frames
   int mTakeComp = 0; // compensation the held loop was recorded with
   float mThruNow = 1.0f;
   float mLevelNow = 1.0f;

   // Main -> audio.
   int mCmds[kCmdCapacity] = {};
   std::atomic<uint32_t> mCmdHead { 0 };
   std::atomic<uint32_t> mCmdTail { 0 };
   std::atomic<int> mTake { 3 };
   std::atomic<bool> mSync { true };
   std::atomic<float> mThru { 1.0f };
   std::atomic<float> mLevel { 1.0f };
   std::atomic<int> mAutoLatencyFrames { 0 };
   std::atomic<float> mTrimMs { 0.0f };

   // Audio -> main.
   std::atomic<int> mPubState { LooperNode::kEmpty };
   std::atomic<float> mPubLoopSec { 0.0f };
   std::atomic<float> mPubRecSec { 0.0f };
   std::atomic<float> mPubPos { 0.0f };
   mutable std::atomic<float> mPubPeak { 0.0f };
   std::atomic<int> mPubComp { 0 };
};

// ---------------------------------------------------------------- main thread
LooperNode::LooperNode() = default;
LooperNode::~LooperNode() = default;

AudioNode* LooperNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioLooperNode>();
   return mAudioNode.get();
}

const char* LooperNode::StateName(int state)
{
   switch (state)
   {
   case kArmed: return "armed";
   case kRecording: return "recording";
   case kPlaying: return "playing";
   case kOverdubbing: return "overdubbing";
   case kStopped: return "stopped";
   default: return "empty";
   }
}

void LooperNode::SetButtonLevel(int button, bool level)
{
   if (button < 0 || button >= kNumButtons)
      return;
   const bool rising = level && !mButtonLevel[button];
   mButtonLevel[button] = level;
   if (rising)
   {
      GetAudioNode();
      mAudioNode->PushCommand(button);
   }
}

void LooperNode::SweepPrepare()
{
   GetAudioNode();
   mAudioNode->PushCommand(-1);
}

void LooperNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   GetAudioNode();

   // The driver's round-trip figure is a handful of property reads; twice a
   // second is plenty for something that only changes with the device.
   if (frameId - mLatencyPollFrame > 30 || frameId < mLatencyPollFrame)
   {
      mLatencyPollFrame = frameId;
      mLatencyFrames = autoLatency ? (int)Platform::AudioRoundTripLatencyFrames(0) : 0;
   }
   mAudioNode->PushParams(*this, mLatencyFrames);
   mAudioNode->DrainRetired();

   mState = mAudioNode->PublishedState();
   mLoopSec = mAudioNode->PublishedLoopSec();
   mRecordedSec = mAudioNode->PublishedRecordedSec();
   mPos01 = mAudioNode->PublishedPos01();
   mPeak = std::max(mPeak * 0.85f, mAudioNode->PublishedPeak());
   const double sr = std::max(1.0, mAudioNode->SampleRate());
   mCompMs = (float)((double)mAudioNode->CompensationFrames() * 1000.0 / sr);
}

void LooperNode::VisitParams(ParamVisitor& v)
{
   v.Int("take", take);
   v.Bool("syncStart", syncStart);
   v.Bool("autoLatency", autoLatency);
   v.Float("trimMs", trimMs);
   v.Float("thru", thru);
   v.Float("level", level);
}
