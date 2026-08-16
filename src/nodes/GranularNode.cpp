#include "GranularNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
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

const char* const GranularNode::kWindowShapeNames[GranularNode::kNumWindowShapes] = {
   "Hann",
   "Gaussian",
   "Triangle",
   "Exp Decay",
   "Trapezoid"
};

namespace
{
   constexpr float kPi = 3.14159265358979323846f;
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr int kMaxGrains = 64;
   constexpr int kMaxRecordSeconds = 30;
   constexpr int kMaxRecordSampleRate = 192000;

   // Lock-free XorShift32 PRNG for audio-thread grain parameter jitter
   struct FastRng
   {
      uint32_t state = 1857248921u;

      inline float NextFloat()
      {
         state ^= state << 13;
         state ^= state >> 17;
         state ^= state << 5;
         return (float)(state & 0x00FFFFFF) * (1.0f / 16777216.0f);
      }

      inline float NextSignedFloat()
      {
         return NextFloat() * 2.0f - 1.0f;
      }
   };

   // Cubic Hermite sample interpolation
   inline float Hermite4(float y0, float y1, float y2, float y3, float frac)
   {
      const float c0 = y1;
      const float c1 = 0.5f * (y2 - y0);
      const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
      const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
      return ((c3 * frac + c2) * frac + c1) * frac + c0;
   }

   inline float ReadSampleHermite(const float* channel, int numFrames, double pos)
   {
      if (numFrames <= 0 || channel == nullptr)
         return 0.0f;
      int i1 = (int)pos;
      float frac = (float)(pos - (double)i1);
      int i0 = std::max(0, i1 - 1);
      int i2 = std::min(numFrames - 1, i1 + 1);
      int i3 = std::min(numFrames - 1, i1 + 2);
      i1 = std::clamp(i1, 0, numFrames - 1);

      return Hermite4(channel[i0], channel[i1], channel[i2], channel[i3], frac);
   }

   inline float EvaluateWindow(int shape, float t)
   {
      t = std::clamp(t, 0.0f, 1.0f);
      switch (shape)
      {
      case GranularNode::kWindowHann:
         return 0.5f * (1.0f - std::cos(kTwoPi * t));
      case GranularNode::kWindowGaussian:
      {
         float x = (t - 0.5f) * 2.0f;
         return std::exp(-8.0f * x * x);
      }
      case GranularNode::kWindowTriangle:
         return 1.0f - 2.0f * std::abs(t - 0.5f);
      case GranularNode::kWindowExpDecay:
         return std::sin(kPi * t) * std::exp(-3.0f * t) * 2.3f;
      case GranularNode::kWindowTrapezoid:
         return std::clamp(std::min(t / 0.12f, (1.0f - t) / 0.12f), 0.0f, 1.0f);
      default:
         return 0.5f * (1.0f - std::cos(kTwoPi * t));
      }
   }
}

// ------------------------------------------------------------- audio thread
class AudioGranularNode : public AudioNode
{
public:
   struct Grain
   {
      bool active = false;
      double samplePos = 0.0;
      double sampleStep = 1.0;
      int currentFrame = 0;
      int totalFrames = 1;
      int windowType = 0;
      float panL = 0.707f;
      float panR = 0.707f;
      float gain = 1.0f;
   };

   AudioGranularNode()
   {
      mGrains.resize(kMaxGrains);
      for (auto& g : mGrains)
         g.active = false;
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSelfEnv.SetSampleRate(sampleRate);
      mSelfEnv.SetADSR(2.0f, 0.0f, 1.0f, 20.0f);

      mSpawnCountdown = 0.0f;
      mScanPlayhead = 0.0;

      for (auto& g : mGrains)
         g.active = false;

      if (mRecordBuffer.empty())
         mRecordBuffer.resize((size_t)kMaxRecordSeconds * kMaxRecordSampleRate);
   }

   void Reset() override
   {
      for (auto& g : mGrains)
         g.active = false;
      mSpawnCountdown = 0.0f;
   }

   SampleSlot& GetSampleSlot() { return mSampleSlot; }

   void PushBuffer(Platform::SampleBuffer* buf)
   {
      mSampleSlot.Push(buf);
   }

   void StartRecording()
   {
      mRecordWritePos.store(0, std::memory_order_release);
      mRecordOverflowed.store(false, std::memory_order_release);
      mRecordingActive.store(true, std::memory_order_release);
   }

   int RecordedFrames() const
   {
      return mRecordWritePos.load(std::memory_order_acquire);
   }

   double RecordSampleRate() const
   {
      return mSampleRate;
   }

   const float* RecordBufferData() const
   {
      return mRecordBuffer.data();
   }

   void StopRecording()
   {
      mRecordingActive.store(false, std::memory_order_release);
   }

   bool IsRecording() const
   {
      return mRecordingActive.load(std::memory_order_acquire);
   }

   MeterRing& PlayheadRing() { return mPlayheadRing; }
   MeterRing& ActiveVoiceRing() { return mActiveVoiceRing; }

   void GetVisualSnapshot(GrainVisualSnapshot& out)
   {
      const int rIdx = mVisualReadIdx.load(std::memory_order_acquire);
      out = mVisualSnapshots[rIdx];
   }

   void Seek(float frac)
   {
      mSeekPos.store(frac, std::memory_order_release);
      mSeekPending.store(true, std::memory_order_release);
   }

   // Atomic parameters updated from UI/Cook
   std::atomic<float> mPosition { 0.0f };
   std::atomic<float> mScan { 1.0f };
   std::atomic<float> mGrainLength { 80.0f };
   std::atomic<float> mDensity { 25.0f };
   std::atomic<bool>  mFreeze { false };

   std::atomic<float> mRandomPos { 0.05f };
   std::atomic<float> mRandomLength { 0.1f };
   std::atomic<float> mRandomPitch { 0.0f };
   std::atomic<float> mRandomPan { 0.5f };
   std::atomic<float> mReverseProb { 0.0f };

   std::atomic<float> mPitchShift { 0.0f };
   std::atomic<float> mFineTune { 0.0f };
   std::atomic<int>   mWindowShape { 0 };

   std::atomic<float> mPan { 0.0f };
   std::atomic<float> mWidth { 1.0f };
   std::atomic<float> mMix { 1.0f };
   std::atomic<float> mVolume { 0.8f };

   std::atomic<float> mStart { 0.0f };
   std::atomic<float> mEnd { 1.0f };
   std::atomic<bool>  mLoop { true };

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      for (int ch = 0; ch < output.numChannels; ++ch)
         std::fill(output.channels[ch], output.channels[ch] + numFrames, 0.0f);

      // 1. Swap in newly loaded sample if ready
      if (mSampleSlot.SwapIn())
      {
         mScanPlayhead = 0.0;
         mSpawnCountdown = 0.0f;
         for (auto& g : mGrains)
            g.active = false;
      }

      // 2. Handle recording from audio input slot (slot 0)
      const bool recording = mRecordingActive.load(std::memory_order_relaxed);
      if (recording && numInputs > 0 && inputs[0] != nullptr && inputs[0]->numChannels > 0)
      {
         const float* inData = inputs[0]->channels[0];
         const int writePos = mRecordWritePos.load(std::memory_order_relaxed);
         const int capacity = (int)mRecordBuffer.size();
         const int inFrames = inputs[0]->numFrames;
         if (writePos < capacity)
         {
            const int toCopy = std::min(inFrames, capacity - writePos);
            std::copy(inData, inData + toCopy, mRecordBuffer.begin() + writePos);
            mRecordWritePos.store(writePos + toCopy, std::memory_order_release);
            if (writePos + toCopy >= capacity)
               mRecordOverflowed.store(true, std::memory_order_release);
         }
      }

      Platform::SampleBuffer* activeSample = mSampleSlot.Active();
      const bool hasSample = (activeSample != nullptr && activeSample->numFrames > 0 && activeSample->channels > 0 && !activeSample->channelData.empty());

      if (mSeekPending.exchange(false, std::memory_order_acq_rel))
      {
         const float sPos = mSeekPos.load(std::memory_order_relaxed);
         mScanPlayhead = std::clamp((double)sPos, 0.0, 1.0);
      }

      if (!hasSample)
      {
         const float zero = 0.0f;
         mActiveVoiceRing.Write(&zero, 1);
         return;
      }

      // 4. Load atomic parameters
      const float basePos = std::clamp(mPosition.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float scanSpeed = mScan.load(std::memory_order_relaxed);
      const float grainLenMs = std::clamp(mGrainLength.load(std::memory_order_relaxed), 5.0f, 1000.0f);
      const float grainDensity = std::clamp(mDensity.load(std::memory_order_relaxed), 1.0f, 100.0f);
      const bool isFrozen = mFreeze.load(std::memory_order_relaxed);

      const float rndPos = std::clamp(mRandomPos.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float rndLen = std::clamp(mRandomLength.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float rndPitch = std::clamp(mRandomPitch.load(std::memory_order_relaxed), 0.0f, 24.0f);
      const float rndPan = std::clamp(mRandomPan.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float revChance = std::clamp(mReverseProb.load(std::memory_order_relaxed), 0.0f, 1.0f);

      const float basePitch = std::clamp(mPitchShift.load(std::memory_order_relaxed), -24.0f, 24.0f);
      const float fine = std::clamp(mFineTune.load(std::memory_order_relaxed), -100.0f, 100.0f);
      const int winShape = std::clamp(mWindowShape.load(std::memory_order_relaxed), 0, (int)GranularNode::kNumWindowShapes - 1);

      const float masterPan = std::clamp(mPan.load(std::memory_order_relaxed), -1.0f, 1.0f);
      const float stereoWidth = std::clamp(mWidth.load(std::memory_order_relaxed), 0.0f, 2.0f);
      const float dryWetMix = std::clamp(mMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float masterVol = std::max(0.0f, mVolume.load(std::memory_order_relaxed));

      const float trimStart = std::clamp(mStart.load(std::memory_order_relaxed), 0.0f, 0.99f);
      const float trimEnd = std::clamp(mEnd.load(std::memory_order_relaxed), trimStart + 0.01f, 1.0f);
      const bool isLooping = mLoop.load(std::memory_order_relaxed);

      const int sampleFrames = activeSample->numFrames;
      const float* srcL = activeSample->channelData.data();
      const float* srcR = (activeSample->channels > 1) ? (activeSample->channelData.data() + sampleFrames) : srcL;
      const double srRatio = (activeSample->sampleRate > 0.0) ? (activeSample->sampleRate / mSampleRate) : 1.0;

      float* dstL = output.channels[0];
      float* dstR = (output.numChannels > 1) ? output.channels[1] : dstL;

      const double trimRange = std::max(0.001, (double)(trimEnd - trimStart));
      const double sampleDurationSec = (double)sampleFrames / (activeSample->sampleRate > 0.0 ? activeSample->sampleRate : mSampleRate);

      // 5. Per-sample granular synthesis
      for (int i = 0; i < numFrames; ++i)
      {
         const float envGain = 1.0f;

         // Advance scan position
         if (isFrozen || std::abs(scanSpeed) < 0.0001f)
         {
            mScanPlayhead = (double)basePos;
         }
         else if (sampleDurationSec > 0.001)
         {
            const double scanDelta = ((double)scanSpeed / (sampleDurationSec * mSampleRate));
            mScanPlayhead += scanDelta;
            if (isLooping)
            {
               while (mScanPlayhead >= (double)trimEnd)
                  mScanPlayhead -= trimRange;
               while (mScanPlayhead < (double)trimStart)
                  mScanPlayhead += trimRange;
            }
            else
            {
               mScanPlayhead = std::clamp(mScanPlayhead, (double)trimStart, (double)trimEnd);
            }
         }

         // Grain spawn timer
         mSpawnCountdown -= 1.0f;
         if (mSpawnCountdown <= 0.0f)
         {
            const float intervalFrames = (float)mSampleRate / grainDensity;
            mSpawnCountdown += intervalFrames;

            // Find an inactive grain slot or oldest active grain
            int chosenIdx = -1;
            int maxFramesLeft = -1;
            for (int g = 0; g < kMaxGrains; ++g)
            {
               if (!mGrains[g].active)
               {
                  chosenIdx = g;
                  break;
               }
               int framesLeft = mGrains[g].totalFrames - mGrains[g].currentFrame;
               if (framesLeft > maxFramesLeft)
               {
                  maxFramesLeft = framesLeft;
               }
            }
            if (chosenIdx < 0)
            {
               chosenIdx = 0;
            }

            Grain& g = mGrains[chosenIdx];
            g.active = true;
            g.currentFrame = 0;

            // Grain duration with jitter
            const float durRand = 1.0f + mRng.NextSignedFloat() * rndLen * 0.5f;
            const float effectiveLenMs = std::max(2.0f, grainLenMs * durRand);
            g.totalFrames = std::max(8, (int)(effectiveLenMs * 0.001f * (float)mSampleRate));

            // Grain spawn position
            double effectivePosFrac = basePos;
            if (std::abs(scanSpeed) > 0.001f || isFrozen)
               effectivePosFrac = mScanPlayhead;

            effectivePosFrac += (double)(mRng.NextSignedFloat() * rndPos * 0.5f);
            if (isLooping)
            {
               while (effectivePosFrac >= (double)trimEnd)
                  effectivePosFrac -= trimRange;
               while (effectivePosFrac < (double)trimStart)
                  effectivePosFrac += trimRange;
            }
            else
            {
               effectivePosFrac = std::clamp(effectivePosFrac, (double)trimStart, (double)trimEnd);
            }

            g.samplePos = effectivePosFrac * (double)sampleFrames;

            // Pitch calculation (semitones to ratio)
            const float st = basePitch + fine * 0.01f + mRng.NextSignedFloat() * rndPitch;
            const double pitchRatio = std::pow(2.0, (double)st / 12.0);
            const bool isRev = (revChance > 0.0f && mRng.NextFloat() < revChance);
            g.sampleStep = (isRev ? -1.0 : 1.0) * pitchRatio * srRatio;

            // If reverse, advance start position so grain reads backward from there
            if (isRev)
            {
               g.samplePos += (double)g.totalFrames * pitchRatio * srRatio;
               if (g.samplePos >= (double)sampleFrames)
                  g.samplePos = (double)sampleFrames - 1.0;
            }

            // Pan calculation
            float grainPan = masterPan + mRng.NextSignedFloat() * rndPan;
            grainPan = std::clamp(grainPan * stereoWidth, -1.0f, 1.0f);
            const float panAngle = (grainPan + 1.0f) * 0.25f * kPi; // 0..pi/2
            g.panL = std::cos(panAngle);
            g.panR = std::sin(panAngle);

            g.windowType = winShape;
            // Normalization gain to maintain consistent energy across densities
            const float overlapFactor = std::max(1.0f, (float)g.totalFrames / intervalFrames);
            g.gain = 1.0f / std::sqrt(overlapFactor);
         }

         // Accumulate grains
         float grainSumL = 0.0f;
         float grainSumR = 0.0f;

         for (int g = 0; g < kMaxGrains; ++g)
         {
            Grain& gr = mGrains[g];
            if (!gr.active)
               continue;

            const float winPhase = (float)gr.currentFrame / (float)(gr.totalFrames > 1 ? gr.totalFrames - 1 : 1);
            const float env = EvaluateWindow(gr.windowType, winPhase) * gr.gain;

            const float sL = ReadSampleHermite(srcL, sampleFrames, gr.samplePos);
            const float sR = ReadSampleHermite(srcR, sampleFrames, gr.samplePos);

            grainSumL += sL * env * gr.panL;
            grainSumR += sR * env * gr.panR;

            gr.samplePos += gr.sampleStep;
            // Wrap or bounds check
            if (gr.samplePos < 0.0 || gr.samplePos >= (double)sampleFrames)
            {
               if (isLooping)
               {
                  while (gr.samplePos >= (double)sampleFrames) gr.samplePos -= (double)sampleFrames;
                  while (gr.samplePos < 0.0) gr.samplePos += (double)sampleFrames;
               }
               else
               {
                  gr.active = false;
               }
            }

            gr.currentFrame++;
            if (gr.currentFrame >= gr.totalFrames)
               gr.active = false;
         }

         // Dry/Wet mixing with live input if present
         float outL = grainSumL * dryWetMix * envGain;
         float outR = grainSumR * dryWetMix * envGain;

         if (dryWetMix < 1.0f && numInputs > 0 && inputs[0] != nullptr && inputs[0]->numChannels > 0)
         {
            const float dryL = inputs[0]->channels[0][i];
            const float dryR = (inputs[0]->numChannels > 1) ? inputs[0]->channels[1][i] : dryL;
            const float dryGain = 1.0f - dryWetMix;
            outL += dryL * dryGain;
            outR += dryR * dryGain;
         }

         dstL[i] = outL * masterVol;
         if (output.numChannels > 1)
            dstR[i] = outR * masterVol;
      }

      // 6. Update MeterRing and visual snapshot
      const float playheadPos = (float)mScanPlayhead;
      mPlayheadRing.Write(&playheadPos, 1);

      int activeCount = 0;
      for (const auto& g : mGrains)
         if (g.active) activeCount++;
      const float activeCountF = (float)activeCount;
      mActiveVoiceRing.Write(&activeCountF, 1);

      // Snapshot active grains for UI particle visualization
      const int wIdx = (mVisualWriteIdx.load(std::memory_order_relaxed) + 1) % 3;
      GrainVisualSnapshot& snap = mVisualSnapshots[wIdx];
      snap.scanPos = playheadPos;
      int snapCount = 0;
      for (int g = 0; g < kMaxGrains && snapCount < GrainVisualSnapshot::kMaxVisualGrains; ++g)
      {
         const Grain& gr = mGrains[g];
         if (!gr.active)
            continue;
         const float normPos = (float)(gr.samplePos / (double)std::max(1, sampleFrames));
         const float winPhase = (float)gr.currentFrame / (float)(gr.totalFrames > 1 ? gr.totalFrames - 1 : 1);
         const float amp = EvaluateWindow(gr.windowType, winPhase);
         snap.grains[snapCount].position = std::clamp(normPos, 0.0f, 1.0f);
         snap.grains[snapCount].amp = amp;
         snap.grains[snapCount].pan = gr.panR - gr.panL; // -1 to +1
         snap.grains[snapCount].pitchRatio = (float)std::abs(gr.sampleStep / srRatio);
         snapCount++;
      }
      snap.count = snapCount;
      mVisualWriteIdx.store(wIdx, std::memory_order_release);
      mVisualReadIdx.store(wIdx, std::memory_order_release);
   }

private:
   double mSampleRate = 44100.0;
   FastRng mRng;
   SampleSlot mSampleSlot;
   Envelope mSelfEnv;

   std::vector<Grain> mGrains;
   float mSpawnCountdown = 0.0f;
   double mScanPlayhead = 0.0;

   std::atomic<bool> mSeekPending { false };
   std::atomic<float> mSeekPos { 0.0f };

   std::vector<float> mRecordBuffer;
   std::atomic<int> mRecordWritePos { 0 };
   std::atomic<bool> mRecordingActive { false };
   std::atomic<bool> mRecordOverflowed { false };

   MeterRing mPlayheadRing;
   MeterRing mActiveVoiceRing;

   GrainVisualSnapshot mVisualSnapshots[3];
   std::atomic<int> mVisualWriteIdx { 0 };
   std::atomic<int> mVisualReadIdx { 0 };
};

// ------------------------------------------------------------- main thread
GranularNode::GranularNode() = default;
GranularNode::~GranularNode() = default;

void GranularNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGranularNode>();

   // Propagate parameters to audio node
   mAudioNode->mPosition.store(position, std::memory_order_relaxed);
   mAudioNode->mScan.store(scan, std::memory_order_relaxed);
   mAudioNode->mGrainLength.store(grainLength, std::memory_order_relaxed);
   mAudioNode->mDensity.store(density, std::memory_order_relaxed);
   mAudioNode->mFreeze.store(freeze, std::memory_order_relaxed);

   mAudioNode->mRandomPos.store(randomPos, std::memory_order_relaxed);
   mAudioNode->mRandomLength.store(randomLength, std::memory_order_relaxed);
   mAudioNode->mRandomPitch.store(randomPitch, std::memory_order_relaxed);
   mAudioNode->mRandomPan.store(randomPan, std::memory_order_relaxed);
   mAudioNode->mReverseProb.store(reverseProb, std::memory_order_relaxed);

   mAudioNode->mPitchShift.store(pitchShift, std::memory_order_relaxed);
   mAudioNode->mFineTune.store(fineTune, std::memory_order_relaxed);
   mAudioNode->mWindowShape.store(windowShape, std::memory_order_relaxed);

   mAudioNode->mPan.store(pan, std::memory_order_relaxed);
   mAudioNode->mWidth.store(width, std::memory_order_relaxed);
   mAudioNode->mMix.store(mix, std::memory_order_relaxed);
   mAudioNode->mVolume.store(volume, std::memory_order_relaxed);

   mAudioNode->mStart.store(start, std::memory_order_relaxed);
   mAudioNode->mEnd.store(end, std::memory_order_relaxed);
   mAudioNode->mLoop.store(loop, std::memory_order_relaxed);

   // Drain playhead and voice counts from audio thread
   float ph = 0.0f;
   if (mAudioNode->PlayheadRing().ReadLatest(ph))
      mPlayhead = ph;

   float voiceCount = 0.0f;
   if (mAudioNode->ActiveVoiceRing().ReadLatest(voiceCount))
      mIsPlaying = (voiceCount > 0.5f);

   // Pull visual snapshot for UI rendering
   mAudioNode->GetVisualSnapshot(mLatestVisualSnapshot);

   // Check if recording finished
   if (mRecording && !mAudioNode->IsRecording())
   {
      StopRecording();
   }
}

void GranularNode::VisitParams(ParamVisitor& v)
{
   v.Text("filePath", mFilePath);
   v.Float("position", position);
   v.Float("scan", scan);
   v.Float("grainLength", grainLength);
   v.Float("density", density);
   v.Bool("freeze", freeze);

   v.Float("randomPos", randomPos);
   v.Float("randomLength", randomLength);
   v.Float("randomPitch", randomPitch);
   v.Float("randomPan", randomPan);
   v.Float("reverseProb", reverseProb);

   v.Float("pitchShift", pitchShift);
   v.Float("fineTune", fineTune);
   v.Int("windowShape", windowShape);

   v.Float("pan", pan);
   v.Float("width", width);
   v.Float("mix", mix);
   v.Float("volume", volume);

   v.Float("start", start);
   v.Float("end", end);
   v.Bool("loop", loop);
}

AudioNode* GranularNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGranularNode>();
   return mAudioNode.get();
}

void GranularNode::Seek(float frac)
{
   position = std::clamp(frac, 0.0f, 1.0f);
   mPlayhead = position;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGranularNode>();
   mAudioNode->Seek(position);
}

void GranularNode::StartRecording()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGranularNode>();
   mRecording = true;
   mStatus = "recording...";
   mAudioNode->StartRecording();
   AudioTopologyRequest::Request();
}

void GranularNode::StopRecording()
{
   if (!mAudioNode || !mRecording)
      return;

   mRecording = false;
   AudioTopologyRequest::Request();
   mAudioNode->StopRecording();

   const int frames = mAudioNode->RecordedFrames();
   if (frames <= 0)
   {
      mStatus = "recording was empty";
      return;
   }

   const double sr = mAudioNode->RecordSampleRate();
   auto* decoded = new Platform::SampleBuffer();
   decoded->channels = 1;
   decoded->numFrames = frames;
   decoded->sampleRate = sr;
   decoded->channelData.assign(mAudioNode->RecordBufferData(), mAudioNode->RecordBufferData() + frames);

   char stat[128];
   const double durationSec = (double)frames / sr;
   snprintf(stat, sizeof(stat), "1 ch · %.1fkHz · %.2fs (rec)", sr / 1000.0, durationSec);

   FinishBuffer(decoded, "recording", "", stat);
}

bool GranularNode::LoadFile(const std::string& path)
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
   const std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;

   char stat[128];
   const double durationSec = (decoded->sampleRate > 0.0) ? (double)decoded->numFrames / decoded->sampleRate : 0.0;
   snprintf(stat, sizeof(stat), "%d ch · %.1fkHz · %.2fs",
            decoded->channels, decoded->sampleRate / 1000.0, durationSec);

   FinishBuffer(decoded, name, path, stat);
   return true;
}

void GranularNode::ReloadFromPath()
{
   if (!mFilePath.empty())
      LoadFile(mFilePath);
}

void GranularNode::FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                                const std::string& filePath, const std::string& status)
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGranularNode>();

   mFileName = fileName;
   mFilePath = filePath;
   mStatus = status;

   // Build decimated waveform cache for visualization
   if (decoded != nullptr && decoded->numFrames > 0 && !decoded->channelData.empty())
   {
      const int numFrames = decoded->numFrames;
      const int channels = decoded->channels;
      const float* data = decoded->channelData.data();
      waveformCacheCount = std::min(kWaveformCacheSize, numFrames);

      if (waveformCacheCount > 0)
      {
         const int bucketSize = std::max(1, numFrames / waveformCacheCount);
         for (int b = 0; b < waveformCacheCount; ++b)
         {
            const int startFrame = b * bucketSize;
            const int endFrame = std::min(numFrames, (b + 1) * bucketSize);
            float minV = 0.0f;
            float maxV = 0.0f;
            for (int f = startFrame; f < endFrame; ++f)
            {
               float v = data[f];
               if (channels > 1)
                  v = 0.5f * (v + data[f + numFrames]);
               if (v < minV) minV = v;
               if (v > maxV) maxV = v;
            }
            waveformMin[b] = minV;
            waveformMax[b] = maxV;
         }
      }
   }
   else
   {
      waveformCacheCount = 0;
   }

   mAudioNode->PushBuffer(decoded);
}
