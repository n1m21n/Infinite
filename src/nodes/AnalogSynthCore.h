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

      ZdfLadderFilter::State ladder;
      DspMath::TptSvf svf[SynthModes::kMaxFilterStages];

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
         for (int s = 0; s < SynthModes::kMaxFilterStages; ++s)
         {
            svf[s].Reset();
            svf[s].SetSampleRate(sampleRate);
         }
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
      for (int s = 0; s < SynthModes::kMaxFilterStages; ++s)
      {
         mFreeSvf[s].Reset();
         mFreeSvf[s].SetSampleRate(mSampleRate);
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

         // Equal-power crossfade weights for osc1 and osc2
         const float mixAngle = oscMix * (float)M_PI * 0.5f;
         const float osc1Gain = cosf(mixAngle);
         const float osc2Gain = sinf(mixAngle);

         // Pre-filter drive stage gain
         const float driveGain = 1.0f + drive * 4.0f;

         float outSample = 0.0f;

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

            // Osc 1 Unison
            float osc1Sum = 0.0f;
            bool osc1Wrapped = false;
            for (int u = 0; u < unisonCount; ++u)
            {
               float uDetuneCents = 0.0f;
               if (unisonCount > 1)
               {
                  const float uFrac = ((float)u / (float)(unisonCount - 1) - 0.5f) * 2.0f;
                  uDetuneCents = uFrac * (detune * spread);
               }
               const float uHz = std::clamp(osc1ModHz * powf(2.0f, uDetuneCents / 1200.0f),
                                            10.0f, (float)mSampleRate * 0.45f);
               mFreeOsc1[u].SetFrequency(uHz, mSampleRate);
               osc1Sum += mFreeOsc1[u].Generate(wave1Dsp, pw1);
               if (u == 0 && (mFreeOsc1[0].phase + mFreeOsc1[0].phaseInc >= 1.0))
                  osc1Wrapped = true;
               mFreeOsc1[u].Advance();
            }
            osc1Sum *= osc1Norm;

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

            // Mixer
            float mixed = osc1Sum * osc1Gain * osc1Vol + osc2Sample * osc2Gain * osc2Vol + subSample * subVol + noiseSample * noiseVol;

            // Pre-filter drive
            float driven = DspMath::FastTanh(mixed * driveGain);
            if (analog)
               driven += mFreeNoise.Next() * 0.001f;

            // Filter
            const float effectiveCutoff = std::clamp(cutoff, 20.0f, (float)mSampleRate * 0.45f);
            float filtered = driven;

            if (filterType == kAFilterLadder)
            {
               const float g = ZdfLadderFilter::CutoffToG(effectiveCutoff, mSampleRate);
               const float k = resonance * 3.98f;
               filtered = ZdfLadderFilter::Process(mFreeLadder, driven, g, k);
            }
            else if (filterType >= kAFilterSvfLP12 && filterType < kNumAFilterTypes)
            {
               const int svfType = filterType - 1; // maps to SynthModes::kFilterLP12 ...
               const int stages = SynthModes::FilterStages(svfType);
               const int shape = SynthModes::FilterShapeOf(svfType);
               const float q = 0.707f + resonance * resonance * 9.3f;
               const float g = tanf((float)M_PI * effectiveCutoff / (float)mSampleRate);
               const float k = 1.0f / (q < 0.01f ? 0.01f : q);
               for (int s = 0; s < stages; ++s)
               {
                  mFreeSvf[s].g = g;
                  mFreeSvf[s].k = k;
                  const DspMath::TptSvf::Outputs o = mFreeSvf[s].Process(filtered);
                  switch (shape)
                  {
                     case SynthModes::kShapeHigh:  filtered = o.high; break;
                     case SynthModes::kShapeBand:  filtered = o.band; break;
                     case SynthModes::kShapeNotch: filtered = o.notch; break;
                     default:                      filtered = o.low; break;
                  }
               }
            }

            outSample = filtered;
            activeCount = 1;
         }
         else
         {
            // Note-driven polyphonic mode
            int active = 0;
            float voiceSum = 0.0f;

            for (Voice& v : mVoices)
            {
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

               // Osc 1 Unison
               float osc1Sum = 0.0f;
               bool osc1Wrapped = false;
               for (int u = 0; u < unisonCount; ++u)
               {
                  float uDetuneCents = 0.0f;
                  if (unisonCount > 1)
                  {
                     const float uFrac = ((float)u / (float)(unisonCount - 1) - 0.5f) * 2.0f;
                     uDetuneCents = uFrac * (detune * spread);
                  }
                  const float uHz = std::clamp(osc1ModHz * powf(2.0f, uDetuneCents / 1200.0f),
                                               10.0f, (float)mSampleRate * 0.45f);
                  v.osc1[u].SetFrequency(uHz, mSampleRate);
                  osc1Sum += v.osc1[u].Generate(wave1Dsp, pw1);
                  if (u == 0 && (v.osc1[0].phase + v.osc1[0].phaseInc >= 1.0))
                     osc1Wrapped = true;
                  v.osc1[u].Advance();
               }
               osc1Sum *= osc1Norm;

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

               // Mixer
               float mixed = osc1Sum * osc1Gain * osc1Vol + osc2Sample * osc2Gain * osc2Vol + subSample * subVol + noiseSample * noiseVol;

               // Pre-filter drive
               float driven = DspMath::FastTanh(mixed * driveGain);
               if (analog)
                  driven += v.noise.Next() * 0.001f;

               // Filter with KeyTracking
               const float keyTrackOctaves = ((currentPitch - 60.0f) / 12.0f) * keyTrack;
               const float driftOctaves = analog ? v.driftCutoffOffset : 0.0f;
               const float effectiveCutoff = std::clamp(cutoff * powf(2.0f, keyTrackOctaves + driftOctaves),
                                                        20.0f, (float)mSampleRate * 0.45f);
               float filtered = driven;

               if (filterType == kAFilterLadder)
               {
                  const float g = ZdfLadderFilter::CutoffToG(effectiveCutoff, mSampleRate);
                  const float k = resonance * 3.98f;
                  filtered = ZdfLadderFilter::Process(v.ladder, driven, g, k);
               }
               else if (filterType >= kAFilterSvfLP12 && filterType < kNumAFilterTypes)
               {
                  const int svfType = filterType - 1;
                  const int stages = SynthModes::FilterStages(svfType);
                  const int shape = SynthModes::FilterShapeOf(svfType);
                  const float q = 0.707f + resonance * resonance * 9.3f;
                  const float g = tanf((float)M_PI * effectiveCutoff / (float)mSampleRate);
                  const float k = 1.0f / (q < 0.01f ? 0.01f : q);
                  for (int s = 0; s < stages; ++s)
                  {
                     v.svf[s].g = g;
                     v.svf[s].k = k;
                     const DspMath::TptSvf::Outputs o = v.svf[s].Process(filtered);
                     switch (shape)
                     {
                        case SynthModes::kShapeHigh:  filtered = o.high; break;
                        case SynthModes::kShapeBand:  filtered = o.band; break;
                        case SynthModes::kShapeNotch: filtered = o.notch; break;
                        default:                      filtered = o.low; break;
                     }
                  }
               }

               const float voiceOut = filtered * ampEnvVal * velGain;
               v.lastOut = voiceOut;
               voiceSum += voiceOut;
               active++;
            }

            activeCount = active;
            const float normTarget = active > 1 ? 1.0f / sqrtf((float)active) : 1.0f;
            const float norm = mPolyNormSmooth.Process(normTarget);
            outSample = voiceSum * norm;
         }

         outSample *= vol;

         // Write to output channels
         if (buffer.numChannels >= 2)
         {
            buffer.channels[0][i] = outSample;
            buffer.channels[1][i] = outSample;
            for (int ch = 2; ch < buffer.numChannels; ++ch)
               buffer.channels[ch][i] = 0.0f;
         }
         else if (buffer.numChannels == 1)
         {
            buffer.channels[0][i] = outSample;
         }

         // Decimated scope tap (every 4th sample)
         if ((i & 3) == 0)
         {
            mScopeRing.Write(&outSample, 1);
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
   DspMath::TptSvf mFreeSvf[SynthModes::kMaxFilterStages];

   DspMath::OnePole mPolyNormSmooth;
   std::atomic<int> mActiveVoices { 0 };

   std::atomic<int> mWave1 { kAWaveSaw };
   std::atomic<int> mWave2 { kAWaveSaw };
   std::atomic<int> mFilterType { kAFilterLadder };
   std::atomic<int> mSync { 0 };
   std::atomic<int> mAnalog { 1 };
};
