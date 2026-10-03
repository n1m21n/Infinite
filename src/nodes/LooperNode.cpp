#include "LooperNode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "audio/AudioEngine.h"
#include "audio/AudioFileWriter.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/PassFade.h"
#include "core/AudioTopologyRequest.h"
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
      kCmdClear,
      kCmdSwap, // adopt the inactive bank (undo / redo / load), see RequestSwap
      // Turbo 0.48: presses resolved against the audio thread's own state.
      kCmdRecPress,
      kCmdPlayPress,
      kCmdDubPress
   };

   // Two banks of 120 s stereo at 48 kHz (~46 MB each): the audio thread
   // plays/records the active one while the main thread fills the other for
   // an undo, redo or file load, then a kCmdSwap flips them at a block
   // boundary. At 96 kHz the same banks hold 60 s.
   constexpr int kCapacityFrames = 48000 * 120;
   constexpr int kPreRollFrames = 48000; // input history for late REC presses
   constexpr int kDeclickFrames = 256;
   constexpr int kTouchStride = 1024;    // floats per 4 KB page
   constexpr int kTouchPagesPerBlock = 32;
   constexpr size_t kMaxHistory = 32;
   constexpr size_t kMaxHistoryBytes = (size_t)512 << 20;
   constexpr double kOrphanSeconds = 180.0;
}

// One stable state of the loop: interleaved stereo.
struct LoopSnapshot
{
   std::vector<float> interleaved;
   int frames = 0;
   double sampleRate = 48000.0;
};

class AudioLooperNode : public AudioNode
{
public:
   // ---- main thread -----------------------------------------------------

   // Allocates every buffer the looper will ever use. std::vector's value
   // initialisation writes each element, so every page is committed here,
   // on the main thread, not on the first take. Runs once, on the first
   // frame after the node is inserted (or respawned with a fresh identity).
   void Allocate()
   {
      if (mReady.load(std::memory_order_acquire))
         return;
      for (int b = 0; b < 2; b++)
         for (int ch = 0; ch < 2; ch++)
            mBuf[b][ch].assign((size_t)kCapacityFrames, 0.0f);
      for (int ch = 0; ch < 2; ch++)
         mPre[ch].assign((size_t)kPreRollFrames, 0.0f);
      mReady.store(true, std::memory_order_release);
   }
   bool IsReady() const { return mReady.load(std::memory_order_acquire); }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
   }

   bool PushCommand(int cmd)
   {
      const uint32_t tail = mCmdTail.load(std::memory_order_relaxed);
      const uint32_t next = (tail + 1) % kCmdCapacity;
      if (next == mCmdHead.load(std::memory_order_acquire))
         return false; // full: a burst of 64 unread presses, drop the newest
      mCmds[tail] = cmd;
      mCmdTail.store(next, std::memory_order_release);
      mPushedMain++;
      return true;
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
      mTakeDivision.store(std::clamp(n.takeDivision, 0, (int)MusicTime::kNumRateDivisions - 1),
                          std::memory_order_relaxed);
      mRate.store(LooperNode::RateFor(std::clamp(n.pitch, -24.0f, 24.0f), std::clamp(n.finetune, -50.0f, 50.0f),
                                      std::clamp(n.speed, -2.0f, 2.0f)),
                  std::memory_order_relaxed);
      mFadeInMs.store(std::clamp(n.fadeIn, 0.0f, 250.0f), std::memory_order_relaxed);
      mFadeOutMs.store(std::clamp(n.fadeOut, 0.0f, 250.0f), std::memory_order_relaxed);
      mVolume.store(std::clamp(n.volume, 0.0f, 2.0f), std::memory_order_relaxed);
      const double autoFrames = n.autoLatency ? (double)Platform::AudioRoundTripLatencyFrames() : 0.0;
      const double frames = autoFrames + (double)std::clamp(n.latencyOffsetMs, -100.0f, 300.0f) * 0.001 * mSampleRate;
      mLatency.store((int)std::clamp(frames, 0.0, mSampleRate), std::memory_order_relaxed);
   }

   // True once the audio thread has applied (and published the result of)
   // every command pushed so far, and no bank swap is in flight: the
   // published state then describes the buffers exactly, and nothing will
   // change them until the main thread pushes another command.
   bool Settled() const
   {
      return mPubApplied.load(std::memory_order_acquire) == mPushedMain &&
             !mSwapPending.load(std::memory_order_acquire);
   }

   // Bank handoff. The main thread only writes the bank the audio thread is
   // not using, and only while no swap is pending.
   int ActiveBank() const { return mPubBank.load(std::memory_order_acquire); }
   float* BankData(int bank, int ch) { return mBuf[bank & 1][ch & 1].data(); }
   void RequestSwap(int frames)
   {
      mSwapFrames.store(std::clamp(frames, 0, kCapacityFrames), std::memory_order_relaxed);
      mSwapPending.store(true, std::memory_order_release);
      if (!PushCommand(kCmdSwap))
         mSwapPending.store(false, std::memory_order_release);
   }

   int LatencyFrames() const { return mLatency.load(std::memory_order_relaxed); }
   int PublishedState() const { return mPubState.load(std::memory_order_acquire); }
   int PublishedLengthFrames() const { return mPubLengthFrames.load(std::memory_order_acquire); }
   uint32_t PublishedGen() const { return mPubGen.load(std::memory_order_acquire); }
   uint32_t PublishedSwapAck() const { return mPubSwapAck.load(std::memory_order_acquire); }
   float PublishedLengthSec() const { return mPubLength.load(std::memory_order_relaxed); }
   float PublishedRecordedSec() const { return mPubRecorded.load(std::memory_order_relaxed); }
   float PublishedPos01() const { return mPubPos.load(std::memory_order_relaxed); }
   float PublishedPeak() const { return mPubPeak.load(std::memory_order_relaxed); }
   float PublishedArmedSec() const { return mPubArmedSec.load(std::memory_order_relaxed); }
   float PublishedPlayWait() const { return mPubPlayWait.load(std::memory_order_relaxed); }
   double SampleRate() const { return mSampleRate; }

   // ---- audio thread ----------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      const int numFrames = output.numFrames;
      const float thru = mThru.load(std::memory_order_relaxed);
      // Turbo 0.48: output volume and playback rate, ramped across the block.
      const float volTarget = mVolume.load(std::memory_order_relaxed);
      const float rateTarget = mRate.load(std::memory_order_relaxed);
      const float fadeInMs = mFadeInMs.load(std::memory_order_relaxed);
      const float fadeOutMs = mFadeOutMs.load(std::memory_order_relaxed);
      const float invN = 1.0f / (float)std::max(1, numFrames);

      if (!mReady.load(std::memory_order_acquire))
      {
         // Not allocated yet (the very first frames after insertion): pass
         // the input through and leave the commands queued.
         for (int i = 0; i < numFrames; i++)
         {
            float inL = 0.0f, inR = 0.0f;
            if (in != nullptr && in->numChannels > 0)
            {
               inL = in->channels[0][i];
               inR = in->numChannels > 1 ? in->channels[1][i] : inL;
            }
            const float vol = mVolNow + (volTarget - mVolNow) * (float)(i + 1) * invN;
            if (output.numChannels > 0)
               output.channels[0][i] = inL * thru * vol;
            if (output.numChannels > 1)
               output.channels[1][i] = inR * thru * vol;
            for (int ch = 2; ch < output.numChannels; ch++)
               output.channels[ch][i] = inL * thru * vol;
         }
         mVolNow = volTarget;
         mRateNow = rateTarget;
         return;
      }

      // A negative speed walks the loop the other way: Forward and Reverse
      // swap (Ping-pong already goes both ways). At speed >= 0 this is the
      // direction param itself.
      const int dirParam = mDirection.load(std::memory_order_relaxed);
      const int direction = rateTarget >= 0.0f || dirParam == LooperNode::kPingPong
                               ? dirParam
                               : (dirParam == LooperNode::kReverse ? LooperNode::kForward : LooperNode::kReverse);
      // Layers are only written at the recorded speed (as upstream): at any
      // other rate they would land smeared or doubled.
      const bool unity = std::fabs(rateTarget - 1.0f) < 1e-4f && std::fabs(mRateNow - 1.0f) < 1e-4f;
      const double sr = mSampleRate;
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const float level = mLevelParam.load(std::memory_order_relaxed);

      mBlockFrames = numFrames;
      // Commands, in order.
      for (;;)
      {
         const uint32_t head = mCmdHead.load(std::memory_order_relaxed);
         if (head == mCmdTail.load(std::memory_order_acquire))
            break;
         const int cmd = mCmds[head];
         mCmdHead.store((head + 1) % kCmdCapacity, std::memory_order_release);
         ApplyCommand(cmd, direction);
         mApplied++;
      }

      // An armed take starts on the next grid line inside this block, if any.
      int armedOffset = -1;
      float armedSec = 0.0f;
      if (mState == LooperNode::kArmed)
         armedOffset = ArmedStartOffset(numFrames, armedSec);
      // Turbo 0.48: a quantized PLAY (or DUB from stopped) waits the same way.
      int playOffset = -1;
      float playSec = 0.0f;
      if (mPlayPending != 0)
      {
         if (mLength <= 0 || (mState != LooperNode::kIdle && mState != LooperNode::kStopped))
            mPlayPending = 0;
         else
            playOffset = ArmedStartOffset(numFrames, playSec);
      }

      float* bufL = mBuf[mBank][0].data();
      float* bufR = mBuf[mBank][1].data();
      float* preL = mPre[0].data();
      float* preR = mPre[1].data();

      float peak = 0.0f;
      for (int i = 0; i < numFrames; i++)
      {
         float inL = 0.0f, inR = 0.0f;
         if (in != nullptr && in->numChannels > 0)
         {
            inL = in->channels[0][i];
            inR = in->numChannels > 1 ? in->channels[1][i] : inL;
         }
         const float tRamp = (float)(i + 1) * invN;
         const float vol = mVolNow + (volTarget - mVolNow) * tRamp;
         const float rate = mRateNow + (rateTarget - mRateNow) * tRamp;
         float outL = inL * thru;
         float outR = inR * thru;

         if (mState == LooperNode::kArmed && armedOffset >= 0 && i >= armedOffset)
            BeginTake();
         if (mPlayPending != 0 && playOffset >= 0 && i >= playOffset)
         {
            mState = mPlayPending == 2 ? LooperNode::kOverdubbing : LooperNode::kPlaying;
            StartPosition(direction);
            mPlayPending = 0;
         }

         if (mState == LooperNode::kRecording)
         {
            // Latency compensation: the first mSkip input frames belong to
            // before the take (they left the performer before the grid line).
            if (mSkip > 0)
               mSkip--;
            else if (mLength < kCapacityFrames)
            {
               bufL[mLength] = inL;
               bufR[mLength] = inR;
               mLength++;
            }
            if ((mTarget > 0 && mLength >= mTarget) || mLength >= kCapacityFrames)
               FinishTake(direction);
         }
         else if ((mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing) && mLength > 0 &&
                  std::fabs(rateTarget) < 1e-6f)
         {
            // Turbo 0.48: speed 0. The playhead would hold one frame forever (a
            // stuck DC level): a one-pass playback (loop off) stops, a loop
            // stays on but is silent until the speed moves again.
            if (!loop)
               Stop(direction);
         }
         else if ((mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing) && mLength > 0)
         {
            const int idx = std::clamp(mPos, 0, mLength - 1);
            // Turbo 0.48: fractional playhead (mPhase, 0..1 toward the next
            // frame in the direction of travel), linear interpolation and the
            // per-pass fade. At rate 1 mPhase stays 0 and the fade is exactly
            // 1, so this reads bufL[idx] * level as before.
            const int step = (direction == LooperNode::kReverse) ? -1 : (direction == LooperNode::kPingPong ? mPing : 1);
            int nxt = idx + step;
            if (nxt >= mLength)
               nxt = (direction == LooperNode::kPingPong || !loop) ? idx : 0;
            else if (nxt < 0)
               nxt = (direction == LooperNode::kPingPong || !loop) ? idx : mLength - 1;
            const float fr = (float)mPhase;
            const float g = level * PassFade::Gain((double)idx + (double)fr * (double)step, 0.0, (double)mLength,
                                                   (float)step, fadeInMs, fadeOutMs, std::fabs(rate), sr);
            outL += (bufL[idx] + (bufL[nxt] - bufL[idx]) * fr) * g;
            outR += (bufR[idx] + (bufR[nxt] - bufR[idx]) * fr) * g;
            if (mState == LooperNode::kOverdubbing && unity)
            {
               // Same compensation: this input was played against the loop
               // position `latency` frames ago.
               int w = idx;
               if (direction == LooperNode::kForward)
                  w = (idx - mTakeLatency % mLength + mLength) % mLength;
               bufL[w] += inL;
               bufR[w] += inR;
            }
            mPhase += (double)std::fabs(rate);
            for (int guard = 0; mPhase >= 1.0 && guard < 16; guard++)
            {
               mPhase -= 1.0;
               Advance(direction, loop);
               if (mState != LooperNode::kPlaying && mState != LooperNode::kOverdubbing)
               {
                  mPhase = 0.0;
                  break;
               }
            }
            if (mPhase >= 1.0)
               mPhase = 0.0;
         }

         preL[mPreWrite] = inL;
         preR[mPreWrite] = inR;
         if (++mPreWrite >= kPreRollFrames)
            mPreWrite = 0;

         outL *= vol;
         outR *= vol;
         if (output.numChannels > 0)
            output.channels[0][i] = outL;
         if (output.numChannels > 1)
            output.channels[1][i] = outR;
         for (int ch = 2; ch < output.numChannels; ch++)
            output.channels[ch][i] = outL;
         peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
      }
      mVolNow = volTarget;
      mRateNow = rateTarget;

      // Keep the active bank resident: Windows trims the working set of
      // pages nobody touched for a while, and a trimmed page faults (maybe
      // from the page file) on the first write of the next take. One read
      // per page, a few dozen pages per block, sweeps the bank in seconds.
      float sink = 0.0f;
      for (int k = 0; k < kTouchPagesPerBlock; k++)
      {
         sink += bufL[mTouch] + bufR[mTouch];
         mTouch += kTouchStride;
         if (mTouch >= kCapacityFrames)
            mTouch = 0;
      }
      mTouchSink = sink;

      mPubState.store(mState, std::memory_order_relaxed);
      const bool hasLoop = mState != LooperNode::kRecording && mState != LooperNode::kArmed && mLength > 0;
      mPubLengthFrames.store(hasLoop ? mLength : 0, std::memory_order_relaxed);
      mPubLength.store(hasLoop ? (float)(mLength / mSampleRate) : 0.0f, std::memory_order_relaxed);
      mPubRecorded.store(mState == LooperNode::kRecording ? (float)(mLength / mSampleRate) : 0.0f,
                         std::memory_order_relaxed);
      mPubPos.store(hasLoop ? (float)mPos / (float)std::max(1, mLength) : 0.0f, std::memory_order_relaxed);
      mPubPeak.store(peak, std::memory_order_relaxed);
      mPubArmedSec.store(mState == LooperNode::kArmed ? armedSec : 0.0f, std::memory_order_relaxed);
      mPubPlayWait.store(mPlayPending != 0 ? std::max(1e-3f, playSec) : 0.0f, std::memory_order_relaxed);
      mPubGen.store(mGen, std::memory_order_relaxed);
      // Last: Settled() reads this with acquire, so everything above is
      // visible once the applied count matches.
      mPubApplied.store(mApplied, std::memory_order_release);
   }

private:
   static constexpr int kCmdCapacity = 64;

   // Turbo 0.48: the take length of kLengthDivision, in beats.
   double DivisionBeats() const
   {
      const int d = std::clamp(mTakeDivision.load(std::memory_order_relaxed), 0, (int)MusicTime::kNumRateDivisions - 1);
      return std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)d));
   }

   double GridBeats() const
   {
      const double beatsPerBar = Transport::Instance().BeatsPerBar();
      // A division take waits for its own division, never longer than a bar
      // (a 4-bar take still starts on a bar line), as upstream.
      if (mLengthMode.load(std::memory_order_relaxed) == LooperNode::kLengthDivision)
         return std::min(DivisionBeats(), std::max(1e-6, beatsPerBar));
      if (mLengthMode.load(std::memory_order_relaxed) == LooperNode::kLengthSubBar)
      {
         static const double kFraction[4] = { 0.5, 0.25, 0.125, 0.0625 };
         return beatsPerBar * kFraction[std::clamp(mSubDivision.load(std::memory_order_relaxed), 0, 3)];
      }
      return beatsPerBar;
   }

   int TargetFrames() const
   {
      if (mLengthMode.load(std::memory_order_relaxed) == LooperNode::kLengthFree)
         return 0;
      const double bpm = std::max(1.0f, Transport::Instance().Tempo());
      double beats = GridBeats();
      if (mLengthMode.load(std::memory_order_relaxed) == LooperNode::kLengthBars)
         beats = Transport::Instance().BeatsPerBar() * (double)mBars.load(std::memory_order_relaxed);
      else if (mLengthMode.load(std::memory_order_relaxed) == LooperNode::kLengthDivision)
         beats = DivisionBeats();
      const double frames = beats * (60.0 / bpm) * mSampleRate;
      return (int)std::clamp(frames, 1.0, (double)kCapacityFrames);
   }

   double BeatsPerSample() const
   {
      return std::max(1.0f, Transport::Instance().Tempo()) / 60.0 / mSampleRate;
   }

   // Sample offset in this block where the next grid line falls, or -1.
   int ArmedStartOffset(int numFrames, float& secondsToGo) const
   {
      secondsToGo = 0.0f;
      if (!Transport::Instance().IsPlaying())
         return 0; // nothing to sync to: start now
      const double beatsPerSample = BeatsPerSample();
      const double grid = std::max(1e-6, GridBeats());
      // Turbo 0.48: Beats() is already the END of this block (the engine
      // advances the clock before the graph runs), so step back one block.
      const double end = Transport::Instance().Beats();
      const double now = end - beatsPerSample * (double)numFrames;
      const double next = std::ceil(now / grid - 1e-9) * grid;
      secondsToGo = (float)((next - now) / beatsPerSample / mSampleRate);
      if (next >= end)
         return -1;
      return std::clamp((int)((next - now) / beatsPerSample), 0, std::max(0, numFrames - 1));
   }

   // Frames since the last grid line (block start), or -1 without a
   // running transport.
   int FramesSinceGrid() const
   {
      if (!Transport::Instance().IsPlaying())
         return -1;
      const double grid = std::max(1e-6, GridBeats());
      // Block start (Beats() is the block end, see ArmedStartOffset).
      const double now = Transport::Instance().Beats() - BeatsPerSample() * (double)mBlockFrames;
      const double prev = std::floor(now / grid + 1e-9) * grid;
      return (int)std::max(0.0, (now - prev) / BeatsPerSample());
   }

   void StartPosition(int direction)
   {
      mPing = 1;
      mPhase = 0.0;
      mPos = (direction == LooperNode::kReverse) ? std::max(0, mLength - 1) : 0;
   }

   void FinishTake(int direction)
   {
      mGen++;
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
            mBuf[mBank][ch][(size_t)i] *= g;
            mBuf[mBank][ch][(size_t)(mLength - 1 - i)] *= g;
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

   // REC came `late` frames after the grid line: start the take on that
   // line anyway. The input that belongs to it (from line + latency on) is
   // already in the pre-roll; copy it in and carry on recording.
   void BackFill(int late)
   {
      if (late <= mTakeLatency)
      {
         mSkip = mTakeLatency - late;
         return;
      }
      int n = std::min(late - mTakeLatency, kPreRollFrames - 1);
      if (mTarget > 0)
         n = std::min(n, mTarget);
      mSkip = 0;
      for (int i = 0; i < n; i++)
      {
         const int src = (mPreWrite - n + i + kPreRollFrames) % kPreRollFrames;
         mBuf[mBank][0][(size_t)i] = mPre[0][(size_t)src];
         mBuf[mBank][1][(size_t)i] = mPre[1][(size_t)src];
      }
      mLength = n;
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

   // Turbo 0.48: PLAY (and DUB from stopped) with "sync start" on and the
   // transport running starts on the next grid line, like REC: the loop
   // lands in time. Pressed just after a line (human timing) it starts at
   // once, already that far into the loop, so it is still in phase.
   void StartQuantized(int playState, int direction)
   {
      if (!mSyncStart.load(std::memory_order_relaxed) || !Transport::Instance().IsPlaying())
      {
         mState = playState;
         StartPosition(direction);
         return;
      }
      const int late = FramesSinceGrid();
      const double gridFrames = GridBeats() / BeatsPerSample();
      const int grace = (int)std::min(0.2 * mSampleRate, 0.25 * gridFrames);
      if (late >= 0 && late <= grace)
      {
         mState = playState;
         StartPosition(direction);
         // Catch up the frames since the line (plain frames: rate 1 is the
         // in-time case; at other speeds the loop drifts anyway).
         for (int k = 0; k < late && mLength > 0; k++)
            Advance(direction, true);
      }
      else
         mPlayPending = playState == LooperNode::kOverdubbing ? 2 : 1;
   }

   void Stop(int direction)
   {
      if (mState == LooperNode::kOverdubbing)
         mGen++; // the layer is done
      mState = LooperNode::kStopped;
      StartPosition(direction);
   }

   void ApplyCommand(int cmd, int direction)
   {
      switch (cmd)
      {
         case kCmdRecOn:
         {
            if (mState == LooperNode::kRecording || mState == LooperNode::kArmed)
               break;
            mPlayPending = 0;
            mLength = 0;
            mPos = 0;
            mGen++;
            mTarget = TargetFrames();
            if (mTarget > 0 && mSyncStart.load(std::memory_order_relaxed))
            {
               // Pressed just after a line (human timing): start on it.
               const int late = FramesSinceGrid();
               const double gridFrames = GridBeats() / BeatsPerSample();
               const int grace = (int)std::min(0.2 * mSampleRate, 0.25 * gridFrames);
               if (late >= 0 && late <= grace)
               {
                  BeginTake();
                  BackFill(late);
               }
               else
                  mState = LooperNode::kArmed;
            }
            else
               BeginTake();
            break;
         }
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
               StartQuantized(LooperNode::kPlaying, direction);
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
            mPlayPending = 0; // a second PLAY cancels a start still waiting
            break;
         case kCmdDubOn:
            if (mState == LooperNode::kPlaying)
               mState = LooperNode::kOverdubbing;
            else if (mLength > 0 && (mState == LooperNode::kIdle || mState == LooperNode::kStopped))
               StartQuantized(LooperNode::kOverdubbing, direction);
            break;
         case kCmdDubOff:
            if (mState == LooperNode::kOverdubbing)
            {
               mState = LooperNode::kPlaying;
               mGen++;
            }
            break;
         case kCmdClear:
            mPlayPending = 0;
            mState = LooperNode::kIdle;
            mLength = 0;
            mPos = 0;
            mTarget = 0;
            mGen++;
            break;
         case kCmdRecPress:
            ApplyCommand(mState == LooperNode::kRecording || mState == LooperNode::kArmed ? kCmdRecOff : kCmdRecOn,
                         direction);
            break;
         case kCmdPlayPress:
            ApplyCommand(mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing || mPlayPending != 0
                            ? kCmdPlayOff
                            : kCmdPlayOn,
                         direction);
            break;
         case kCmdDubPress:
            ApplyCommand(mState == LooperNode::kOverdubbing ? kCmdDubOff : kCmdDubOn, direction);
            break;
         case kCmdSwap:
         {
            mBank ^= 1;
            mLength = mSwapFrames.load(std::memory_order_relaxed);
            if (mState == LooperNode::kRecording || mState == LooperNode::kArmed)
               mState = LooperNode::kStopped;
            else if (mState == LooperNode::kOverdubbing)
               mState = LooperNode::kPlaying;
            if (mLength <= 0)
            {
               mLength = 0;
               mState = LooperNode::kIdle;
               mPos = 0;
            }
            else if (mPos < 0 || mPos >= mLength)
               StartPosition(direction);
            mGen++;
            mPubSwapAck.store(mGen, std::memory_order_relaxed);
            mPubBank.store(mBank, std::memory_order_release);
            mSwapPending.store(false, std::memory_order_release);
            break;
         }
         default:
            break;
      }
   }

   std::vector<float> mBuf[2][2]; // [bank][channel]
   std::vector<float> mPre[2];    // pre-roll ring, per channel
   std::atomic<bool> mReady { false };
   double mSampleRate = 48000.0;

   // Audio-thread state.
   int mState = LooperNode::kIdle;
   int mBank = 0;
   int mLength = 0; // frames in the loop (or recorded so far)
   int mTarget = 0; // fixed-length take, 0 = free
   int mPos = 0;
   int mPing = 1;
   int mPlayPending = 0; // Turbo 0.48: quantized start waiting, 1 = play, 2 = overdub
   int mBlockFrames = 0;
   double mPhase = 0.0;  // Turbo 0.48: fraction toward the next frame (rate != 1)
   float mRateNow = 1.0f;
   float mVolNow = 1.0f;
   int mSkip = 0;        // input frames still to drop before the take starts
   int mTakeLatency = 0; // compensation used by the current loop
   int mPreWrite = 0;
   int mTouch = 0;
   volatile float mTouchSink = 0.0f;
   uint32_t mGen = 0;     // bumped by every content change
   uint32_t mApplied = 0; // commands applied

   // Main -> audio.
   int mCmds[kCmdCapacity] = {};
   std::atomic<uint32_t> mCmdHead { 0 };
   std::atomic<uint32_t> mCmdTail { 0 };
   uint32_t mPushedMain = 0; // main thread only
   std::atomic<int> mLengthMode { LooperNode::kLengthBars };
   std::atomic<int> mBars { 1 };
   std::atomic<int> mSubDivision { 1 };
   std::atomic<bool> mSyncStart { true };
   std::atomic<bool> mLoop { true };
   std::atomic<int> mDirection { LooperNode::kForward };
   std::atomic<float> mThru { 1.0f };
   std::atomic<float> mLevelParam { 1.0f };
   std::atomic<int> mTakeDivision { 2 };
   std::atomic<float> mRate { 1.0f };
   std::atomic<float> mFadeInMs { 0.0f };
   std::atomic<float> mFadeOutMs { 0.0f };
   std::atomic<float> mVolume { 1.0f };
   std::atomic<int> mLatency { 0 };
   std::atomic<int> mSwapFrames { 0 };
   std::atomic<bool> mSwapPending { false };

   // Audio -> main.
   std::atomic<int> mPubState { LooperNode::kIdle };
   std::atomic<int> mPubLengthFrames { 0 };
   std::atomic<int> mPubBank { 0 };
   std::atomic<uint32_t> mPubGen { 0 };
   std::atomic<uint32_t> mPubSwapAck { 0 };
   std::atomic<uint32_t> mPubApplied { 0 };
   std::atomic<float> mPubLength { 0.0f };
   std::atomic<float> mPubRecorded { 0.0f };
   std::atomic<float> mPubPos { 0.0f };
   std::atomic<float> mPubPeak { 0.0f };
   std::atomic<float> mPubArmedSec { 0.0f };
   std::atomic<float> mPubPlayWait { 0.0f }; // seconds until a quantized PLAY starts, 0 = none
};

// ---------------------------------------------------------------------------
// Main-thread side: identity, layer history, WAV sidecar.
// ---------------------------------------------------------------------------

struct LooperCore
{
   std::string id;
   std::unique_ptr<AudioLooperNode> audio = std::make_unique<AudioLooperNode>();
   std::vector<std::shared_ptr<const LoopSnapshot>> history; // stable states
   int cursor = -1;
   uint32_t seenGen = 0;
   int pendingStep = 0; // -1 undo, +1 redo, applied once settled
   std::shared_ptr<const LoopSnapshot> pendingLoad;
   std::string filePath; // current WAV sidecar ("" = nothing recorded)
   bool writeQueued = false;
   std::shared_ptr<const LoopSnapshot> queuedWrite;
   std::shared_ptr<std::atomic<bool>> writeBusy = std::make_shared<std::atomic<bool>>(false);
   double orphanSince = -1.0;
};

namespace
{
   bool gRestoringPatch = false;

   // Leaked on purpose: the audio thread may still hold a core's audio
   // node while static destructors run at exit.
   std::map<std::string, std::shared_ptr<LooperCore>>& Registry()
   {
      static auto* sRegistry = new std::map<std::string, std::shared_ptr<LooperCore>>();
      return *sRegistry;
   }

   double NowSeconds()
   {
      using namespace std::chrono;
      return duration<double>(steady_clock::now().time_since_epoch()).count();
   }

   std::string NewLoopId()
   {
      static std::mt19937_64 sRng(((uint64_t)std::random_device {}() << 32) ^
                                  (uint64_t)std::chrono::system_clock::now().time_since_epoch().count());
      char id[24];
      for (;;)
      {
         snprintf(id, sizeof(id), "%016llx", (unsigned long long)sRng());
         if (Registry().count(id) == 0)
            return id;
      }
   }

   // A core nobody owns any more (node deleted, or replaced by an undo
   // respawn that adopted it) is kept a few minutes so undoing the delete
   // brings the loop back, then freed. By then the audio topology was
   // rebuilt long ago, so the audio thread no longer references it.
   void PruneRegistry()
   {
      const double now = NowSeconds();
      auto& reg = Registry();
      for (auto it = reg.begin(); it != reg.end();)
      {
         LooperCore& c = *it->second;
         if (it->second.use_count() > 1)
         {
            c.orphanSince = -1.0;
            ++it;
            continue;
         }
         if (c.orphanSince < 0.0)
            c.orphanSince = now;
         if (now - c.orphanSince > kOrphanSeconds && !c.writeBusy->load())
            it = reg.erase(it);
         else
            ++it;
      }
   }

   void PutU16(std::string& s, uint16_t v) { s.append((const char*)&v, 2); }
   void PutU32(std::string& s, uint32_t v) { s.append((const char*)&v, 4); }

   // 32-bit float WAV (format 3): overdubs can sum past 0 dBFS, so the
   // sidecar must not clip or quantise what the looper holds.
   bool WriteFloatWav(const std::string& path, const LoopSnapshot& snap, std::string& error)
   {
      const uint32_t channels = 2;
      const uint32_t frames = (uint32_t)std::max(0, snap.frames);
      const uint32_t dataBytes = frames * channels * 4;
      const uint32_t rate = (uint32_t)std::lround(snap.sampleRate > 0.0 ? snap.sampleRate : 48000.0);
      std::string h;
      h.append("RIFF", 4);
      PutU32(h, 4 + (8 + 18) + (8 + 4) + (8 + dataBytes));
      h.append("WAVEfmt ", 8);
      PutU32(h, 18);
      PutU16(h, 3); // WAVE_FORMAT_IEEE_FLOAT
      PutU16(h, (uint16_t)channels);
      PutU32(h, rate);
      PutU32(h, rate * channels * 4);
      PutU16(h, (uint16_t)(channels * 4));
      PutU16(h, 32);
      PutU16(h, 0);
      h.append("fact", 4);
      PutU32(h, 4);
      PutU32(h, frames);
      h.append("data", 4);
      PutU32(h, dataBytes);

      std::error_code ec;
      const std::filesystem::path target = std::filesystem::u8path(path);
      std::filesystem::create_directories(target.parent_path(), ec);
      std::filesystem::path temp = target;
      temp += ".tmp";
      {
         std::ofstream f(temp, std::ios::binary | std::ios::trunc);
         if (!f)
         {
            error = "cannot write " + path;
            return false;
         }
         f.write(h.data(), (std::streamsize)h.size());
         if (frames > 0)
            f.write((const char*)snap.interleaved.data(), (std::streamsize)dataBytes);
         if (!f)
         {
            error = "write failed: " + path;
            return false;
         }
      }
      ec.clear();
      std::filesystem::rename(temp, target, ec);
      if (ec)
      {
         ec.clear();
         std::filesystem::remove(target, ec);
         ec.clear();
         std::filesystem::rename(temp, target, ec);
      }
      if (ec)
      {
         error = ec.message();
         return false;
      }
      return true;
   }

   std::string SidecarPath(const LooperCore& c)
   {
      return (std::filesystem::u8path(AudioRecordings::GetRecordingsDirectory()) / ("looper_" + c.id + ".wav")).u8string();
   }

   void StartWrite(LooperCore& c, const std::shared_ptr<const LoopSnapshot>& snap)
   {
      if (!snap || snap->frames <= 0)
      {
         std::error_code ec;
         std::filesystem::remove(std::filesystem::u8path(SidecarPath(c)), ec);
         c.filePath.clear();
         return;
      }
      c.filePath = SidecarPath(c);
      c.writeBusy->store(true);
      std::thread([snap, path = c.filePath, busy = c.writeBusy]() {
         std::string error;
         WriteFloatWav(path, *snap, error);
         busy->store(false);
      }).detach();
   }

   // Writes are serialised per core; a change during a write is queued and
   // only its latest state is written.
   void ScheduleWrite(LooperCore& c, const std::shared_ptr<const LoopSnapshot>& snap)
   {
      if (c.writeBusy->load())
      {
         c.writeQueued = true;
         c.queuedWrite = snap;
         if (!snap || snap->frames <= 0)
            c.filePath.clear();
         else
            c.filePath = SidecarPath(c);
         return;
      }
      c.writeQueued = false;
      c.queuedWrite.reset();
      StartWrite(c, snap);
   }

   std::shared_ptr<const LoopSnapshot> Capture(AudioLooperNode& a)
   {
      auto snap = std::make_shared<LoopSnapshot>();
      const int frames = std::clamp(a.PublishedLengthFrames(), 0, kCapacityFrames);
      const int bank = a.ActiveBank();
      const float* l = a.BankData(bank, 0);
      const float* r = a.BankData(bank, 1);
      snap->frames = frames;
      snap->sampleRate = a.SampleRate();
      snap->interleaved.resize((size_t)frames * 2);
      for (int i = 0; i < frames; i++)
      {
         snap->interleaved[(size_t)i * 2] = l[i];
         snap->interleaved[(size_t)i * 2 + 1] = r[i];
      }
      return snap;
   }

   // Fills the inactive bank (resampled to the engine rate if needed) and
   // asks the audio thread to swap to it.
   void LoadIntoAudio(AudioLooperNode& a, const LoopSnapshot& snap)
   {
      const int bank = a.ActiveBank() ^ 1;
      float* l = a.BankData(bank, 0);
      float* r = a.BankData(bank, 1);
      const double rate = a.SampleRate();
      int frames = snap.frames;
      if (snap.frames > 0 && snap.sampleRate > 0.0 && std::fabs(snap.sampleRate - rate) > 0.5)
      {
         const double step = snap.sampleRate / rate;
         frames = std::min(kCapacityFrames, (int)((double)snap.frames / step));
         for (int i = 0; i < frames; i++)
         {
            const double x = (double)i * step;
            const int i0 = std::min((int)x, snap.frames - 1);
            const int i1 = std::min(i0 + 1, snap.frames - 1);
            const float t = (float)(x - (double)i0);
            l[i] = snap.interleaved[(size_t)i0 * 2] * (1.0f - t) + snap.interleaved[(size_t)i1 * 2] * t;
            r[i] = snap.interleaved[(size_t)i0 * 2 + 1] * (1.0f - t) + snap.interleaved[(size_t)i1 * 2 + 1] * t;
         }
      }
      else
      {
         frames = std::min(frames, kCapacityFrames);
         for (int i = 0; i < frames; i++)
         {
            l[i] = snap.interleaved[(size_t)i * 2];
            r[i] = snap.interleaved[(size_t)i * 2 + 1];
         }
      }
      a.RequestSwap(frames);
   }

   size_t HistoryBytes(const LooperCore& c)
   {
      size_t bytes = 0;
      for (const auto& s : c.history)
         bytes += s ? s->interleaved.size() * sizeof(float) : 0;
      return bytes;
   }

   void PushHistory(LooperCore& c, const std::shared_ptr<const LoopSnapshot>& snap)
   {
      if (c.cursor + 1 < (int)c.history.size())
         c.history.resize((size_t)(c.cursor + 1)); // a new layer drops the redo branch
      c.history.push_back(snap);
      c.cursor = (int)c.history.size() - 1;
      while (c.history.size() > 2 && (c.history.size() > kMaxHistory || HistoryBytes(c) > kMaxHistoryBytes))
      {
         c.history.erase(c.history.begin());
         c.cursor--;
      }
   }

   std::shared_ptr<const LoopSnapshot> DecodeLoopFile(const std::string& path, std::string& error)
   {
      Platform::SampleBuffer buf;
      if (!Platform::DecodeAudioFileToBuffer(path, buf, error) || buf.numFrames <= 0 || buf.channels <= 0)
      {
         if (error.empty())
            error = "could not read the loop file";
         return nullptr;
      }
      auto snap = std::make_shared<LoopSnapshot>();
      const int frames = std::min(buf.numFrames, kCapacityFrames);
      snap->frames = frames;
      snap->sampleRate = buf.sampleRate > 0.0 ? buf.sampleRate : 48000.0;
      snap->interleaved.resize((size_t)frames * 2);
      const float* c0 = buf.channelData.data();
      const float* c1 = buf.channels > 1 ? c0 + buf.numFrames : c0;
      for (int i = 0; i < frames; i++)
      {
         snap->interleaved[(size_t)i * 2] = c0[i];
         snap->interleaved[(size_t)i * 2 + 1] = c1[i];
      }
      return snap;
   }

   // Once per frame per core: allocate, then (only while the audio side is
   // settled in a stable state) load / capture / step the history.
   void Service(LooperCore& c, std::string& status)
   {
      AudioLooperNode& a = *c.audio;
      if (!a.IsReady())
         a.Allocate();
      if (c.history.empty())
      {
         c.history.push_back(std::make_shared<LoopSnapshot>()); // "empty" is undoable to
         c.cursor = 0;
      }
      if (c.writeQueued && !c.writeBusy->load())
      {
         c.writeQueued = false;
         auto snap = std::move(c.queuedWrite);
         StartWrite(c, snap);
      }

      const int st = a.PublishedState();
      const bool stable = a.Settled() &&
                          (st == LooperNode::kIdle || st == LooperNode::kPlaying || st == LooperNode::kStopped);
      if (!stable)
         return;
      // Loads need the real engine rate (and a running audio thread to
      // take the swap).
      if (c.pendingLoad)
      {
         if (AudioEngine::Instance().SampleRate() <= 0.0)
            return;
         LoadIntoAudio(a, *c.pendingLoad);
         c.history.assign(1, c.pendingLoad);
         c.cursor = 0;
         c.pendingLoad.reset();
         if (c.filePath != SidecarPath(c)) // a file from elsewhere: re-home it
            ScheduleWrite(c, c.history[0]);
         return;
      }

      const uint32_t gen = a.PublishedGen();
      if (gen != c.seenGen)
      {
         c.seenGen = gen;
         if (gen != a.PublishedSwapAck())
         {
            auto snap = Capture(a);
            PushHistory(c, snap);
            ScheduleWrite(c, snap);
         }
      }

      if (c.pendingStep != 0)
      {
         const int target = c.cursor + c.pendingStep;
         c.pendingStep = 0;
         if (target >= 0 && target < (int)c.history.size() && AudioEngine::Instance().SampleRate() > 0.0)
         {
            c.cursor = target;
            LoadIntoAudio(a, *c.history[(size_t)target]);
            ScheduleWrite(c, c.history[(size_t)target]);
            status.clear();
         }
      }
   }
}

LooperNode::LooperNode()
{
   PruneRegistry();
   auto core = std::make_shared<LooperCore>();
   core->id = NewLoopId();
   Registry()[core->id] = core;
   BindCore(core);
}

LooperNode::~LooperNode() = default;

void LooperNode::BindCore(const std::shared_ptr<LooperCore>& core)
{
   mCore = core;
   loopId = core->id;
   loopFile = core->filePath;
}

AudioLooperNode* LooperNode::Audio() const
{
   return mCore->audio.get();
}

AudioNode* LooperNode::GetAudioNode()
{
   return Audio();
}

void LooperNode::SetRestoringPatch(bool restoring)
{
   gRestoringPatch = restoring;
}

void LooperNode::ReloadFromState()
{
   auto& reg = Registry();
   if (loopId.empty() || loopId == mCore->id)
   {
      loopId = mCore->id;
      loopFile = mCore->filePath;
      return;
   }

   const std::string savedFile = loopFile;
   auto it = reg.find(loopId);
   if (gRestoringPatch)
   {
      // Patch open / undo: the same looper coming back.
      if (it != reg.end())
      {
         BindCore(it->second); // the fresh core is pruned later
         AudioTopologyRequest::Request();
         return;
      }
      reg.erase(mCore->id);
      mCore->id = loopId;
      reg[loopId] = mCore;
      if (!savedFile.empty())
      {
         std::string error;
         if (auto snap = DecodeLoopFile(savedFile, error))
         {
            mCore->pendingLoad = snap;
            mCore->filePath = savedFile;
         }
         else
            mStatus = "loop file missing";
      }
   }
   else
   {
      // Paste / duplicate: a new looper with a copy of the source's loop.
      if (it != reg.end() && it->second && it->second->cursor >= 0 &&
          it->second->cursor < (int)it->second->history.size())
      {
         auto snap = it->second->history[(size_t)it->second->cursor];
         if (snap && snap->frames > 0)
            mCore->pendingLoad = snap;
      }
      else if (!savedFile.empty())
      {
         std::string error;
         mCore->pendingLoad = DecodeLoopFile(savedFile, error);
      }
   }
   loopId = mCore->id;
   loopFile = mCore->filePath;
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

void LooperNode::SetRecord(bool on) { Audio()->PushCommand(on ? kCmdRecOn : kCmdRecOff); }
void LooperNode::SetPlay(bool on) { Audio()->PushCommand(on ? kCmdPlayOn : kCmdPlayOff); }
void LooperNode::SetOverdub(bool on) { Audio()->PushCommand(on ? kCmdDubOn : kCmdDubOff); }
void LooperNode::Clear() { Audio()->PushCommand(kCmdClear); }
void LooperNode::PressRecord() { Audio()->PushCommand(kCmdRecPress); }
void LooperNode::PressPlay() { Audio()->PushCommand(kCmdPlayPress); }
void LooperNode::PressOverdub() { Audio()->PushCommand(kCmdDubPress); }

void LooperNode::UndoLayer()
{
   if (IsRecordingOrArmed())
      return;
   if (IsOverdubbing())
      SetOverdub(false); // closes the layer; it is captured, then undone
   else if (!CanUndoLayer())
      return;
   mCore->pendingStep = -1;
}

void LooperNode::RedoLayer()
{
   if (IsRecordingOrArmed() || IsOverdubbing() || !CanRedoLayer())
      return;
   mCore->pendingStep = 1;
}

bool LooperNode::CanUndoLayer() const
{
   return mCore->cursor > 0 || mState == kOverdubbing;
}

bool LooperNode::CanRedoLayer() const
{
   return mCore->cursor + 1 < (int)mCore->history.size();
}

int LooperNode::HistoryIndex() const { return mCore->cursor; }
int LooperNode::HistoryCount() const { return (int)mCore->history.size(); }

bool LooperNode::ExportWav(const std::string& path, std::string& error)
{
   if (mCore->cursor < 0 || mCore->cursor >= (int)mCore->history.size() ||
       !mCore->history[(size_t)mCore->cursor] || mCore->history[(size_t)mCore->cursor]->frames <= 0)
   {
      error = "nothing recorded";
      mStatus = error;
      return false;
   }
   std::string target = path;
   if (target.size() < 4 || (target.compare(target.size() - 4, 4, ".wav") != 0 &&
                             target.compare(target.size() - 4, 4, ".WAV") != 0))
      target += ".wav";
   if (!WriteFloatWav(target, *mCore->history[(size_t)mCore->cursor], error))
   {
      mStatus = "export failed";
      return false;
   }
   const size_t slash = target.find_last_of("/\\");
   mStatus = "exported " + (slash == std::string::npos ? target : target.substr(slash + 1));
   return true;
}

void LooperNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if ((frameId & 63) == 0)
      PruneRegistry();
   AudioLooperNode* a = Audio();
   a->PushParams(*this);
   Service(*mCore, mStatus);
   loopId = mCore->id;
   loopFile = mCore->filePath;
   mState = a->PublishedState();
   mLengthSec = a->PublishedLengthSec();
   mRecordedSec = a->PublishedRecordedSec();
   mPos01 = a->PublishedPos01();
   mLevel = a->PublishedPeak();
   mArmedSec = a->PublishedArmedSec();
   mPlayWaitSec = a->PublishedPlayWait();
   mCompMs = (float)(1000.0 * a->LatencyFrames() / std::max(1.0, a->SampleRate()));
   if (mState == kRecording || mState == kArmed)
      mStatus.clear();
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
   v.Text("loopId", loopId);
   v.Text("loopFile", loopFile);
   // Turbo 0.48: appended (older patches load with the old behaviour).
   v.Int("takeDivision", takeDivision);
   v.Float("speed", speed);
   v.Float("pitch", pitch);
   v.Float("finetune", finetune);
   v.Float("fadeIn", fadeIn);
   v.Float("fadeOut", fadeOut);
   v.Float("volume", volume);
}
