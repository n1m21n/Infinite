#include "DrumSequencerNode.h"
#include "DrumPatterns.h"
#include "DrumMidiLibrary.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "core/MidiFile.h"
#include "platform/SettingsPaths.h"
#include "json.hpp"
#include "core/Transport.h"
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

   void PushLaneAccentPitch(int lane, float semis) { mLaneAccentPitch[lane].store(semis, std::memory_order_relaxed); }

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

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int numInputs, AudioBuffer& buffer) override
   {
      (void)numInputs;
      // Turbo 0.47: a lane that just got a new sample (file drop, kit load,
      // clear) retires its old buffer, which the main thread frees a frame
      // later. Voices still playing the old one must stop now, before they
      // read freed memory: that was the crash on dropping a WAV onto a lane
      // that was sounding.
      for (int lane = 0; lane < kNumLanes; lane++)
         if (mSampleSlots[lane].SwapIn())
            for (int slot = 0; slot < kVoicesPerLane; slot++)
               mVoices[lane * kVoicesPerLane + slot].active = false;

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

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
                        std::clamp((int)(frac * buffer.numFrames), 0, std::max(0, buffer.numFrames - 1));
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
      for (int i = 0; i < buffer.numFrames; i++)
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
            sampleL += s * voice.panL;
            sampleR += s * voice.panR;

            voice.readPos += voice.rate;
            if (voice.readPos >= voice.endFrame - 1 || (voice.decayCoeff < 1.0f && totalAmp < 1e-4f))
               voice.active = false;
         }

         if (buffer.numChannels > 0)
            buffer.channels[0][i] = sampleL * masterVolNow;
         if (buffer.numChannels > 1)
            buffer.channels[1][i] = sampleR * masterVolNow;
      }

      // Turbo 0.49: publish each voice's play position for the lane cards'
      // play cursors (0..1 of the whole buffer, -1 = idle).
      for (int v = 0; v < kNumVoices; v++)
      {
         const Voice& voice = mVoices[v];
         if (voice.active && voice.buffer != nullptr && voice.buffer->numFrames > 0)
            mCursors.Publish(v, (float)(voice.readPos / (double)voice.buffer->numFrames));
         else
            mCursors.Idle(v);
      }
   }

   PlayCursorSet<kNumVoices>& Cursors() { return mCursors; }

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
      const float accentSemis = velocity >= 0.99f ? mLaneAccentPitch[lane].load(std::memory_order_relaxed) : 0.0f;
      voice.rate = NoteRateForPitch(lanePitchNow[lane] + accentSemis);
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
   PlayCursorSet<kNumVoices> mCursors; // Turbo 0.49
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
   std::atomic<float> mLaneAccentPitch[kNumLanes] = {};
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

// Turbo 0.50: the analysed MIDI groove file. Complete before the
// constructor and destructor below, which own it through a unique_ptr.
struct DrumSequencerNode::MidiCache
{
   std::string path;
   Part parts[3];
   int rate = (int)MusicTime::kSixteenth;
   float swing = 0.0f;
   int bpm = 0;
   std::string info;
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
      laneAccentPitch[lane] = 0.0f;
      mLastLaneAccentPitch[lane] = -999.0f;

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

      if (laneAccentPitch[lane] != mLastLaneAccentPitch[lane])
      {
         mAudioNode->PushLaneAccentPitch(lane, std::clamp(laneAccentPitch[lane], -24.0f, 24.0f));
         mLastLaneAccentPitch[lane] = laneAccentPitch[lane];
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
   v.Int("editPage", editPage);
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
   // Turbo 0.47, appended so the older params keep their order.
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      snprintf(name, sizeof(name), "lane%d_accentPitch", lane);
      v.Float(name, laneAccentPitch[lane]);
   }
   v.Text("patternName", patternName);
   v.Int("patternPart", patternPart);
   // Turbo 0.50, appended: the other parts' edits, the per-groove stash and
   // the preset name. The visitor is read or write with one interface: a
   // value that comes back different from what was handed in was read.
   std::string blob = SerializeParts();
   const std::string written = blob;
   v.Text("partData", blob);
   if (blob != written)
      DeserializeParts(blob);
   v.Text("presetName", presetName);
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
   if (!Platform::DecodeAudioFileToBuffer(path, *decoded, error))
   {
      delete decoded;
      laneStatus[lane] = error.empty() ? "failed to load" : error;
      return false;
   }
   const size_t slash = path.find_last_of("/\\");
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

   // Turbo 0.49: full-resolution peaks for the waveform view (all channels).
   lanePeaks[lane].BuildFrom(*decoded);

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
   // Turbo 0.50: LoadFileToLane resets the lane trim to the full sample
   // (right for a freshly picked file), which wiped the saved start/end on
   // every patch load, paste and undo. Keep the loaded values.
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      if (laneFilePath[lane].empty())
         continue;
      const float savedStart = std::clamp(laneStart[lane], 0.0f, 1.0f);
      const float savedEnd = std::clamp(laneEnd[lane], savedStart, 1.0f);
      const std::string saved = laneFilePath[lane];
      // Turbo 0.51: a stale bundled-kit path (install moved) retries from the
      // current kit folder by file name; success rewrites the saved path.
      const std::string retry = DrumPatterns::StaleKitRetryPath(saved);
      if (!(!retry.empty() && LoadFileToLane(lane, retry)))
         LoadFileToLane(lane, saved);
      laneStart[lane] = savedStart;
      laneEnd[lane] = savedEnd;
   }
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

int DrumSequencerNode::LaneVoicePositions(int lane, float* out, int max) const
{
   if (!mAudioNode || out == nullptr || max <= 0)
      return 0;
   lane = Clamp(lane);
   return mAudioNode->Cursors().Collect(out, max, lane * kVoicesPerLane, kVoicesPerLane);
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
   lanePeaks[lane].Clear();
   laneStart[lane] = 0.0f;
   laneEnd[lane] = 1.0f;
}

bool DrumSequencerNode::ApplyPattern(int groove, int part)
{
   int count = 0;
   const DrumPatterns::Groove* all = DrumPatterns::All(count);
   if (groove < 0 || groove >= count || part < 0 || part > 2)
      return false;
   const DrumPatterns::Groove& g = all[groove];
   const DrumPatterns::Part& pt = g.parts[part];
   numSteps = std::clamp(pt.steps, 1, kMaxSteps);
   rate = std::clamp(g.rate, 0, (int)MusicTime::kNumRateDivisions - 1);
   swing = std::clamp(g.swing, 0.0f, 1.0f);
   editPage = 0;
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      const char* cells = lane < 8 ? pt.lanes[lane] : nullptr;
      const size_t len = cells != nullptr ? strlen(cells) : 0;
      for (int s = 0; s < kMaxSteps; s++)
         stepVel[lane][s] = (cells != nullptr && s < numSteps && (size_t)s < len) ? DrumPatterns::CellVelocity(cells[s]) : 0.0f;
      laneAccentPitch[lane] = lane < 8 ? g.tones[lane] : 0.0f;
   }
   patternName = g.name;
   patternPart = part;
   return true;
}

int DrumSequencerNode::LoadKitIntoEmptyLanes(const std::string& kitDir)
{
   if (kitDir.empty())
      return 0;
   int loaded = 0;
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      if (!laneFilePath[lane].empty())
         continue;
      std::string path = kitDir;
      if (path.back() != '/' && path.back() != '\\')
         path += '/';
      path += DrumPatterns::KitFile(lane);
      if (LoadFileToLane(lane, path))
      {
         loaded++;
         // Closed and open hat share a choke group, as on hardware, unless
         // the lane already has one.
         if ((lane == 2 || lane == 3) && laneChoke[lane] == 0)
            laneChoke[lane] = 1;
      }
   }
   return loaded;
}

// ------------------------------------------------------------ Turbo 0.50: .mid import
// The analysis (GM map, grid, swing / triplet detection, bars through the
// time signature map) lives in DrumMidiLibrary, shared with the MIDI folder
// browser below.
bool DrumSequencerNode::ImportMidiFile(const std::string& path, std::string& message)
{
   MidiFile::Data data;
   std::string error;
   if (!MidiFile::Load(path, data, error))
   {
      message = error.empty() ? "could not read the MIDI file" : error;
      importStatus = message;
      return false;
   }
   std::string name = path;
   const size_t slash = name.find_last_of("/\\");
   if (slash != std::string::npos)
      name = name.substr(slash + 1);
   return ImportMidiData(data, name, message);
}

namespace
{
   // The notes the analysis left out or changed, for the status line.
   std::string MidiImportNotes(const DrumMidi::Pattern& p)
   {
      std::string out;
      if (p.meter != "4/4" && !p.meter.empty())
         out += ", " + p.meter;
      if (p.swing > 0.0f)
         out += ", swing";
      if (p.leadBars > 0)
         out += ", " + std::to_string(p.leadBars) + " empty bar" + (p.leadBars == 1 ? "" : "s") + " dropped";
      if (p.pickupNotes > 0)
         out += ", pickup bar dropped";
      if (p.merged > 0)
         out += ", " + std::to_string(p.merged) + " notes merged";
      if (p.skipped > 0)
         out += ", " + std::to_string(p.skipped) + " non-drum notes skipped";
      if (p.allChannels)
         out += ", no channel 10 (all channels read)";
      return out;
   }

   void PartFromPattern(const DrumMidi::Pattern& p, int firstBar, int maxSteps, DrumSequencerNode::Part& out)
   {
      out = DrumSequencerNode::Part();
      out.filled = true;
      const int start = p.BarStart(firstBar);
      out.steps = std::clamp(std::min(maxSteps, p.Steps() - start), 1, kMaxSteps);
      for (int st = 0; st < out.steps; st++)
         for (int lane = 0; lane < kNumLanes; lane++)
            out.vel[lane][st] = p.cells[(size_t)(start + st)][lane];
   }
}

bool DrumSequencerNode::ImportMidiData(const MidiFile::Data& data, const std::string& name, std::string& message)
{
   DrumMidi::Pattern pat;
   std::string error;
   if (!DrumMidi::Analyze(data, pat, error))
   {
      message = error + " in " + name;
      importStatus = message;
      return false;
   }
   // Whole bars from the first one with a note, as many as fit kMaxSteps.
   int bars = 0, steps = 0;
   while (bars < pat.Bars() && steps + pat.barSteps[bars] <= kMaxSteps)
      steps += pat.barSteps[bars++];
   const bool truncated = bars < pat.Bars();
   if (bars == 0) // one bar longer than kMaxSteps: cut
   {
      bars = 1;
      steps = kMaxSteps;
   }

   // Into the live part only: the other parts keep their content, made
   // concrete first since the groove name is about to go.
   MaterializeParts();
   Part live;
   PartFromPattern(pat, 0, steps, live);
   int placed = 0;
   for (int lane = 0; lane < kNumLanes; lane++)
      for (int s = 0; s < live.steps; s++)
         placed += live.vel[lane][s] > 0.0f ? 1 : 0;
   // Library grooves use accent pitch for two-tone bells; an imported
   // accent means velocity only.
   for (int lane = 0; lane < kNumLanes; lane++)
      laneAccentPitch[lane] = 0.0f;
   LoadLivePart(live);
   rate = pat.triplet ? (int)MusicTime::kSixteenthTrip : (int)MusicTime::kSixteenth;
   swing = pat.swing;
   patternName.clear(); // no longer a library groove
   presetName.clear();
   browsingCategory = false;
   StoreLivePart();

   char buf[256];
   snprintf(buf, sizeof(buf), "%s: %d bar%s, %d steps of %s, %d hits%s", name.c_str(), bars, bars == 1 ? "" : "s",
            numSteps, pat.triplet ? "1/16T" : "1/16", placed, truncated ? " (cut to fit)" : "");
   message = buf + MidiImportNotes(pat);
   importStatus = message;
   return true;
}

// ------------------------------------------- Turbo 0.50: grooves from the MIDI folder

bool DrumSequencerNode::LoadMidiCache(const std::string& rawPath, bool reload, std::string& message)
{
   const std::string path = std::filesystem::u8path(rawPath).lexically_normal().u8string();
   if (!reload && mMidi && mMidi->path == path)
      return true;
   std::error_code ec;
   const uintmax_t size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
   if (ec)
   {
      message = "MIDI file not found: " + path;
      return false;
   }
   if (size > 4u * 1024u * 1024u)
   {
      message = "MIDI file too large for a drum pattern: " + path;
      return false;
   }
   MidiFile::Data data;
   std::string error;
   if (!MidiFile::Load(path, data, error))
   {
      message = error.empty() ? "could not read " + path : error;
      return false;
   }
   DrumMidi::Pattern pat;
   if (!DrumMidi::Analyze(data, pat, error))
   {
      message = error + ": " + DrumMidi::TitleFromFileName(path);
      return false;
   }
   DrumMidi::PartSpan spans[3];
   DrumMidi::SplitParts(pat, kMaxSteps, spans);
   auto cache = std::make_unique<MidiCache>();
   cache->path = path;
   for (int pi = 0; pi < 3; pi++)
      PartFromPattern(pat, spans[pi].firstBar, spans[pi].steps, cache->parts[pi]);
   cache->rate = pat.triplet ? (int)MusicTime::kSixteenthTrip : (int)MusicTime::kSixteenth;
   cache->swing = pat.swing;

   // Title and bpm as the browser shows them (index.json when there is one).
   std::string title = DrumMidi::TitleFromFileName(path);
   const std::shared_ptr<const DrumMidi::Library> lib = DrumMidi::Current();
   const int entry = lib->Find(path);
   if (entry >= 0)
   {
      title = lib->entries[entry].title;
      cache->bpm = lib->entries[entry].bpm;
   }
   if (cache->bpm <= 0 && !data.tempos.empty())
      cache->bpm = (int)std::lround(pat.bpm);
   std::string info = title + ": ";
   if (cache->bpm > 0)
      info += "orig " + std::to_string(cache->bpm) + " bpm, ";
   info += std::to_string(pat.Bars()) + (pat.Bars() == 1 ? " bar" : " bars") + (pat.triplet ? " of 1/16T" : "");
   info += "; A " + DrumMidi::SpanLabel(spans[0]) + ", B " + DrumMidi::SpanLabel(spans[1]) + ", C " +
           DrumMidi::SpanLabel(spans[2]);
   if (pat.tempoChanges > 0)
      info += ", tempo changes in the file";
   cache->info = info + MidiImportNotes(pat);
   mMidi = std::move(cache);
   return true;
}

int DrumSequencerNode::MidiGrooveBpm() const
{
   if (!IsMidiGroove())
      return 0;
   if (mMidi && mMidi->path == MidiGroovePath())
      return mMidi->bpm;
   const std::shared_ptr<const DrumMidi::Library> lib = DrumMidi::Current();
   const int entry = lib->Find(MidiGroovePath());
   return entry >= 0 ? lib->entries[entry].bpm : 0;
}

bool DrumSequencerNode::SelectMidiGroove(const std::string& path, int part, bool fresh, std::string& message)
{
   if (part < 0 || part > 2)
   {
      message = "part: A, B or C (0-2)";
      return false;
   }
   // Always re-read on a pick: the file may have been edited since.
   if (!LoadMidiCache(path, true, message))
   {
      importStatus = message;
      return false;
   }
   const std::string name = kMidiPrefix + mMidi->path;
   message = mMidi->info;
   importStatus = message;
   if (fresh && grooveStash.erase(name) > 0)
      mStashRev++;
   if (name == patternName && !fresh)
   {
      SwitchPart(part);
      return true;
   }
   if (name != patternName)
      StashCurrent();
   presetName.clear();
   if (RestoreStash(name, part))
      return true;
   for (int pi = 0; pi < 3; pi++)
      parts[pi] = mMidi->parts[pi];
   rate = mMidi->rate;
   swing = mMidi->swing;
   for (int lane = 0; lane < kNumLanes; lane++)
      laneAccentPitch[lane] = 0.0f;
   patternName = name;
   patternPart = part;
   LoadLivePart(parts[part]);
   return true;
}

bool DrumSequencerNode::SourcePart(int part, Part& out)
{
   if (LibraryPart(GrooveIndex(), part, out))
      return true;
   if (!IsMidiGroove() || part < 0 || part > 2)
      return false;
   std::string message;
   if (!LoadMidiCache(MidiGroovePath(), false, message))
   {
      importStatus = message;
      return false;
   }
   out = mMidi->parts[part];
   return true;
}

// ------------------------------------------------- Turbo 0.50: parts, stash, presets
void DrumSequencerNode::StoreLivePart()
{
   Part& p = parts[std::clamp(patternPart, 0, 2)];
   p.filled = true;
   p.steps = std::clamp(numSteps, 1, kMaxSteps);
   memcpy(p.vel, stepVel, sizeof(stepVel));
}

void DrumSequencerNode::LoadLivePart(const Part& part)
{
   numSteps = std::clamp(part.steps, 1, kMaxSteps);
   memcpy(stepVel, part.vel, sizeof(stepVel));
   editPage = 0;
}

int DrumSequencerNode::GrooveIndex() const
{
   if (patternName.empty())
      return -1;
   int count = 0;
   const DrumPatterns::Groove* all = DrumPatterns::All(count);
   for (int i = 0; i < count; i++)
      if (patternName == all[i].name)
         return i;
   return -1;
}

bool DrumSequencerNode::LibraryPart(int groove, int part, Part& out) const
{
   int count = 0;
   const DrumPatterns::Groove* all = DrumPatterns::All(count);
   if (groove < 0 || groove >= count || part < 0 || part > 2)
      return false;
   const DrumPatterns::Part& pt = all[groove].parts[part];
   out.filled = true;
   out.steps = std::clamp(pt.steps, 1, kMaxSteps);
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      const char* cells = pt.lanes[lane];
      const size_t len = cells != nullptr ? strlen(cells) : 0;
      for (int st = 0; st < kMaxSteps; st++)
         out.vel[lane][st] = (cells != nullptr && st < out.steps && (size_t)st < len) ? DrumPatterns::CellVelocity(cells[st]) : 0.0f;
   }
   return true;
}

void DrumSequencerNode::MaterializeParts()
{
   for (int pi = 0; pi < 3; pi++)
   {
      if (parts[pi].filled || pi == patternPart)
         continue;
      if (!SourcePart(pi, parts[pi]))
      {
         parts[pi].filled = true;
         parts[pi].steps = std::clamp(numSteps, 1, kMaxSteps);
         memcpy(parts[pi].vel, stepVel, sizeof(stepVel));
      }
   }
}

void DrumSequencerNode::SwitchPart(int part)
{
   part = std::clamp(part, 0, 2);
   if (part == patternPart && parts[part].filled)
      return;
   StoreLivePart();
   patternPart = part;
   if (parts[part].filled)
      LoadLivePart(parts[part]);
   else if (SourcePart(part, parts[part]))
      LoadLivePart(parts[part]);
   else
      StoreLivePart(); // no groove: a new part starts as a copy of the grid
}

void DrumSequencerNode::StashCurrent()
{
   bool any = false;
   for (const Part& p : parts)
      any = any || p.filled;
   if (patternName.empty() && !any)
      return; // a fresh node: nothing of the user's to keep
   StoreLivePart();
   GrooveStash st;
   for (int pi = 0; pi < 3; pi++)
      st.parts[pi] = parts[pi];
   st.part = patternPart;
   st.rate = rate;
   st.swing = swing;
   memcpy(st.accentPitch, laneAccentPitch, sizeof(laneAccentPitch));
   // Bounded so a long browsing session can't grow the patch forever.
   if (grooveStash.size() >= 32 && grooveStash.find(patternName) == grooveStash.end())
      grooveStash.erase(grooveStash.begin());
   grooveStash[patternName] = st;
   mStashRev++;
}

bool DrumSequencerNode::SelectGroove(int groove, int part, bool fresh)
{
   int count = 0;
   const DrumPatterns::Groove* all = DrumPatterns::All(count);
   if (groove < 0 || groove >= count || part < 0 || part > 2)
      return false;
   const std::string name = all[groove].name;
   if (fresh && grooveStash.erase(name) > 0)
      mStashRev++;
   if (name == patternName && !fresh)
   {
      SwitchPart(part);
      return true;
   }
   if (name != patternName)
      StashCurrent();
   presetName.clear();
   if (RestoreStash(name, part))
      return true;
   for (Part& p : parts)
      p.filled = false;
   ApplyPattern(groove, part); // a new groove brings its own rate and swing
   StoreLivePart();
   return true;
}

bool DrumSequencerNode::RestoreStash(const std::string& name, int part)
{
   auto it = grooveStash.find(name);
   if (it == grooveStash.end())
      return false;
   const GrooveStash& st = it->second;
   for (int pi = 0; pi < 3; pi++)
      parts[pi] = st.parts[pi];
   rate = st.rate;
   swing = st.swing;
   memcpy(laneAccentPitch, st.accentPitch, sizeof(laneAccentPitch));
   patternName = name;
   patternPart = part;
   if (parts[part].filled || SourcePart(part, parts[part]))
      LoadLivePart(parts[part]);
   return true;
}

bool DrumSequencerNode::RevertPart()
{
   Part p;
   if (!SourcePart(std::clamp(patternPart, 0, 2), p))
      return false;
   parts[std::clamp(patternPart, 0, 2)] = p;
   LoadLivePart(p);
   return true;
}

namespace
{
   using json = nlohmann::json;

   json PartToJson(const DrumSequencerNode::Part& p)
   {
      static const char* kHex = "0123456789abcdef";
      json lanes = json::array();
      const int steps = std::clamp(p.steps, 1, kMaxSteps);
      for (int lane = 0; lane < kNumLanes; lane++)
      {
         // Two hex digits per step (velocity * 255, 00 = off): compact and exact enough.
         std::string cells;
         cells.reserve((size_t)steps * 2);
         for (int st = 0; st < steps; st++)
         {
            const float v = std::clamp(p.vel[lane][st], 0.0f, 1.0f);
            const int b = v <= 0.0f ? 0 : std::max(1, (int)std::lround(v * 255.0f));
            cells += kHex[b >> 4];
            cells += kHex[b & 15];
         }
         lanes.push_back(cells);
      }
      return json { { "filled", p.filled }, { "steps", steps }, { "lanes", lanes } };
   }

   int HexDigit(char c)
   {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
   }

   void PartFromJson(const json& j, DrumSequencerNode::Part& p)
   {
      p = DrumSequencerNode::Part();
      if (!j.is_object())
         return;
      p.filled = j.value("filled", true);
      p.steps = std::clamp(j.value("steps", 16), 1, kMaxSteps);
      const json lanes = j.value("lanes", json::array());
      for (int lane = 0; lane < kNumLanes && lane < (int)lanes.size(); lane++)
      {
         if (!lanes[lane].is_string())
            continue;
         const std::string& cells = lanes[lane].get_ref<const std::string&>();
         for (int st = 0; st < p.steps && (size_t)st * 2 + 1 < cells.size(); st++)
            p.vel[lane][st] = (float)(HexDigit(cells[(size_t)st * 2]) * 16 + HexDigit(cells[(size_t)st * 2 + 1])) / 255.0f;
      }
   }

   json AccentToJson(const float* a)
   {
      json arr = json::array();
      for (int lane = 0; lane < kNumLanes; lane++)
         arr.push_back(a[lane]);
      return arr;
   }

   void AccentFromJson(const json& j, float* a)
   {
      if (!j.is_array())
         return;
      for (int lane = 0; lane < kNumLanes && lane < (int)j.size(); lane++)
         if (j[lane].is_number())
            a[lane] = std::clamp(j[lane].get<float>(), -24.0f, 24.0f);
   }

   std::string SanitizePresetName(const std::string& name)
   {
      std::string out;
      for (char c : name)
         if ((unsigned char)c >= 32 && strchr("<>:\"/\\|?*", c) == nullptr)
            out += c;
      while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
         out.pop_back();
      while (!out.empty() && out.front() == ' ')
         out.erase(out.begin());
      return out.substr(0, 80);
   }
}

std::string DrumSequencerNode::SerializeParts() const
{
   bool same = mBlobValid && mBlobStashRev == mStashRev && mBlobNumSteps == numSteps && mBlobPart == patternPart &&
               memcmp(mBlobLive, stepVel, sizeof(stepVel)) == 0;
   for (int pi = 0; pi < 3 && same; pi++)
      same = mBlobParts[pi].filled == parts[pi].filled && mBlobParts[pi].steps == parts[pi].steps &&
             memcmp(mBlobParts[pi].vel, parts[pi].vel, sizeof(parts[pi].vel)) == 0;
   if (same)
      return mBlobCache;
   mBlobCache = SerializePartsUncached();
   mBlobValid = true;
   mBlobStashRev = mStashRev;
   mBlobNumSteps = numSteps;
   mBlobPart = patternPart;
   memcpy(mBlobLive, stepVel, sizeof(stepVel));
   for (int pi = 0; pi < 3; pi++)
      mBlobParts[pi] = parts[pi];
   return mBlobCache;
}

std::string DrumSequencerNode::SerializePartsUncached() const
{
   // Nothing to keep (an old patch, or parts never used): write nothing so
   // the patch and the MCP param list stay as they were.
   bool any = !grooveStash.empty();
   for (const Part& p : parts)
      any = any || p.filled;
   if (!any)
      return std::string();
   json j;
   json ps = json::array();
   for (int pi = 0; pi < 3; pi++)
   {
      if (pi == patternPart)
      {
         // The live part is the grid itself (saved as lane<L>_step<S>);
         // written here too so the blob is complete on its own.
         Part live;
         live.filled = true;
         live.steps = numSteps;
         memcpy(live.vel, stepVel, sizeof(stepVel));
         ps.push_back(PartToJson(live));
      }
      else
         ps.push_back(PartToJson(parts[pi]));
   }
   j["parts"] = ps;
   json stash = json::object();
   for (const auto& kv : grooveStash)
   {
      json e;
      json sp = json::array();
      for (int pi = 0; pi < 3; pi++)
         sp.push_back(PartToJson(kv.second.parts[pi]));
      e["parts"] = sp;
      e["part"] = kv.second.part;
      e["rate"] = kv.second.rate;
      e["swing"] = kv.second.swing;
      e["accentPitch"] = AccentToJson(kv.second.accentPitch);
      stash[kv.first] = e;
   }
   j["stash"] = stash;
   return j.dump();
}

void DrumSequencerNode::DeserializeParts(const std::string& blob)
{
   for (Part& p : parts)
      p = Part();
   grooveStash.clear();
   mStashRev++;
   if (blob.empty())
      return;
   const json j = json::parse(blob, nullptr, false);
   if (j.is_discarded() || !j.is_object())
      return;
   try // json::value throws on a wrong type (a hand-edited patch)
   {
   const json ps = j.value("parts", json::array());
   for (int pi = 0; pi < 3 && pi < (int)ps.size(); pi++)
      PartFromJson(ps[pi], parts[pi]);
   const json stash = j.value("stash", json::object());
   if (stash.is_object())
      for (auto it = stash.begin(); it != stash.end(); ++it)
      {
         GrooveStash st;
         const json sp = it.value().value("parts", json::array());
         for (int pi = 0; pi < 3 && pi < (int)sp.size(); pi++)
            PartFromJson(sp[pi], st.parts[pi]);
         st.part = std::clamp(it.value().value("part", 0), 0, 2);
         st.rate = std::clamp(it.value().value("rate", 12), 0, (int)MusicTime::kNumRateDivisions - 1);
         st.swing = std::clamp(it.value().value("swing", 0.0f), 0.0f, 1.0f);
         AccentFromJson(it.value().value("accentPitch", json::array()), st.accentPitch);
         grooveStash[it.key()] = st;
      }
   }
   catch (const std::exception&)
   {
      for (Part& p : parts)
         p = Part();
      grooveStash.clear();
   }
}

std::string DrumSequencerNode::PresetDirectory()
{
   const std::string root = InfiniteSettingsDirectory();
   if (root.empty())
      return std::string();
   const std::filesystem::path dir = std::filesystem::u8path(root) / "DrumPresets";
   std::error_code ec;
   std::filesystem::create_directories(dir, ec);
   return ec ? std::string() : dir.u8string();
}

std::vector<std::string> DrumSequencerNode::ListPresets()
{
   std::vector<std::string> out;
   const std::string dir = PresetDirectory();
   if (dir.empty())
      return out;
   // increment(ec), not a range-for: operator++ throws on an I/O error.
   std::error_code ec;
   for (std::filesystem::directory_iterator it(std::filesystem::u8path(dir), ec), end; !ec && it != end;
        it.increment(ec))
   {
      std::error_code fileEc;
      if (it->is_regular_file(fileEc) && it->path().extension() == ".json")
         out.push_back(it->path().stem().u8string());
   }
   std::sort(out.begin(), out.end());
   return out;
}

bool DrumSequencerNode::SavePreset(const std::string& rawName, std::string& error)
{
   const std::string name = SanitizePresetName(rawName);
   const std::string dir = PresetDirectory();
   if (name.empty() || dir.empty())
   {
      error = name.empty() ? "preset name is empty" : "no user data folder";
      return false;
   }
   // A preset holds every part: empty slots are filled from the groove first.
   StoreLivePart();
   MaterializeParts();
   json j;
   j["format"] = "infinite-turbo-drum-preset";
   j["version"] = 1;
   j["name"] = name;
   j["groove"] = patternName;
   j["part"] = patternPart;
   j["rate"] = rate;
   j["swing"] = swing;
   j["accentPitch"] = AccentToJson(laneAccentPitch);
   json ps = json::array();
   for (const Part& p : parts)
      ps.push_back(PartToJson(p));
   j["parts"] = ps;
   const std::filesystem::path path = std::filesystem::u8path(dir) / std::filesystem::u8path(name + ".json");
   std::ofstream f(path, std::ios::binary | std::ios::trunc);
   if (!f)
   {
      error = "cannot write " + path.u8string();
      return false;
   }
   f << j.dump(1);
   f.close();
   if (!f)
   {
      error = "could not finish writing " + path.u8string();
      return false;
   }
   presetName = name;
   return true;
}

bool DrumSequencerNode::LoadPreset(const std::string& rawName, std::string& error)
{
   const std::string name = SanitizePresetName(rawName);
   const std::string dir = PresetDirectory();
   if (name.empty() || dir.empty())
   {
      error = name.empty() ? "preset name is empty" : "no user data folder";
      return false;
   }
   std::ifstream f(std::filesystem::u8path(dir) / std::filesystem::u8path(name + ".json"), std::ios::binary);
   if (!f)
   {
      error = "no preset named " + name;
      return false;
   }
   std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
   const json j = json::parse(text, nullptr, false);
   if (j.is_discarded() || !j.is_object() || !j.contains("parts"))
   {
      error = "not a drum preset: " + name;
      return false;
   }
   // Parse into locals first: a malformed file leaves the node untouched.
   Part loaded[3];
   int newRate = rate, newPart = 0;
   float newSwing = swing;
   float newAccent[kNumLanes];
   memcpy(newAccent, laneAccentPitch, sizeof(newAccent));
   try
   {
      const json ps = j["parts"];
      for (int pi = 0; pi < 3 && ps.is_array() && pi < (int)ps.size(); pi++)
         PartFromJson(ps[pi], loaded[pi]);
      newRate = std::clamp(j.value("rate", rate), 0, (int)MusicTime::kNumRateDivisions - 1);
      newSwing = std::clamp(j.value("swing", swing), 0.0f, 1.0f);
      newPart = std::clamp(j.value("part", 0), 0, 2);
      AccentFromJson(j.value("accentPitch", json::array()), newAccent);
   }
   catch (const std::exception&)
   {
      error = "not a drum preset: " + name;
      return false;
   }
   StashCurrent(); // the pattern being replaced stays reachable from its groove
   for (int pi = 0; pi < 3; pi++)
      parts[pi] = loaded[pi];
   rate = newRate;
   swing = newSwing;
   memcpy(laneAccentPitch, newAccent, sizeof(laneAccentPitch));
   // A preset is its own pattern, not the library groove it may have
   // started from: leaving the groove name empty keeps the stash of that
   // groove separate from the preset.
   patternName.clear();
   presetName = name;
   patternPart = newPart;
   if (!parts[patternPart].filled)
      StoreLivePart();
   LoadLivePart(parts[patternPart]);
   for (Part& p : parts)
      if (!p.filled)
         p = parts[patternPart];
   return true;
}
