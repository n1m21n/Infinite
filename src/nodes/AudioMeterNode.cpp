#include "AudioMeterNode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"

// Audio-thread processing class
class AudioMeterAudioNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMeanSquareL = 0.0f;
      mMeanSquareR = 0.0f;
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      float blockPeakL = 0.0f;
      float blockPeakR = 0.0f;
      float sumSqL = 0.0f;
      float sumSqR = 0.0f;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float inL = (in != nullptr && in->numChannels > 0) ? in->channels[0][i] : 0.0f;
         const float inR = (in != nullptr && in->numChannels > 1) ? in->channels[1][i] : inL;

         if (buffer.numChannels > 0)
            buffer.channels[0][i] = inL;
         if (buffer.numChannels > 1)
            buffer.channels[1][i] = inR;
         for (int ch = 2; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = (ch % 2 == 0) ? inL : inR;

         const float absL = std::fabs(inL);
         const float absR = std::fabs(inR);
         if (absL > blockPeakL)
            blockPeakL = absL;
         if (absR > blockPeakR)
            blockPeakR = absR;

         sumSqL += inL * inL;
         sumSqR += inR * inR;
      }

      // AES-17 / Standard 300 ms RMS integration filter
      const float numFrames = (float)buffer.numFrames;
      const float blockMeanSqL = (buffer.numFrames > 0) ? (sumSqL / numFrames) : 0.0f;
      const float blockMeanSqR = (buffer.numFrames > 0) ? (sumSqR / numFrames) : 0.0f;

      const float tau = 0.300f; // 300ms time constant
      const float alpha = 1.0f - std::exp(-numFrames / (tau * (float)mSampleRate));
      mMeanSquareL += alpha * (blockMeanSqL - mMeanSquareL);
      mMeanSquareR += alpha * (blockMeanSqR - mMeanSquareR);

      const float rmsL = std::sqrt(std::max(0.0f, mMeanSquareL));
      const float rmsR = std::sqrt(std::max(0.0f, mMeanSquareR));

      // Publish level measurements to lock-free atomics
      float curPeakL = mPeakL.load(std::memory_order_relaxed);
      while (blockPeakL > curPeakL && !mPeakL.compare_exchange_weak(curPeakL, blockPeakL, std::memory_order_relaxed)) {}

      float curPeakR = mPeakR.load(std::memory_order_relaxed);
      while (blockPeakR > curPeakR && !mPeakR.compare_exchange_weak(curPeakR, blockPeakR, std::memory_order_relaxed)) {}

      mRmsL.store(rmsL, std::memory_order_relaxed);
      mRmsR.store(rmsR, std::memory_order_relaxed);
      mBlocks.fetch_add(1, std::memory_order_relaxed);

      if (blockPeakL >= 1.0f)
         mClipL.store(true, std::memory_order_relaxed);
      if (blockPeakR >= 1.0f)
         mClipR.store(true, std::memory_order_relaxed);
   }

   void PollTelemetry(float& peakL, float& peakR, float& rmsL, float& rmsR, bool& clipL, bool& clipR)
   {
      peakL = mPeakL.exchange(0.0f, std::memory_order_relaxed);
      peakR = mPeakR.exchange(0.0f, std::memory_order_relaxed);
      // The RMS atomics hold the last block's value until the next block,
      // so when the graph stops calling ProcessBlock (transport stopped,
      // input unpatched) they would hold it forever and the bars would sit
      // lit over a "no signal" status. No new block since the last poll
      // means no signal now.
      // Judged over 100 ms, not per poll: the UI can poll faster than
      // blocks arrive (120 fps vs a 512-frame block at 48 kHz), and a
      // per-poll check would flicker the bars to zero between blocks.
      const unsigned blocks = mBlocks.load(std::memory_order_relaxed);
      const auto now = std::chrono::steady_clock::now();
      if (blocks != mLastPolledBlocks)
      {
         mLastPolledBlocks = blocks;
         mLastBlockSeen = now;
      }
      const bool fresh = now - mLastBlockSeen < std::chrono::milliseconds(100);
      rmsL = fresh ? mRmsL.load(std::memory_order_relaxed) : 0.0f;
      rmsR = fresh ? mRmsR.load(std::memory_order_relaxed) : 0.0f;
      clipL = mClipL.exchange(false, std::memory_order_relaxed);
      clipR = mClipR.exchange(false, std::memory_order_relaxed);
   }

private:
   double mSampleRate = 44100.0;
   float mMeanSquareL = 0.0f;
   float mMeanSquareR = 0.0f;

   std::atomic<float> mPeakL { 0.0f };
   std::atomic<float> mPeakR { 0.0f };
   std::atomic<float> mRmsL { 0.0f };
   std::atomic<float> mRmsR { 0.0f };
   std::atomic<bool> mClipL { false };
   std::atomic<bool> mClipR { false };
   std::atomic<unsigned> mBlocks { 0 };
   unsigned mLastPolledBlocks = 0; // main thread only
   std::chrono::steady_clock::time_point mLastBlockSeen{}; // main thread only
};

// ----------------------------------------------------------- AudioMeterNode
AudioMeterNode::AudioMeterNode() = default;
AudioMeterNode::~AudioMeterNode() = default;

void AudioMeterNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMeterAudioNode>();

   float inPeakL = 0.0f, inPeakR = 0.0f;
   float inRmsL = 0.0f, inRmsR = 0.0f;
   bool inClipL = false, inClipR = false;
   mAudioNode->PollTelemetry(inPeakL, inPeakR, inRmsL, inRmsR, inClipL, inClipR);

   // Calculate delta time for smooth ballistics
   const auto now = std::chrono::steady_clock::now();
   float dt = 1.0f / 60.0f;
   if (mHasLastTime)
   {
      const float elapsed = (float)std::chrono::duration<double>(now - mLastTime).count();
      dt = std::clamp(elapsed, 0.001f, 0.1f);
   }
   mLastTime = now;
   mHasLastTime = true;

   // Peak ballistics: instant attack, ~20 dB/s decay
   const float peakDecayFactor = std::exp(-dt * 2.3f); // ~20 dB/s decay
   if (inPeakL >= mDisplayPeakL)
      mDisplayPeakL = inPeakL;
   else
      mDisplayPeakL = std::max(inPeakL, mDisplayPeakL * peakDecayFactor);

   if (inPeakR >= mDisplayPeakR)
      mDisplayPeakR = inPeakR;
   else
      mDisplayPeakR = std::max(inPeakR, mDisplayPeakR * peakDecayFactor);

   // RMS ballistics: smooth follow
   const float rmsAlpha = std::clamp(dt * 10.0f, 0.0f, 1.0f);
   mDisplayRmsL += rmsAlpha * (inRmsL - mDisplayRmsL);
   mDisplayRmsR += rmsAlpha * (inRmsR - mDisplayRmsR);

   // Latch clip indicators
   if (inClipL || inPeakL >= 1.0f)
      mClipL = true;
   if (inClipR || inPeakR >= 1.0f)
      mClipR = true;

   // Max peak since reset, from the raw block peaks so a single-block
   // transient between two UI frames still registers.
   mMaxPeakL = std::max(mMaxPeakL, inPeakL);
   mMaxPeakR = std::max(mMaxPeakR, inPeakR);

   // Peak Hold logic (1.5s hold)
   const float holdDuration = 1.5f;
   if (inPeakL >= mPeakHoldL)
   {
      mPeakHoldL = inPeakL;
      mPeakHoldTimerL = holdDuration;
   }
   else
   {
      mPeakHoldTimerL -= dt;
      if (mPeakHoldTimerL <= 0.0f)
      {
         mPeakHoldL = std::max(mDisplayPeakL, mPeakHoldL * std::exp(-dt * 4.0f));
      }
   }

   if (inPeakR >= mPeakHoldR)
   {
      mPeakHoldR = inPeakR;
      mPeakHoldTimerR = holdDuration;
   }
   else
   {
      mPeakHoldTimerR -= dt;
      if (mPeakHoldTimerR <= 0.0f)
      {
         mPeakHoldR = std::max(mDisplayPeakR, mPeakHoldR * std::exp(-dt * 4.0f));
      }
   }
}

void AudioMeterNode::ResetPeaks()
{
   mPeakHoldL = 0.0f;
   mPeakHoldR = 0.0f;
   mPeakHoldTimerL = 0.0f;
   mPeakHoldTimerR = 0.0f;
   mMaxPeakL = 0.0f;
   mMaxPeakR = 0.0f;
   mClipL = false;
   mClipR = false;
}

AudioNode* AudioMeterNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMeterAudioNode>();
   return mAudioNode.get();
}
