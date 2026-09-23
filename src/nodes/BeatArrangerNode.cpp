#include "BeatArrangerNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "audio/dsp/SlicerDsp.h"
#include "core/AudioDecodeCache.h"
#include "core/AudioTopologyRequest.h"
#include "core/Transport.h"
#include "platform/Platform.h"

namespace
{
   // Mailbox params: continuous, audibly-live knobs only. swing is a
   // scheduling-time decision (affects WHEN a step lands, not a per-sample
   // signal), and transient/decay are captured once per hit at trigger time
   // (see TriggerStep) from plain atomics - stepping those doesn't need a
   // click-free ramp since it only shapes *new* hits, matching
   // DrumSequencerNode's decay/transient convention.
   constexpr int kSpeedParam = 0;
   constexpr int kOutputParam = 1;
   constexpr int kRandPitchParam = 2;

   constexpr float kFadeInMs = 2.0f;
   constexpr float kFadeOutMs = 3.0f;
   constexpr float kStealFadeMs = 2.0f;
   constexpr int kNumVoices = 8; // KHS-simple polyphony, steal oldest
   constexpr int kNumGhosts = 4;
   constexpr int kMaxHits = 512;

   inline float RaisedCosine(float x01)
   {
      const float x = std::clamp(x01, 0.0f, 1.0f);
      return 0.5f * (1.0f - std::cos(3.14159265358979323846f * x));
   }

   // Fix A3: pitch range keyed on the SLICE's own class, never the role it
   // was placed into.
   float PitchRangeSemisFor(DrumClassifier::DrumClass c)
   {
      return (c == DrumClassifier::DrumClass::Kick || c == DrumClassifier::DrumClass::Bass) ? 3.0f : 12.0f;
   }
}

const char* const BeatArrangerNode::kTimeSigNames[BeatArrangerNode::kNumTimeSigs] = {
   "transport", "4/4", "3/4", "6/8", "5/4", "7/8"
};

namespace
{
   void TimeSigFor(int index, int& outNum, int& outDen)
   {
      switch (index)
      {
         case 0: outNum = Transport::Instance().TimeSigNumerator(); outDen = Transport::Instance().TimeSigDenominator(); return;
         case 1: outNum = 4; outDen = 4; return;
         case 2: outNum = 3; outDen = 4; return;
         case 3: outNum = 6; outDen = 8; return;
         case 4: outNum = 5; outDen = 4; return;
         case 5: outNum = 7; outDen = 8; return;
         default: outNum = 4; outDen = 4; return;
      }
   }

   // Steps per bar at a 1/16-note grid resolution.
   int StepsPerBarFor(int num, int den)
   {
      const double steps = (double)num * (16.0 / (double)std::max(1, den));
      return std::clamp((int)std::lround(steps), 1, 64);
   }
}

// ------------------------------------------------------------- audio thread
class AudioBeatArrangerNode : public AudioNode
{
public:
   AudioBeatArrangerNode()
   {
      for (int i = 0; i <= BeatArrangerNode::kMaxSlices; i++)
         mSliceStart[i].store(i == 0 ? 0.0f : 1.0f, std::memory_order_relaxed);
      mSliceCount.store(0, std::memory_order_relaxed);
      for (int i = 0; i < BeatArrangerNode::kMaxSlices; i++)
         mSliceClass[i].store((int)DrumClassifier::DrumClass::Perc, std::memory_order_relaxed);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      // Fix Section7#4: seed from the atomics (last main-thread-pushed
      // value), never a hardcoded default - PrepareToPlay can land between
      // two cooks (device change, sample-rate change) and must not forget
      // whatever the user last set.
      mMailbox.SetImmediate(kSpeedParam, mSpeed.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kOutputParam, mOutput.load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kRandPitchParam, mRandPitch.load(std::memory_order_relaxed));
      Reset();
   }

   // Fix Section7#5: resyncs scheduling to Transport's CURRENT position
   // rather than wherever ProcessBlock next happens to look, and clears
   // playing voices - otherwise a rewire/undo replays every hit since
   // whatever mPrevStepPos was left at.
   void Reset() override
   {
      for (auto& v : mVoices)
         v = Voice();
      for (auto& g : mGhosts)
         g = Ghost();
      const double stepBeats = std::max(1e-6, (double)mStepBeats.load(std::memory_order_relaxed));
      mPrevStepPos = Transport::Instance().Beats() / stepBeats - 1e-6;
   }

   void PushBuffer(Platform::SampleBuffer* buf) { mSampleSlot.Push(buf); }
   void DrainRetired() { mSampleSlot.DrainRetired(); }

   void PushParams(float speed, float output, float randPitch, float swing, float transient, float decay)
   {
      mSpeed.store(speed, std::memory_order_relaxed);
      mOutput.store(output, std::memory_order_relaxed);
      mRandPitch.store(randPitch, std::memory_order_relaxed);
      mSwing.store(swing, std::memory_order_relaxed);
      mTransient.store(transient, std::memory_order_relaxed);
      mDecay.store(decay, std::memory_order_relaxed);
      mMailbox.Push(kSpeedParam, speed);
      mMailbox.Push(kOutputParam, output);
      mMailbox.Push(kRandPitchParam, randPitch);
   }

   // Main thread. Fix Section7#7: called every time a fresh analysis lands,
   // BEFORE PushHits, so a stale slice list never outlives its hit list by
   // even one block.
   void PushSlices(const float* starts, int count, const int* classes)
   {
      const int n = std::clamp(count, 0, BeatArrangerNode::kMaxSlices);
      for (int i = 0; i < n; i++)
      {
         mSliceStart[i].store(std::clamp(starts[i], 0.0f, 1.0f), std::memory_order_relaxed);
         mSliceClass[i].store(classes[i], std::memory_order_relaxed);
      }
      mSliceStart[n].store(1.0f, std::memory_order_relaxed);
      mSliceCount.store(n, std::memory_order_release);
   }

   struct HitPod
   {
      int step = 0;
      int slice = 0;
      float velocity = 0.0f;
      float pitchRand = 0.0f;
   };

   // Main thread. `stepBeats`/`totalSteps` describe the pattern these hits
   // were generated against; published alongside so a change to timeSig
   // between Generate() calls can never desync the audio thread's scan.
   void PushHits(const HitPod* hits, int count, double stepBeats, int totalSteps)
   {
      const int n = std::clamp(count, 0, kMaxHits);
      for (int i = 0; i < n; i++)
      {
         mHits[i].step.store(hits[i].step, std::memory_order_relaxed);
         mHits[i].slice.store(hits[i].slice, std::memory_order_relaxed);
         mHits[i].velocity.store(hits[i].velocity, std::memory_order_relaxed);
         mHits[i].pitchRand.store(hits[i].pitchRand, std::memory_order_relaxed);
      }
      mStepBeats.store((float)stepBeats, std::memory_order_relaxed);
      mTotalSteps.store(totalSteps, std::memory_order_relaxed);
      mHitCount.store(n, std::memory_order_release);
   }

   void GetVisualSnapshot(BeatArrangerVisualSnapshot& out)
   {
      const int rIdx = mVisualReadIdx.load(std::memory_order_acquire);
      out = mVisualSnapshots[rIdx];
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
   {
      // Adopt a newly loaded buffer only at the top of the block.
      if (mSampleSlot.SwapIn())
      {
         mActiveBuffer = mSampleSlot.Active();
         for (auto& v : mVoices)
            v.active = false;
         for (auto& g : mGhosts)
            g.active = false;
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      const int hitCount = mHitCount.load(std::memory_order_acquire);
      const int totalSteps = std::max(1, mTotalSteps.load(std::memory_order_relaxed));
      const double stepBeats = std::max(1e-6, (double)mStepBeats.load(std::memory_order_relaxed));

      if (mActiveBuffer == nullptr || mActiveBuffer->numFrames <= 0 || hitCount <= 0)
      {
         PublishSnapshot(totalSteps, 0.0f);
         return;
      }

      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      const float swing = mSwing.load(std::memory_order_relaxed);
      const int numFrames = mActiveBuffer->numFrames;
      const double srRatio = (mActiveBuffer->sampleRate > 0.0) ? (mActiveBuffer->sampleRate / mSampleRate) : 1.0;

      // Envelope shaping keyed off the slice's OWN measured length at
      // trigger time (fix Section7#2), never a hardcoded 0/0.05s fallback.
      const float transient = mTransient.load(std::memory_order_relaxed);
      const float decayParam = mDecay.load(std::memory_order_relaxed);

      for (int i = 0; i < buffer.numFrames; i++)
      {
         const double curBeats = Transport::Instance().BlockStartBeats() + (double)i / mSampleRate * (bpm / 60.0);
         const double curStepPos = curBeats / stepBeats;

         // Sweep every step landmark strictly between the previous scan
         // position and now, honouring swing's delay on odd (off-beat)
         // steps so a hit isn't missed when swing pushes it later than a
         // naive single-landmark check would find.
         while (true)
         {
            const double nextLandmark = std::floor(mPrevStepPos) + 1.0;
            if (nextLandmark > curStepPos)
               break;
            const int stepIdx = ((int)std::llround(nextLandmark)) % totalSteps;
            const bool odd = (stepIdx % 2) != 0;
            const double swungLandmark = odd ? (nextLandmark + swing * 0.33) : nextLandmark;
            if (swungLandmark <= curStepPos)
               TriggerStep(stepIdx, hitCount, transient, decayParam);
            mPrevStepPos = nextLandmark;
         }

         const float speed = mMailbox.SmoothedValue(kSpeedParam);
         const float output = mMailbox.SmoothedValue(kOutputParam);
         const double rate = (double)speed * srRatio;

         float sample = 0.0f;
         for (auto& vo : mVoices)
         {
            if (!vo.active)
               continue;
            float g = vo.velocity;
            if (vo.fadeInLeft > 0)
            {
               g *= RaisedCosine(1.0f - (float)vo.fadeInLeft / (float)vo.fadeInTotal);
               vo.fadeInLeft--;
            }
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
            vo.pos += rate * vo.pitchRate;
            vo.elapsed += 1.0f / (float)mSampleRate;

            if (vo.fadeOutLeft < 0 &&
                (vo.pos >= vo.endPos || std::exp(-(float)vo.elapsed / vo.tau) < 1.0e-4f))
               BeginFadeOut(vo);
         }

         for (auto& gh : mGhosts)
         {
            if (!gh.active)
               continue;
            const float g = gh.gain * RaisedCosine((float)gh.left / (float)gh.total);
            sample += ReadSample(*mActiveBuffer, gh.pos) * g;
            gh.pos += rate * gh.pitchRate;
            gh.left--;
            if (gh.left <= 0 || gh.pos < 0.0 || gh.pos >= (double)numFrames)
               gh.active = false;
         }

         const float out = sample * output;
         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = out;
      }

      PublishSnapshot(totalSteps, (float)std::fmod(mPrevStepPos, (double)totalSteps));
   }

private:
   struct Voice
   {
      bool active = false;
      int slice = 0;
      double pos = 0.0;
      double endPos = 0.0;
      double pitchRate = 1.0;
      float velocity = 1.0f;
      float elapsed = 0.0f;
      float tau = 1.0f;
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
      double pitchRate = 1.0;
      float gain = 0.0f;
      int left = 0;
      int total = 1;
   };

   struct AtomicHit
   {
      std::atomic<int> step { 0 };
      std::atomic<int> slice { 0 };
      std::atomic<float> velocity { 0.0f };
      std::atomic<float> pitchRand { 0.0f };
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
      for (auto& gh : mGhosts)
      {
         if (gh.active)
            continue;
         gh.active = true;
         gh.pos = v.pos;
         gh.pitchRate = v.pitchRate;
         gh.gain = v.lastGain;
         gh.total = std::max(1, (int)(kStealFadeMs * 0.001f * (float)mSampleRate));
         gh.left = gh.total;
         return;
      }
      // All ghost slots busy: the steal simply cuts - inaudible at 2ms.
   }

   void TriggerStep(int stepIdx, int hitCount, float transient, float decayParam)
   {
      const int count = mSliceCount.load(std::memory_order_acquire);
      if (count <= 0 || mActiveBuffer == nullptr)
         return;

      for (int h = 0; h < hitCount; h++)
      {
         if (mHits[h].step.load(std::memory_order_relaxed) != stepIdx)
            continue;

         const int slice = std::clamp(mHits[h].slice.load(std::memory_order_relaxed), 0, count - 1);
         const float velocity = mHits[h].velocity.load(std::memory_order_relaxed);
         const float pitchRand = mHits[h].pitchRand.load(std::memory_order_relaxed);
         const int cls = mSliceClass[slice].load(std::memory_order_relaxed);

         const float startFrac = mSliceStart[slice].load(std::memory_order_relaxed);
         const float endFrac = (slice + 1 < count) ? mSliceStart[slice + 1].load(std::memory_order_relaxed) : 1.0f;
         const int numFrames = mActiveBuffer->numFrames;

         // Fix Section7#2: length comes from THIS slice's own bounds, right
         // now - never a fallback constant.
         const float sliceLenSec = (float)((double)(endFrac - startFrac) * numFrames / mActiveBuffer->sampleRate);

         Voice* target = nullptr;
         for (auto& vo : mVoices)
         {
            if (!vo.active)
            {
               target = &vo;
               break;
            }
         }
         if (target == nullptr)
         {
            // Fix Section7#10: steal the OLDEST voice, not voice 0 / a
            // rotating cursor, with a short crossfade into a ghost tail.
            unsigned long long oldest = ~0ull;
            for (auto& vo : mVoices)
            {
               if (vo.order < oldest)
               {
                  oldest = vo.order;
                  target = &vo;
               }
            }
         }
         if (target == nullptr)
            continue;

         if (target->active)
            SpawnGhost(*target);

         // Live rand-pitch: pitchRand was seeded at Generate() time, but the
         // randPitch AMOUNT and the class-keyed range are read live here, so
         // dragging the randPitch knob audibly changes the next hit with no
         // regenerate needed.
         const float randAmt = mRandPitch.load(std::memory_order_relaxed);
         const float rangeSemis = PitchRangeSemisFor((DrumClassifier::DrumClass)cls);
         const float semis = pitchRand * randAmt * rangeSemis;

         target->active = true;
         target->slice = slice;
         target->pos = (double)startFrac * numFrames;
         target->endPos = (double)std::max(startFrac, endFrac) * numFrames;
         target->pitchRate = std::pow(2.0, (double)semis / 12.0);
         target->velocity = std::clamp(velocity, 0.0f, 1.0f);
         target->elapsed = 0.0f;
         // transient (0..1) shapes attack punch: low = softer/slower attack
         // (30ms), high = snappy (1ms). decay (0..1) maps to a tau derived
         // from THIS slice's own length - at decay=0.5 the natural slice
         // length is used; below/above that shortens/lengthens it.
         const float attackMs = 1.0f + (1.0f - transient) * 29.0f;
         const float decayScale = std::pow(2.0f, (decayParam - 0.5f) * 4.0f); // 0.25x..4x
         target->tau = std::max(1.0e-4f, std::max(0.02f, sliceLenSec) * decayScale / 4.6f);
         target->fadeInTotal = std::max(1, (int)(std::max(kFadeInMs, attackMs) * 0.001f * (float)mSampleRate));
         target->fadeInLeft = target->fadeInTotal;
         target->fadeOutLeft = -1;
         target->lastGain = 0.0f;
         target->order = ++mVoiceOrder;
         return; // at most one voice per step landmark this scan pass
      }
   }

   void PublishSnapshot(int totalSteps, float playheadStep)
   {
      const int frames = (mActiveBuffer != nullptr) ? std::max(1, mActiveBuffer->numFrames) : 1;
      const int wIdx = (mVisualWriteIdx.load(std::memory_order_relaxed) + 1) % 3;
      BeatArrangerVisualSnapshot& snap = mVisualSnapshots[wIdx];
      int n = 0;
      for (auto& vo : mVoices)
      {
         if (!vo.active || n >= BeatArrangerVisualSnapshot::kMaxVisualVoices)
            continue;
         snap.voices[n].slice = vo.slice;
         snap.voices[n].position = (float)(vo.pos / (double)frames);
         snap.voices[n].amp = std::clamp(vo.lastGain, 0.0f, 1.0f);
         n++;
      }
      snap.voiceCount = n;
      snap.totalSteps = totalSteps;
      snap.playheadStep = playheadStep;
      mVisualWriteIdx.store(wIdx, std::memory_order_release);
      mVisualReadIdx.store(wIdx, std::memory_order_release);
   }

   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;

   Voice mVoices[kNumVoices];
   Ghost mGhosts[kNumGhosts];
   unsigned long long mVoiceOrder = 0;

   Platform::SampleBuffer* mActiveBuffer = nullptr;
   SampleSlot mSampleSlot;

   std::atomic<float> mSliceStart[BeatArrangerNode::kMaxSlices + 1];
   std::atomic<int> mSliceClass[BeatArrangerNode::kMaxSlices];
   std::atomic<int> mSliceCount { 0 };

   AtomicHit mHits[kMaxHits];
   std::atomic<int> mHitCount { 0 };
   std::atomic<float> mStepBeats { 0.25f };
   std::atomic<int> mTotalSteps { 32 };
   double mPrevStepPos = 0.0;

   std::atomic<float> mSpeed { 1.0f };
   std::atomic<float> mOutput { 0.8f };
   std::atomic<float> mRandPitch { 0.0f };
   std::atomic<float> mSwing { 0.0f };
   std::atomic<float> mTransient { 0.5f };
   std::atomic<float> mDecay { 0.5f };

   BeatArrangerVisualSnapshot mVisualSnapshots[3];
   std::atomic<int> mVisualWriteIdx { 0 };
   std::atomic<int> mVisualReadIdx { 0 };
};

// -------------------------------------------------------------- main thread
BeatArrangerNode::BeatArrangerNode() = default;

BeatArrangerNode::~BeatArrangerNode()
{
   mAbort.store(true, std::memory_order_release);
   if (mWorkerThread.joinable())
      mWorkerThread.join();
}

AudioNode* BeatArrangerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBeatArrangerNode>();
   return mAudioNode.get();
}

void BeatArrangerNode::VisitParams(ParamVisitor& v)
{
   v.Text("path", mFilePath);
   v.Int("timeSig", timeSig);
   v.Float("swing", swing);
   v.Float("randPitch", randPitch);
   v.Float("speed", speed);
   v.Float("transient", transient);
   v.Float("decay", decay);
   v.Float("output", output);
   // Fix Section7#8: the saved beat must be preserved verbatim across
   // save/load, never re-detected or re-arranged - mirrors SlicerNode's
   // markers blob.
   v.Text("slices", mSliceBlob);
   v.Text("hits", mHitBlob);
}

void BeatArrangerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBeatArrangerNode>();

   timeSig = std::clamp(timeSig, 0, kNumTimeSigs - 1);

   // Fix Section7#1: compare against the shadow BEFORE overwriting it. Doing
   // it the other way around (update the shadow, THEN compare) makes every
   // comparison trivially equal and the param goes permanently dead.
   const bool changed = mFirstCook ||
                        swing != mLastSwing || randPitch != mLastRandPitch || speed != mLastSpeed ||
                        transient != mLastTransient || decay != mLastDecay || output != mLastOutput;
   mLastSwing = swing;
   mLastRandPitch = randPitch;
   mLastSpeed = speed;
   mLastTransient = transient;
   mLastDecay = decay;
   mLastOutput = output;
   mLastTimeSig = timeSig;
   mFirstCook = false;

   if (changed)
      mAudioNode->PushParams(speed, output, randPitch, swing, transient, decay);
   mAudioNode->DrainRetired();

   if (mResultReady.load(std::memory_order_acquire))
   {
      JoinWorkerIfDone();
      mResultReady.store(false, std::memory_order_relaxed);

      mSlices.clear();
      for (const auto& r : mPendingResult.classified)
      {
         BeatArranger::SliceInfo info;
         info.slice = (int)mSlices.size();
         info.cls = r.cls;
         info.confidence = r.confidence;
         info.lenSec = 0.0f;
         info.centroid = r.f.centroidMean;
         info.decaySec = r.f.decayTimeMs * 0.001f;
         info.f0 = r.f.f0Hz;
         mSlices.push_back(info);
      }
      mPendingResult.classified.clear();
      mPendingResult.onsetFrames.clear();

      mSliceBlob = SerializeSlices();
      PushSlicesToAudio();
      mStatus = mSlices.empty() ? "no transients found" : "analyzed";
   }

   mAudioNode->GetVisualSnapshot(mVisualSnapshot);
}

void BeatArrangerNode::Generate()
{
   if (mSlices.empty())
      return;
   int num = 4, den = 4;
   TimeSigFor(timeSig, num, den);
   BeatArranger::ArrangeParams params;
   params.stepsPerBar = StepsPerBarFor(num, den);
   params.bars = 2;
   mArrangedHits = BeatArranger::Generate(mSlices, params, seed);
   mHitBlob = BeatArranger::SerializeHits(mArrangedHits);
   PushHitsToAudio();
}

void BeatArrangerNode::PushHitsToAudio()
{
   if (!mAudioNode)
      return;
   int num = 4, den = 4;
   TimeSigFor(timeSig, num, den);
   const int stepsPerBar = StepsPerBarFor(num, den);
   const int totalSteps = stepsPerBar * 2;
   const double stepBeats = 4.0 / 16.0; // one 1/16 note in quarter-note beats

   std::vector<AudioBeatArrangerNode::HitPod> pods;
   pods.reserve(mArrangedHits.size());
   for (const auto& h : mArrangedHits)
      pods.push_back({ h.step, h.slice, h.velocity, h.pitchRand });
   mAudioNode->PushHits(pods.data(), (int)pods.size(), stepBeats, totalSteps);
}

void BeatArrangerNode::PushSlicesToAudio()
{
   if (!mAudioNode)
      return;
   std::vector<float> starts;
   std::vector<int> classes;
   starts.reserve(mSlices.size());
   classes.reserve(mSlices.size());
   for (size_t i = 0; i < mSlices.size(); i++)
   {
      starts.push_back((float)i / std::max<size_t>(1, mSlices.size()));
      classes.push_back((int)mSlices[i].cls);
   }
   mAudioNode->PushSlices(starts.empty() ? nullptr : starts.data(), (int)starts.size(),
                          classes.empty() ? nullptr : classes.data());
}

void BeatArrangerNode::SetSliceClassOverride(int slice, DrumClassifier::DrumClass cls)
{
   if (slice < 0 || slice >= (int)mSlices.size())
      return;
   mSlices[slice].cls = cls;
   mSliceBlob = SerializeSlices();
   PushSlicesToAudio();
}

std::string BeatArrangerNode::SerializeSlices() const
{
   std::ostringstream os;
   for (size_t i = 0; i < mSlices.size(); i++)
   {
      if (i > 0)
         os << ' ';
      const auto& s = mSlices[i];
      char buf[160];
      snprintf(buf, sizeof(buf), "%.9g:%s:%.9g:%.9g:%.9g:%.9g", (float)i / std::max<size_t>(1, mSlices.size()),
               DrumClassifier::ClassName(s.cls), s.confidence, s.centroid, s.decaySec, s.f0);
      os << buf;
   }
   return os.str();
}

void BeatArrangerNode::DeserializeSlices(const std::string& blob)
{
   mSlices.clear();
   std::istringstream is(blob);
   std::string token;
   while (is >> token)
   {
      float confidence = 0.0f, centroid = 0.0f, decaySec = 0.0f, f0 = 0.0f;
      char clsName[64] = {};
      size_t p1 = token.find(':');
      if (p1 == std::string::npos)
         continue;
      size_t p2 = token.find(':', p1 + 1);
      const std::string namePart = (p2 == std::string::npos) ? token.substr(p1 + 1) : token.substr(p1 + 1, p2 - p1 - 1);
      snprintf(clsName, sizeof(clsName), "%s", namePart.c_str());
      if (p2 != std::string::npos)
         sscanf(token.c_str() + p2 + 1, "%f:%f:%f:%f", &confidence, &centroid, &decaySec, &f0);

      BeatArranger::SliceInfo info;
      info.slice = (int)mSlices.size();
      info.cls = DrumClassifier::ClassFromName(clsName);
      info.confidence = confidence;
      info.centroid = centroid;
      info.decaySec = decaySec;
      info.f0 = f0;
      mSlices.push_back(info);
   }
}

bool BeatArrangerNode::LoadFile(const std::string& path)
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

void BeatArrangerNode::FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
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
   mSourceMono.assign(decoded->channelData.begin(), decoded->channelData.begin() + decoded->numFrames);

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBeatArrangerNode>();
   mAudioNode->PushBuffer(decoded);

   mFilePath = filePath;
   mFileName = fileName;
   mStatus = status;

   // Fix Section7#7: clear old slices/hits before the new analysis lands,
   // so nothing stale is ever pushed to the audio thread mid-analysis.
   mSlices.clear();
   mArrangedHits.clear();
   mSliceBlob.clear();
   mHitBlob.clear();
   PushSlicesToAudio();
   PushHitsToAudio();

   LaunchAnalysis();
}

void BeatArrangerNode::LaunchAnalysis()
{
   if (mWorkerThread.joinable())
   {
      mAbort.store(true, std::memory_order_release);
      mWorkerThread.join();
   }
   if (mSourceMono.empty())
      return;

   mWorking.store(true, std::memory_order_release);
   mAbort.store(false, std::memory_order_release);
   mResultReady.store(false, std::memory_order_relaxed);
   mStatus = "analyzing...";

   SlicerDsp::Params params;
   params.maxSlices = kMaxSlices;

   const std::vector<float> monoCopy = mSourceMono;
   const double sr = mSourceSR;
   const std::string fileNameHint = mFileName;

   mWorkerThread = std::thread([this, monoCopy, sr, params, fileNameHint]()
   {
      std::vector<int> onsetFrames;
      std::vector<float> strengths;
      SlicerDsp::Detect(monoCopy.data(), (int)monoCopy.size(), sr, params, onsetFrames, strengths, &mAbort);

      if (!mAbort.load(std::memory_order_relaxed))
      {
         PendingResult res;
         res.onsetFrames = onsetFrames;
         const int n = (int)onsetFrames.size();
         for (int i = 0; i < n && !mAbort.load(std::memory_order_relaxed); i++)
         {
            const int start = onsetFrames[i];
            const int end = (i + 1 < n) ? onsetFrames[i + 1] : (int)monoCopy.size();
            const int len = std::max(0, end - start);
            const char* hint = (n == 1) ? fileNameHint.c_str() : nullptr;
            res.classified.push_back(DrumClassifier::Classify(monoCopy.data() + start, len, sr, hint));
         }
         if (!mAbort.load(std::memory_order_relaxed))
         {
            mPendingResult = std::move(res);
            mResultReady.store(true, std::memory_order_release);
         }
      }
      mWorking.store(false, std::memory_order_release);
   });
}

void BeatArrangerNode::JoinWorkerIfDone()
{
   if (mWorkerThread.joinable() && !mWorking.load(std::memory_order_acquire))
      mWorkerThread.join();
}

void BeatArrangerNode::ReloadFromPath()
{
   if (mFilePath.empty())
      return;

   // Fix Section7#8: save/restore-and-abort-job dance (SlicerNode's
   // ReloadFromPath pattern) - FinishBuffer resets the blobs and relaunches
   // analysis; every one of these is restored verbatim afterwards, and if a
   // beat was already saved we abandon the freshly launched analysis job
   // entirely rather than let it silently re-detect/re-arrange on load.
   const std::string savedSliceBlob = mSliceBlob;
   const std::string savedHitBlob = mHitBlob;
   const int savedTimeSig = timeSig;
   const float savedSwing = swing;
   const float savedRandPitch = randPitch;
   const float savedSpeed = speed;
   const float savedTransient = transient;
   const float savedDecay = decay;
   const float savedOutput = output;

   LoadFile(mFilePath);

   timeSig = savedTimeSig;
   swing = savedSwing;
   randPitch = savedRandPitch;
   speed = savedSpeed;
   transient = savedTransient;
   decay = savedDecay;
   output = savedOutput;

   if (!savedSliceBlob.empty())
   {
      mAbort.store(true, std::memory_order_release);
      if (mWorkerThread.joinable())
         mWorkerThread.join();
      mWorking.store(false, std::memory_order_release);
      mResultReady.store(false, std::memory_order_release);

      mSliceBlob = savedSliceBlob;
      DeserializeSlices(savedSliceBlob);
      PushSlicesToAudio();

      mHitBlob = savedHitBlob;
      mArrangedHits = BeatArranger::DeserializeHits(savedHitBlob);
      PushHitsToAudio();

      mStatus = "loaded (saved beat)";
   }
}
