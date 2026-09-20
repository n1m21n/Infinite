#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "AnalogNode.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/SynthModes.h"
#include "audio/dsp/ZdfLadderFilter.h"

namespace AnalogSynthCore
{
   constexpr int kMaxVoices = AnalogNode::kMaxVoices;
   constexpr int kMaxUnison = AnalogNode::kMaxUnison;

   enum GlobalParam
   {
      kVolume = 0,
      kFreq,
      kGlide,
      kPitchBend,
      kFine1,
      kSemi1,
      kOct1,
      kFine2,
      kSemi2,
      kOct2,
      kPw1,
      kOsc1Vol,
      kOsc2Vol,
      kVoices,
      kSpread,
      kFm,
      kDetune,
      kOscMix,
      kSub,
      kNoise,
      kCutoff,
      kResonance,
      kDrive,
      kKeyTrack,
      kAttack,
      kDecay,
      kSustain,
      kRelease,
      kNumParams
   };

   // Dedicated slot for timeline clip pitch transpose
   constexpr int kClipPitchParam = kNumParams;
   static_assert(kClipPitchParam < ParamMailbox::kMaxParams,
                 "AnalogNode smoothed params no longer fit ParamMailbox");

   // Deterministic golden-ratio phase seeds for unison voices
   constexpr float kVoicePhaseSeed[kMaxUnison] = {
      0.000000f, 0.618034f, 0.236068f, 0.854102f,
      0.472136f, 0.090170f, 0.708204f
   };

   // Per-voice subtle analog tolerance pitch offsets (-0.08 to +0.08 semitones)
   constexpr float kVoiceDriftPitch[kMaxVoices] = {
      -0.035f, 0.022f, -0.018f, 0.041f, -0.027f, 0.033f, -0.045f, 0.015f
   };

   // Per-voice subtle analog tolerance cutoff offsets in octaves (-0.1 to +0.1)
   constexpr float kVoiceDriftCutoff[kMaxVoices] = {
      0.05f, -0.04f, 0.02f, -0.06f, 0.03f, -0.02f, 0.07f, -0.05f
   };

   // Raw per-unison-voice VCO tolerance figures, normalised per stack size by
   // NormalizeUnisonDetune below. Deliberately irregular: a real oscillator
   // bank is a set of independently mistuned circuits, not an evenly spaced
   // fan, and the uneven interval pattern is most of what makes a stacked
   // analog osc sound like several oscillators rather than one chorused one.
   // No two gaps here are equal and no value is a simple ratio of another, so
   // the beat frequencies between pairs never line up into a single rate.
   constexpr float kUnisonTolerance[kMaxUnison] = {
      -0.91f, 0.37f, 0.78f, -0.29f, 1.00f, -0.63f, 0.14f
   };

   // Writes `count` detune multipliers spanning exactly [-1, +1] into `out`.
   // The used subset of kUnisonTolerance is de-meaned (so a stack is never
   // globally sharp or flat, whatever its size) and then scaled so the
   // outermost pair lands on +/-1 - which lets the caller treat the detune
   // knob as a true total width in cents at every voice count.
   inline void NormalizeUnisonDetune(int count, float* out)
   {
      if (count <= 1)
      {
         out[0] = 0.0f;
         return;
      }
      // Centre on the midpoint of the used subset, not its mean, and scale by
      // half its range. That puts the outermost pair on exactly -1 and +1 at
      // every stack size, which is what lets the caller promise the detune
      // knob is a true total width; de-meaning instead would leave the span
      // slightly short whenever the subset is lopsided.
      float lo = kUnisonTolerance[0], hi = kUnisonTolerance[0];
      for (int u = 1; u < count; ++u)
      {
         lo = std::min(lo, kUnisonTolerance[u]);
         hi = std::max(hi, kUnisonTolerance[u]);
      }
      const float mid = (lo + hi) * 0.5f;
      const float halfRange = (hi - lo) * 0.5f;
      const float scale = (halfRange > 1e-6f) ? (1.0f / halfRange) : 0.0f;
      for (int u = 0; u < count; ++u)
         out[u] = (kUnisonTolerance[u] - mid) * scale;
   }

   // Per-voice-card stereo positions, the way an OB-Xa or Prophet-10 places
   // each voice board in the field: fixed per card and irregular, so a chord
   // lands across the image in an order that has nothing to do with pitch.
   constexpr float kVoicePanSeed[kMaxVoices] = {
      -0.82f, 0.47f, 0.93f, -0.31f, 0.18f, -0.67f, 0.74f, -0.12f
   };

   // Equal-power pan from position [-1, +1] to a pair of gains whose squares
   // sum to 1. Scaled by sqrt(2) so a centred source returns 1.0 per side -
   // i.e. pan 0 is bit-identical to the mono path it replaces.
   inline void TolerancePan(float pos, float& gL, float& gR)
   {
      const float p = std::clamp(pos, -1.0f, 1.0f);
      const float theta = (p + 1.0f) * (float)M_PI * 0.25f;
      constexpr float kUnityCentre = 1.41421356f;
      gL = cosf(theta) * kUnityCentre;
      gR = sinf(theta) * kUnityCentre;
   }

   constexpr float kPolyNormSmoothSec = 0.015f;

   inline float NoteToHz(float midiNote)
   {
      return 440.0f * powf(2.0f, (midiNote - 69.0f) / 12.0f);
   }

   inline int AnalogWaveToDsp(int wave)
   {
      switch (wave)
      {
         case kAWaveSaw: return DspMath::kWaveSaw;
         case kAWaveSquare: return DspMath::kWaveSquare;
         case kAWaveTriangle: return DspMath::kWaveTriangle;
         case kAWaveSine: return DspMath::kWaveSine;
         default: return DspMath::kWaveSaw;
      }
   }
}

class AudioAnalogNode : public AudioNode
{
public:
   struct Voice
   {
      bool active = false;
      bool held = false;
      int note = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      float bend = 0.0f;
      uint64_t age = 0;

      Envelope ampEnv;

      DspMath::PolyBlepOsc osc1[AnalogSynthCore::kMaxUnison];
      DspMath::PolyBlepOsc osc2;
      // Sub osc: its own accumulator, run at half osc1's frequency. It cannot
      // read osc1's phase scaled by 0.5 - scaling a phase compresses its
      // range, it does not halve its rate, so such a "sub" never crosses its
      // own comparison threshold and emits constant DC instead of a tone.
      DspMath::PolyBlepOsc sub;
      DspMath::WhiteNoise noise;
      DspMath::OnePole glide;

      // Two independent filter chains. The stack's unison voices are split
      // between them, so left and right carry different oscillators through
      // different filter instances - decorrelation, not a panned copy of one
      // mono signal. `R` idles unless the stack has something to split.
      ZdfLadderFilter::State ladder;
      ZdfLadderFilter::State ladderR;
      DspMath::TptSvf svf[SynthModes::kMaxFilterStages];
      DspMath::TptSvf svfR[SynthModes::kMaxFilterStages];
      bool stereoPath = false;

      float driftPitchOffset = 0.0f;
      float driftCutoffOffset = 0.0f;
      float lastOut = 0.0f;

      void Reset(double sampleRate)
      {
         for (int u = 0; u < AnalogSynthCore::kMaxUnison; ++u)
         {
            osc1[u].phase = (double)AnalogSynthCore::kVoicePhaseSeed[u];
            osc1[u].phaseInc = 0.0;
         }
         osc2.phase = 0.0;
         osc2.phaseInc = 0.0;
         sub.phase = 0.0;
         sub.phaseInc = 0.0;
         ladder.Reset();
         ladderR.Reset();
         for (int s = 0; s < SynthModes::kMaxFilterStages; ++s)
         {
            svf[s].Reset();
            svf[s].SetSampleRate(sampleRate);
            svfR[s].Reset();
            svfR[s].SetSampleRate(sampleRate);
         }
         stereoPath = false;
         ampEnv.SetSampleRate(sampleRate);
         lastOut = 0.0f;
      }
   };

   AudioAnalogNode()
   {
      for (int i = 0; i < AnalogSynthCore::kMaxVoices; ++i)
      {
         mVoices[i].driftPitchOffset = AnalogSynthCore::kVoiceDriftPitch[i];
         mVoices[i].driftCutoffOffset = AnalogSynthCore::kVoiceDriftCutoff[i];
      }
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      mPolyNormSmooth.SetTimeConstant(AnalogSynthCore::kPolyNormSmoothSec, mSampleRate);
      mFreeGlide.SetTimeConstant(0.0f, mSampleRate);

      for (auto& v : mVoices)
         v.Reset(mSampleRate);

      mFreeLadder.Reset();
      mFreeLadderR.Reset();
      mFreeStereoPath = false;
      for (int s = 0; s < SynthModes::kMaxFilterStages; ++s)
      {
         mFreeSvf[s].Reset();
         mFreeSvf[s].SetSampleRate(mSampleRate);
         mFreeSvfR[s].Reset();
         mFreeSvfR[s].SetSampleRate(mSampleRate);
      }
      for (int u = 0; u < AnalogSynthCore::kMaxUnison; ++u)
      {
         mFreeOsc1[u].phase = (double)AnalogSynthCore::kVoicePhaseSeed[u];
         mFreeOsc1[u].phaseInc = 0.0;
      }
      mFreeOsc2.phase = 0.0;
      mFreeOsc2.phaseInc = 0.0;
      mFreeSub.phase = 0.0;
      mFreeSub.phaseInc = 0.0;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void SetClipPitchOverride(float semitones) override
   {
      mMailbox.SetImmediate(AnalogSynthCore::kClipPitchParam, semitones);
   }

   // One filter chain, run over whichever state pair it is handed. Both the
   // left and right signal paths call this with their own state, so the two
   // sides can never drift apart in anything but their input and their own
   // accumulated history.
   float ApplyFilter(ZdfLadderFilter::State& ladder, DspMath::TptSvf* svfBank, float input,
                     int filterType, float cutoff, float resonance) const
   {
      const float effectiveCutoff = std::clamp(cutoff, 20.0f, (float)mSampleRate * 0.45f);

      if (filterType == kAFilterLadder)
      {
         const float g = ZdfLadderFilter::CutoffToG(effectiveCutoff, mSampleRate);
         const float k = resonance * 3.98f;
         return ZdfLadderFilter::Process(ladder, input, g, k);
      }
      if (filterType >= kAFilterSvfLP12 && filterType < kNumAFilterTypes)
      {
         const int svfType = filterType - 1; // maps to SynthModes::kFilterLP12 ...
         const int stages = SynthModes::FilterStages(svfType);
         const int shape = SynthModes::FilterShapeOf(svfType);
         const float q = 0.707f + resonance * resonance * 9.3f;
         const float g = tanf((float)M_PI * effectiveCutoff / (float)mSampleRate);
         const float k = 1.0f / (q < 0.01f ? 0.01f : q);
         float filtered = input;
         for (int s = 0; s < stages; ++s)
         {
            svfBank[s].g = g;
            svfBank[s].k = k;
            const DspMath::TptSvf::Outputs o = svfBank[s].Process(filtered);
            switch (shape)
            {
               case SynthModes::kShapeHigh:  filtered = o.high; break;
               case SynthModes::kShapeBand:  filtered = o.band; break;
               case SynthModes::kShapeNotch: filtered = o.notch; break;
               default:                      filtered = o.low; break;
            }
         }
         return filtered;
      }
      return input;
   }

   void PushParams(const AnalogSynthParams& p)
   {
      using namespace AnalogSynthCore;
      mMailbox.Push(kVolume, p.volume);
      mMailbox.Push(kFreq, p.freq);
      mMailbox.Push(kGlide, p.glide);
      mMailbox.Push(kPitchBend, p.pitchBend);
      mMailbox.Push(kFine1, p.fine1);
      mMailbox.Push(kSemi1, p.semi1);
      mMailbox.Push(kOct1, p.oct1);
      mMailbox.Push(kFine2, p.fine2);
      mMailbox.Push(kSemi2, p.semi2);
      mMailbox.Push(kOct2, p.oct2);
      mMailbox.Push(kPw1, p.pw1);
      mMailbox.Push(kOsc1Vol, p.osc1Vol);
      mMailbox.Push(kOsc2Vol, p.osc2Vol);
      mMailbox.Push(kVoices, p.voices);
      mMailbox.Push(kSpread, p.spread);
      mMailbox.Push(kFm, p.fm);
      mMailbox.Push(kDetune, p.detune);
      mMailbox.Push(kOscMix, p.oscMix);
      mMailbox.Push(kSub, p.sub);
      mMailbox.Push(kNoise, p.noise);
      mMailbox.Push(kCutoff, p.cutoff);
      mMailbox.Push(kResonance, p.resonance);
      mMailbox.Push(kDrive, p.drive);
      mMailbox.Push(kKeyTrack, p.keyTrack);
      mMailbox.Push(kAttack, p.attack);
      mMailbox.Push(kDecay, p.decay);
      mMailbox.Push(kSustain, p.sustain);
      mMailbox.Push(kRelease, p.release);

      mWave1.store(p.wave1, std::memory_order_relaxed);
      mWave2.store(p.wave2, std::memory_order_relaxed);
      mFilterType.store(p.filterType, std::memory_order_relaxed);
      mSync.store(p.sync ? 1 : 0, std::memory_order_relaxed);
      mAnalog.store(p.analog ? 1 : 0, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
   {
      using namespace AnalogSynthCore;

      const int wave1 = mWave1.load(std::memory_order_relaxed);
      const int wave2 = mWave2.load(std::memory_order_relaxed);
      const int filterType = mFilterType.load(std::memory_order_relaxed);
      const bool sync = mSync.load(std::memory_order_relaxed) != 0;
      const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

      const int wave1Dsp = AnalogWaveToDsp(wave1);
      const int wave2Dsp = AnalogWaveToDsp(wave2);

      const bool noteDriven = mNoteInbox != nullptr;
      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (noteDriven)
         numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);

      int activeCount = 0;

      for (int i = 0; i < buffer.numFrames; ++i)
      {
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            if (evts[evtIdx].isNoteOn)
               NoteOn(evts[evtIdx].note, evts[evtIdx].velocity, evts[evtIdx].voiceId, evts[evtIdx].bendSemitones);
            else if (evts[evtIdx].bendUpdate)
               BendUpdate(evts[evtIdx].voiceId, evts[evtIdx].bendSemitones);
            else
               NoteOff(evts[evtIdx].voiceId);
            evtIdx++;
         }
         const float vol = mMailbox.SmoothedValue(kVolume);
         const float freeFreq = mMailbox.SmoothedValue(kFreq);
         const float glideTime = mMailbox.SmoothedValue(kGlide);
         const float pitchBend = mMailbox.SmoothedValue(kPitchBend);
         const float fine1 = mMailbox.SmoothedValue(kFine1);
         const float semi1 = mMailbox.SmoothedValue(kSemi1);
         const float oct1 = mMailbox.SmoothedValue(kOct1);
         const float fine2 = mMailbox.SmoothedValue(kFine2);
         const float semi2 = mMailbox.SmoothedValue(kSemi2);
         const float oct2 = mMailbox.SmoothedValue(kOct2);
         const float pw1 = mMailbox.SmoothedValue(kPw1);
         const float osc1Vol = mMailbox.SmoothedValue(kOsc1Vol);
         const float osc2Vol = mMailbox.SmoothedValue(kOsc2Vol);
         const float voicesParam = mMailbox.SmoothedValue(kVoices);
         const float spread = mMailbox.SmoothedValue(kSpread);
         const float fm = std::clamp(mMailbox.SmoothedValue(kFm), 0.0f, 1.0f);
         const float detune = std::clamp(mMailbox.SmoothedValue(kDetune), 0.0f, 100.0f);
         const float oscMix = std::clamp(mMailbox.SmoothedValue(kOscMix), 0.0f, 1.0f);
         const float subVol = mMailbox.SmoothedValue(kSub);
         const float noiseVol = mMailbox.SmoothedValue(kNoise);
         const float cutoff = mMailbox.SmoothedValue(kCutoff);
         const float resonance = mMailbox.SmoothedValue(kResonance);
         const float drive = mMailbox.SmoothedValue(kDrive);
         const float keyTrack = mMailbox.SmoothedValue(kKeyTrack);
         const float attackMs = mMailbox.SmoothedValue(kAttack);
         const float decayMs = mMailbox.SmoothedValue(kDecay);
         const float sustainLevel = mMailbox.SmoothedValue(kSustain);
         const float releaseMs = mMailbox.SmoothedValue(kRelease);
         const float clipPitch = mMailbox.SmoothedValue(kClipPitchParam);

         const int unisonCount = std::clamp((int)lroundf(voicesParam), 1, kMaxUnison);
         const float osc1Norm = 1.0f / sqrtf((float)unisonCount);

         if (unisonCount != mCachedUnison)
         {
            NormalizeUnisonDetune(unisonCount, mUnisonDetuneMul);
            mCachedUnison = unisonCount;
         }

         // `spread` is a stereo width, not a second detune depth: it no longer
         // scales `detune`, so the detune knob's cents readout is now the true
         // total width of the stack. One oscillator has no stack to split, so
         // the dual signal path only engages from two upwards.
         const float stereoAmt = std::clamp(spread, 0.0f, 1.0f);
         const bool wantStereo = (unisonCount > 1 && stereoAmt > 0.0001f);
         // Half the knob's cents either side, so "12 c" is a 12 c wide stack.
         const float detuneHalf = detune * 0.5f;

         // Equal-power crossfade weights for osc1 and osc2
         const float mixAngle = oscMix * (float)M_PI * 0.5f;
         const float osc1Gain = cosf(mixAngle);
         const float osc2Gain = sinf(mixAngle);

         // Pre-filter drive stage gain
         const float driveGain = 1.0f + drive * 4.0f;

         float outL = 0.0f;
         float outR = 0.0f;

         if (mNoteInbox == nullptr)
         {
            // Free-running fallback
            mFreeGlide.SetTimeConstant(glideTime, mSampleRate);
            const float currentFreq = mFreeGlide.Process(freeFreq);
            const float totalCents = pitchBend * 100.0f + clipPitch * 100.0f;
            const float baseHz = std::clamp(currentFreq * powf(2.0f, totalCents / 1200.0f),
                                            10.0f, (float)mSampleRate * 0.45f);

            // Osc 2
            const float osc2Cents = oct2 * 1200.0f + semi2 * 100.0f + fine2;
            const float osc2Hz = std::clamp(baseHz * powf(2.0f, osc2Cents / 1200.0f),
                                            10.0f, (float)mSampleRate * 0.45f);
            mFreeOsc2.SetFrequency(osc2Hz, mSampleRate);
            const float osc2Sample = mFreeOsc2.Generate(wave2Dsp, pw1);
            mFreeOsc2.Advance();

            // Osc 1 with FM from Osc 2
            const float osc1Cents = oct1 * 1200.0f + semi1 * 100.0f + fine1;
            const float osc1BaseHz = std::clamp(baseHz * powf(2.0f, osc1Cents / 1200.0f),
                                                10.0f, (float)mSampleRate * 0.45f);
            const float fmRatio = std::max(0.0f, 1.0f + osc2Sample * fm * 2.5f);
            const float osc1ModHz = std::clamp(osc1BaseHz * fmRatio, 10.0f, (float)mSampleRate * 0.45f);

            // Osc 1 Unison, summed into two signal paths at once. Each
            // oscillator's tolerance figure sets both how far it sits off
            // pitch and where it sits in the image, so the stack's most
            // mistuned members are also its widest - the two cues arrive
            // together, as they do when each is a separate circuit.
            float osc1SumL = 0.0f;
            float osc1SumR = 0.0f;
            bool osc1Wrapped = false;
            for (int u = 0; u < unisonCount; ++u)
            {
               const float uDetuneCents = mUnisonDetuneMul[u] * detuneHalf;
               const float uHz = std::clamp(osc1ModHz * powf(2.0f, uDetuneCents / 1200.0f),
                                            10.0f, (float)mSampleRate * 0.45f);
               mFreeOsc1[u].SetFrequency(uHz, mSampleRate);
               const float uSample = mFreeOsc1[u].Generate(wave1Dsp, pw1);
               float ugL = 1.0f, ugR = 1.0f;
               if (wantStereo)
                  TolerancePan(mUnisonDetuneMul[u] * stereoAmt, ugL, ugR);
               osc1SumL += uSample * ugL;
               osc1SumR += uSample * ugR;
               if (u == 0 && (mFreeOsc1[0].phase + mFreeOsc1[0].phaseInc >= 1.0))
                  osc1Wrapped = true;
               mFreeOsc1[u].Advance();
            }
            osc1SumL *= osc1Norm;
            osc1SumR *= osc1Norm;

            if (sync && osc1Wrapped)
               mFreeOsc2.phase = 0.0;

            // Sub osc: one octave below osc1, tracking osc1's post-FM pitch
            // but not its unison detune - a detuned sub beats against itself
            // in the register where beating is least wanted. Symmetric square
            // (pw fixed at 0.5), BLEP'd like every other osc here.
            const float subHz = std::clamp(osc1ModHz * 0.5f, 5.0f, (float)mSampleRate * 0.45f);
            mFreeSub.SetFrequency(subHz, mSampleRate);
            const float subSample = mFreeSub.Generate(DspMath::kWaveSquare, 0.5f);
            mFreeSub.Advance();

            // Noise
            const float noiseSample = mFreeNoise.Next();

            // Mixer. Osc 2, sub and noise stay centred: the sub in
            // particular belongs in the middle, where a wide low end would
            // only smear it and break mono fold-down.
            const float centreMix = osc2Sample * osc2Gain * osc2Vol + subSample * subVol + noiseSample * noiseVol;
            const float mixedL = osc1SumL * osc1Gain * osc1Vol + centreMix;
            const float mixedR = osc1SumR * osc1Gain * osc1Vol + centreMix;

            // Pre-filter drive. The analog hiss is drawn separately per side
            // - two circuits, two noise floors - which decorrelates the pair
            // a little further at no extra cost.
            float drivenL = DspMath::FastTanh(mixedL * driveGain);
            float drivenR = DspMath::FastTanh(mixedR * driveGain);
            if (analog)
            {
               drivenL += mFreeNoise.Next() * 0.001f;
               drivenR += mFreeNoise.Next() * 0.001f;
            }

            // Filter. The right-hand chain starts from silence rather than
            // from whatever it held the last time the stack was wide, so
            // turning spread up mid-note fades a clean path in instead of
            // resuming a stale one.
            if (wantStereo && !mFreeStereoPath)
            {
               mFreeLadderR.Reset();
               for (int st = 0; st < SynthModes::kMaxFilterStages; ++st)
                  mFreeSvfR[st].Reset();
            }
            mFreeStereoPath = wantStereo;

            const float filteredL = ApplyFilter(mFreeLadder, mFreeSvf, drivenL, filterType,
                                                cutoff, resonance);
            const float filteredR = wantStereo
               ? ApplyFilter(mFreeLadderR, mFreeSvfR, drivenR, filterType, cutoff, resonance)
               : filteredL;

            outL = filteredL;
            outR = filteredR;
            activeCount = 1;
         }
         else
         {
            // Note-driven polyphonic mode
            int active = 0;
            float voiceSumL = 0.0f;
            float voiceSumR = 0.0f;

            for (int vi = 0; vi < AnalogSynthCore::kMaxVoices; ++vi)
            {
               Voice& v = mVoices[vi];
               if (!v.active)
                  continue;

               v.ampEnv.SetADSR(attackMs, decayMs, sustainLevel, releaseMs);
               const float ampEnvVal = v.ampEnv.Process();
               if (!v.ampEnv.IsActive())
               {
                  v.active = false;
                  v.note = -1;
                  continue;
               }

               const float velGain = v.velocity * v.velocity;
               v.glide.SetTimeConstant(glideTime, mSampleRate);
               const float currentPitch = v.glide.Process((float)v.note);
               const float baseHz = NoteToHz(currentPitch + (analog ? v.driftPitchOffset : 0.0f));

               const float totalCents = pitchBend * 100.0f + v.bend * 100.0f + clipPitch * 100.0f;
               const float voiceBaseHz = std::clamp(baseHz * powf(2.0f, totalCents / 1200.0f),
                                                    10.0f, (float)mSampleRate * 0.45f);

               // Osc 2
               const float osc2Cents = oct2 * 1200.0f + semi2 * 100.0f + fine2;
               const float osc2Hz = std::clamp(voiceBaseHz * powf(2.0f, osc2Cents / 1200.0f),
                                               10.0f, (float)mSampleRate * 0.45f);
               v.osc2.SetFrequency(osc2Hz, mSampleRate);
               const float osc2Sample = v.osc2.Generate(wave2Dsp, pw1);
               v.osc2.Advance();

               // Osc 1 with FM from Osc 2
               const float osc1Cents = oct1 * 1200.0f + semi1 * 100.0f + fine1;
               const float osc1BaseHz = std::clamp(voiceBaseHz * powf(2.0f, osc1Cents / 1200.0f),
                                                   10.0f, (float)mSampleRate * 0.45f);
               const float fmRatio = std::max(0.0f, 1.0f + osc2Sample * fm * 2.5f);
               const float osc1ModHz = std::clamp(osc1BaseHz * fmRatio, 10.0f, (float)mSampleRate * 0.45f);

               // Osc 1 Unison - see the free-running path for why the same
               // tolerance figure drives both detune and image position.
               float osc1SumL = 0.0f;
               float osc1SumR = 0.0f;
               bool osc1Wrapped = false;
               for (int u = 0; u < unisonCount; ++u)
               {
                  const float uDetuneCents = mUnisonDetuneMul[u] * detuneHalf;
                  const float uHz = std::clamp(osc1ModHz * powf(2.0f, uDetuneCents / 1200.0f),
                                               10.0f, (float)mSampleRate * 0.45f);
                  v.osc1[u].SetFrequency(uHz, mSampleRate);
                  const float uSample = v.osc1[u].Generate(wave1Dsp, pw1);
                  float ugL = 1.0f, ugR = 1.0f;
                  if (wantStereo)
                     TolerancePan(mUnisonDetuneMul[u] * stereoAmt, ugL, ugR);
                  osc1SumL += uSample * ugL;
                  osc1SumR += uSample * ugR;
                  if (u == 0 && (v.osc1[0].phase + v.osc1[0].phaseInc >= 1.0))
                     osc1Wrapped = true;
                  v.osc1[u].Advance();
               }
               osc1SumL *= osc1Norm;
               osc1SumR *= osc1Norm;

               if (sync && osc1Wrapped)
                  v.osc2.phase = 0.0;

               // Sub osc: see the free-running path - own accumulator at half
               // osc1's post-FM pitch, unaffected by unison detune.
               const float subHz = std::clamp(osc1ModHz * 0.5f, 5.0f, (float)mSampleRate * 0.45f);
               v.sub.SetFrequency(subHz, mSampleRate);
               const float subSample = v.sub.Generate(DspMath::kWaveSquare, 0.5f);
               v.sub.Advance();

               // Noise
               const float noiseSample = v.noise.Next();

               // Mixer - osc 2, sub and noise stay centred.
               const float centreMix = osc2Sample * osc2Gain * osc2Vol + subSample * subVol + noiseSample * noiseVol;
               const float mixedL = osc1SumL * osc1Gain * osc1Vol + centreMix;
               const float mixedR = osc1SumR * osc1Gain * osc1Vol + centreMix;

               // Pre-filter drive, one noise draw per side
               float drivenL = DspMath::FastTanh(mixedL * driveGain);
               float drivenR = DspMath::FastTanh(mixedR * driveGain);
               if (analog)
               {
                  drivenL += v.noise.Next() * 0.001f;
                  drivenR += v.noise.Next() * 0.001f;
               }

               // Filter with KeyTracking
               const float keyTrackOctaves = ((currentPitch - 60.0f) / 12.0f) * keyTrack;
               const float driftOctaves = analog ? v.driftCutoffOffset : 0.0f;
               const float effectiveCutoff = std::clamp(cutoff * powf(2.0f, keyTrackOctaves + driftOctaves),
                                                        20.0f, (float)mSampleRate * 0.45f);
               if (wantStereo && !v.stereoPath)
               {
                  v.ladderR.Reset();
                  for (int st = 0; st < SynthModes::kMaxFilterStages; ++st)
                     v.svfR[st].Reset();
               }
               v.stereoPath = wantStereo;

               const float filteredL = ApplyFilter(v.ladder, v.svf, drivenL, filterType,
                                                   effectiveCutoff, resonance);
               const float filteredR = wantStereo
                  ? ApplyFilter(v.ladderR, v.svfR, drivenR, filterType, effectiveCutoff, resonance)
                  : filteredL;

               // Voice-card position on top of the stack's own width: each
               // card sits at a fixed irregular spot in the image, so a chord
               // lays out across the field in an order unrelated to pitch.
               float vgL = 1.0f, vgR = 1.0f;
               if (stereoAmt > 0.0001f)
                  TolerancePan(AnalogSynthCore::kVoicePanSeed[vi] * stereoAmt, vgL, vgR);

               const float envGain = ampEnvVal * velGain;
               const float voiceOutL = filteredL * envGain * vgL;
               const float voiceOutR = filteredR * envGain * vgR;
               v.lastOut = (voiceOutL + voiceOutR) * 0.5f;
               voiceSumL += voiceOutL;
               voiceSumR += voiceOutR;
               active++;
            }

            activeCount = active;
            const float normTarget = active > 1 ? 1.0f / sqrtf((float)active) : 1.0f;
            const float norm = mPolyNormSmooth.Process(normTarget);
            outL = voiceSumL * norm;
            outR = voiceSumR * norm;
         }

         outL *= vol;
         outR *= vol;

         // Write to output channels. A mono host gets the fold-down rather
         // than the left path alone, so nothing panned off-centre goes
         // missing when the node feeds a mono chain.
         const float outMono = (outL + outR) * 0.5f;
         if (buffer.numChannels >= 2)
         {
            buffer.channels[0][i] = outL;
            buffer.channels[1][i] = outR;
            for (int ch = 2; ch < buffer.numChannels; ++ch)
               buffer.channels[ch][i] = 0.0f;
         }
         else if (buffer.numChannels == 1)
         {
            buffer.channels[0][i] = outMono;
         }

         // Decimated scope tap (every 4th sample)
         if ((i & 3) == 0)
         {
            mScopeRing.Write(&outMono, 1);
         }
      }

      mActiveVoices.store(activeCount, std::memory_order_relaxed);
   }

   void NoteOn(int note, float velocity, int voiceId, float bendSemitones)
   {
      using namespace AnalogSynthCore;

      int slot = -1;
      // 1. Retrigger voice playing the same note
      for (int i = 0; i < kMaxVoices; ++i)
      {
         if (mVoices[i].active && mVoices[i].note == note)
         {
            slot = i;
            break;
         }
      }
      // 2. Allocate inactive slot
      if (slot < 0)
      {
         for (int i = 0; i < kMaxVoices; ++i)
         {
            if (!mVoices[i].active)
            {
               slot = i;
               break;
            }
         }
      }
      // 3. Steal oldest voice
      if (slot < 0)
      {
         uint64_t oldest = UINT64_MAX;
         slot = 0;
         for (int i = 0; i < kMaxVoices; ++i)
         {
            if (mVoices[i].age < oldest)
            {
               oldest = mVoices[i].age;
               slot = i;
            }
         }
      }

      Voice& v = mVoices[slot];
      const bool fresh = !v.active || v.note != note;
      const bool stolen = v.active && v.note != note;
      const bool wasInactive = !v.active;

      v.active = true;
      v.held = true;
      v.note = note;
      v.voiceId = voiceId;
      v.velocity = velocity;
      v.bend = bendSemitones;
      v.age = mNextAge++;

      if (fresh)
      {
         v.Reset(mSampleRate);
         const float glide = mMailbox.SmoothedValue(kGlide);
         const float targetPitch = (float)note;
         if (wasInactive)
         {
            if (mHasLastNote && glide > 0.001f)
               v.glide.SetImmediate(mLastNotePitch);
            else
               v.glide.SetImmediate(targetPitch);
         }
         else
         {
            if (glide <= 0.001f)
               v.glide.SetImmediate(targetPitch);
         }
      }

      mLastNotePitch = (float)note;
      mHasLastNote = true;

      const float attackMs = mMailbox.SmoothedValue(kAttack);
      const float decayMs = mMailbox.SmoothedValue(kDecay);
      const float sustainLevel = mMailbox.SmoothedValue(kSustain);
      const float releaseMs = mMailbox.SmoothedValue(kRelease);

      v.ampEnv.SetADSR(attackMs, decayMs, sustainLevel, releaseMs);
      if (stolen)
         v.ampEnv.ResetLevel();
      v.ampEnv.NoteOn();
   }

   void NoteOff(int voiceId)
   {
      for (Voice& v : mVoices)
      {
         if (!v.active || v.voiceId != voiceId || !v.held)
            continue;
         v.held = false;
         v.ampEnv.NoteOff();
      }
   }

   void BendUpdate(int voiceId, float bendSemitones)
   {
      for (Voice& v : mVoices)
      {
         if (!v.active || v.voiceId != voiceId)
            continue;
         v.bend = bendSemitones;
      }
   }

   int ActiveVoices() const { return mActiveVoices.load(std::memory_order_relaxed); }
   MeterRing& ScopeRing() { return mScopeRing; }
   double DebugMailboxSampleRate() const { return mMailbox.SampleRate(); }

private:
   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;
   MeterRing mScopeRing;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   Voice mVoices[AnalogSynthCore::kMaxVoices];
   uint64_t mNextAge = 1;
   float mLastNotePitch = 60.0f;
   bool mHasLastNote = false;

   // Free-running fallback state
   DspMath::OnePole mFreeGlide;
   DspMath::PolyBlepOsc mFreeOsc1[AnalogSynthCore::kMaxUnison];
   DspMath::PolyBlepOsc mFreeOsc2;
   DspMath::PolyBlepOsc mFreeSub;
   DspMath::WhiteNoise mFreeNoise;
   ZdfLadderFilter::State mFreeLadder;
   ZdfLadderFilter::State mFreeLadderR;
   bool mFreeStereoPath = false;
   // Cached normalised detune multipliers, rebuilt only when the stack size
   // changes - NormalizeUnisonDetune is a per-stack constant, not per-sample
   // work.
   int mCachedUnison = -1;
   float mUnisonDetuneMul[AnalogSynthCore::kMaxUnison] = {};
   DspMath::TptSvf mFreeSvf[SynthModes::kMaxFilterStages];
   DspMath::TptSvf mFreeSvfR[SynthModes::kMaxFilterStages];

   DspMath::OnePole mPolyNormSmooth;
   std::atomic<int> mActiveVoices { 0 };

   std::atomic<int> mWave1 { kAWaveSaw };
   std::atomic<int> mWave2 { kAWaveSaw };
   std::atomic<int> mFilterType { kAFilterLadder };
   std::atomic<int> mSync { 0 };
   std::atomic<int> mAnalog { 1 };
};
