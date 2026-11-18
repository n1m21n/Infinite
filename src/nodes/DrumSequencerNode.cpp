#include "DrumSequencerNode.h"
#include "DrumPatterns.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cctype>
#include "json.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "audio/SweepTone.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MidiFile.h"
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
      PublishCursors();
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

   // A pad hit from the Drum panel: fired on frame 0 of the next block, transport or not.
   void PushAudition(int lane, float vel) { mAudition[lane].store(vel, std::memory_order_relaxed); }

   void PushLaneState(int lane, bool mute, bool solo, int choke)
   {
      mLaneMute[lane].store(mute, std::memory_order_relaxed);
      mLaneSolo[lane].store(solo, std::memory_order_relaxed);
      mLaneChoke[lane].store(choke, std::memory_order_relaxed);
   }

   // `accent` is already clamped to +-24 st on the main thread
   // (DrumSequencerNode::PushDirtyParams); TriggerLane adds it to the lane's
   // pitch for full-velocity steps only.
   void PushLaneAccent(int lane, float accent) { mLaneAccentPitch[lane].store(accent, std::memory_order_relaxed); }
   void PushLaneAccentGain(int lane, float gain) { mLaneAccentGain[lane].store(gain - 1.0f, std::memory_order_relaxed); }

   void PushStep(int lane, int step, float vel) { mStepVel[lane][step].store(vel, std::memory_order_relaxed); }
   void PushTrig(int lane, int step, float prob, int cond, float nudge)
   {
      mStepProb[lane][step].store(prob, std::memory_order_relaxed);
      mStepCond[lane][step].store(cond, std::memory_order_relaxed);
      mStepNudge[lane][step].store(nudge, std::memory_order_relaxed);
   }
   // `cutoffHz` / `q` are final values (the 0..1 -> Hz map happens on the main thread).
   void PushLaneShape(int lane, int steps, bool reverse, int filter, float cutoffHz, float q)
   {
      mLaneSteps[lane].store(steps, std::memory_order_relaxed);
      mLaneReverse[lane].store(reverse, std::memory_order_relaxed);
      mLaneFilter[lane].store(filter, std::memory_order_relaxed);
      mLaneCutoffHz[lane].store(cutoffHz, std::memory_order_relaxed);
      mLaneQ[lane].store(q, std::memory_order_relaxed);
   }
   void PushFill(bool on) { mFill.store(on, std::memory_order_relaxed); }

   void SetRateFix(bool on) { mRateFix.store(on, std::memory_order_relaxed); }

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
      // A swap retires the lane's previous buffer, which the main thread
      // frees on its next DrainRetired(). Any voice still pointing at it
      // would read freed memory (crash report 2026-09-23: SIGSEGV in
      // ReadSample after a lane's sample was replaced mid-playback), so
      // drop every voice that isn't reading the lane's new buffer.
      for (int lane = 0; lane < kNumLanes; lane++)
      {
         if (!mSampleSlots[lane].SwapIn())
            continue;
         const Platform::SampleBuffer* fresh = mSampleSlots[lane].Active();
         for (int slot = 0; slot < kVoicesPerLane; slot++)
         {
            Voice& v = mVoices[lane * kVoicesPerLane + slot];
            if (v.buffer != fresh)
            {
               v.active = false;
               v.buffer = nullptr;
            }
         }
      }

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
      // node's ProcessBlock), so Beats() is the block's *end* and
      // BlockStartBeats() its start (taken after a seek, before a loop wrap).
      //
      // Three cases the old "resync to the end on any backwards move" lost:
      //  - a seek (Return to Start, a ruler click): the block starts at the
      //    target, so a step sitting exactly there (step 1 at beat 0) fires on
      //    the block's first frame instead of being skipped, and a forward
      //    seek never bursts every step it jumped over;
      //  - a loop lap: the block is split at the wrap frame, so the step at the
      //    loop start fires there instead of being skipped every pass;
      //  - a stopped clock: nothing fires, but a playhead parked on a step
      //    (rewound while stopped) fires that step when Play starts.
      const Transport& tr = Transport::Instance();
      const int rateDiv = mRate.load(std::memory_order_relaxed);
      const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv));
      const int numSteps = std::clamp(mNumSteps.load(std::memory_order_relaxed), 1, kMaxSteps);
      const float swing = std::clamp(mSwing.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const bool run = mRun.load(std::memory_order_relaxed);

      const bool fillOn = mFill.load(std::memory_order_relaxed);
      int laneLen[kNumLanes];
      for (int lane = 0; lane < kNumLanes; lane++)
      {
         const int own = mLaneSteps[lane].load(std::memory_order_relaxed);
         laneLen[lane] = own > 0 ? std::min(own, kMaxSteps) : numSteps;
      }

      const double rawStart = tr.BlockStartBeats() / beatsPerStep;
      const double rawPosNow = tr.Beats() / beatsPerStep;
      // A start that is not where the last block ended is a jump (Reset()'s
      // 1e-6 lead is well inside the tolerance, so it still reads as "here").
      const bool jumped = std::fabs(rawStart - mPrevRawPos) > 1e-4;
      const double from = jumped ? rawStart - 1e-6 : mPrevRawPos;
      // Trig conditions count pattern loops from where play started (or last jumped to), like hardware.
      if (tr.IsPlaying() && (!mWasPlaying || jumped))
         mStartK = (long long)std::floor(from + 1e-6);
      mWasPlaying = tr.IsPlaying();

      struct FireEvent
      {
         int lane;
         int frameOffset;
         float vel;
      };
      FireEvent stepEvts[64];
      int numStepEvts = 0;

      // Landmarks in (lo, hi] - or (lo, hi) when the span ends at a loop end,
      // which belongs to the lap - placed over frames [f0, f1).
      auto scan = [&](double lo, double hi, bool hiInclusive, int f0, int f1)
      {
         const double span = hi - lo;
         // Swing (<= 0.5) plus nudge (+-0.48) can move a hit up to one step either way of its slot.
         const int kStart = (int)std::floor(lo - 1.0);
         const int kEnd = (int)std::ceil(hi + 0.5);
         for (int lane = 0; lane < kNumLanes && numStepEvts < 64; lane++)
         {
            const int len = laneLen[lane];
            for (int k = kStart; k <= kEnd && numStepEvts < 64; k++)
            {
               const int stepIndex = ((k % len) + len) % len;
               const bool odd = (((k % 2) + 2) % 2) == 1; // swing follows the time grid, not the lane's own length
               const double landmark = (double)k + (odd ? (double)swing * 0.5 : 0.0) +
                                       (double)mStepNudge[lane][stepIndex].load(std::memory_order_relaxed);
               if (landmark > lo && (hiInclusive ? landmark <= hi : landmark < hi))
               {
                  const float v = mStepVel[lane][stepIndex].load(std::memory_order_relaxed);
                  if (v > 0.0f && TrigPasses(lane, stepIndex, k, len, fillOn))
                  {
                     const double frac = span > 1e-9 ? (landmark - lo) / span : 0.0;
                     const int frameOffset =
                        std::clamp(f0 + (int)(frac * (f1 - f0)), 0, std::max(0, numFrames - 1));
                     stepEvts[numStepEvts++] = { lane, frameOffset, v };
                  }
               }
            }
         }
      };

      if (!tr.IsPlaying())
         mPrevRawPos = from;
      else if (rawPosNow >= from)
      {
         scan(from, rawPosNow, true, 0, numFrames);
         mPrevRawPos = rawPosNow;
      }
      else
      {
         const double loopStart = tr.LoopStartBeats() / beatsPerStep;
         const double loopEnd = tr.LoopEndBeats() / beatsPerStep;
         if (tr.LoopEnabled() && loopEnd > loopStart && from < loopEnd && rawPosNow >= loopStart)
         {
            const double before = loopEnd - from, after = rawPosNow - loopStart;
            const int wrapFrame = std::clamp((int)std::lround(before / std::max(1e-12, before + after) * numFrames), 0, numFrames);
            scan(from, loopEnd, false, 0, wrapFrame);
            scan(loopStart - 1e-6, rawPosNow, true, wrapFrame, numFrames);
         }
         mPrevRawPos = rawPosNow; // anything else moving backwards: resync
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

      // Filter coefficients once per block, only for lanes whose filter is on (state cleared when it turns on).
      int filterType[kNumLanes];
      for (int lane = 0; lane < kNumLanes; lane++)
      {
         filterType[lane] = mLaneFilter[lane].load(std::memory_order_relaxed);
         if (filterType[lane] == 0)
         {
            mSvfOn[lane] = false;
            continue;
         }
         const float hz = std::min(mLaneCutoffHz[lane].load(std::memory_order_relaxed), (float)mSampleRate * 0.45f);
         const float q = mLaneQ[lane].load(std::memory_order_relaxed);
         for (int ch = 0; ch < 2; ch++)
         {
            DspMath::TptSvf& f = mLaneSvf[lane][ch];
            f.SetSampleRate(mSampleRate);
            if (!mSvfOn[lane])
               f.Reset();
            if (!mSvfOn[lane] || hz != mSvfHz[lane] || q != mSvfQ[lane])
               f.SetCutoff(hz, q);
         }
         mSvfOn[lane] = true;
         mSvfHz[lane] = hz;
         mSvfQ[lane] = q;
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

         if (i == 0)
            for (int lane = 0; lane < kNumLanes; lane++)
               if (const float av = mAudition[lane].exchange(0.0f, std::memory_order_relaxed); av > 0.0f)
                  TriggerLane(lane, av, laneVolNow, lanePanNow, lanePitchNow);
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
            const bool ended = voice.rate >= 0.0f ? voice.readPos >= voice.endFrame - 1 : voice.readPos <= voice.startFrame;
            if (ended || (voice.decayCoeff < 1.0f && totalAmp < 1e-4f))
               voice.active = false;
         }

         // Per-lane filter on the lane's own bus (TR-8S style instrument FX), then back into the mix.
         for (int lane = 0; lane < kNumLanes; lane++)
         {
            if (filterType[lane] == 0)
               continue;
            const float dl = laneL[lane], dr = laneR[lane];
            const auto oL = mLaneSvf[lane][0].Process(dl);
            const auto oR = mLaneSvf[lane][1].Process(dr);
            laneL[lane] = filterType[lane] == 1 ? oL.low : oL.high;
            laneR[lane] = filterType[lane] == 1 ? oR.low : oR.high;
            sampleL += laneL[lane] - dl;
            sampleR += laneR[lane] - dr;
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
      PublishCursors();
   }

   // Where each voice is reading (0..1 of its buffer, -1 = idle) and how loud it is, for the
   // lane waveforms' play cursors. Written once per block; the UI reads them relaxed.
   void GetCursors(int lane, float* pos, float* amp) const
   {
      for (int slot = 0; slot < kVoicesPerLane; slot++)
      {
         pos[slot] = mCursorPos[lane * kVoicesPerLane + slot].load(std::memory_order_relaxed);
         amp[slot] = mCursorAmp[lane * kVoicesPerLane + slot].load(std::memory_order_relaxed);
      }
   }

private:
   void PublishCursors()
   {
      for (int v = 0; v < kNumVoices; v++)
      {
         const Voice& voice = mVoices[v];
         const bool on = voice.active && voice.buffer != nullptr && voice.buffer->numFrames > 0;
         mCursorPos[v].store(on ? (float)(voice.readPos / voice.buffer->numFrames) : -1.0f,
                             std::memory_order_relaxed);
         mCursorAmp[v].store(on ? std::clamp(voice.decayAmp * voice.boostEnv * voice.velocity, 0.0f, 1.0f) : 0.0f,
                             std::memory_order_relaxed);
      }
   }

   struct Voice
   {
      const Platform::SampleBuffer* buffer = nullptr;
      double readPos = 0.0;
      double endFrame = 0.0; // voice stops at this frame (laneEnd * buffer->numFrames)
      double startFrame = 0.0; // a reversed voice stops here instead
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
      const bool reverse = mLaneReverse[lane].load(std::memory_order_relaxed);
      voice.startFrame = (double)startFrac * buf->numFrames;
      voice.endFrame = (double)endFrac * buf->numFrames;
      voice.readPos = reverse ? std::max(voice.startFrame, voice.endFrame - 1.0) : voice.startFrame;
      // Accent: full-velocity steps (X cells / a fully dragged cell) play
      // accent semitones higher. Both terms are clamped to +-24 on the main
      // thread, so the sum stays within +-48 st with no clamp needed here.
      const float accent = velocity >= 0.99f ? mLaneAccentPitch[lane].load(std::memory_order_relaxed) : 0.0f;
      // Scaled by the sample's own rate over the device rate, so a 44.1 kHz
      // hit plays at its pitch on a 48 kHz device (1 when rateFix is 0).
      const double srRatio = (mRateFix.load(std::memory_order_relaxed) && buf->sampleRate > 0.0 && mSampleRate > 0.0)
                                ? buf->sampleRate / mSampleRate
                                : 1.0;
      voice.rate = NoteRateForPitch(lanePitchNow[lane] + accent) * (float)srRatio * (reverse ? -1.0f : 1.0f);
      const float accentGain = velocity >= 0.99f ? 1.0f + mLaneAccentGain[lane].load(std::memory_order_relaxed) : 1.0f;
      voice.velocity = velocity * laneVolNow[lane] * accentGain;
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

   // Probability and condition for the hit on `step` at absolute step slot `k`. Probability is a
   // hash of the slot, not a running RNG, so a render of the same bars always plays the same hits.
   bool TrigPasses(int lane, int step, int k, int len, bool fillOn) const
   {
      const int cond = mStepCond[lane][step].load(std::memory_order_relaxed);
      if (cond != 0)
      {
         // Loop number counted in whole lane passes since play started; a pass begins on the lane's step 1,
         // so starting mid-pattern makes that first partial pass loop 0.
         auto passOf = [len](long long x) { return x >= 0 ? x / len : -((-x + len - 1) / len); };
         const long long iter = passOf((long long)k) - passOf(mStartK);
         switch (cond)
         {
         case 1: if (!fillOn) return false; break;
         case 2: if (fillOn) return false; break;
         case 3: if (iter != 0) return false; break;
         case 4: if (iter == 0) return false; break;
         default:
         {
            static constexpr int kAB[9][2] = { { 1, 2 }, { 2, 2 }, { 1, 3 }, { 2, 3 }, { 3, 3 },
                                               { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 } };
            const int ci = std::clamp(cond - 5, 0, 8);
            const long long b = kAB[ci][1];
            if ((((iter % b) + b) % b) != kAB[ci][0] - 1)
               return false;
         }
         }
      }
      const float prob = mStepProb[lane][step].load(std::memory_order_relaxed);
      if (prob >= 1.0f)
         return true;
      uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)(lane * 40503 + step * 9973 + 0x9E3779B9u);
      h ^= h >> 15;
      h *= 0x2C1B3C6Du;
      h ^= h >> 12;
      return (float)(h & 0xFFFFFF) / 16777216.0f < prob;
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
   std::atomic<float> mCursorPos[kNumVoices] = {};
   std::atomic<float> mCursorAmp[kNumVoices] = {};
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
   std::atomic<float> mLaneAccentGain[kNumLanes] = {}; // linear gain minus 1, so zero-init = no boost
   std::atomic<float> mStepVel[kNumLanes][kMaxSteps] = {};
   std::atomic<float> mAudition[kNumLanes] = {};
   std::atomic<float> mStepProb[kNumLanes][kMaxSteps] = {};
   std::atomic<int> mStepCond[kNumLanes][kMaxSteps] = {};
   std::atomic<float> mStepNudge[kNumLanes][kMaxSteps] = {};
   std::atomic<int> mLaneSteps[kNumLanes] = {};
   std::atomic<bool> mLaneReverse[kNumLanes] = {};
   std::atomic<int> mLaneFilter[kNumLanes] = {};
   std::atomic<float> mLaneCutoffHz[kNumLanes] = {};
   std::atomic<float> mLaneQ[kNumLanes] = {};
   std::atomic<bool> mFill { false };
   DspMath::TptSvf mLaneSvf[kNumLanes][2];
   bool mSvfOn[kNumLanes] = {};
   float mSvfHz[kNumLanes] = {};
   float mSvfQ[kNumLanes] = {};
   long long mStartK = 0;
   bool mWasPlaying = false;
   std::atomic<float> mLaneStart[kNumLanes] = {};
   std::atomic<float> mLaneEnd[kNumLanes] = {};

   std::atomic<int> mRate { 12 };
   std::atomic<int> mNumSteps { 8 };
   std::atomic<float> mSwing { 0.0f };
   std::atomic<float> mMasterVolume { 0.8f };
   std::atomic<bool> mRun { true };
   std::atomic<bool> mRateFix { true };

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
      laneAccentPitch[lane] = 0.0f;
      laneAccentLevel[lane] = 0.0f;
      laneCutoff[lane] = 1.0f;
      for (int st = 0; st < kMaxSteps; st++)
      {
         stepProb[lane][st] = 1.0f;
         mLastStepProb[lane][st] = -1.0f;
         mLastStepCond[lane][st] = -1;
         mLastStepNudge[lane][st] = -9.0f;
      }
      mLastLaneSteps[lane] = -1;
      mLastLaneReverse[lane] = false;
      mLastLaneFilter[lane] = -1;
      mLastLaneCutoff[lane] = -1.0f;
      mLastLaneReso[lane] = -1.0f;

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
      mLastLaneAccentPitch[lane] = -999.0f;
      mLastLaneAccentLevel[lane] = -999.0f;
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
   editPage = std::clamp(editPage, 0, (std::clamp(numSteps, 1, kMaxSteps) - 1) / kEditPageSteps);
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
   mAudioNode->SetRateFix(rateFix != 0);
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

      if (mFirstCook || laneAccentPitch[lane] != mLastLaneAccentPitch[lane])
      {
         // Clamped here, on the main thread; a patch or a modulator can hand
         // over anything. NaN reads as "no accent".
         const float acc = std::isfinite(laneAccentPitch[lane]) ? std::clamp(laneAccentPitch[lane], -24.0f, 24.0f) : 0.0f;
         mAudioNode->PushLaneAccent(lane, acc);
         mLastLaneAccentPitch[lane] = laneAccentPitch[lane];
      }

      if (mFirstCook || laneAccentLevel[lane] != mLastLaneAccentLevel[lane])
      {
         const float db = std::isfinite(laneAccentLevel[lane]) ? std::clamp(laneAccentLevel[lane], 0.0f, 12.0f) : 0.0f;
         mAudioNode->PushLaneAccentGain(lane, std::pow(10.0f, db / 20.0f));
         mLastLaneAccentLevel[lane] = laneAccentLevel[lane];
      }

      for (int step = 0; step < kMaxSteps; step++)
      {
         if (mFirstCook || stepVel[lane][step] != mLastStepVel[lane][step])
         {
            mAudioNode->PushStep(lane, step, stepVel[lane][step]);
            mLastStepVel[lane][step] = stepVel[lane][step];
         }
         if (stepProb[lane][step] != mLastStepProb[lane][step] || stepCond[lane][step] != mLastStepCond[lane][step] ||
             stepNudge[lane][step] != mLastStepNudge[lane][step])
         {
            mAudioNode->PushTrig(lane, step, std::clamp(stepProb[lane][step], 0.0f, 1.0f),
                                 std::clamp(stepCond[lane][step], 0, kNumConds - 1),
                                 std::clamp(stepNudge[lane][step], -0.48f, 0.48f));
            mLastStepProb[lane][step] = stepProb[lane][step];
            mLastStepCond[lane][step] = stepCond[lane][step];
            mLastStepNudge[lane][step] = stepNudge[lane][step];
         }
      }
      if (laneSteps[lane] != mLastLaneSteps[lane] || laneReverse[lane] != mLastLaneReverse[lane] ||
          laneFilter[lane] != mLastLaneFilter[lane] || laneCutoff[lane] != mLastLaneCutoff[lane] ||
          laneReso[lane] != mLastLaneReso[lane])
      {
         const float c = std::clamp(laneCutoff[lane], 0.0f, 1.0f), r = std::clamp(laneReso[lane], 0.0f, 1.0f);
         mAudioNode->PushLaneShape(lane, std::clamp(laneSteps[lane], 0, kMaxSteps), laneReverse[lane],
                                   std::clamp(laneFilter[lane], 0, 2), 20.0f * std::pow(1000.0f, c), 0.5f + 9.5f * r * r);
         mLastLaneSteps[lane] = laneSteps[lane];
         mLastLaneReverse[lane] = laneReverse[lane];
         mLastLaneFilter[lane] = laneFilter[lane];
         mLastLaneCutoff[lane] = laneCutoff[lane];
         mLastLaneReso[lane] = laneReso[lane];
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

   if (fill != mLastFill)
   {
      mAudioNode->PushFill(fill);
      mLastFill = fill;
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
      snprintf(name, sizeof(name), "lane%d_kit", lane);
      v.Bool(name, laneKit[lane]);
      snprintf(name, sizeof(name), "lane%d_accentPitch", lane);
      v.Float(name, laneAccentPitch[lane]);
      snprintf(name, sizeof(name), "lane%d_accentLevel", lane);
      v.Float(name, laneAccentLevel[lane]);
      snprintf(name, sizeof(name), "lane%d_start", lane);
      v.Float(name, laneStart[lane]);
      snprintf(name, sizeof(name), "lane%d_end", lane);
      v.Float(name, laneEnd[lane]);
      for (int step = 0; step < kMaxSteps; step++)
      {
         snprintf(name, sizeof(name), "lane%d_step%d", lane, step);
         v.Float(name, stepVel[lane][step]);
         stepVel[lane][step] = std::clamp(stepVel[lane][step], 0.0f, 1.0f); // the grid's range; 1e9 played ~3e8 loud (B-K-036)
      }
      snprintf(name, sizeof(name), "lane%d_len", lane);
      v.Int(name, laneSteps[lane]);
      laneSteps[lane] = std::clamp(laneSteps[lane], 0, kMaxSteps);
      snprintf(name, sizeof(name), "lane%d_reverse", lane);
      v.Bool(name, laneReverse[lane]);
      snprintf(name, sizeof(name), "lane%d_filter", lane);
      v.Int(name, laneFilter[lane]);
      laneFilter[lane] = std::clamp(laneFilter[lane], 0, 2);
      snprintf(name, sizeof(name), "lane%d_cutoff", lane);
      v.Float(name, laneCutoff[lane]);
      snprintf(name, sizeof(name), "lane%d_reso", lane);
      v.Float(name, laneReso[lane]);
      // Trig data packs into one text per lane ("" when plain): per step PP C NN in hex =
      // probability 0..100, condition, nudge+50 (hundredths of a step). Keeps a saved patch small.
      {
         std::string packed;
         bool plain = true;
         for (int st = 0; st < kMaxSteps; st++)
            if (stepProb[lane][st] < 1.0f || stepCond[lane][st] != 0 || stepNudge[lane][st] != 0.0f)
               plain = false;
         if (!plain)
         {
            char cell[16];
            for (int st = 0; st < kMaxSteps; st++)
            {
               snprintf(cell, sizeof(cell), "%02x%x%02x", (int)std::lround(std::clamp(stepProb[lane][st], 0.0f, 1.0f) * 100.0f),
                        std::clamp(stepCond[lane][st], 0, kNumConds - 1),
                        (int)std::lround(std::clamp(stepNudge[lane][st], -0.48f, 0.48f) * 100.0f) + 50);
               packed += cell;
            }
         }
         snprintf(name, sizeof(name), "lane%d_trigs", lane);
         v.Text(name, packed);
         if (packed.size() == (size_t)kMaxSteps * 5)
            for (int st = 0; st < kMaxSteps; st++)
            {
               const std::string c = packed.substr((size_t)st * 5, 5);
               stepProb[lane][st] = std::clamp((float)std::strtol(c.substr(0, 2).c_str(), nullptr, 16) / 100.0f, 0.0f, 1.0f);
               stepCond[lane][st] = std::clamp((int)std::strtol(c.substr(2, 1).c_str(), nullptr, 16), 0, kNumConds - 1);
               stepNudge[lane][st] = std::clamp((float)(std::strtol(c.substr(3, 2).c_str(), nullptr, 16) - 50) / 100.0f, -0.48f, 0.48f);
            }
      }
   }
   v.Int("rate", rate);
   rate = std::clamp(rate, 0, (int)MusicTime::kNumRateDivisions - 1); // loaded unchecked; cast to RateDivision
   v.Int("rateFix", rateFix); // not a control: 0 = plays samples at the device rate (saved before the fix)
   v.Int("steps", numSteps);
   v.Float("swing", swing);
   v.Float("volume", volume);
   volume = std::clamp(volume, 0.0f, 1.0f);
   v.Bool("run", run);
   v.Text("patternName", patternName);
   {
      v.Text("pagename", pageName);
      if (pageName.size() > 48)
         pageName.resize(48);
      char hex[16];
      snprintf(hex, sizeof(hex), "%06X", pageColor & 0xFFFFFFu);
      std::string col = pageColor ? hex : "";
      v.Text("pagecolor", col);
      pageColor = col.empty() ? 0u : (unsigned int)(strtoul(col.c_str(), nullptr, 16) & 0xFFFFFFu);
      v.Int("pageorder", pageOrder);
   }
   v.Int("patternPart", patternPart);
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

int DrumSequencerNode::LaneStepCount(int lane) const
{
   const int own = laneSteps[Clamp(lane)];
   return own > 0 ? std::min(own, kMaxSteps) : std::clamp(numSteps, 1, kMaxSteps);
}

int DrumSequencerNode::LaneCurrentStep(int lane) const
{
   const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)rate));
   const int len = LaneStepCount(lane);
   const int rawStep = (int)std::floor(Transport::Instance().Beats() / beatsPerStep);
   return ((rawStep % len) + len) % len;
}

void DrumSequencerNode::ResetTrigs(int lane)
{
   for (int l = 0; l < kNumLanes; l++)
      if (lane < 0 || l == lane)
         for (int st = 0; st < kMaxSteps; st++)
         {
            stepProb[l][st] = 1.0f;
            stepCond[l][st] = 0;
            stepNudge[l][st] = 0.0f;
         }
}

const char* DrumSequencerNode::CondName(int cond)
{
   static const char* kNames[kNumConds] = { "--",  "Fill", "!Fill", "1st", "!1st", "1:2", "2:2",
                                            "1:3", "2:3",  "3:3",   "1:4", "2:4",  "3:4", "4:4" };
   return kNames[std::clamp(cond, 0, kNumConds - 1)];
}

void DrumSequencerNode::Audition(int lane, float vel)
{
   if (mAudioNode)
      mAudioNode->PushAudition(Clamp(lane), std::clamp(vel, 0.05f, 1.0f));
}

int DrumSequencerNode::LoadedLaneCount() const
{
   int n = 0;
   for (int lane = 0; lane < kNumLanes; lane++)
      if (!laneFileName[lane].empty())
         n++;
   return n;
}

namespace
{
   std::string gKitDir;

   // A lane path saved from a bundled-kit load points inside the app's
   // Resources/drumkits/infinite-basic folder, which moves when the app is
   // reinstalled elsewhere (or a patch travels to another machine). Returns
   // where the same file would be in the current kit folder, or "" when `path`
   // is not a bundled-kit path or no kit folder is set. Splits on both / and backslash
   // because the saved path may come from another OS.
   std::string StaleKitRetryPath(const std::string& path)
   {
      if (gKitDir.empty())
         return {};
      const size_t last = path.find_last_of("/\\");
      if (last == std::string::npos || last == 0)
         return {};
      const size_t prev = path.find_last_of("/\\", last - 1);
      const std::string parent = path.substr(prev == std::string::npos ? 0 : prev + 1,
                                             last - (prev == std::string::npos ? 0 : prev + 1));
      if (parent != "infinite-basic")
         return {};
      const char sep = gKitDir.find('\\') != std::string::npos ? '\\' : '/';
      const char tail = gKitDir.back();
      return gKitDir + ((tail == '/' || tail == '\\') ? "" : std::string(1, sep)) + path.substr(last + 1);
   }
}

void DrumSequencerNode::SetKitDir(const std::string& dir) { gKitDir = dir; }
const std::string& DrumSequencerNode::KitDir() { return gKitDir; }

namespace
{
   std::string gUserKitRoot;

   struct UserLane { std::string path; float gainDb = 0.0f; float tune = 0.0f; };

   // The first kit (by file name) in <root>/<genre slug>/, or false when there is none.
   bool FindUserKit(const char* genre, UserLane (&lanes)[8])
   {
      if (gUserKitRoot.empty() || genre == nullptr || genre[0] == '\0')
         return false;
      std::string slug;
      for (const char* c = genre; *c; c++)
      {
         const char ch = (char)std::tolower((unsigned char)*c);
         if (std::isalnum((unsigned char)ch)) slug += ch;
         else if (!slug.empty() && slug.back() != '-') slug += '-';
      }
      while (!slug.empty() && slug.back() == '-') slug.pop_back();
      std::error_code ec;
      const std::filesystem::path dir = std::filesystem::u8path(gUserKitRoot) / slug;
      if (!std::filesystem::is_directory(dir, ec))
         return false;
      std::vector<std::filesystem::path> files;
      for (const auto& e : std::filesystem::directory_iterator(dir, ec))
         if (e.path().extension() == ".json")
            files.push_back(e.path());
      if (files.empty())
         return false;
      std::sort(files.begin(), files.end());
      std::ifstream f(files.front());
      const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
      if (j.is_discarded() || !j.contains("lanes") || !j["lanes"].is_array())
         return false;
      const auto& arr = j["lanes"];
      for (size_t i = 0; i < arr.size() && i < 8; i++)
      {
         if (!arr[i].is_object()) continue;
         lanes[i].path = arr[i].value("path", std::string());
         lanes[i].gainDb = arr[i].value("gain_db", 0.0f);
         lanes[i].tune = arr[i].value("tune", 0.0f);
      }
      return true;
   }
}

void DrumSequencerNode::SetUserKitRoot(const std::string& dir) { gUserKitRoot = dir; }
const std::string& DrumSequencerNode::UserKitRoot() { return gUserKitRoot; }

bool DrumSequencerNode::TryLoadLane(int lane, const std::string& path, std::string& error)
{
   auto* decoded = new Platform::SampleBuffer();
   error.clear();
   if (!AudioDecodeCache::DecodeCached(path, *decoded, error))
   {
      delete decoded;
      return false;
   }
   const size_t slash = path.find_last_of("/\\");
   const std::string fileName = (slash == std::string::npos) ? path : path.substr(slash + 1);
   FinishLaneBuffer(lane, decoded, fileName, path, "loaded");
   return true;
}

bool DrumSequencerNode::LoadFileToLane(int lane, const std::string& path)
{
   lane = Clamp(lane);
   laneKit[lane] = false;   // the user's own sample from now on (ReloadFromPaths puts the flag back)
   std::string error;
   if (TryLoadLane(lane, path, error))
      return true;
   // Stale bundled-kit path: retry from the current kit folder, and on success
   // FinishLaneBuffer has already rewritten laneFilePath to the new location.
   const std::string retry = StaleKitRetryPath(path);
   if (!retry.empty() && retry != path)
   {
      std::string retryError;
      if (TryLoadLane(lane, retry, retryError))
         return true;
   }
   laneStatus[lane] = error.empty() ? "failed to load" : error;
   return false;
}

void DrumSequencerNode::SweepPrepare()
{
   // A tone on every lane and a hit on every step of each, so lane and step params have something to
   // change; the sweep ticks the transport (SweepNeedsClock) so the sequencer actually steps.
   // 1/32 so every probe block (about 1.4 steps long) contains a step boundary; at 1/16 every third one is empty.
   rate = MusicTime::kThirtySecond;
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      FinishLaneBuffer(lane, MakeSweepToneBuffer(), "sweep-tone", "sweep-tone.wav", "loaded");
      for (int s = 0; s < kMaxSteps; s++)
         stepVel[lane][s] = (s % 3 == 0) ? 1.0f : 0.5f; // accentPitch (>= 0.99 only) is audible on every third step; the
                                                         // unevenness lets a changed step count change the output
   }
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
   mLastLaneStart[lane] = -1.0f;
   mLastLaneEnd[lane] = -1.0f;
}

void DrumSequencerNode::ReloadFromPaths()
{
   // Preserve start/end trim settings across file reloads (mirrors
   // SamplerNode::ReloadFromPath) - patch loading, undo/redo, and copy/paste
   // all restore node params first and then call ReloadDerivedState ->
   // ReloadFromPaths to reload decoded sample buffers. Without preserving
   // them here, FinishLaneBuffer's fresh-buffer defaults (0..1) wipe out
   // the user's trim settings on every reload and undo checkpoint.
   float savedStart[kNumLanes];
   float savedEnd[kNumLanes];
   bool savedKit[kNumLanes];
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      savedStart[lane] = laneStart[lane];
      savedEnd[lane] = laneEnd[lane];
      savedKit[lane] = laneKit[lane];
   }
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      if (!laneFilePath[lane].empty())
      {
         LoadFileToLane(lane, laneFilePath[lane]);
         laneStart[lane] = savedStart[lane];
         laneEnd[lane] = savedEnd[lane];
         laneKit[lane] = savedKit[lane];
      }
      else if (!laneFileName[lane].empty())
      {
         // The restored state says "no sample" but this node still holds one
         // (e.g. a state captured before a kit auto-fill, applied in place):
         // follow the restored state rather than keep the stale buffer.
         ClearLane(lane);
         laneStart[lane] = savedStart[lane];
         laneEnd[lane] = savedEnd[lane];
      }
   }
}

void DrumSequencerNode::Randomize()
{
   ResetTrigs(-1);
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
   ResetTrigs(-1);
   for (int lane = 0; lane < kNumLanes; lane++)
      for (int s = 0; s < kMaxSteps; s++)
         stepVel[lane][s] = 0.0f;
}

void DrumSequencerNode::RandomizeLane(int lane)
{
   lane = Clamp(lane);
   ResetTrigs(lane);
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

void DrumSequencerNode::LaneCursors(int lane, float* pos, float* amp) const
{
   for (int slot = 0; slot < kVoicesPerLane; slot++)
   {
      pos[slot] = -1.0f;
      amp[slot] = 0.0f;
   }
   if (mAudioNode && lane >= 0 && lane < kNumLanes)
      mAudioNode->GetCursors(lane, pos, amp);
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
   laneKit[lane] = false;
   laneStatus[lane] = "--";
   laneWaveCount[lane] = 0;
   lanePeaks[lane].Clear();
   laneStart[lane] = 0.0f;
   laneEnd[lane] = 1.0f;
   mLastLaneDecay[lane] = -1.0f;
   mLastLaneStart[lane] = -1.0f;
   mLastLaneEnd[lane] = -1.0f;
}

void DrumSequencerNode::ApplyPattern(const DrumPatterns::Groove& g, int part)
{
   ResetTrigs(-1);
   part = std::clamp(part, 0, 2);
   const DrumPatterns::Part& p = g.parts[part];
   const int steps = std::clamp(p.steps, 1, kMaxSteps);

   // Every slot, not just the part's own length: a 12-step part applied over a
   // 32-step pattern must not leave steps 12..31 of the old pattern behind.
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      for (int s = 0; s < kMaxSteps; s++)
         stepVel[lane][s] = 0.0f;
      const char* cells = p.lanes[lane];
      if (cells == nullptr)
         continue;
      for (int s = 0; s < steps && cells[s] != '\0'; s++)
         stepVel[lane][s] = DrumPatterns::CellVelocity(cells[s]);
   }

   rate = std::clamp(g.rate, 0, (int)MusicTime::kNumRateDivisions - 1);
   swing = std::clamp(g.swing, 0.0f, 1.0f);
   numSteps = steps;
   editPage = 0;
   // Assigned for every lane, so a lane the groove gives no accent (0) drops
   // the accent a previous groove left on it.
   for (int lane = 0; lane < kNumLanes; lane++)
      laneAccentPitch[lane] = std::clamp(g.tones[lane], -24.0f, 24.0f);
   patternName = g.name;
   patternPart = part;

   LoadKitIntoEmptyLanes(p, g.category);
}

namespace
{
   // General MIDI percussion key -> lane (DrumPatterns::LaneRole order), -1 = no lane.
   int GmDrumLane(int note)
   {
      switch (note)
      {
         case 35: case 36: return 0;                                  // kick
         case 38: case 40: return 1;                                  // snare
         case 42: case 44: return 2;                                  // closed / pedal hat
         case 46: return 3;                                           // open hat
         case 37: case 39: return 4;                                  // side stick, clap
         case 41: case 43: case 45: case 64: case 66: return 5;       // low / floor toms, low conga, low timbale
         case 47: case 48: case 50: case 60: case 61:
         case 62: case 63: case 65: return 6;                         // high toms, bongos, high congas, high timbale
         case 49: case 51: case 52: case 53: case 55: case 56: case 57:
         case 59: case 67: case 68: return 7;                         // crash / ride / china / splash, cowbell, agogo
         default: return -1;
      }
   }
}

int DrumSequencerNode::ApplyMidiSong(const MidiFile::Song& song, const std::string& name)
{
   ResetTrigs(-1);
   constexpr int kStepsPerBeat = 4; // 1/16
   int laneOf[128];
   // GM drum file: it uses the percussion channel (10), or every hit is a GM drum key.
   bool percussionChannel = false, allGm = true;
   for (const MidiFile::Event& e : song.events)
      if (e.velocity > 0)
      {
         percussionChannel = percussionChannel || e.channel == 9;
         allGm = allGm && GmDrumLane(e.note) >= 0;
      }
   if (percussionChannel || allGm)
      for (int k = 0; k < 128; k++)
         laneOf[k] = GmDrumLane(k);
   else
   {
      // Not a GM drum file: the 8 lowest pitches it uses, in order.
      bool used[128] = {};
      for (const MidiFile::Event& e : song.events)
         if (e.velocity > 0)
            used[e.note] = true;
      int next = 0;
      for (int k = 0; k < 128; k++)
         laneOf[k] = (used[k] && next < kNumLanes) ? next++ : -1;
   }

   // Two bars at most; one bar when the file fits in one.
   const int steps = (song.lengthBeats * kStepsPerBeat > 16.0 + 1e-6) ? kMaxSteps : kEditPageSteps;
   for (int lane = 0; lane < kNumLanes; lane++)
      for (int st = 0; st < kMaxSteps; st++)
         stepVel[lane][st] = 0.0f;
   int placed = 0;
   int truncated = 0;
   bool laneUsed[kNumLanes] = {};
   for (const MidiFile::Event& e : song.events)
   {
      if (e.velocity == 0)
         continue;
      const int lane = laneOf[e.note];
      int st = (int)std::lround(e.beat * kStepsPerBeat);
      // A pickup just before the end of the pattern rounds onto the step past
      // it: it belongs to the top of the loop, not to the bin.
      if (st == steps)
         st = 0;
      if (lane < 0)
         continue;
      if (st < 0 || st >= steps)
      {
         truncated++;
         continue;
      }
      const float v = std::clamp((float)e.velocity / 127.0f, 0.05f, 1.0f);
      if (stepVel[lane][st] == 0.0f)
         placed++;
      stepVel[lane][st] = std::max(stepVel[lane][st], v); // two hits on one cell: the louder
      laneUsed[lane] = true;
   }

   // Velocity 127 would read as an accent (>= 0.99 steps play the lane's accent
   // pitch / level) on every hit of a file that simply plays loud. When most of
   // a lane's hits are at full velocity, full velocity is the norm there, not
   // an accent, so those hits drop just below the accent threshold. A lane
   // with a few full-velocity hits among softer ones keeps them as accents.
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      int cnt = 0;
      int loud = 0;
      for (int st = 0; st < steps; st++)
         if (stepVel[lane][st] > 0.0f)
         {
            cnt++;
            loud += stepVel[lane][st] >= 0.99f ? 1 : 0;
         }
      if (cnt < 2 || loud * 2 <= cnt)
         continue;
      for (int st = 0; st < steps; st++)
         if (stepVel[lane][st] >= 0.99f)
            stepVel[lane][st] = 0.98f;
   }

   if (truncated > 0)
      fprintf(stderr, "Drum Sequencer: '%s' is longer than 2 bars; %d hit(s) past the end were left out\n", name.c_str(),
              truncated);

   rate = MusicTime::kSixteenth;
   swing = 0.0f;
   numSteps = steps;
   editPage = 0;
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      laneAccentPitch[lane] = 0.0f;
      laneAccentLevel[lane] = 0.0f;
   }
   patternName = name;
   patternPart = 0;

   DrumPatterns::Part part = { steps, {} };
   for (int lane = 0; lane < kNumLanes; lane++)
      part.lanes[lane] = laneUsed[lane] ? "x" : nullptr;
   LoadKitIntoEmptyLanes(part);
   return placed;
}

bool DrumSequencerNode::ApplyMidiFile(const std::string& path, std::string& error)
{
   MidiFile::Song song;
   if (!MidiFile::Load(path, song, error))
      return false;
   if (song.noteCount == 0)
   {
      error = "no notes in the file";
      return false;
   }
   std::string name = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
   const size_t dot = name.find_last_of('.');
   if (dot != std::string::npos && dot > 0)
      name = name.substr(0, dot);
   ApplyMidiSong(song, name);
   return true;
}

void DrumSequencerNode::LoadKitIntoEmptyLanes(const DrumPatterns::Part& part, const char* genre)
{
   UserLane user[8];
   const bool hasUser = FindUserKit(genre, user);
   if (gKitDir.empty() && !hasUser)
      return;
   const char sep = gKitDir.find('\\') != std::string::npos ? '\\' : '/';
   const char tail = gKitDir.empty() ? '/' : gKitDir.back();
   const std::string base = gKitDir + ((tail == '/' || tail == '\\') ? "" : std::string(1, sep));

   bool filled[kNumLanes] = {};
   for (int lane = 0; lane < kNumLanes; lane++)
   {
      // Empty = nothing loaded AND no path to a sample that is merely
      // missing: a lane the user loaded (even one whose file has since moved)
      // is theirs and is never replaced.
      // A lane a kit filled is the groove's to change; a lane the user loaded is never touched.
      const bool fromKit = laneKit[lane] && !laneFilePath[lane].empty();
      if (part.lanes[lane] == nullptr || (!fromKit && (!laneFilePath[lane].empty() || !laneFileName[lane].empty())))
         continue;
      std::string error; // a missing kit is silent: lane status stays "--"
      if (hasUser && !user[lane].path.empty() && TryLoadLane(lane, user[lane].path, error))
      {
         // The kit's loudness match (gain_db against the lane's 0.8 default; lane volume tops out at 1).
         laneVolume[lane] = std::clamp(0.8f * std::pow(10.0f, user[lane].gainDb / 20.0f), 0.0f, 1.0f);
         lanePitch[lane] = std::clamp(user[lane].tune, -24.0f, 24.0f);
         filled[lane] = laneKit[lane] = true;
         continue;
      }
      if (!gKitDir.empty() && (!fromKit || laneFilePath[lane] != base + DrumPatterns::KitFile(lane)))
      {
         filled[lane] = TryLoadLane(lane, base + DrumPatterns::KitFile(lane), error);
         if (filled[lane])
         {
            laneKit[lane] = true;
            if (fromKit)   // the last kit's level and tuning were for its own sample
            {
               laneVolume[lane] = 0.8f;
               lanePitch[lane] = 0.0f;
            }
         }
      }
   }
   // Closed hat (lane 2) cuts the open hat (lane 3) - only when this call put
   // both there and the user has not set a choke group on either.
   if (filled[2] && filled[3] && laneChoke[2] == 0 && laneChoke[3] == 0)
   {
      laneChoke[2] = 1;
      laneChoke[3] = 1;
   }
}
