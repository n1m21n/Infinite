#include "LooperNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioEngine.h"
#include "audio/AudioFileWriter.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/PassFade.h"
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
   constexpr int kCmdCapacity = 32;
   // A synced REC pressed this long after a grid line still starts the take on
   // that line, back-filled from the pre-roll ring (which holds this much input).
   constexpr double kLateSeconds = 0.2;
   // Waveform peak bins: one min/max pair per 1024 frames of loop, written by
   // the audio thread (relaxed atomics, no allocation) and read by the UI.
   constexpr int kBinFrames = 1024;
   constexpr int kMaxBins = kMaxFrames / kBinFrames + 2;

   struct LoopBuf
   {
      std::vector<float> ch[2];
      int capacity = 0;
      double sampleRate = 0.0;
      // A loop read back from its file: frames already in ch[], and their
      // peak bins, adopted by the audio thread when it swaps the buffer in.
      int preload = 0;
      std::vector<float> binMin, binMax;
      // The last kLateSeconds of input, always running, for a late REC.
      std::vector<float> pre[2];
      int preCap = 0;
   };

   // A buffer for `sr`, holding `src` (resampled linearly when its rate
   // differs, cut at the capacity) when given. Main thread.
   LoopBuf* MakeLoopBuf(double sr, const Platform::SampleBuffer* src)
   {
      auto* buf = new LoopBuf();
      buf->sampleRate = sr;
      buf->capacity = (int)std::min<double>(sr * kMaxSeconds, (double)kMaxFrames);
      buf->ch[0].assign((size_t)buf->capacity, 0.0f);
      buf->ch[1].assign((size_t)buf->capacity, 0.0f);
      buf->preCap = (int)std::ceil(sr * kLateSeconds) + 1;
      buf->pre[0].assign((size_t)buf->preCap, 0.0f);
      buf->pre[1].assign((size_t)buf->preCap, 0.0f);
      if (src == nullptr || src->numFrames <= 0 || src->channels <= 0 || src->sampleRate <= 0.0)
         return buf;
      const float* L = src->channelData.data();
      const float* R = src->channels > 1 ? L + src->numFrames : L;
      const double step = src->sampleRate / sr;
      const int frames = (int)std::min<double>((double)buf->capacity, std::floor((double)src->numFrames / step));
      for (int i = 0; i < frames; i++)
      {
         const double x = (double)i * step;
         const int i0 = std::min((int)x, src->numFrames - 1);
         const int i1 = std::min(i0 + 1, src->numFrames - 1);
         const float fr = (float)(x - (double)i0);
         buf->ch[0][(size_t)i] = L[i0] + (L[i1] - L[i0]) * fr;
         buf->ch[1][(size_t)i] = R[i0] + (R[i1] - R[i0]) * fr;
      }
      buf->preload = frames >= kMinTakeFrames ? frames : 0;
      const int bins = std::min(kMaxBins, (buf->preload + kBinFrames - 1) / kBinFrames);
      buf->binMin.assign((size_t)bins, 0.0f);
      buf->binMax.assign((size_t)bins, 0.0f);
      for (int i = 0; i < buf->preload; i++)
      {
         const int b = i / kBinFrames;
         if (b >= bins)
            break;
         const float m = 0.5f * (buf->ch[0][(size_t)i] + buf->ch[1][(size_t)i]);
         buf->binMin[(size_t)b] = std::min(buf->binMin[(size_t)b], m);
         buf->binMax[(size_t)b] = std::max(buf->binMax[(size_t)b], m);
      }
      return buf;
   }

   // Loop files being written by a worker right now. A respawned node (undo)
   // must not read one half written; it waits for it instead. Main thread and
   // the writer threads only - never the audio thread.
   std::mutex gLoopWritesMutex;
   std::set<std::string> gLoopWrites;

   bool LoopWriteInFlight(const std::string& path)
   {
      std::lock_guard<std::mutex> lock(gLoopWritesMutex);
      return gLoopWrites.count(path) != 0;
   }

   // 32-bit IEEE float WAV (format 3, with the `fact` chunk non-PCM WAVs
   // carry). Written to a temp name and renamed over the target, so a reader
   // never sees a partial file.
   bool WriteFloatWav(const std::string& path, const std::vector<float>& interleaved, int frames, double sr)
   {
      namespace fs = std::filesystem;
      const fs::path target = fs::u8path(path);
      const fs::path tmp = fs::u8path(path + ".tmp");
      {
         std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
         if (!f)
            return false;
         auto u32 = [&f](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
         auto u16 = [&f](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
         const uint32_t dataBytes = (uint32_t)frames * 2u * 4u;
         f.write("RIFF", 4);
         u32(4 + (8 + 18) + (8 + 4) + (8 + dataBytes));
         f.write("WAVE", 4);
         f.write("fmt ", 4);
         u32(18);
         u16(3);                              // WAVE_FORMAT_IEEE_FLOAT
         u16(2);                              // channels
         u32((uint32_t)std::lround(sr));      // sample rate
         u32((uint32_t)std::lround(sr) * 8u); // byte rate
         u16(8);                              // block align
         u16(32);                             // bits per sample
         u16(0);                              // cbSize
         f.write("fact", 4);
         u32(4);
         u32((uint32_t)frames);
         f.write("data", 4);
         u32(dataBytes);
         f.write(reinterpret_cast<const char*>(interleaved.data()), (std::streamsize)dataBytes);
         if (!f)
            return false;
      }
      std::error_code ec;
      fs::rename(tmp, target, ec);
      if (ec)
      {
         fs::remove(tmp, ec);
         return false;
      }
      return true;
   }
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
      mVolumeNow = mVolume.load(std::memory_order_relaxed);
      mRateNow = mRate.load(std::memory_order_relaxed);
      if (mBufRate == sr)
         return;
      mBufSlot.Push(MakeLoopBuf(sr, mPendingLoad.get()));
      mPendingLoad.reset();
      mBufRate = sr;
   }

   // Main thread. Hands a loop read back from its file to the audio thread:
   // now if the engine already has a rate, else at the first PrepareToPlay.
   void LoadLoop(std::unique_ptr<Platform::SampleBuffer> loop)
   {
      if (mBufRate > 0.0)
         mBufSlot.Push(MakeLoopBuf(mBufRate, loop.get()));
      else
         mPendingLoad = std::move(loop);
   }

   // ---- loop read lease (main thread) -----------------------------------
   // The main thread may read the loop buffer only while the audio thread
   // has granted a lease: it grants one at a block boundary when nothing is
   // being recorded, and while it is held it neither applies presses nor
   // swaps buffers, so nothing writes the loop. Presses wait (one cook,
   // typically well under a frame) rather than being dropped.
   enum Lease { kLeaseIdle = 0, kLeaseAsked, kLeaseGranted, kLeaseRefused };
   void AskLease() { mLease.store(kLeaseAsked, std::memory_order_release); }
   int LeaseState() const { return mLease.load(std::memory_order_acquire); }
   void ReleaseLease() { mLease.store(kLeaseIdle, std::memory_order_release); }
   // Valid only while the lease is granted.
   const LoopBuf* LeasedBuf() const { return mLeaseBuf; }
   int LeasedLength() const { return mLeaseLen; }
   int LeasedVersion() const { return mLeaseVersion; }
   int LeasedTake() const { return mLeaseTake; }
   int PublishedVersion() const { return mPubVersion.load(std::memory_order_relaxed); }

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
      mThru.store(n.thru ? 1.0f : 0.0f, std::memory_order_relaxed);
      mVolume.store(std::clamp(n.volume, 0.0f, 1.0f), std::memory_order_relaxed);
      mRate.store(LooperNode::RateFor(std::clamp(n.pitch, -24.0f, 24.0f), std::clamp(n.finetune, -50.0f, 50.0f),
                                      std::clamp(n.speed, -2.0f, 2.0f)),
                  std::memory_order_relaxed);
      mFadeInMs.store(std::clamp(n.fadeIn, 0.0f, 250.0f), std::memory_order_relaxed);
      mFadeOutMs.store(std::clamp(n.fadeOut, 0.0f, 250.0f), std::memory_order_relaxed);
      mAutoLatencyFrames.store(std::max(0, autoLatencyFrames), std::memory_order_relaxed);
   }

   void DrainRetired() { mBufSlot.DrainRetired(); }

   int PublishedState() const { return mPubState.load(std::memory_order_relaxed); }
   float PublishedLoopSec() const { return mPubLoopSec.load(std::memory_order_relaxed); }
   float PublishedRecordedSec() const { return mPubRecSec.load(std::memory_order_relaxed); }
   float PublishedPos01() const { return mPubPos.load(std::memory_order_relaxed); }
   float PublishedPeak() const { return mPubPeak.exchange(0.0f, std::memory_order_relaxed); }
   double SampleRate() const { return mSampleRate.load(std::memory_order_relaxed); }
   int CompensationFrames() const { return mPubComp.load(std::memory_order_relaxed); }
   int PublishedLenFrames() const { return mPubLen.load(std::memory_order_relaxed); }
   int PublishedTargetFrames() const { return mPubTarget.load(std::memory_order_relaxed); }

   // Main thread. Fills `cols` min/max columns covering frames [0, total) from
   // the peak bins; columns past `len` (not recorded yet) come back zero.
   void CopyPeaks(int len, int total, int cols, float* mn, float* mx) const
   {
      const int avail = std::min(kMaxBins, (len + kBinFrames - 1) / kBinFrames);
      for (int c = 0; c < cols; c++)
      {
         mn[c] = 0.0f;
         mx[c] = 0.0f;
         if (total <= 0 || len <= 0)
            continue;
         const int64_t f0 = (int64_t)total * c / cols;
         const int64_t f1 = std::max<int64_t>(f0 + 1, (int64_t)total * (c + 1) / cols);
         if (f0 >= len)
            continue;
         const int b0 = (int)(f0 / kBinFrames);
         const int b1 = std::min(avail - 1, (int)((std::min<int64_t>(f1, len) - 1) / kBinFrames));
         float lo = 0.0f, hi = 0.0f;
         for (int b = b0; b <= b1; b++)
         {
            lo = std::min(lo, mBinMin[b].load(std::memory_order_relaxed));
            hi = std::max(hi, mBinMax[b].load(std::memory_order_relaxed));
         }
         mn[c] = lo;
         mx[c] = hi;
      }
   }

   // ---- audio thread -----------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      const int numFrames = output.numFrames;
      const double sr = mSampleRate.load(std::memory_order_relaxed);

      int lease = mLease.load(std::memory_order_acquire);
      if (lease != kLeaseGranted && mBufSlot.SwapIn())
      {
         // Fresh buffer: first prepare or a rate change (empty), or a loop
         // read back from its file (held, stopped).
         mBuf = mBufSlot.Active();
         mLength = mBuf != nullptr ? mBuf->preload : 0;
         mState = mLength > 0 ? LooperNode::kStopped : LooperNode::kEmpty;
         mPos = 0.0;
         mTarget = 0;
         mTakeComp = 0;
         mAccBin = -1;
         mPreW = 0;
         mPreFill = 0;
         if (mBuf != nullptr)
            for (size_t b = 0; b < mBuf->binMin.size(); b++)
            {
               mBinMin[b].store(mBuf->binMin[b], std::memory_order_relaxed);
               mBinMax[b].store(mBuf->binMax[b], std::memory_order_relaxed);
            }
      }
      if (lease == kLeaseAsked)
      {
         const bool writing = mState == LooperNode::kRecording || mState == LooperNode::kOverdubbing ||
                              mState == LooperNode::kArmed;
         if (writing)
            lease = kLeaseRefused;
         else
         {
            lease = kLeaseGranted;
            mLeaseBuf = mBuf;
            mLeaseLen = mState == LooperNode::kEmpty ? 0 : mLength;
            mLeaseVersion = mVersion;
            mLeaseTake = mTakeSerial;
         }
         mLease.store(lease, std::memory_order_release);
      }

      const float thruTarget = mThru.load(std::memory_order_relaxed);
      const float volumeTarget = mVolume.load(std::memory_order_relaxed);
      const float rateTarget = mRate.load(std::memory_order_relaxed);
      const float fadeInMs = mFadeInMs.load(std::memory_order_relaxed);
      const float fadeOutMs = mFadeOutMs.load(std::memory_order_relaxed);
      const bool unity = std::fabs(rateTarget - 1.0f) < 1e-4f && std::fabs(mRateNow - 1.0f) < 1e-4f;
      const double compFrames = (double)mAutoLatencyFrames.load(std::memory_order_relaxed);
      mComp = (int)std::clamp(compFrames, 0.0, sr);
      mPubComp.store(mComp, std::memory_order_relaxed);

      mBlockFrames = numFrames;
      // Presses, in order (held back while the main thread reads the loop).
      for (; lease != kLeaseGranted;)
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
         const float vol = mVolumeNow + (volumeTarget - mVolumeNow) * t;
         const float rate = mRateNow + (rateTarget - mRateNow) * t;
         float outL = inL * thru;
         float outR = inR * thru;

         if (mState == LooperNode::kArmed && armedOffset >= 0 && i >= armedOffset)
            BeginTake();

         if (mBuf != nullptr)
         {
            mBuf->pre[0][(size_t)mPreW] = inL;
            mBuf->pre[1][(size_t)mPreW] = inR;
            mPreW = (mPreW + 1) % mBuf->preCap;
            mPreFill = std::min(mPreFill + 1, mBuf->preCap);
            if (mState == LooperNode::kRecording)
            {
               if (mSkip > 0)
                  mSkip--; // input from before the take's musical start
               else if (mLength < mBuf->capacity)
               {
                  mBuf->ch[0][(size_t)mLength] = inL;
                  mBuf->ch[1][(size_t)mLength] = inR;
                  TouchBin(mLength, 0.5f * (inL + inR), false);
                  mLength++;
               }
               if ((mTarget > 0 && mLength >= mTarget) || mLength >= mBuf->capacity)
                  FinishTake(LooperNode::kPlaying);
            }
            else if ((mState == LooperNode::kPlaying || mState == LooperNode::kOverdubbing) && mLength > 0)
            {
               // Linear-interpolated read at the fractional playhead, scaled by the
               // per-pass fade (the whole loop is one pass; a negative rate walks it
               // backwards and the fade-in follows).
               const int i0 = std::clamp((int)mPos, 0, mLength - 1);
               const int i1 = (i0 + 1 >= mLength) ? 0 : i0 + 1;
               const float fr = (float)(mPos - (double)i0);
               const float g = PassFade::Gain(mPos, 0.0, (double)mLength, rate < 0.0f ? -1.0f : 1.0f, fadeInMs, fadeOutMs,
                                              std::fabs(rate), sr) * vol;
               outL += (mBuf->ch[0][(size_t)i0] + (mBuf->ch[0][(size_t)i1] - mBuf->ch[0][(size_t)i0]) * fr) * g;
               outR += (mBuf->ch[1][(size_t)i0] + (mBuf->ch[1][(size_t)i1] - mBuf->ch[1][(size_t)i0]) * fr) * g;
               if (mState == LooperNode::kOverdubbing && unity)
               {
                  // This input was played against the loop position `latency`
                  // frames ago. Only at the recorded speed: layers written at another
                  // rate would land smeared or doubled.
                  const int w = ((i0 - mTakeComp % mLength) + mLength) % mLength;
                  mBuf->ch[0][(size_t)w] = std::clamp(mBuf->ch[0][(size_t)w] + inL, -4.0f, 4.0f);
                  mBuf->ch[1][(size_t)w] = std::clamp(mBuf->ch[1][(size_t)w] + inR, -4.0f, 4.0f);
                  TouchBin(w, 0.5f * (mBuf->ch[0][(size_t)w] + mBuf->ch[1][(size_t)w]), true);
               }
               mPos += (double)rate;
               const double len = (double)mLength;
               if (mPos >= len)
                  mPos = std::fmod(mPos, len);
               else if (mPos < 0.0)
                  mPos = len + std::fmod(mPos, len);
            }
         }

         for (int ch = 0; ch < output.numChannels; ch++)
            output.channels[ch][i] = (ch & 1) ? outR : outL;
         peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
      }
      mThruNow = thruTarget;
      mVolumeNow = volumeTarget;
      mRateNow = rateTarget;
      FlushBin();
      mPubVersion.store(mVersion, std::memory_order_relaxed);
      mPubLen.store(mState == LooperNode::kEmpty ? 0 : mLength, std::memory_order_relaxed);
      mPubTarget.store(mState == LooperNode::kRecording ? mTarget : 0, std::memory_order_relaxed);

      const bool hasLoop = mLength > 0 && mState != LooperNode::kRecording && mState != LooperNode::kArmed;
      mPubState.store(mState, std::memory_order_relaxed);
      mPubLoopSec.store(hasLoop ? (float)((double)mLength / sr) : 0.0f, std::memory_order_relaxed);
      mPubRecSec.store(mState == LooperNode::kRecording ? (float)((double)mLength / sr) : 0.0f,
                       std::memory_order_relaxed);
      mPubPos.store(hasLoop ? (float)(mPos / (double)std::max(1, mLength)) : 0.0f, std::memory_order_relaxed);
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

   // Frames since the grid line just passed, at this block's start (where
   // presses land), when a REC this late should still start on that line:
   // within kLateSeconds and nearer that line than the next. Otherwise -1.
   int LateFrames(double sr) const
   {
      const double bpm = std::max(1.0f, Transport::Instance().Tempo());
      const double beatsPerFrame = bpm / 60.0 / sr;
      const double start = Transport::Instance().Beats() - beatsPerFrame * (double)mBlockFrames;
      const double grid = GridBeats();
      const double last = std::floor(start / grid + 1e-6) * grid;
      const double late = std::max(0.0, (start - last) / beatsPerFrame);
      if (late > kLateSeconds * sr || late >= 0.5 * grid / beatsPerFrame)
         return -1;
      return (int)std::lround(late);
   }

   // A take whose musical start was `late` frames ago. Input for that moment
   // arrives `comp` frames after it, so when late <= comp it is still to come
   // (skip less); otherwise the missed frames come from the pre-roll ring.
   // False when the ring does not hold them yet (arm instead).
   bool BeginLateTake(int late)
   {
      const int missed = late - mComp;
      if (missed > mPreFill || mBuf == nullptr)
         return false;
      BeginTake();
      if (missed <= 0)
      {
         mSkip = -missed;
         return true;
      }
      mSkip = 0;
      for (int k = 0; k < missed; k++)
      {
         const int r = ((mPreW - missed + k) % mBuf->preCap + mBuf->preCap) % mBuf->preCap;
         const float l = mBuf->pre[0][(size_t)r];
         const float rr = mBuf->pre[1][(size_t)r];
         mBuf->ch[0][(size_t)k] = l;
         mBuf->ch[1][(size_t)k] = rr;
         TouchBin(k, 0.5f * (l + rr), false);
      }
      mLength = missed;
      return true;
   }

   // Publishes the bin being accumulated. Audio thread.
   void FlushBin()
   {
      if (mAccBin >= 0 && mAccBin < kMaxBins)
      {
         mBinMin[mAccBin].store(mAccMin, std::memory_order_relaxed);
         mBinMax[mAccBin].store(mAccMax, std::memory_order_relaxed);
      }
   }

   // A frame was written at `idx` with mono value `v`. Moving into another bin
   // publishes the finished one; an overdub seeds the new bin from what the
   // loop already holds there (one bounded 1024-frame scan per bin).
   void TouchBin(int idx, float v, bool seedFromBuffer)
   {
      const int b = idx / kBinFrames;
      if (b >= kMaxBins)
         return;
      if (b != mAccBin)
      {
         FlushBin();
         mAccBin = b;
         mAccMin = 0.0f;
         mAccMax = 0.0f;
         if (seedFromBuffer && mBuf != nullptr)
         {
            const int end = std::min(mLength, (b + 1) * kBinFrames);
            for (int i = b * kBinFrames; i < end; i++)
            {
               const float m = 0.5f * (mBuf->ch[0][(size_t)i] + mBuf->ch[1][(size_t)i]);
               mAccMin = std::min(mAccMin, m);
               mAccMax = std::max(mAccMax, m);
            }
         }
      }
      mAccMin = std::min(mAccMin, v);
      mAccMax = std::max(mAccMax, v);
   }

   void BeginTake()
   {
      mTakeSerial++;
      mAccBin = -1;
      mState = LooperNode::kRecording;
      mLength = 0;
      mPos = 0.0;
      mTakeComp = mComp;
      mSkip = mComp;
   }

   // Closes the take being recorded and moves on to `next` (Playing or
   // Overdubbing). A take too short to be anything is thrown away.
   void FinishTake(int next)
   {
      mVersion++;
      if (mBuf == nullptr || mLength < kMinTakeFrames)
      {
         mState = LooperNode::kEmpty;
         mLength = 0;
         return;
      }
      // No destructive fade here: the per-pass fade in / fade out (see
      // PassFade.h) is applied on playback, so the recording stays untouched.
      mState = next;
      // The take ended `comp` frames after its musical end, so the loop is
      // already that far into its next pass.
      mPos = (double)(mTakeComp % mLength);
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
      mPos = 0.0;
      mTakeComp = 0;
      mState = LooperNode::kPlaying;
      mAccBin = -1;
      for (int b = 0; b * kBinFrames < mLength && b < kMaxBins; b++)
      {
         float lo = 0.0f, hi = 0.0f;
         for (int i = b * kBinFrames; i < std::min(mLength, (b + 1) * kBinFrames); i++)
         {
            lo = std::min(lo, mBuf->ch[0][(size_t)i]);
            hi = std::max(hi, mBuf->ch[0][(size_t)i]);
         }
         mBinMin[b].store(lo, std::memory_order_relaxed);
         mBinMax[b].store(hi, std::memory_order_relaxed);
      }
   }

   // Where a restarted loop begins: the top, or the end when it plays backwards.
   double StartPos() const
   {
      return mRate.load(std::memory_order_relaxed) < 0.0f ? std::max(0.0, (double)mLength - 1.0) : 0.0;
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
            {
               const int late = LateFrames(mSampleRate.load(std::memory_order_relaxed));
               if (late < 0 || !BeginLateTake(late))
                  mState = LooperNode::kArmed;
            }
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
         {
            if (mState == LooperNode::kOverdubbing)
               mVersion++; // a layer settled
            mState = LooperNode::kStopped;
         }
         else if (mState == LooperNode::kStopped && mLength > 0)
         {
            mState = LooperNode::kPlaying;
            mPos = StartPos();
         }
         break;
      case LooperNode::kDub:
         if (mState == LooperNode::kRecording)
            FinishTake(LooperNode::kOverdubbing);
         else if (mState == LooperNode::kPlaying)
            mState = LooperNode::kOverdubbing;
         else if (mState == LooperNode::kOverdubbing)
         {
            mVersion++; // a layer settled
            mState = LooperNode::kPlaying;
         }
         else if (mState == LooperNode::kStopped && mLength > 0)
         {
            mState = LooperNode::kOverdubbing;
            mPos = StartPos();
         }
         break;
      case LooperNode::kClear:
         if (mState != LooperNode::kEmpty)
            mVersion++;
         mState = LooperNode::kEmpty;
         mLength = 0;
         mPos = 0.0;
         mTarget = 0;
         break;
      default:
         break;
      }
   }

   SampleSlotT<LoopBuf> mBufSlot;
   double mBufRate = 0.0; // main thread: the rate the last pushed buffer was sized for
   std::unique_ptr<Platform::SampleBuffer> mPendingLoad; // main thread: loop waiting for a rate
   std::atomic<double> mSampleRate { 48000.0 };

   // Audio-thread state.
   LoopBuf* mBuf = nullptr;
   int mState = LooperNode::kEmpty;
   int mLength = 0;   // frames in the loop, or recorded so far
   int mTarget = 0;   // fixed-length take, 0 = free
   double mPos = 0.0; // fractional playhead, frames
   int mSkip = 0;     // input frames still to drop before the take's start
   int mComp = 0;     // current compensation, frames
   int mTakeComp = 0; // compensation the held loop was recorded with
   int mBlockFrames = 0; // frames in the block being processed
   int mPreW = 0;        // pre-roll ring write index
   int mPreFill = 0;     // valid frames in the pre-roll ring
   float mThruNow = 1.0f;
   float mVolumeNow = 1.0f;
   float mRateNow = 1.0f;
   int mAccBin = -1;
   float mAccMin = 0.0f;
   float mAccMax = 0.0f;
   int mVersion = 0;    // bumped whenever the held loop settles into new content
   int mTakeSerial = 0; // bumped at every take start

   // Lease handshake (see AskLease). Written by the audio thread when it
   // grants, read by the main thread after it sees kLeaseGranted.
   std::atomic<int> mLease { kLeaseIdle };
   const LoopBuf* mLeaseBuf = nullptr;
   int mLeaseLen = 0;
   int mLeaseVersion = 0;
   int mLeaseTake = 0;

   // Main -> audio.
   int mCmds[kCmdCapacity] = {};
   std::atomic<uint32_t> mCmdHead { 0 };
   std::atomic<uint32_t> mCmdTail { 0 };
   std::atomic<int> mTake { 3 };
   std::atomic<bool> mSync { true };
   std::atomic<float> mThru { 1.0f };
   std::atomic<float> mVolume { 1.0f };
   std::atomic<float> mRate { 1.0f };
   std::atomic<float> mFadeInMs { 3.0f };
   std::atomic<float> mFadeOutMs { 3.0f };
   std::atomic<int> mAutoLatencyFrames { 0 };

   // Audio -> main.
   std::atomic<int> mPubState { LooperNode::kEmpty };
   std::atomic<float> mPubLoopSec { 0.0f };
   std::atomic<float> mPubRecSec { 0.0f };
   std::atomic<float> mPubPos { 0.0f };
   mutable std::atomic<float> mPubPeak { 0.0f };
   std::atomic<int> mPubComp { 0 };
   std::atomic<int> mPubLen { 0 };
   std::atomic<int> mPubVersion { 0 };
   std::atomic<int> mPubTarget { 0 };
   std::atomic<float> mBinMin[kMaxBins] = {};
   std::atomic<float> mBinMax[kMaxBins] = {};
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
      mButtonPresses[button]++;
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
   if (frameId - mLatencyPollFrame > 30 || frameId < mLatencyPollFrame || testLatencyFrames >= 0)
   {
      mLatencyPollFrame = frameId;
      mLatencyFrames = testLatencyFrames >= 0 ? testLatencyFrames : (int)Platform::AudioRoundTripLatencyFrames(0);
   }
   mAudioNode->PushParams(*this, mLatencyFrames);
   SyncLoopFile(); // before DrainRetired: a leased buffer may be retiring
   mAudioNode->DrainRetired();

   mState = mAudioNode->PublishedState();
   mLoopSec = mAudioNode->PublishedLoopSec();
   mRecordedSec = mAudioNode->PublishedRecordedSec();
   mPos01 = mAudioNode->PublishedPos01();
   mPeak = std::max(mPeak * 0.85f, mAudioNode->PublishedPeak());
   const double sr = std::max(1.0, mAudioNode->SampleRate());
   mCompMs = (float)((double)mAudioNode->CompensationFrames() * 1000.0 / sr);

   const int len = mAudioNode->PublishedLenFrames();
   const int tgt = mAudioNode->PublishedTargetFrames();
   mLenSec = (float)((double)len / sr);
   mTargetSec = (float)((double)tgt / sr);
   mAudioNode->CopyPeaks(len, tgt > 0 ? tgt : len, kWaveCols, waveMin, waveMax);
}

// Keeps loopFile and the audio half's loop in step, both ways. Main thread.
void LooperNode::SyncLoopFile()
{
   // 1) A loop named by the patch (open, paste, undo respawn) that this node
   //    does not hold yet: read it back. A file still being written by a
   //    worker is waited for, not read half done.
   if (loopFile != mAppliedFile && !loopFile.empty())
   {
      if (LoopWriteInFlight(loopFile))
         return;
      auto decoded = std::make_unique<Platform::SampleBuffer>();
      std::string error;
      if (Platform::DecodeAudioFileToBuffer(loopFile, *decoded, error) && decoded->numFrames > 0)
      {
         mAudioNode->LoadLoop(std::move(decoded));
         mLoopStatus.clear();
      }
      else
         mLoopStatus = "loop file could not be read: " + (error.empty() ? loopFile : error);
      mAppliedFile = loopFile;
      mFileTake = -1; // not ours: never rewritten in place
      // Loading does not change the audio half's version: the file already
      // holds what it will hold, so there is nothing to write back.
      mSavedVersion = mAudioNode->PublishedVersion();
      return;
   }
   if (loopFile.empty() && mFileTake < 0)
      mAppliedFile.clear();

   // 2) The held loop settled into new content: write it.
   const int lease = mAudioNode->LeaseState();
   if (lease == AudioLooperNode::kLeaseIdle)
   {
      if (mAudioNode->PublishedVersion() != mSavedVersion)
         mAudioNode->AskLease();
      return;
   }
   if (lease == AudioLooperNode::kLeaseRefused)
   {
      mAudioNode->ReleaseLease(); // recording again: ask once it settles
      return;
   }
   if (lease != AudioLooperNode::kLeaseGranted)
      return;

   const LoopBuf* buf = mAudioNode->LeasedBuf();
   const int len = buf != nullptr ? std::min(mAudioNode->LeasedLength(), buf->capacity) : 0;
   const int version = mAudioNode->LeasedVersion();
   const int takeSerial = mAudioNode->LeasedTake();
   const double sr = buf != nullptr ? buf->sampleRate : 0.0;
   std::vector<float> interleaved;
   if (len > 0)
   {
      interleaved.resize((size_t)len * 2);
      for (int i = 0; i < len; i++)
      {
         interleaved[(size_t)i * 2] = buf->ch[0][(size_t)i];
         interleaved[(size_t)i * 2 + 1] = buf->ch[1][(size_t)i];
      }
   }
   mAudioNode->ReleaseLease();
   mSavedVersion = version;

   if (len <= 0)
   {
      // Cleared: no loop to keep. The file stays on disk (an older save or
      // an undo step may still name it).
      loopFile.clear();
      mAppliedFile.clear();
      mFileTake = -1;
      return;
   }
   // Overdubs rewrite this take's own file; a new take, or a loop that came
   // from elsewhere, gets a new one.
   std::string path = loopFile;
   if (path.empty() || mFileTake != takeSerial)
      path = testLoopDir.empty() ? AudioRecordings::GenerateFilePath("looper")
                                 : testLoopDir + "/looper_" + std::to_string(takeSerial) + "_" +
                                      std::to_string((uintptr_t)this) + ".wav";
   loopFile = path;
   mAppliedFile = path;
   mFileTake = takeSerial;
   mLoopStatus.clear();
   if (!testLoopDir.empty())
   {
      if (!WriteFloatWav(path, interleaved, len, sr))
         mLoopStatus = "loop file could not be written";
      return;
   }
   {
      std::lock_guard<std::mutex> lock(gLoopWritesMutex);
      gLoopWrites.insert(path);
   }
   std::thread([path, data = std::move(interleaved), len, sr]() {
      WriteFloatWav(path, data, len, sr);
      std::lock_guard<std::mutex> lock(gLoopWritesMutex);
      gLoopWrites.erase(path);
   }).detach();
}

void LooperNode::VisitParams(ParamVisitor& v)
{
   v.Int("take", take);
   v.Bool("syncStart", syncStart);
   v.Bool("thru", thru);
   v.Float("finetune", finetune);
   v.Float("pitch", pitch);
   v.Float("speed", speed);
   v.Float("volume", volume);
   v.Float("fadeIn", fadeIn);
   v.Float("fadeOut", fadeOut);
   v.Text("loopFile", loopFile);
}
