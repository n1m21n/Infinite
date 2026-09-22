#include "BeatArrangerNode.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "core/Transport.h"
#include "audio/dsp/SlicerDsp.h"
#include "platform/Platform.h"
#include "core/AudioDecodeCache.h"

namespace
{
   enum ArrangerMailboxParams
   {
      kMasterVolParam = 0,
      kGlobalTransientParam,
      kGlobalDecayParam,
      kGlobalSpeedParam,

      // Per-strip params: 8 strips x 4 params = 32
      kStripVolBase = 10,
      kStripPanBase = 20,
      kStripPitchBase = 30,
      kStripSpeedBase = 40,
   };

   inline int VolParam(int s) { return kStripVolBase + s; }
   inline int PanParam(int s) { return kStripPanBase + s; }
   inline int PitchParam(int s) { return kStripPitchBase + s; }
   inline int SpeedParam(int s) { return kStripSpeedBase + s; }

   struct ArrangedHitList
   {
      std::vector<BeatArranger::ArrangedHit> hits;
   };
}

class AudioBeatArrangerNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = 32;
   static constexpr int kMaxSlicesPerStrip = 64;

   AudioBeatArrangerNode()
   {
      for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
      {
         mStripMute[s].store(false, std::memory_order_relaxed);
         mStripSolo[s].store(false, std::memory_order_relaxed);
         mStripSliceCount[s].store(0, std::memory_order_relaxed);
         mStripSampleRate[s].store(44100.0, std::memory_order_relaxed);
         mStripDecayCoeff[s].store(1.0f, std::memory_order_relaxed);
         mStripAttackInc[s].store(1.0f, std::memory_order_relaxed);
         mStripAttackSamples[s].store(1, std::memory_order_relaxed);
         mStripBoostPeak[s].store(1.0f, std::memory_order_relaxed);
         mStripBoostDecayCoeff[s].store(0.0f, std::memory_order_relaxed);
      }
   }

   ~AudioBeatArrangerNode() override
   {
      mHitSlot.DrainRetired();
      delete mHitSlot.Active();
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);

      mMailbox.SetImmediate(kMasterVolParam, 0.8f);
      mMailbox.SetImmediate(kGlobalTransientParam, 0.0f);
      mMailbox.SetImmediate(kGlobalDecayParam, 0.0f);
      mMailbox.SetImmediate(kGlobalSpeedParam, 1.0f);

      for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
      {
         mMailbox.SetImmediate(VolParam(s), 0.8f);
         mMailbox.SetImmediate(PanParam(s), 0.0f);
         mMailbox.SetImmediate(PitchParam(s), 0.0f);
         mMailbox.SetImmediate(SpeedParam(s), 1.0f);
      }
   }

   // Main thread calls:
   void PushBuffer(int strip, Platform::SampleBuffer* buf)
   {
      if (strip >= 0 && strip < BeatArrangerNode::kNumStrips)
      {
         if (buf != nullptr && buf->sampleRate > 1000.0)
            mStripSampleRate[strip].store(buf->sampleRate, std::memory_order_relaxed);
         mSampleSlots[strip].Push(buf);
      }
   }

   void DrainRetired()
   {
      for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
         mSampleSlots[s].DrainRetired();
      mHitSlot.DrainRetired();
   }

   void PushHitList(ArrangedHitList* list)
   {
      mHitSlot.Push(list);
   }

   void PushStripSlices(int strip, const int* starts, const int* ends, int count)
   {
      if (strip < 0 || strip >= BeatArrangerNode::kNumStrips)
         return;
      const int n = std::clamp(count, 0, kMaxSlicesPerStrip);
      for (int i = 0; i < n; i++)
      {
         mSliceStart[strip][i].store(starts[i], std::memory_order_relaxed);
         mSliceEnd[strip][i].store(ends[i], std::memory_order_relaxed);
      }
      mStripSliceCount[strip].store(n, std::memory_order_release);
   }

   void PushStripEnvelope(int strip, float decayCoeff, float attackInc, int attackSamples,
                          float boostPeak, float boostDecayCoeff)
   {
      if (strip < 0 || strip >= BeatArrangerNode::kNumStrips)
         return;
      mStripDecayCoeff[strip].store(decayCoeff, std::memory_order_relaxed);
      mStripAttackInc[strip].store(attackInc, std::memory_order_relaxed);
      mStripAttackSamples[strip].store(attackSamples, std::memory_order_relaxed);
      mStripBoostPeak[strip].store(boostPeak, std::memory_order_relaxed);
      mStripBoostDecayCoeff[strip].store(boostDecayCoeff, std::memory_order_relaxed);
   }

   void SetStripMute(int strip, bool m) { mStripMute[strip].store(m, std::memory_order_relaxed); }
   void SetStripSolo(int strip, bool s) { mStripSolo[strip].store(s, std::memory_order_relaxed); }
   void SetRate(int r) { mRate.store(r, std::memory_order_relaxed); }
   void SetBars(int b) { mBars.store(b, std::memory_order_relaxed); }
   void SetSwing(float sw) { mSwing.store(sw, std::memory_order_relaxed); }

   void PushGlobalParams(float masterVol, float transient, float decay, float speed)
   {
      mMailbox.Push(kMasterVolParam, masterVol);
      mMailbox.Push(kGlobalTransientParam, transient);
      mMailbox.Push(kGlobalDecayParam, decay);
      mMailbox.Push(kGlobalSpeedParam, speed);
   }

   void PushStripParams(int strip, float vol, float pan, float pitch, float speed)
   {
      mMailbox.Push(VolParam(strip), vol);
      mMailbox.Push(PanParam(strip), pan);
      mMailbox.Push(PitchParam(strip), pitch);
      mMailbox.Push(SpeedParam(strip), speed);
   }

   void GetVisualSnapshot(BeatArrangerVisualSnapshot& out)
   {
      const int rIdx = mVisualReadIdx.load(std::memory_order_acquire);
      out = mVisualSnapshots[rIdx];
   }

   void ProcessBlock(const AudioBuffer* const*, int, AudioBuffer& buffer) override
   {
      AudioBuffer* outputs[1 + BeatArrangerNode::kNumStrips] = {};
      outputs[0] = &buffer;
      ProcessBlockMulti(nullptr, 0, outputs, 1);
   }

   void ProcessBlockMulti(const AudioBuffer* const* inputs, int numInputs,
                          AudioBuffer* const* outputs, int numOutputs) override
   {
      // 1. Swap in buffers and hit list
      for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
      {
         if (mSampleSlots[s].SwapIn())
         {
            // Reset active voices playing this strip
            for (int v = 0; v < kMaxVoices; v++)
            {
               if (mVoices[v].active && mVoices[v].sample == s)
                  mVoices[v].active = false;
            }
         }
      }

      if (mHitSlot.SwapIn())
      {
         mActiveHitList = mHitSlot.Active();
      }

      const int numFrames = outputs[0] ? outputs[0]->numFrames : 0;
      if (numFrames <= 0)
         return;

      for (int o = 0; o < numOutputs; o++)
      {
         if (outputs[o] == nullptr)
            continue;
         for (int ch = 0; ch < outputs[o]->numChannels; ch++)
            std::fill(outputs[o]->channels[ch], outputs[o]->channels[ch] + outputs[o]->numFrames, 0.0f);
      }

      // Step advancement from Transport
      const int rateDiv = mRate.load(std::memory_order_relaxed);
      const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv));
      const int bars = std::clamp(mBars.load(std::memory_order_relaxed), 1, 4);
      const int totalSteps = bars * 16;
      const float swing = std::clamp(mSwing.load(std::memory_order_relaxed), 0.0f, 1.0f);

      const double rawPosNow = Transport::Instance().Beats() / beatsPerStep;

      struct FireEvent
      {
         int frameOffset;
         BeatArranger::ArrangedHit hit;
      };
      FireEvent stepEvts[64];
      int numStepEvts = 0;

      if (rawPosNow < mPrevRawPos)
      {
         mPrevRawPos = rawPosNow;
      }
      else
      {
         const double span = rawPosNow - mPrevRawPos;
         const int kStart = (int)std::floor(mPrevRawPos - 0.5);
         const int kEnd = (int)std::ceil(rawPosNow);

         if (mActiveHitList != nullptr && !mActiveHitList->hits.empty())
         {
            for (int k = kStart; k <= kEnd && numStepEvts < 64; k++)
            {
               const int stepIndex = ((k % totalSteps) + totalSteps) % totalSteps;
               const bool odd = (stepIndex % 2) == 1;
               const double landmark = (double)k + (odd ? (double)swing * 0.5 : 0.0);
               if (landmark > mPrevRawPos && landmark <= rawPosNow)
               {
                  const double frac = span > 1e-9 ? (landmark - mPrevRawPos) / span : 0.0;
                  const int frameOffset = std::clamp((int)(frac * numFrames), 0, std::max(0, numFrames - 1));

                  for (const auto& hit : mActiveHitList->hits)
                  {
                     if (hit.step == stepIndex && numStepEvts < 64)
                     {
                        stepEvts[numStepEvts++] = { frameOffset, hit };
                     }
                  }
               }
            }
         }
         mPrevRawPos = rawPosNow;
      }

      std::sort(stepEvts, stepEvts + numStepEvts,
                [](const FireEvent& a, const FireEvent& b) { return a.frameOffset < b.frameOffset; });

      // Mute / Solo resolution
      bool anySolo = false;
      bool stripAudible[BeatArrangerNode::kNumStrips];
      for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
      {
         if (mStripSolo[s].load(std::memory_order_relaxed))
            anySolo = true;
      }
      for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
      {
         const bool solo = mStripSolo[s].load(std::memory_order_relaxed);
         const bool mute = mStripMute[s].load(std::memory_order_relaxed);
         stripAudible[s] = anySolo ? solo : !mute;
      }

      int stepIdx = 0;
      for (int i = 0; i < numFrames; i++)
      {
         float stripVolNow[BeatArrangerNode::kNumStrips];
         float stripPanNow[BeatArrangerNode::kNumStrips];
         float stripPitchNow[BeatArrangerNode::kNumStrips];
         float stripSpeedNow[BeatArrangerNode::kNumStrips];

         for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
         {
            stripVolNow[s] = mMailbox.SmoothedValue(VolParam(s));
            stripPanNow[s] = mMailbox.SmoothedValue(PanParam(s));
            stripPitchNow[s] = mMailbox.SmoothedValue(PitchParam(s));
            stripSpeedNow[s] = mMailbox.SmoothedValue(SpeedParam(s));
         }

         const float masterVolNow = mMailbox.SmoothedValue(kMasterVolParam);
         const float globalSpeedNow = mMailbox.SmoothedValue(kGlobalSpeedParam);

         while (stepIdx < numStepEvts && stepEvts[stepIdx].frameOffset <= i)
         {
            TriggerHit(stepEvts[stepIdx].hit, stripVolNow, stripPanNow, stripPitchNow,
                       stripSpeedNow, globalSpeedNow);
            stepIdx++;
         }

         float sampleL = 0.0f, sampleR = 0.0f;
         float stripL[BeatArrangerNode::kNumStrips] = {};
         float stripR[BeatArrangerNode::kNumStrips] = {};

         for (int v = 0; v < kMaxVoices; v++)
         {
            Voice& voice = mVoices[v];
            if (!voice.active)
               continue;

            const int strip = voice.sample;
            if (voice.buffer == nullptr || !stripAudible[strip])
            {
               voice.active = false;
               continue;
            }

            float ampAttack = 1.0f;
            if (voice.attackRemaining > 0)
            {
               voice.attackLevel += voice.attackInc;
               voice.attackRemaining--;
               ampAttack = std::min(1.0f, voice.attackLevel);
            }

            voice.boostEnv = 1.0f + (voice.boostEnv - 1.0f) * voice.boostDecayCoeff;
            voice.decayAmp *= voice.decayCoeff;

            const float totalAmp = ampAttack * voice.boostEnv * voice.decayAmp * voice.velocity;
            const float s = ReadSample(*voice.buffer, voice.readPos) * totalAmp;
            const float vL = s * voice.panL;
            const float vR = s * voice.panR;

            sampleL += vL;
            sampleR += vR;
            stripL[strip] += vL;
            stripR[strip] += vR;

            voice.readPos += voice.rate;
            if (voice.readPos >= voice.endFrame - 1.0 || (voice.decayCoeff < 1.0f && totalAmp < 1e-4f))
            {
               voice.active = false;
            }
         }

         if (outputs[0] != nullptr)
         {
            if (outputs[0]->numChannels > 0)
               outputs[0]->channels[0][i] = sampleL * masterVolNow;
            if (outputs[0]->numChannels > 1)
               outputs[0]->channels[1][i] = sampleR * masterVolNow;
         }

         for (int s = 0; s < BeatArrangerNode::kNumStrips; s++)
         {
            const int outIdx = 1 + s;
            if (outIdx < numOutputs && outputs[outIdx] != nullptr)
            {
               if (outputs[outIdx]->numChannels > 0)
                  outputs[outIdx]->channels[0][i] = stripL[s];
               if (outputs[outIdx]->numChannels > 1)
                  outputs[outIdx]->channels[1][i] = stripR[s];
            }
         }
      }

      // Publish visual snapshot
      PublishVisualSnapshot(rawPosNow, totalSteps);
   }

   double mSampleRate = 44100.0;

private:
   struct Voice
   {
      const Platform::SampleBuffer* buffer = nullptr;
      int sample = 0;
      int slice = 0;
      double readPos = 0.0;
      double startFrame = 0.0;
      double endFrame = 0.0;
      float rate = 1.0f;
      float velocity = 0.0f;
      float panL = 1.0f, panR = 1.0f;
      float attackLevel = 0.0f;
      float attackInc = 1.0f;
      int attackRemaining = 0;
      float boostEnv = 1.0f;
      float boostDecayCoeff = 0.0f;
      float decayAmp = 1.0f;
      float decayCoeff = 1.0f;
      bool active = false;
      bool isClosedHat = false;
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

   void TriggerHit(const BeatArranger::ArrangedHit& hit,
                   const float* stripVolNow, const float* stripPanNow,
                   const float* stripPitchNow, const float* stripSpeedNow,
                   float globalSpeedNow)
   {
      const int strip = std::clamp(hit.sample, 0, BeatArrangerNode::kNumStrips - 1);
      const Platform::SampleBuffer* buf = mSampleSlots[strip].Active();
      if (buf == nullptr || buf->numFrames <= 0)
         return;

      // Choke closed hats if triggering open hat
      const int sliceCount = mStripSliceCount[strip].load(std::memory_order_acquire);
      int startFrame = 0;
      int endFrame = buf->numFrames;
      if (sliceCount > 0 && hit.slice >= 0 && hit.slice < sliceCount)
      {
         startFrame = mSliceStart[strip][hit.slice].load(std::memory_order_relaxed);
         endFrame = mSliceEnd[strip][hit.slice].load(std::memory_order_relaxed);
      }
      startFrame = std::clamp(startFrame, 0, buf->numFrames - 1);
      endFrame = std::clamp(endFrame, startFrame + 1, buf->numFrames);

      // Voice allocation: find inactive or oldest voice
      int bestVoice = -1;
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (!mVoices[v].active)
         {
            bestVoice = v;
            break;
         }
      }
      if (bestVoice < 0)
      {
         bestVoice = mVoiceCursor;
         mVoiceCursor = (mVoiceCursor + 1) % kMaxVoices;
      }

      Voice& v = mVoices[bestVoice];
      v.buffer = buf;
      v.sample = strip;
      v.slice = hit.slice;
      v.startFrame = (double)startFrame;
      v.endFrame = (double)endFrame;
      v.readPos = (double)startFrame;

      const float totalPitch = stripPitchNow[strip] + hit.pitchOffsetSemis;
      const float sampleRateRatio = (float)(mStripSampleRate[strip].load(std::memory_order_relaxed) / mSampleRate);
      const float effSpeed = std::clamp(stripSpeedNow[strip] * globalSpeedNow, 0.1f, 8.0f);
      v.rate = std::pow(2.0f, totalPitch / 12.0f) * effSpeed * sampleRateRatio;
      v.velocity = hit.velocity * stripVolNow[strip];
      DspMath::EqualPowerPan(stripPanNow[strip], v.panL, v.panR);

      v.attackLevel = 0.0f;
      v.attackInc = mStripAttackInc[strip].load(std::memory_order_relaxed);
      v.attackRemaining = mStripAttackSamples[strip].load(std::memory_order_relaxed);
      v.boostEnv = mStripBoostPeak[strip].load(std::memory_order_relaxed);
      v.boostDecayCoeff = mStripBoostDecayCoeff[strip].load(std::memory_order_relaxed);
      v.decayAmp = 1.0f;
      v.decayCoeff = mStripDecayCoeff[strip].load(std::memory_order_relaxed);
      v.active = true;
   }

   void PublishVisualSnapshot(double rawPosNow, int totalSteps)
   {
      const int wIdx = (mVisualWriteIdx.load(std::memory_order_relaxed) + 1) % 3;
      BeatArrangerVisualSnapshot& snap = mVisualSnapshots[wIdx];

      const double posInPattern = std::fmod(std::fmod(rawPosNow, (double)totalSteps) + (double)totalSteps, (double)totalSteps);
      snap.playheadStep = (float)posInPattern;
      snap.totalSteps = totalSteps;

      int count = 0;
      for (int v = 0; v < kMaxVoices && count < BeatArrangerVisualSnapshot::kMaxVisualVoices; v++)
      {
         if (mVoices[v].active && mVoices[v].buffer != nullptr && mVoices[v].buffer->numFrames > 0)
         {
            snap.voices[count].sample = mVoices[v].sample;
            snap.voices[count].slice = mVoices[v].slice;
            snap.voices[count].position = (float)(mVoices[v].readPos / (double)mVoices[v].buffer->numFrames);
            snap.voices[count].amp = mVoices[v].decayAmp * mVoices[v].velocity;
            count++;
         }
      }
      snap.voiceCount = count;

      mVisualWriteIdx.store(wIdx, std::memory_order_release);
      mVisualReadIdx.store(wIdx, std::memory_order_release);
   }

   ParamMailbox mMailbox;
   SampleSlot mSampleSlots[BeatArrangerNode::kNumStrips];
   SampleSlotT<ArrangedHitList> mHitSlot;
   ArrangedHitList* mActiveHitList = nullptr;

   Voice mVoices[kMaxVoices];
   int mVoiceCursor = 0;
   double mPrevRawPos = 0.0;

   std::atomic<int> mRate { 12 };
   std::atomic<int> mBars { 2 };
   std::atomic<float> mSwing { 0.0f };

   std::atomic<bool> mStripMute[BeatArrangerNode::kNumStrips];
   std::atomic<bool> mStripSolo[BeatArrangerNode::kNumStrips];
   std::atomic<int> mStripSliceCount[BeatArrangerNode::kNumStrips];
   std::atomic<double> mStripSampleRate[BeatArrangerNode::kNumStrips];

   std::atomic<int> mSliceStart[BeatArrangerNode::kNumStrips][kMaxSlicesPerStrip];
   std::atomic<int> mSliceEnd[BeatArrangerNode::kNumStrips][kMaxSlicesPerStrip];

   std::atomic<float> mStripDecayCoeff[BeatArrangerNode::kNumStrips];
   std::atomic<float> mStripAttackInc[BeatArrangerNode::kNumStrips];
   std::atomic<int> mStripAttackSamples[BeatArrangerNode::kNumStrips];
   std::atomic<float> mStripBoostPeak[BeatArrangerNode::kNumStrips];
   std::atomic<float> mStripBoostDecayCoeff[BeatArrangerNode::kNumStrips];

   BeatArrangerVisualSnapshot mVisualSnapshots[3];
   std::atomic<int> mVisualWriteIdx { 0 };
   std::atomic<int> mVisualReadIdx { 0 };
};

// =========================================================================
// BeatArrangerNode Implementation
// =========================================================================

BeatArrangerNode::BeatArrangerNode()
   : mAudioNode(std::make_unique<AudioBeatArrangerNode>())
{
   for (int s = 0; s < kNumStrips; s++)
   {
      stripVolume[s] = 0.8f;
      stripPan[s] = 0.0f;
      stripPitch[s] = 0.0f;
      stripFineTune[s] = 0.0f;
      stripSpeed[s] = 1.0f;
      stripTransient[s] = 0.0f;
      stripDecay[s] = 0.0f;
      stripMute[s] = false;
      stripSolo[s] = false;
      stripClassOverride[s] = 0; // Auto

      mLastStripVolume[s] = -999.0f;
      mLastStripPan[s] = -999.0f;
      mLastStripPitch[s] = -999.0f;
      mLastStripFineTune[s] = -999.0f;
      mLastStripSpeed[s] = -999.0f;
      mLastStripTransient[s] = -999.0f;
      mLastStripDecay[s] = -999.0f;
   }
}

BeatArrangerNode::~BeatArrangerNode()
{
   for (int s = 0; s < kNumStrips; s++)
   {
      mWorkerAbort[s].store(true, std::memory_order_relaxed);
      if (mWorkerThreads[s] && mWorkerThreads[s]->joinable())
         mWorkerThreads[s]->join();
      delete mLoadedBuffers[s];
      mLoadedBuffers[s] = nullptr;
   }
}

AudioNode* BeatArrangerNode::GetAudioNode()
{
   return mAudioNode.get();
}

int BeatArrangerNode::LoadedStripCount() const
{
   int count = 0;
   for (int s = 0; s < kNumStrips; s++)
   {
      if (!stripFileName[s].empty())
         count++;
   }
   return count;
}

void BeatArrangerNode::JoinWorkerIfDone(int strip)
{
   if (strip < 0 || strip >= kNumStrips)
      return;
   if (mWorkerThreads[strip] && mWorkerDone[strip].load(std::memory_order_acquire))
   {
      if (mWorkerThreads[strip]->joinable())
         mWorkerThreads[strip]->join();
      mWorkerThreads[strip].reset();

      // Adopt worker results
      stripOnsets[strip] = std::move(mWorkerResults[strip].onsets);
      stripSlices[strip] = std::move(mWorkerResults[strip].slices);

      // Push slice ranges to audio node
      if (mAudioNode && !stripOnsets[strip].empty() && mLoadedBuffers[strip])
      {
         const int count = (int)stripOnsets[strip].size();
         const int totalFrames = mLoadedBuffers[strip]->numFrames;
         std::vector<int> starts(count), ends(count);
         for (int i = 0; i < count; i++)
         {
            starts[i] = stripOnsets[strip][i];
            ends[i] = (i + 1 < count) ? stripOnsets[strip][i + 1] : totalFrames;
         }
         mAudioNode->PushStripSlices(strip, starts.data(), ends.data(), count);
      }

      // Format status line
      const int numSlices = (int)stripSlices[strip].size();
      if (numSlices > 0)
      {
         const char* topCls = DrumClassifier::ClassName(stripSlices[strip][0].cls);
         char buf[64];
         snprintf(buf, sizeof(buf), "%d slice%s (%s)", numSlices, numSlices > 1 ? "s" : "", topCls);
         stripStatus[strip] = buf;
      }
      else
      {
         stripStatus[strip] = "ready";
      }

      mWorkerDone[strip].store(false, std::memory_order_relaxed);
   }
}

void BeatArrangerNode::FinishStripBuffer(int strip, Platform::SampleBuffer* buf)
{
   if (strip < 0 || strip >= kNumStrips || buf == nullptr)
      return;

   // Compute waveform cache for thumbnail
   const int numFrames = buf->numFrames;
   stripWaveCount[strip] = kWaveCache;
   const int step = std::max(1, numFrames / kWaveCache);
   for (int i = 0; i < kWaveCache; i++)
   {
      const int start = i * step;
      const int end = std::min(numFrames, start + step);
      float minVal = 0.0f, maxVal = 0.0f;
      for (int j = start; j < end; j++)
      {
         const float s = buf->channelData[j];
         minVal = std::min(minVal, s);
         maxVal = std::max(maxVal, s);
      }
      stripWaveMin[strip][i] = minVal;
      stripWaveMax[strip][i] = maxVal;
   }
   stripSampleLenSec[strip] = (double)numFrames / std::max(1.0, buf->sampleRate);
}

bool BeatArrangerNode::LoadFileToStrip(int strip, const std::string& path)
{
   strip = ClampStrip(strip);
   if (path.empty())
      return false;

   // Wait for any existing worker on this strip
   mWorkerAbort[strip].store(true, std::memory_order_relaxed);
   if (mWorkerThreads[strip] && mWorkerThreads[strip]->joinable())
      mWorkerThreads[strip]->join();
   mWorkerThreads[strip].reset();
   mWorkerAbort[strip].store(false, std::memory_order_relaxed);
   mWorkerDone[strip].store(false, std::memory_order_relaxed);

   auto* decoded = new Platform::SampleBuffer();
   std::string decodeErr;
   if (!AudioDecodeCache::DecodeCached(path, *decoded, decodeErr) || decoded->numFrames <= 0)
   {
      delete decoded;
      stripStatus[strip] = decodeErr.empty() ? "decode failed" : decodeErr;
      return false;
   }

   delete mLoadedBuffers[strip];
   mLoadedBuffers[strip] = decoded;
   stripFilePath[strip] = path;

   // Extract filename
   const size_t slash = path.find_last_of("/\\");
   stripFileName[strip] = (slash == std::string::npos) ? path : path.substr(slash + 1);
   stripStatus[strip] = "analyzing...";

   FinishStripBuffer(strip, decoded);

   // Pass duplicate buffer to audio node
   if (mAudioNode)
   {
      auto* audioCopy = new Platform::SampleBuffer();
      audioCopy->numFrames = decoded->numFrames;
      audioCopy->sampleRate = decoded->sampleRate;
      audioCopy->channelData.resize(decoded->channelData.size());
      std::memcpy(audioCopy->channelData.data(), decoded->channelData.data(),
                  decoded->channelData.size() * sizeof(float));
      mAudioNode->PushBuffer(strip, audioCopy);
   }

   // Launch worker thread for transient detection & slice classification
   const std::string fnHint = stripFileName[strip];
   mWorkerThreads[strip] = std::make_unique<std::thread>([this, strip, decoded, fnHint]() {
      WorkerResult res;
      SlicerDsp::Params sp;
      sp.sensitivity = 65.0f;
      sp.minIoiSeconds = 0.028;
      sp.silenceGateDb = -65.0f;

      std::vector<float> strengths;
      SlicerDsp::Detect(decoded->channelData.data(), decoded->numFrames, decoded->sampleRate,
                        sp, res.onsets, strengths, &mWorkerAbort[strip]);

      if (res.onsets.empty())
         res.onsets.push_back(0);

      const int numOnsets = (int)res.onsets.size();
      res.slices.resize(numOnsets);

      for (int i = 0; i < numOnsets; i++)
      {
         if (mWorkerAbort[strip].load(std::memory_order_relaxed))
            return;

         const int start = res.onsets[i];
         const int end = (i + 1 < numOnsets) ? res.onsets[i + 1] : decoded->numFrames;
         const int sliceLen = std::max(1, end - start);
         const float* sliceData = decoded->channelData.data() + start;

         // Filename prior applies only if sample yields 1 slice (prompt §2a)
         const char* hint = (numOnsets == 1) ? fnHint.c_str() : nullptr;
         res.slices[i] = DrumClassifier::Classify(sliceData, sliceLen, decoded->sampleRate, hint);
      }

      mWorkerResults[strip] = std::move(res);
      mWorkerDone[strip].store(true, std::memory_order_release);
   });

   return true;
}

void BeatArrangerNode::ReloadFromPaths()
{
   for (int s = 0; s < kNumStrips; s++)
   {
      if (!stripFilePath[s].empty())
         LoadFileToStrip(s, stripFilePath[s]);
   }
}

void BeatArrangerNode::ClearStrip(int strip)
{
   strip = ClampStrip(strip);
   mWorkerAbort[strip].store(true, std::memory_order_relaxed);
   if (mWorkerThreads[strip] && mWorkerThreads[strip]->joinable())
      mWorkerThreads[strip]->join();
   mWorkerThreads[strip].reset();

   delete mLoadedBuffers[strip];
   mLoadedBuffers[strip] = nullptr;
   stripFilePath[strip].clear();
   stripFileName[strip].clear();
   stripStatus[strip].clear();
   stripOnsets[strip].clear();
   stripSlices[strip].clear();
   stripWaveCount[strip] = 0;

   if (mAudioNode)
      mAudioNode->PushBuffer(strip, nullptr);
}

void BeatArrangerNode::Arrange()
{
   std::vector<BeatArranger::SliceInfo> pool;
   for (int s = 0; s < kNumStrips; s++)
   {
      if (mLoadedBuffers[s] == nullptr)
         continue;

      const int numSlices = (int)stripSlices[s].size();
      if (numSlices == 0)
      {
         // Sample without slices: treat whole buffer as 1 slice
         BeatArranger::SliceInfo info;
         info.sample = s;
         info.slice = 0;
         info.cls = (stripClassOverride[s] > 0)
            ? (DrumClassifier::DrumClass)(stripClassOverride[s] - 1)
            : DrumClassifier::DrumClass::Perc;
         info.confidence = 1.0f;
         info.lenSec = (float)stripSampleLenSec[s];
         pool.push_back(info);
      }
      else
      {
         for (int i = 0; i < numSlices; i++)
         {
            BeatArranger::SliceInfo info;
            info.sample = s;
            info.slice = i;
            info.cls = (stripClassOverride[s] > 0)
               ? (DrumClassifier::DrumClass)(stripClassOverride[s] - 1)
               : stripSlices[s][i].cls;
            info.confidence = stripSlices[s][i].confidence;
            const int start = stripOnsets[s][i];
            const int end = (i + 1 < numSlices) ? stripOnsets[s][i + 1] : mLoadedBuffers[s]->numFrames;
            info.lenSec = (float)(end - start) / (float)std::max(1.0, mLoadedBuffers[s]->sampleRate);
            pool.push_back(info);
         }
      }
   }

   BeatArranger::ArrangeParams params;
   params.bars = (bars == 1 || bars == 4) ? bars : 2;
   params.randPitch = randPitch;
   params.swing = swing;

   mArrangedHits = BeatArranger::Arrange(pool, params, (uint32_t)seed);
   mHitBlob = BeatArranger::SerializeHits(mArrangedHits);

   // Hand over hits to audio thread
   if (mAudioNode)
   {
      auto* hitList = new ArrangedHitList();
      hitList->hits = mArrangedHits;
      mAudioNode->PushHitList(hitList);
   }
}

void BeatArrangerNode::ReArrange()
{
   seed = (seed + 1) % 10000;
   Arrange();
}

void BeatArrangerNode::ClearGroove()
{
   mArrangedHits.clear();
   mHitBlob.clear();
   if (mAudioNode)
   {
      auto* hitList = new ArrangedHitList();
      mAudioNode->PushHitList(hitList);
   }
}

void BeatArrangerNode::CookIfNeeded(int /*frameId*/)
{
   // Check background worker threads
   for (int s = 0; s < kNumStrips; s++)
      JoinWorkerIfDone(s);

   if (!mAudioNode)
      return;

   mAudioNode->DrainRetired();

   // Push global continuous params
   if (mFirstCook || volume != mLastVolume || globalTransient != mLastGlobalTransient ||
       globalDecay != mLastGlobalDecay || globalSpeed != mLastGlobalSpeed)
   {
      mAudioNode->PushGlobalParams(volume, globalTransient, globalDecay, globalSpeed);
      mLastVolume = volume;
      mLastGlobalTransient = globalTransient;
      mLastGlobalDecay = globalDecay;
      mLastGlobalSpeed = globalSpeed;
   }

   if (mFirstCook || rate != mLastRate)
   {
      mAudioNode->SetRate(rate);
      mLastRate = rate;
   }
   if (mFirstCook || bars != mLastBars)
   {
      mAudioNode->SetBars(bars);
      mLastBars = bars;
   }
   if (mFirstCook || swing != mLastSwing)
   {
      mAudioNode->SetSwing(swing);
      mLastSwing = swing;
   }

   // Push per-strip params and envelope calculations
   for (int s = 0; s < kNumStrips; s++)
   {
      if (mFirstCook || stripVolume[s] != mLastStripVolume[s] || stripPan[s] != mLastStripPan[s] ||
          stripPitch[s] != mLastStripPitch[s] || stripFineTune[s] != mLastStripFineTune[s] ||
          stripSpeed[s] != mLastStripSpeed[s])
      {
         const float pitchTotal = stripPitch[s] + stripFineTune[s] * 0.01f;
         mAudioNode->PushStripParams(s, stripVolume[s], stripPan[s], pitchTotal, stripSpeed[s]);
         mLastStripVolume[s] = stripVolume[s];
         mLastStripPan[s] = stripPan[s];
         mLastStripPitch[s] = stripPitch[s];
         mLastStripFineTune[s] = stripFineTune[s];
         mLastStripSpeed[s] = stripSpeed[s];
      }

      if (mFirstCook || stripTransient[s] != mLastStripTransient[s] || stripDecay[s] != mLastStripDecay[s] ||
          globalTransient != mLastGlobalTransient || globalDecay != mLastGlobalDecay)
      {
         const float effDecay = std::clamp(stripDecay[s] + globalDecay, -1.0f, 1.0f);
         const float effTransient = std::clamp(stripTransient[s] + globalTransient, -1.0f, 1.0f);

         float decayCoeff = 1.0f;
         if (effDecay < 0.999f)
         {
            const double decayNorm = (double)(effDecay * 0.5f + 0.5f); // 0..1 throw
            const double lenSec = std::max(0.05, stripSampleLenSec[s]);
            const double timeConstSec = std::max(0.005, decayNorm * lenSec);
            decayCoeff = std::exp((float)(-1.0 / (timeConstSec * mAudioNode->mSampleRate)));
         }

         const float attackMs = effTransient >= 0.0f ? (3.0f + (0.5f - 3.0f) * effTransient)
                                                     : (3.0f + (40.0f - 3.0f) * -effTransient);
         const double sr = mAudioNode->mSampleRate;
         const int attackSamples = std::max(1, (int)(attackMs * 0.001 * sr));
         const float attackInc = 1.0f / (float)attackSamples;
         const float boostDb = effTransient > 0.0f ? effTransient * 4.0f : 0.0f;
         const float boostPeak = DspMath::DbToLinear(boostDb);
         const double boostDecaySec = 0.015;
         const float boostDecayCoeff = std::exp((float)(-1.0 / (boostDecaySec * sr)));

         mAudioNode->PushStripEnvelope(s, decayCoeff, attackInc, attackSamples, boostPeak, boostDecayCoeff);
         mLastStripTransient[s] = stripTransient[s];
         mLastStripDecay[s] = stripDecay[s];
      }

      mAudioNode->SetStripMute(s, stripMute[s]);
      mAudioNode->SetStripSolo(s, stripSolo[s]);
   }

   mFirstCook = false;
   mAudioNode->GetVisualSnapshot(mVisualSnapshot);
}

void BeatArrangerNode::VisitParams(ParamVisitor& v)
{
   v.Int("seed", seed);
   v.Float("randPitch", randPitch);
   v.Float("globalTransient", globalTransient);
   v.Float("globalDecay", globalDecay);
   v.Float("globalSpeed", globalSpeed);
   v.Int("bars", bars);
   v.Int("rate", rate);
   v.Float("swing", swing);
   v.Float("volume", volume);

   char name[64];
   for (int s = 0; s < kNumStrips; s++)
   {
      snprintf(name, sizeof(name), "strip%d_path", s + 1);
      v.Text(name, stripFilePath[s]);
      snprintf(name, sizeof(name), "strip%d_volume", s + 1);
      v.Float(name, stripVolume[s]);
      snprintf(name, sizeof(name), "strip%d_pan", s + 1);
      v.Float(name, stripPan[s]);
      snprintf(name, sizeof(name), "strip%d_pitch", s + 1);
      v.Float(name, stripPitch[s]);
      snprintf(name, sizeof(name), "strip%d_fine", s + 1);
      v.Float(name, stripFineTune[s]);
      snprintf(name, sizeof(name), "strip%d_speed", s + 1);
      v.Float(name, stripSpeed[s]);
      snprintf(name, sizeof(name), "strip%d_transient", s + 1);
      v.Float(name, stripTransient[s]);
      snprintf(name, sizeof(name), "strip%d_decay", s + 1);
      v.Float(name, stripDecay[s]);
      snprintf(name, sizeof(name), "strip%d_mute", s + 1);
      v.Bool(name, stripMute[s]);
      snprintf(name, sizeof(name), "strip%d_solo", s + 1);
      v.Bool(name, stripSolo[s]);
      snprintf(name, sizeof(name), "strip%d_class", s + 1);
      v.Int(name, stripClassOverride[s]);
   }

   v.Text("hitBlob", mHitBlob);
   if (!mHitBlob.empty() && mArrangedHits.empty())
   {
      mArrangedHits = BeatArranger::DeserializeHits(mHitBlob);
      if (mAudioNode && !mArrangedHits.empty())
      {
         auto* hitList = new ArrangedHitList();
         hitList->hits = mArrangedHits;
         mAudioNode->PushHitList(hitList);
      }
   }
}
