#include "DrumSequencerNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "core/Transport.h"
#include "core/AudioDecodeCache.h"
#include "platform/Platform.h"

namespace
{
   constexpr int kNumLanes = DrumSequencerNode::kNumLanes;
   constexpr int kMaxSteps = DrumSequencerNode::kMaxSteps;
   constexpr int kVoicesPerLane = DrumSequencerNode::kVoicesPerLane;
   constexpr int kNumVoices = kNumLanes * kVoicesPerLane;

   // ParamMailbox slots: only the three continuous, audibly-live per-lane
   // knobs go through the mailbox's per-sample smoothing (volume/pan/pitch -
   // the ones a modulator or a live drag could step without a click-free
   // ramp). decay/transient are captured once, per voice, at trigger time
   // (see TriggerLane) from plain atomics instead - stepping those doesn't
   // need to be click-free since they only shape *new* hits.
   int VolParam(int lane) { return lane * 3 + 0; }
   int PanParam(int lane) { return lane * 3 + 1; }
   int PitchParam(int lane) { return lane * 3 + 2; }
   constexpr int kMasterVolumeParam = kNumLanes * 3;

   float NoteRateForPitch(float semitones) { return powf(2.0f, semitones / 12.0f); }
}

// ------------------------------------------------------------- audio thread
class AudioDrumSequencerNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      // ~0.15ms time-constant exponential tail for a choked voice (settles
      // under -80dB within ~1.4ms) - fast enough to read as an instant cut
      // (the closed-hat-stops-the-open-hat case) without the sample
      // discontinuity a hard active=false would leave.
      mChokeCoeff = expf((float)(-1.0 / (0.00015 * sampleRate)));
      mMailbox.PrepareToPlay(sampleRate);
      for (int lane = 0; lane < kNumLanes; lane++)
      {
         mMailbox.SetImmediate(VolParam(lane), mLaneVolume[lane].load(std::memory_order_relaxed));
         mMailbox.SetImmediate(PanParam(lane), mLanePan[lane].load(std::memory_order_relaxed));
         mMailbox.SetImmediate(PitchParam(lane), mLanePitch[lane].load(std::memory_order_relaxed));
      }
      mMailbox.SetImmediate(kMasterVolumeParam, mMasterVolume.load(std::memory_order_relaxed));
      Reset();
   }

   // Resyncs scheduling to Transport's *current* position rather than
   // wherever ProcessBlock next happens to look - called from PrepareToPlay
   // and whenever the topology rebuilds. Subtracting a hair below the raw
   // grid position (rather than seeding it exactly) means a step landmark
   // sitting exactly at that instant (the common case: a fresh node created
   // with the transport at beat 0, with a hit programmed on step 0) still
   // satisfies the scan's exclusive lower bound on the very next block,
   // instead of being silently skipped because "now" already equals the
   // landmark it needs to be strictly after.
   void Reset() override
   {
      for (auto& v : mVoices)
         v = Voice();
      const int rateDiv = mRate.load(std::memory_order_relaxed);
      const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv));
      mPrevRawPos = Transport::Instance().Beats() / beatsPerStep - 1e-6;
   }

   // ---- main-thread setters, one per dirty-pushed param group -----------
   void PushLaneContinuous(int lane, float volume, float pan, float pitch)
   {
      mLaneVolume[lane].store(volume, std::memory_order_relaxed);
      mLanePan[lane].store(pan, std::memory_order_relaxed);
      mLanePitch[lane].store(pitch, std::memory_order_relaxed);
      mMailbox.Push(VolParam(lane), volume);
      mMailbox.Push(PanParam(lane), pan);
      mMailbox.Push(PitchParam(lane), pitch);
   }

   // `start`/`end` composition with the global offsets already happened on
   // the main thread (DrumSequencerNode::PushDirtyParams) - this pushes the
   // final effective values, same as PushLaneContinuous/PushLaneEnvelope.
   void PushLaneRange(int lane, float start, float end)
   {
      mLaneStart[lane].store(start, std::memory_order_relaxed);
      mLaneEnd[lane].store(end, std::memory_order_relaxed);
   }

   // `decayCoeff` is the fully precomputed one-pole coefficient (never
   // exp() on this thread - see PushDirtyParams). `attackInc`/`boostPeak`/
   // `boostDecayCoeff` are the transient shape, likewise precomputed.
   void PushLaneEnvelope(int lane, float decayCoeff, float attackInc, int attackSamples, float boostPeak,
                         float boostDecayCoeff)
   {
      mLaneDecayCoeff[lane].store(decayCoeff, std::memory_order_relaxed);
      mLaneAttackInc[lane].store(attackInc, std::memory_order_relaxed);
      mLaneAttackSamples[lane].store(attackSamples, std::memory_order_relaxed);
      mLaneBoostPeak[lane].store(boostPeak, std::memory_order_relaxed);
      mLaneBoostDecayCoeff[lane].store(boostDecayCoeff, std::memory_order_relaxed);
   }

   void PushLaneState(int lane, bool mute, bool solo, int choke)
   {
      mLaneMute[lane].store(mute, std::memory_order_relaxed);
      mLaneSolo[lane].store(solo, std::memory_order_relaxed);
      mLaneChoke[lane].store(choke, std::memory_order_relaxed);
   }

   void PushStep(int lane, int step, float vel) { mStepVel[lane][step].store(vel, std::memory_order_relaxed); }

   void PushGlobals(int rate, int numSteps, float swing, float masterVolume, bool run)
   {
      mRate.store(rate, std::memory_order_relaxed);
      mNumSteps.store(numSteps, std::memory_order_relaxed);
      mSwing.store(swing, std::memory_order_relaxed);
      mRun.store(run, std::memory_order_relaxed);
      mMasterVolume.store(masterVolume, std::memory_order_relaxed);
      mMailbox.Push(kMasterVolumeParam, masterVolume);
   }

   // Main thread only. Hands over ownership of a freshly decoded buffer for
   // `lane` - the previously active one (if any) is retired through the
   // lane's own SampleSlot rather than freed here.
   void PushBuffer(int lane, Platform::SampleBuffer* buf) { mSampleSlots[lane].Push(buf); }
   void DrainRetired() { for (auto& slot : mSampleSlots) slot.DrainRetired(); }

   int AudioOutputCount() const override { return 1 + kNumLanes; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      AudioBuffer* outPtrs[1] = { &buffer };
      ProcessBlockMulti(inputs, numInputs, outPtrs, 1);
   }

   void ProcessBlockMulti(const AudioBuffer* const* /*inputs*/, int /*numInputs*/,
                          AudioBuffer* const* outputs, int numOutputs) override
   {
      for (auto& slot : mSampleSlots)
         slot.SwapIn();

      for (int o = 0; o < numOutputs; o++)
      {
         if (outputs[o] == nullptr)
            continue;
         for (int ch = 0; ch < outputs[o]->numChannels; ch++)
            std::fill(outputs[o]->channels[ch], outputs[o]->channels[ch] + outputs[o]->numFrames, 0.0f);
      }

      if (numOutputs == 0 || outputs[0] == nullptr)
         return;

      const int numFrames = outputs[0]->numFrames;

      // ---- schedule this block's step boundaries -------------------------
      // Sample-accurate: derived every block from Transport's own position,
      // never from an internal "steps fired so far" counter - see the class
      // comment on DrumSequencerNode. Transport::AdvanceAudioClock() has
      // already run for this block (AudioEngine::Process calls it before any
      // node's ProcessBlock), so Beats() here is the block's *end* position;
      // the previous call's end position (mPrevRawPos) is this block's start.
      const int rateDiv = mRate.load(std::memory_order_relaxed);
      const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv));
      const int numSteps = std::clamp(mNumSteps.load(std::memory_order_relaxed), 1, kMaxSteps);
      const float swing = std::clamp(mSwing.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const bool run = mRun.load(std::memory_order_relaxed);

      const double rawPosNow = Transport::Instance().Beats() / beatsPerStep;

      struct FireEvent
      {
         int lane;
         int frameOffset;
         float vel;
      };
      FireEvent stepEvts[64];
      int numStepEvts = 0;

      if (rawPosNow < mPrevRawPos)
      {
         // Rewind/scrub: don't iterate a negative range, just resync.
         mPrevRawPos = rawPosNow;
      }
      else
      {
         const double span = rawPosNow - mPrevRawPos;
         const int kStart = (int)std::floor(mPrevRawPos - 0.5);
         const int kEnd = (int)std::ceil(rawPosNow);
         for (int lane = 0; lane < kNumLanes && numStepEvts < 64; lane++)
         {
            for (int k = kStart; k <= kEnd && numStepEvts < 64; k++)
            {
               const int stepIndex = ((k % numSteps) + numSteps) % numSteps;
               const bool odd = (stepIndex % 2) == 1;
               const double landmark = (double)k + (odd ? (double)swing * 0.5 : 0.0);
               if (landmark > mPrevRawPos && landmark <= rawPosNow)
               {
                  const float v = mStepVel[lane][stepIndex].load(std::memory_order_relaxed);
                  if (v > 0.0f)
                  {
                     const double frac = span > 1e-9 ? (landmark - mPrevRawPos) / span : 0.0;
                     const int frameOffset =
                        std::clamp((int)(frac * numFrames), 0, std::max(0, numFrames - 1));
                     stepEvts[numStepEvts++] = { lane, frameOffset, v };
                  }
               }
            }
         }
         mPrevRawPos = rawPosNow;
      }

      // Keep events time-ordered so the per-sample merge below is a single
      // forward pass, same shape as SamplerNode/Wavetable's note-event loop.
      std::sort(stepEvts, stepEvts + numStepEvts,
                [](const FireEvent& a, const FireEvent& b) { return a.frameOffset < b.frameOffset; });

      // Which lanes are audible this block, per mute/solo (mute/solo don't
      // need per-sample smoothing - they gate whether a lane's *new* hits
      // sound, same "captured at trigger" treatment as decay/transient).
      bool anySolo = false;
      bool laneAudible[kNumLanes];
      for (int lane = 0; lane < kNumLanes; lane++)
         if (mLaneSolo[lane].load(std::memory_order_relaxed))
            anySolo = true;
      for (int lane = 0; lane < kNumLanes; lane++)
      {
         const bool solo = mLaneSolo[lane].load(std::memory_order_relaxed);
         const bool mute = mLaneMute[lane].load(std::memory_order_relaxed);
         laneAudible[lane] = anySolo ? solo : !mute;
      }

      int stepIdx = 0;
      for (int i = 0; i < numFrames; i++)
      {
         // Advance every lane's smoothed continuous params exactly once per
         // sample (ParamMailbox has one smoother per id - calling
         // SmoothedValue more than once a sample for the same id would
         // double-advance it), and reuse the results both for already-
         // sounding voices and any voice triggered this very sample.
         float laneVolNow[kNumLanes], lanePanNow[kNumLanes], lanePitchNow[kNumLanes];
         for (int lane = 0; lane < kNumLanes; lane++)
         {
            laneVolNow[lane] = mMailbox.SmoothedValue(VolParam(lane));
            lanePanNow[lane] = mMailbox.SmoothedValue(PanParam(lane));
            lanePitchNow[lane] = mMailbox.SmoothedValue(PitchParam(lane));
         }
         const float masterVolNow = mMailbox.SmoothedValue(kMasterVolumeParam);

         while (run && stepIdx < numStepEvts && stepEvts[stepIdx].frameOffset <= i)
         {
            TriggerLane(stepEvts[stepIdx].lane, stepEvts[stepIdx].vel, laneVolNow, lanePanNow, lanePitchNow);
            stepIdx++;
         }
         // Step events not fired because run==false still have to be
         // consumed so a later block doesn't see a stale index.
         while (!run && stepIdx < numStepEvts && stepEvts[stepIdx].frameOffset <= i)
            stepIdx++;

         float sampleL = 0.0f, sampleR = 0.0f;
         float laneL[kNumLanes] = {};
         float laneR[kNumLanes] = {};

         for (int v = 0; v < kNumVoices; v++)
         {
            Voice& voice = mVoices[v];
            if (!voice.active)
               continue;
            const int lane = v / kVoicesPerLane;
            if (voice.buffer == nullptr || !laneAudible[lane])
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
            laneL[lane] += vL;
            laneR[lane] += vR;

            voice.readPos += voice.rate;
            if (voice.readPos >= voice.endFrame - 1 || (voice.decayCoeff < 1.0f && totalAmp < 1e-4f))
               voice.active = false;
         }

         if (outputs[0] != nullptr)
         {
            if (outputs[0]->numChannels > 0)
               outputs[0]->channels[0][i] = sampleL * masterVolNow;
            if (outputs[0]->numChannels > 1)
               outputs[0]->channels[1][i] = sampleR * masterVolNow;
         }

         for (int lane = 0; lane < kNumLanes; lane++)
         {
            const int outIdx = 1 + lane;
            if (outIdx < numOutputs && outputs[outIdx] != nullptr)
            {
               if (outputs[outIdx]->numChannels > 0)
                  outputs[outIdx]->channels[0][i] = laneL[lane];
               if (outputs[outIdx]->numChannels > 1)
                  outputs[outIdx]->channels[1][i] = laneR[lane];
            }
         }
      }
   }

private:
   struct Voice
   {
      const Platform::SampleBuffer* buffer = nullptr;
      double readPos = 0.0;
      double endFrame = 0.0; // voice stops at this frame (laneEnd * buffer->numFrames)
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

   void TriggerLane(int lane, float velocity, const float* laneVolNow, const float* lanePanNow,
                     const float* lanePitchNow)
   {
      const Platform::SampleBuffer* buf = mSampleSlots[lane].Active();
      if (buf == nullptr || buf->numFrames <= 0)
         return;

      const int choke = mLaneChoke[lane].load(std::memory_order_relaxed);
      if (choke != 0)
      {
         for (int v = 0; v < kNumVoices; v++)
         {
            const int voiceLane = v / kVoicesPerLane;
            if (!mVoices[v].active || voiceLane == lane)
               continue; // same-lane retrigger handled by the round-robin slot below, not choked here
            if (mLaneChoke[voiceLane].load(std::memory_order_relaxed) == choke)
               ChokeVoice(mVoices[v]);
         }
         // A lane in its own choke group also cuts its own previous voice -
         // real hardware behaviour for closed-hat-style self-choke.
         for (int slot = 0; slot < kVoicesPerLane; slot++)
         {
            Voice& v = mVoices[lane * kVoicesPerLane + slot];
            if (v.active)
               ChokeVoice(v);
         }
      }

      const int slot = mLaneVoiceCursor[lane];
      mLaneVoiceCursor[lane] = (slot + 1) % kVoicesPerLane;
      Voice& voice = mVoices[lane * kVoicesPerLane + slot];

      // start/end are already clamped `end > start` by at least one frame at
      // the push site (DrumSequencerNode::PushDirtyParams), not here.
      const float startFrac = mLaneStart[lane].load(std::memory_order_relaxed);
      const float endFrac = mLaneEnd[lane].load(std::memory_order_relaxed);
      voice.buffer = buf;
      voice.readPos = (double)startFrac * buf->numFrames;
      voice.endFrame = (double)endFrac * buf->numFrames;
      voice.rate = NoteRateForPitch(lanePitchNow[lane]);
      voice.velocity = velocity * laneVolNow[lane];
      DspMath::EqualPowerPan(lanePanNow[lane], voice.panL, voice.panR);
      voice.attackLevel = 0.0f;
      voice.attackInc = mLaneAttackInc[lane].load(std::memory_order_relaxed);
      voice.attackRemaining = mLaneAttackSamples[lane].load(std::memory_order_relaxed);
      voice.boostEnv = mLaneBoostPeak[lane].load(std::memory_order_relaxed);
      voice.boostDecayCoeff = mLaneBoostDecayCoeff[lane].load(std::memory_order_relaxed);
      voice.decayAmp = 1.0f;
      voice.decayCoeff = mLaneDecayCoeff[lane].load(std::memory_order_relaxed);
      voice.active = true;
   }

   // Fast, click-avoiding fade rather than a hard stop: forces the voice
   // into a ~0.3ms exponential tail instead of zeroing it in place.
   void ChokeVoice(Voice& v)
   {
      v.attackRemaining = 0;
      v.decayCoeff = std::min(v.decayCoeff, mChokeCoeff);
   }

   double mSampleRate = 44100.0;
   float mChokeCoeff = 0.99f;
   ParamMailbox mMailbox;

   Voice mVoices[kNumVoices];
   int mLaneVoiceCursor[kNumLanes] = {};
   double mPrevRawPos = 0.0;

   SampleSlot mSampleSlots[kNumLanes];

   std::atomic<float> mLaneVolume[kNumLanes] = {};
   std::atomic<float> mLanePan[kNumLanes] = {};
   std::atomic<float> mLanePitch[kNumLanes] = {};
   std::atomic<float> mLaneDecayCoeff[kNumLanes] = {};
   std::atomic<float> mLaneAttackInc[kNumLanes] = {};
   std::atomic<int> mLaneAttackSamples[kNumLanes] = {};
   std::atomic<float> mLaneBoostPeak[kNumLanes] = {};
   std::atomic<float> mLaneBoostDecayCoeff[kNumLanes] = {};
   std::atomic<bool> mLaneMute[kNumLanes] = {};
   std::atomic<bool> mLaneSolo[kNumLanes] = {};
   std::atomic<int> mLaneChoke[kNumLanes] = {};
   std::atomic<float> mStepVel[kNumLanes][kMaxSteps] = {};
   std::atomic<float> mLaneStart[kNumLanes] = {};
   std::atomic<float> mLaneEnd[kNumLanes] = {};

   std::atomic<int> mRate { 12 };
   std::atomic<int> mNumSteps { 8 };
   std::atomic<float> mSwing { 0.0f };
   std::atomic<float> mMasterVolume { 0.8f };
   std::atomic<bool> mRun { true };

   friend class ::DrumSequencerNode;
};

DrumSequencerNode::DrumSequencerNode()
{
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      laneVolume[lane] = 0.8f;
      lanePan[lane] = 0.0f;
      lanePitch[lane] = 0.0f;
      laneFineTune[lane] = 0.0f;
      laneDecay[lane] = 1.0f;
      laneTransient[lane] = 0.0f;
      laneStart[lane] = 0.0f;
      laneEnd[lane] = 1.0f;
      laneMute[lane] = false;
      laneSolo[lane] = false;
      laneChoke[lane] = 0;

      mLastLaneVolume[lane] = -1.0f;
      mLastLanePan[lane] = -99.0f;
      mLastLanePitch[lane] = -999.0f;
      mLastLaneFineTune[lane] = -999.0f;
      mLastLaneDecay[lane] = -1.0f;
      mLastLaneTransient[lane] = -99.0f;
      mLastLaneStart[lane] = -1.0f;
      mLastLaneEnd[lane] = -1.0f;
      mLastLaneMute[lane] = false;
      mLastLaneSolo[lane] = false;
      mLastLaneChoke[lane] = -1;
   }
}
DrumSequencerNode::~DrumSequencerNode() = default;

AudioNode* DrumSequencerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioDrumSequencerNode>();
   return mAudioNode.get();
}

void DrumSequencerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioDrumSequencerNode>();

   PushDirtyParams();
   mAudioNode->DrainRetired();
}

// Precomputes every coefficient the audio thread would otherwise need exp()
// for (rule: never exp() on the audio thread), then pushes only what
// actually changed since the last cook.
void DrumSequencerNode::PushDirtyParams()
{
   // See mLastCoeffSampleRate's declaration: PrepareToPlay can land between
   // two cooks, so a coefficient already pushed this session might have
   // been computed against the pre-PrepareToPlay default rate. Force one
   // corrective re-push, for every lane, the cook after that happens.
   const double currentSr = mAudioNode->mSampleRate;
   const bool srChanged = currentSr != mLastCoeffSampleRate;
   mLastCoeffSampleRate = currentSr;

   // Global offsets are folded in here rather than on the audio thread: a
   // change to any one of them has to reach every lane's effective value,
   // so it's cheaper to compose once, on the main thread, than to push five
   // more atomics and repeat the composition every sample. `contGlobal`/
   // `envGlobal` mirror `srChanged`'s "force one corrective re-push"
   // pattern - a lane whose own value didn't move still needs repushing the
   // cook a global offset does.
   const bool contGlobalChanged = mFirstCook || globalPan != mLastGlobalPan || globalPitch != mLastGlobalPitch;
   const bool envGlobalChanged = mFirstCook || globalDecay != mLastGlobalDecay || globalTransient != mLastGlobalTransient;

   for (int lane = 0; lane < kNumLanes; lane++)
   {
      if (contGlobalChanged || laneVolume[lane] != mLastLaneVolume[lane] || lanePan[lane] != mLastLanePan[lane] ||
          lanePitch[lane] != mLastLanePitch[lane] || laneFineTune[lane] != mLastLaneFineTune[lane])
      {
         const float effVolume = laneVolume[lane];
         const float effPan = std::clamp(lanePan[lane] + globalPan, -1.0f, 1.0f);
         const float effPitch =
            std::clamp(lanePitch[lane] + laneFineTune[lane] / 100.0f + globalPitch, -24.0f, 24.0f);
         mAudioNode->PushLaneContinuous(lane, effVolume, effPan, effPitch);
         mLastLaneVolume[lane] = laneVolume[lane];
         mLastLanePan[lane] = lanePan[lane];
         mLastLanePitch[lane] = lanePitch[lane];
         mLastLaneFineTune[lane] = laneFineTune[lane];
      }

      if (envGlobalChanged || srChanged || laneDecay[lane] != mLastLaneDecay[lane] ||
          laneTransient[lane] != mLastLaneTransient[lane] || laneStart[lane] != mLastLaneStart[lane] ||
          laneEnd[lane] != mLastLaneEnd[lane])
      {
         const float effDecay = std::clamp(laneDecay[lane] + globalDecay, 0.0f, 1.0f);
         const float effTransient = std::clamp(laneTransient[lane] + globalTransient, -1.0f, 1.0f);

         // decay: 1 = play the sample out (no decay coefficient at all);
         // below 1, a one-pole whose time constant scales to the loaded
         // sample's own *selected range* length, so the knob reads the same
         // "how much of the range" regardless of whether the lane holds a
         // kick or a snare, or has been trimmed with start/end.
         float decayCoeff = 1.0f;
         if (effDecay < 0.999f)
         {
            // laneSampleLenSec is captured at load time on this (main)
            // thread - see its declaration for why reaching into the audio
            // thread's SampleSlot here instead would race the buffer not
            // having been adopted by ProcessBlock yet.
            const double rangeLenSec =
               std::max(0.0f, laneEnd[lane] - laneStart[lane]) * laneSampleLenSec[lane];
            const double timeConstSec = std::max(0.005, (double)effDecay * rangeLenSec);
            decayCoeff = expf((float)(-1.0 / (timeConstSec * mAudioNode->mSampleRate)));
         }

         // transient: <0 lengthens attack up to ~40ms with no boost; 0 is a
         // flat ~3ms click-avoidance ramp; >0 shortens attack toward ~0.5ms
         // and adds up to +4dB that decays back to unity over ~15ms.
         const float attackMs =
            effTransient >= 0.0f ? (3.0f + (0.5f - 3.0f) * effTransient) : (3.0f + (40.0f - 3.0f) * -effTransient);
         const double sr = mAudioNode ? mAudioNode->mSampleRate : 44100.0;
         const int attackSamples = std::max(1, (int)(attackMs * 0.001 * sr));
         const float attackInc = 1.0f / (float)attackSamples;
         const float boostDb = effTransient > 0.0f ? effTransient * 4.0f : 0.0f;
         const float boostPeak = DspMath::DbToLinear(boostDb);
         const double boostDecaySec = 0.015;
         const float boostDecayCoeff = expf((float)(-1.0 / (boostDecaySec * sr)));

         mAudioNode->PushLaneEnvelope(lane, decayCoeff, attackInc, attackSamples, boostPeak, boostDecayCoeff);
         mLastLaneDecay[lane] = laneDecay[lane];
         mLastLaneTransient[lane] = laneTransient[lane];
         // mLastLaneStart/End are NOT written here - the range-push block
         // below reads the same "did start/end change" condition and must
         // see it too; whichever of the two blocks runs second retires it.
      }

      if (mFirstCook || laneStart[lane] != mLastLaneStart[lane] || laneEnd[lane] != mLastLaneEnd[lane])
      {
         // end > start by at least one frame, clamped here (main thread),
         // not on the audio thread - see AudioDrumSequencerNode::TriggerLane.
         const float start = std::clamp(laneStart[lane], 0.0f, 1.0f);
         const float end = std::max(start + 0.0001f, std::clamp(laneEnd[lane], 0.0f, 1.0f));
         mAudioNode->PushLaneRange(lane, start, end);
         mLastLaneStart[lane] = laneStart[lane];
         mLastLaneEnd[lane] = laneEnd[lane];
      }

      if (mFirstCook || laneMute[lane] != mLastLaneMute[lane] || laneSolo[lane] != mLastLaneSolo[lane] ||
          laneChoke[lane] != mLastLaneChoke[lane])
      {
         mAudioNode->PushLaneState(lane, laneMute[lane], laneSolo[lane], laneChoke[lane]);
         mLastLaneMute[lane] = laneMute[lane];
         mLastLaneSolo[lane] = laneSolo[lane];
         mLastLaneChoke[lane] = laneChoke[lane];
      }

      for (int step = 0; step < kMaxSteps; step++)
      {
         if (mFirstCook || stepVel[lane][step] != mLastStepVel[lane][step])
         {
            mAudioNode->PushStep(lane, step, stepVel[lane][step]);
            mLastStepVel[lane][step] = stepVel[lane][step];
         }
      }
   }

   if (contGlobalChanged)
   {
      mLastGlobalPan = globalPan;
      mLastGlobalPitch = globalPitch;
   }
   if (envGlobalChanged)
   {
      mLastGlobalDecay = globalDecay;
      mLastGlobalTransient = globalTransient;
   }

   if (mFirstCook || rate != mLastRate || numSteps != mLastNumSteps || swing != mLastSwing || volume != mLastVolume ||
       run != mLastRun)
   {
      mAudioNode->PushGlobals(rate, std::clamp(numSteps, 1, kMaxSteps), swing, volume, run);
      mLastRate = rate;
      mLastNumSteps = numSteps;
      mLastSwing = swing;
      mLastVolume = volume;
      mLastRun = run;
   }

   mFirstCook = false;
}

void DrumSequencerNode::VisitParams(ParamVisitor& v)
{
   char name[32];
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      snprintf(name, sizeof(name), "lane%d_path", lane);
      v.Text(name, laneFilePath[lane]);
      snprintf(name, sizeof(name), "lane%d_volume", lane);
      v.Float(name, laneVolume[lane]);
      snprintf(name, sizeof(name), "lane%d_pan", lane);
      v.Float(name, lanePan[lane]);
      snprintf(name, sizeof(name), "lane%d_pitch", lane);
      v.Float(name, lanePitch[lane]);
      snprintf(name, sizeof(name), "lane%d_finetune", lane);
      v.Float(name, laneFineTune[lane]);
      snprintf(name, sizeof(name), "lane%d_decay", lane);
      v.Float(name, laneDecay[lane]);
      snprintf(name, sizeof(name), "lane%d_transient", lane);
      v.Float(name, laneTransient[lane]);
      snprintf(name, sizeof(name), "lane%d_mute", lane);
      v.Bool(name, laneMute[lane]);
      snprintf(name, sizeof(name), "lane%d_solo", lane);
      v.Bool(name, laneSolo[lane]);
      snprintf(name, sizeof(name), "lane%d_choke", lane);
      v.Int(name, laneChoke[lane]);
      snprintf(name, sizeof(name), "lane%d_start", lane);
      v.Float(name, laneStart[lane]);
      snprintf(name, sizeof(name), "lane%d_end", lane);
      v.Float(name, laneEnd[lane]);
      for (int step = 0; step < kMaxSteps; step++)
      {
         snprintf(name, sizeof(name), "lane%d_step%d", lane, step);
         v.Float(name, stepVel[lane][step]);
      }
   }
   v.Int("rate", rate);
   v.Int("steps", numSteps);
   v.Float("swing", swing);
   v.Float("volume", volume);
   v.Bool("run", run);
   v.Float("globalTransient", globalTransient);
   v.Float("globalDecay", globalDecay);
   v.Float("globalPitch", globalPitch);
   v.Float("globalPan", globalPan);
}

int DrumSequencerNode::CurrentStep() const
{
   const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)rate));
   const double rawPos = Transport::Instance().Beats() / beatsPerStep;
   const int steps = std::clamp(numSteps, 1, kMaxSteps);
   const int rawStep = (int)std::floor(rawPos);
   return ((rawStep % steps) + steps) % steps;
}

int DrumSequencerNode::LoadedLaneCount() const
{
   int n = 0;
   for (int lane = 0; lane < kNumLanes; lane++)
      if (!laneFileName[lane].empty())
         n++;
   return n;
}

bool DrumSequencerNode::LoadFileToLane(int lane, const std::string& path)
{
   lane = Clamp(lane);
   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (!AudioDecodeCache::DecodeCached(path, *decoded, error))
   {
      delete decoded;
      laneStatus[lane] = error.empty() ? "failed to load" : error;
      return false;
   }
   const size_t slash = path.find_last_of('/');
   const std::string fileName = (slash == std::string::npos) ? path : path.substr(slash + 1);
   FinishLaneBuffer(lane, decoded, fileName, path, "loaded");
   return true;
}

void DrumSequencerNode::FinishLaneBuffer(int lane, Platform::SampleBuffer* decoded, const std::string& fileName,
                                          const std::string& filePath, const std::string& status)
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioDrumSequencerNode>();
   // Captured before the handoff below: once PushBuffer runs, `decoded`
   // belongs to the audio thread and reading it here is a race in spirit
   // even though this process is single-threaded in practice.
   if (decoded->sampleRate > 0.0 && decoded->numFrames > 0)
      laneSampleLenSec[lane] = (double)decoded->numFrames / decoded->sampleRate;

   // Decimated min/max waveform for the lane card's visualizer - mirrors
   // SamplerNode::FinishBuffer, built once here on the main thread from
   // channel 0 only.
   laneWaveCount[lane] = std::min(kWaveCache, decoded->numFrames);
   if (laneWaveCount[lane] > 0)
   {
      const int framesPerBucket = std::max(1, decoded->numFrames / laneWaveCount[lane]);
      for (int b = 0; b < laneWaveCount[lane]; b++)
      {
         float mn = 0.0f, mx = 0.0f;
         const int bucketStart = b * framesPerBucket;
         const int bucketEnd = std::min(decoded->numFrames, bucketStart + framesPerBucket);
         for (int i = bucketStart; i < bucketEnd; i++)
         {
            mn = std::min(mn, decoded->channelData[i]);
            mx = std::max(mx, decoded->channelData[i]);
         }
         laneWaveMin[lane][b] = mn;
         laneWaveMax[lane][b] = mx;
      }
   }

   mAudioNode->PushBuffer(lane, decoded);
   laneFilePath[lane] = filePath;
   laneFileName[lane] = fileName;
   laneStatus[lane] = status;
   // A fresh buffer has neither been scrubbed nor range-trimmed yet.
   laneStart[lane] = 0.0f;
   laneEnd[lane] = 1.0f;
   // A new sample invalidates the decay coefficient (it scales to the
   // selected range's length) - force a re-push next cook even though
   // laneDecay itself didn't change.
   mLastLaneDecay[lane] = -1.0f;
}

void DrumSequencerNode::ReloadFromPaths()
{
   for (int lane = 0; lane < kNumLanes; lane++)
      if (!laneFilePath[lane].empty())
         LoadFileToLane(lane, laneFilePath[lane]);
}

void DrumSequencerNode::Randomize()
{
   // Seeds a musical starting pattern rather than white noise: kick on
   // downbeats, hats dense, snare on the backbeat (steps 4/12 of a 16-step
   // grid - "5/13" 1-based, matching a standard 4-on-the-floor feel).
   const int steps = std::clamp(numSteps, 1, kMaxSteps);
   auto rnd01 = []() { return (float)rand() / (float)RAND_MAX; };
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      float density = 0.15f;
      bool backbeat = false;
      if (lane == 0)
         density = 0.0f; // kick handled explicitly below
      else if (lane == 1)
         backbeat = true; // snare
      else if (lane == 2)
         density = 0.85f; // hats

      for (int s = 0; s < steps; s++)
      {
         bool on = false;
         float vel = 0.7f + rnd01() * 0.3f;
         if (lane == 0)
            on = (s % 4) == 0;
         else if (backbeat)
            on = (s % 8) == 4;
         else
            on = rnd01() < density;
         stepVel[lane][s] = on ? vel : 0.0f;
      }
      for (int s = steps; s < kMaxSteps; s++)
         stepVel[lane][s] = 0.0f;
   }
}

void DrumSequencerNode::ClearPattern()
{
   for (int lane = 0; lane < kNumLanes; lane++)
      for (int s = 0; s < kMaxSteps; s++)
         stepVel[lane][s] = 0.0f;
}

void DrumSequencerNode::RandomizeLane(int lane)
{
   lane = Clamp(lane);
   const int steps = std::clamp(numSteps, 1, kMaxSteps);
   auto rnd01 = []() { return (float)rand() / (float)RAND_MAX; };
   // Same density buckets as Randomize()'s per-lane cases, but with no
   // notion of "this lane is the kick/snare/hats" - a single-row reroll
   // just wants a plausible, moderately busy fill.
   const float density = 0.35f;
   for (int s = 0; s < steps; s++)
   {
      const float vel = 0.7f + rnd01() * 0.3f;
      stepVel[lane][s] = (rnd01() < density) ? vel : 0.0f;
   }
   for (int s = steps; s < kMaxSteps; s++)
      stepVel[lane][s] = 0.0f;
}

void DrumSequencerNode::ClearLane(int lane)
{
   lane = Clamp(lane);
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioDrumSequencerNode>();
   // SampleSlot::Push(nullptr) wouldn't actually silence the lane - SwapIn
   // treats "nothing pending" and "pending is null" identically, so the
   // previously active buffer would keep playing. Push an empty buffer
   // instead: TriggerLane's `buf->numFrames <= 0` guard then makes the lane
   // a no-op, same as a lane that was never loaded.
   mAudioNode->PushBuffer(lane, new Platform::SampleBuffer());
   laneFilePath[lane].clear();
   laneFileName[lane].clear();
   laneStatus[lane] = "--";
   laneWaveCount[lane] = 0;
   laneStart[lane] = 0.0f;
   laneEnd[lane] = 1.0f;
}
