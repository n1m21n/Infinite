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
#include "audio/VoiceMod.h"
#include "audio/dsp/BlepOscillator.h"
#include "audio/dsp/Oversampler.h"
#include "audio/dsp/VoiceLanes.h"
#include "audio/dsp/FilterModels.h"
#include "core/Transport.h"

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

   // The params each voice evaluates for itself, so a per-voice modulator
   // (audio/VoiceMod.h) can give every voice its own value. Built once per
   // sample from the smoothed shared values; a voice with routes copies it
   // and overwrites what its routes drive.
   struct VoiceKnobs
   {
      float fine1, semi1, oct1, fine2, semi2, oct2, pw1, fm, oscMix, osc1Vol, osc2Vol, subVol, noiseVol;
      float cutoff, resonance, drive, keyTrack, detune, attackMs, decayMs, sustainLevel, releaseMs;
      // Derived
      float osc1Gain, osc2Gain, driveGain, detuneHalf;

      void Derive()
      {
         fm = std::clamp(fm, 0.0f, 1.0f);
         detune = std::clamp(detune, 0.0f, 100.0f);
         oscMix = std::clamp(oscMix, 0.0f, 1.0f);
         // Half the knob's cents either side, so "12 c" is a 12 c wide stack.
         detuneHalf = detune * 0.5f;
         // Equal-power crossfade weights for osc1 and osc2
         const float mixAngle = oscMix * (float)M_PI * 0.5f;
         osc1Gain = cosf(mixAngle);
         osc2Gain = sinf(mixAngle);
         // Pre-filter drive stage gain
         driveGain = 1.0f + drive * 4.0f;
      }

      void Set(int id, float x)
      {
         switch (id)
         {
            case kFine1: fine1 = x; break;
            case kSemi1: semi1 = x; break;
            case kOct1: oct1 = x; break;
            case kFine2: fine2 = x; break;
            case kSemi2: semi2 = x; break;
            case kOct2: oct2 = x; break;
            case kPw1: pw1 = x; break;
            case kFm: fm = x; break;
            case kOscMix: oscMix = x; break;
            case kOsc1Vol: osc1Vol = x; break;
            case kOsc2Vol: osc2Vol = x; break;
            case kSub: subVol = x; break;
            case kNoise: noiseVol = x; break;
            case kCutoff: cutoff = x; break;
            case kResonance: resonance = x; break;
            case kDrive: drive = x; break;
            case kKeyTrack: keyTrack = x; break;
            case kDetune: detune = x; break;
            case kAttack: attackMs = x; break;
            case kDecay: decayMs = x; break;
            case kSustain: sustainLevel = x; break;
            case kRelease: releaseMs = x; break;
            default: break;
         }
      }
   };

   // Deterministic golden-ratio phase seeds for unison voices
   constexpr float kVoicePhaseSeed[kMaxUnison] = {
      0.000000f, 0.618034f, 0.236068f, 0.854102f,
      0.472136f, 0.090170f, 0.708204f
   };

   // Per-voice analog tolerance pitch offsets in semitones (-0.045 to +0.041).
   // Voice 0 is the tuning reference, the card a technician tunes to A440
   // before matching the others: NoteOn always takes the first free voice, so
   // every single-note line and the first note of every chord plays on it.
   // It was -0.035 (3.5 c flat), which put every default patch out of tune
   // (synth_probe probe.pitch_worst_c 3.52 c). The other cards keep their
   // spread, so a chord still has its voice-to-voice mistuning.
   constexpr float kVoiceDriftPitch[kMaxVoices] = {
      0.0f, 0.022f, -0.018f, 0.041f, -0.027f, 0.033f, -0.045f, 0.015f
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

   // Osc 1's frequency multiplier for osc 2's value `m` in [-1, +1]. Linear, so a
   // zero-mean modulator leaves osc 1's mean pitch exactly where it was (an
   // exponential ratio drifts sharp as depth rises). Depth x = 2.5 fm is bent
   // to x / (1 + x), which keeps |m d| < 1: the old 1 + 2.5 m fm went negative
   // above fm 0.4, stalled osc 1 at the 10 Hz floor and sounded like pitch
   // dives. Slope at fm 0 is the old 2.5; fm 1 reaches +-0.71.
   inline float FmRatio(float m, float fm)
   {
      const float x = fm * 2.5f;
      return 1.0f + m * (x / (1.0f + x));
   }

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

   // The voice's drive and filter run at this multiple of the sample rate:
   // the drive's tanh and the ladder's per-stage tanh both generate
   // harmonics, and at the base rate they folded back (-48 dB on a 1.75 kHz
   // saw at 0% drive even with ADAA - the drive stage is always in the
   // path). A lighter first half-band than an effect's (see Oversampler.h):
   // up to 16 of these run at once. 4x, not 2x: the ladder's tanh stages
   // still folded at 2x (-73 dB non-harmonic on an input clean to 19 kHz,
   // -89 dB at 4x). Together with the narrow-band oscillators below this
   // takes a C4/C7/C8 saw from -68/-63/-61 to under -80 dB.
   constexpr int kFilterOversample = 4;
   constexpr int kFilterOversampleGroupDelay = 32;

   // AnalogWaveToDsp's DspMath ids are also BlepOsc's waveform ids.
   static_assert(DspMath::kWaveSine == BlepOsc::kSine && DspMath::kWaveTriangle == BlepOsc::kTriangle &&
                    DspMath::kWaveSaw == BlepOsc::kSaw && DspMath::kWaveSquare == BlepOsc::kSquare,
                 "BlepOsc waveform ids must match DspMath's");

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

      // Band-limited (BLEP/BLAMP) oscillators - see BlepOscillator.h. Every
      // oscillator output is BlepOsc::kLatency samples late, all equally.
      // Narrow band (edge 0.8 Nyquist): their output feeds the drive and the
      // ladder, which fold any 20-24 kHz content into the audible band.
      BlepOsc::NarrowOscillator osc1[AnalogSynthCore::kMaxUnison];
      BlepOsc::NarrowOscillator osc2;
      // Sub osc: its own accumulator, run at half osc1's frequency. It cannot
      // read osc1's phase scaled by 0.5 - scaling a phase compresses its
      // range, it does not halve its rate, so such a "sub" never crosses its
      // own comparison threshold and emits constant DC instead of a tone.
      BlepOsc::NarrowOscillator sub;
      DspMath::WhiteNoise noise;
      DspMath::OnePole glide;
      // Anti-aliased pre-filter drive, one per signal path, and the 4x
      // oversampler both it and the filter run inside (see ProcessBlock).
      DspMath::FastTanhAdaa driveL, driveR;
      // AC coupling on the oscillator mix, ahead of the drive: a hard-synced
      // saw restarted early in its cycle has a non-zero mean (-0.035 of DC
      // on a sync lead, measured), which would bias the drive's tanh and
      // ride out of the synth.
      DspMath::DcBlocker acL, acR;
      Oversampler os; // prepared in PrepareToPlay; Reset() only clears it

      // Two independent filter chains. The stack's unison voices are split
      // between them, so left and right carry different oscillators through
      // different filter instances - decorrelation, not a panned copy of one
      // mono signal. `R` idles unless the stack has something to split.
      FilterModels::Bank<float> ladder;
      FilterModels::Bank<float> ladderR;
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
            osc1[u].Reset((double)AnalogSynthCore::kVoicePhaseSeed[u]);
         }
         osc2.Reset(0.0);
         sub.Reset(0.0);
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
         driveL.Reset();
         driveR.Reset();
         acL.Reset();
         acL.SetSampleRate(sampleRate);
         acR.Reset();
         acR.SetSampleRate(sampleRate);
         os.Reset();
         ampEnv.SetSampleRate(sampleRate);
         lastOut = 0.0f;
      }
   };

   AudioAnalogNode()
   {
      BlepOsc::EnsureTables(BlepOsc::kBandNarrow); // main thread, before the audio thread can touch them
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
      mVoiceMod.Prepare(mSampleRate);
      mPolyNormSmooth.SetTimeConstant(AnalogSynthCore::kPolyNormSmoothSec, mSampleRate);
      mFreeGlide.SetTimeConstant(0.0f, mSampleRate);

      for (auto& v : mVoices)
      {
         v.os.Prepare(AnalogSynthCore::kFilterOversample, AnalogSynthCore::kFilterOversampleGroupDelay);
         v.Reset(mSampleRate);
      }
      mFreeOs.Prepare(AnalogSynthCore::kFilterOversample, AnalogSynthCore::kFilterOversampleGroupDelay);
      mUseLanes = !VoiceSimd::ForceScalar().load(std::memory_order_relaxed);
      for (auto& side : mLanes)
         for (LaneGroup& grp : side)
         {
            grp.os.Prepare(mFreeOs);
            grp.ac.SetSampleRate(mSampleRate);
         }
      for (int vi = 0; vi < AnalogSynthCore::kMaxVoices; ++vi)
         ResetVoiceLanes(vi);

      mFreeLadder.Reset();
      mFreeLadderR.Reset();
      mFreeDriveL.Reset();
      mFreeDriveR.Reset();
      mFreeAcL.Reset();
      mFreeAcL.SetSampleRate(mSampleRate);
      mFreeAcR.Reset();
      mFreeAcR.SetSampleRate(mSampleRate);
      mFreeOs.Reset();
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
         mFreeOsc1[u].Reset((double)AnalogSynthCore::kVoicePhaseSeed[u]);
      }
      mFreeOsc2.Reset(0.0);
      mFreeSub.Reset(0.0);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   // The band-limited oscillators' correction kernel plus the drive/filter
   // oversampler, both linear-phase.
   int LatencySamples() const override { return BlepOsc::kLatency + mFreeOs.Latency(); }

   void SetClipPitchOverride(float semitones) override
   {
      mMailbox.SetImmediate(AnalogSynthCore::kClipPitchParam, semitones);
   }

   // Filter coefficients, resolved once per base-rate sample (the cutoff is
   // smoothed at that rate) and reused for every oversampled sample, so the
   // oversampled filter does not pay for tan() per sample.
   struct FilterCoeffs
   {
      int type = kAFilterOff;
      float g = 0.0f, k = 0.0f;
      int stages = 0, shape = 0;
      int model = -1;        // FilterModels::Model for the ladder/MS-20/diode types, else -1
      float makeup = 1.0f;   // FilterModels::MakeupGain, applied after the filter
   };

   static int ModelOf(int filterType)
   {
      switch (filterType)
      {
         case kAFilterLadder: return FilterModels::kModelLadder;
         case kAFilterMs20: return FilterModels::kModelMs20;
         case kAFilterDiode: return FilterModels::kModelDiode;
         default: return -1;
      }
   }
   static bool IsSvfType(int t) { return t >= kAFilterSvfLP12 && t <= kAFilterSvfNotch24; }

   // `rate` is the rate the filter runs at (the oversampled rate). The
   // cutoff stays clamped below the *base* Nyquist, as it always was.
   FilterCoeffs MakeFilterCoeffs(int filterType, float cutoff, float resonance, double rate) const
   {
      FilterCoeffs c;
      c.type = filterType;
      const float effectiveCutoff = std::clamp(cutoff, 20.0f, (float)mSampleRate * 0.45f);
      c.model = ModelOf(filterType);
      if (c.model >= 0)
      {
         c.g = FilterModels::CutoffToG(c.model, effectiveCutoff, rate, (float)mSampleRate * 0.45f);
         // Full resonance sits 10% past each model's self-oscillation
         // threshold, so the top of the knob sings at the cutoff (FilterModels.h).
         const float res = resonance * 1.1f;
         c.k = FilterModels::KFromResonance(c.model, res);
         c.makeup = FilterModels::MakeupGain(c.model, res);
      }
      else if (IsSvfType(filterType))
      {
         const int svfType = filterType - 1; // maps to SynthModes::kFilterLP12 ...
         c.stages = SynthModes::FilterStages(svfType);
         c.shape = SynthModes::FilterShapeOf(svfType);
         const float q = 0.707f + resonance * resonance * 9.3f;
         c.g = tanf((float)M_PI * effectiveCutoff / (float)rate);
         c.k = 1.0f / (q < 0.01f ? 0.01f : q);
      }
      return c;
   }

   // One filter chain, run over whichever state pair it is handed. Both the
   // left and right signal paths call this with their own state, so the two
   // sides can never drift apart in anything but their input and their own
   // accumulated history.
   static float ApplyFilter(FilterModels::Bank<float>& ladder, DspMath::TptSvf* svfBank, float input,
                            const FilterCoeffs& c)
   {
      if (c.model >= 0)
         return ladder.Process(c.model, input, c.g, c.k);
      if (IsSvfType(c.type))
      {
         float filtered = input;
         for (int s = 0; s < c.stages; ++s)
         {
            svfBank[s].g = c.g;
            svfBank[s].k = c.k;
            const DspMath::TptSvf::Outputs o = svfBank[s].Process(filtered);
            switch (c.shape)
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

   // Drive + filter for one signal path, at the oversampled rate: AC
   // coupling, up, ADAA'd tanh (plus the analog hiss), filter, down.
   static float DriveAndFilter(Oversampler& os, int ch, DspMath::DcBlocker& ac, DspMath::FastTanhAdaa& drive,
                               FilterModels::Bank<float>& ladder, DspMath::TptSvf* svfBank, float mixed, float driveGain,
                               float hiss, const FilterCoeffs& c)
   {
      float x[Oversampler::kMaxFactor];
      os.Up(ch, ac.Process(mixed) * driveGain, x);
      for (int k = 0; k < os.Factor(); k++)
         x[k] = ApplyFilter(ladder, svfBank, drive.Process(x[k]) + hiss, c);
      return os.Down(ch, x);
   }

   // 2.16: four voices' signal paths at once - lane l of group g is voice
   // 4g + l, one group per side. Every lane advances; an idle voice's lane
   // filters silence and is reset when the voice next starts a note.
   struct LaneGroup
   {
      VoiceSimd::DcBlocker4 ac;
      VoiceSimd::Oversampler4 os;
      FilterModels::Bank<VoiceSimd::F4> ladder;
      VoiceSimd::Svf4 svf[SynthModes::kMaxFilterStages];

      void ResetLane(int l)
      {
         ac.ResetLane(l);
         os.ResetLane(l);
         ladder.ResetLane(l);
         for (auto& st : svf)
            st.ResetLane(l);
      }
   };
   static constexpr int kLaneGroups = AnalogSynthCore::kMaxVoices / 4;
   static_assert(AnalogSynthCore::kMaxVoices % 4 == 0, "voices fill whole lane groups");

   void ResetVoiceLanes(int vi)
   {
      mLanes[0][vi / 4].ResetLane(vi % 4);
      mLanes[1][vi / 4].ResetLane(vi % 4);
   }

   // DriveAndFilter for one lane group. The ADAA drive stays per voice (it
   // runs in double precision); only lanes whose voice is sounding call it.
   static VoiceSimd::F4 DriveAndFilter4(LaneGroup& grp, VoiceSimd::F4 mixed, VoiceSimd::F4 driveGain,
                                        DspMath::FastTanhAdaa* const* drive, const float* hiss, VoiceSimd::F4 g,
                                        VoiceSimd::F4 k, const FilterCoeffs& c)
   {
      using VoiceSimd::F4;
      F4 x[Oversampler::kMaxFactor];
      grp.os.Up(grp.ac.Process(mixed) * driveGain, x);
      for (int j = 0; j < grp.os.Factor(); j++)
      {
         alignas(16) float t[4];
         x[j].Store(t);
         for (int l = 0; l < 4; l++)
            t[l] = drive[l] ? drive[l]->Process(t[l]) + hiss[l] : 0.0f;
         F4 y = F4::Load(t);
         if (c.model >= 0)
            y = grp.ladder.Process(c.model, y, g, k);
         else if (IsSvfType(c.type))
            for (int st = 0; st < c.stages; ++st)
               y = grp.svf[st].Process(y, g, k, c.shape);
         x[j] = y;
      }
      return grp.os.Down(x);
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
      mVoiceMod.BeginBlock(mVoicePlan, Transport::Instance().Tempo());
      const bool perVoice = mVoiceMod.Active();

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
               NoteOn(evts[evtIdx].note, evts[evtIdx].velocity, evts[evtIdx].voiceId, evts[evtIdx].bendSemitones, evts[evtIdx].pressure, evts[evtIdx].slide);
            else if (evts[evtIdx].bendUpdate)
               BendUpdate(evts[evtIdx]);
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
            // FM reads osc 2's value at this instant, with no delay; the
            // audio path hears its band-limited output (stepped below, after
            // osc 1, so a hard sync knows exactly when osc 1 wrapped).
            const float osc2Fm = mFreeOsc2.Peek(wave2Dsp, pw1);

            // Osc 1 with FM from Osc 2
            const float osc1Cents = oct1 * 1200.0f + semi1 * 100.0f + fine1;
            const float osc1BaseHz = std::clamp(baseHz * powf(2.0f, osc1Cents / 1200.0f),
                                                10.0f, (float)mSampleRate * 0.45f);
            const float fmRatio = FmRatio(osc2Fm, fm);
            const float osc1ModHz = std::clamp(osc1BaseHz * fmRatio, 10.0f, (float)mSampleRate * 0.45f);

            // Osc 1 Unison, summed into two signal paths at once. Each
            // oscillator's tolerance figure sets both how far it sits off
            // pitch and where it sits in the image, so the stack's most
            // mistuned members are also its widest - the two cues arrive
            // together, as they do when each is a separate circuit.
            float osc1SumL = 0.0f;
            float osc1SumR = 0.0f;
            bool osc1Wrapped = false;
            double syncAt = -1.0;
            for (int u = 0; u < unisonCount; ++u)
            {
               const float uDetuneCents = mUnisonDetuneMul[u] * detuneHalf;
               const float uHz = std::clamp(osc1ModHz * powf(2.0f, uDetuneCents / 1200.0f),
                                            10.0f, (float)mSampleRate * 0.45f);
               mFreeOsc1[u].SetFrequency(uHz, mSampleRate);
               if (u == 0)
                  osc1Wrapped = mFreeOsc1[0].WillWrap(syncAt);
               const float uSample = mFreeOsc1[u].Step(wave1Dsp, pw1);
               float ugL = 1.0f, ugR = 1.0f;
               if (wantStereo)
                  TolerancePan(mUnisonDetuneMul[u] * stereoAmt, ugL, ugR);
               osc1SumL += uSample * ugL;
               osc1SumR += uSample * ugR;
            }
            osc1SumL *= osc1Norm;
            osc1SumR *= osc1Norm;

            // Hard sync: osc 2 is reset at the fraction of the sample where
            // osc 1 wrapped, and the jump is band-limited (BlepOscillator.h).
            const float osc2Sample = mFreeOsc2.Step(wave2Dsp, pw1, (sync && osc1Wrapped) ? syncAt : -1.0);

            // Sub osc: one octave below osc1, tracking osc1's post-FM pitch
            // but not its unison detune - a detuned sub beats against itself
            // in the register where beating is least wanted. Symmetric square
            // (pw fixed at 0.5), BLEP'd like every other osc here.
            const float subHz = std::clamp(osc1ModHz * 0.5f, 5.0f, (float)mSampleRate * 0.45f);
            mFreeSub.SetFrequency(subHz, mSampleRate);
            const float subSample = mFreeSub.Step(BlepOsc::kSquare, 0.5f);

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
            // Drive and filter at the oversampled rate (DriveAndFilter). The
            // analog hiss is drawn separately per side - two circuits, two
            // noise floors - which decorrelates the pair a little further.
            const float hissL = analog ? mFreeNoise.Next() * 0.0001f : 0.0f;
            const float hissR = analog ? mFreeNoise.Next() * 0.0001f : 0.0f;

            // The right-hand chain starts from silence rather than from
            // whatever it held the last time the stack was wide, so turning
            // spread up mid-note fades a clean path in instead of resuming a
            // stale one.
            if (wantStereo && !mFreeStereoPath)
            {
               mFreeLadderR.Reset();
               for (int st = 0; st < SynthModes::kMaxFilterStages; ++st)
                  mFreeSvfR[st].Reset();
               mFreeDriveR.Reset();
               mFreeAcR = mFreeAcL; // same coupling state, so R starts without a DC step
               mFreeOs.ResetChannel(1);
            }
            mFreeStereoPath = wantStereo;

            const FilterCoeffs fc =
               MakeFilterCoeffs(filterType, cutoff, resonance, mSampleRate * AnalogSynthCore::kFilterOversample);
            const float filteredL =
               DriveAndFilter(mFreeOs, 0, mFreeAcL, mFreeDriveL, mFreeLadder, mFreeSvf, mixedL, driveGain, hissL, fc);
            const float filteredR = wantStereo
               ? DriveAndFilter(mFreeOs, 1, mFreeAcR, mFreeDriveR, mFreeLadderR, mFreeSvfR, mixedR, driveGain, hissR, fc)
               : filteredL;

            outL = filteredL * fc.makeup;
            outR = filteredR * fc.makeup;
            activeCount = 1;
         }
         else
         {
            // Note-driven polyphonic mode
            int active = 0;
            float voiceSumL = 0.0f;
            float voiceSumR = 0.0f;

            const AnalogSynthCore::VoiceKnobs shared = { fine1, semi1, oct1, fine2, semi2, oct2, pw1, fm, oscMix, osc1Vol, osc2Vol, subVol, noiseVol,
                                                         cutoff, resonance, drive, keyTrack, detune, attackMs, decayMs, sustainLevel, releaseMs,
                                                         osc1Gain, osc2Gain, driveGain, detuneHalf };
            for (int vi = 0; vi < AnalogSynthCore::kMaxVoices; ++vi)
            {
               Voice& v = mVoices[vi];
               if (!v.active)
                  continue;

               AnalogSynthCore::VoiceKnobs vk = shared;
               if (perVoice)
               {
                  mVoiceMod.Apply(vi, [&vk](int id, float x) { vk.Set(id, x); });
                  vk.Derive();
               }

               v.ampEnv.SetADSR(vk.attackMs, vk.decayMs, vk.sustainLevel, vk.releaseMs);
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
               const float osc2Cents = vk.oct2 * 1200.0f + vk.semi2 * 100.0f + vk.fine2;
               const float osc2Hz = std::clamp(voiceBaseHz * powf(2.0f, osc2Cents / 1200.0f),
                                               10.0f, (float)mSampleRate * 0.45f);
               v.osc2.SetFrequency(osc2Hz, mSampleRate);
               const float osc2Fm = v.osc2.Peek(wave2Dsp, vk.pw1); // see the free-running path

               // Osc 1 with FM from Osc 2
               const float osc1Cents = vk.oct1 * 1200.0f + vk.semi1 * 100.0f + vk.fine1;
               const float osc1BaseHz = std::clamp(voiceBaseHz * powf(2.0f, osc1Cents / 1200.0f),
                                                   10.0f, (float)mSampleRate * 0.45f);
               const float fmRatio = FmRatio(osc2Fm, vk.fm);
               const float osc1ModHz = std::clamp(osc1BaseHz * fmRatio, 10.0f, (float)mSampleRate * 0.45f);

               // Osc 1 Unison - see the free-running path for why the same
               // tolerance figure drives both detune and image position.
               float osc1SumL = 0.0f;
               float osc1SumR = 0.0f;
               bool osc1Wrapped = false;
               double syncAt = -1.0;
               for (int u = 0; u < unisonCount; ++u)
               {
                  const float uDetuneCents = mUnisonDetuneMul[u] * vk.detuneHalf;
                  const float uHz = std::clamp(osc1ModHz * powf(2.0f, uDetuneCents / 1200.0f),
                                               10.0f, (float)mSampleRate * 0.45f);
                  v.osc1[u].SetFrequency(uHz, mSampleRate);
                  if (u == 0)
                     osc1Wrapped = v.osc1[0].WillWrap(syncAt);
                  const float uSample = v.osc1[u].Step(wave1Dsp, vk.pw1);
                  float ugL = 1.0f, ugR = 1.0f;
                  if (wantStereo)
                     TolerancePan(mUnisonDetuneMul[u] * stereoAmt, ugL, ugR);
                  osc1SumL += uSample * ugL;
                  osc1SumR += uSample * ugR;
               }
               osc1SumL *= osc1Norm;
               osc1SumR *= osc1Norm;

               const float osc2Sample = v.osc2.Step(wave2Dsp, vk.pw1, (sync && osc1Wrapped) ? syncAt : -1.0);

               // Sub osc: see the free-running path - own accumulator at half
               // osc1's post-FM pitch, unaffected by unison detune.
               const float subHz = std::clamp(osc1ModHz * 0.5f, 5.0f, (float)mSampleRate * 0.45f);
               v.sub.SetFrequency(subHz, mSampleRate);
               const float subSample = v.sub.Step(BlepOsc::kSquare, 0.5f);

               // Noise
               const float noiseSample = v.noise.Next();

               // Mixer - osc 2, sub and noise stay centred.
               const float centreMix = osc2Sample * vk.osc2Gain * vk.osc2Vol + subSample * vk.subVol + noiseSample * vk.noiseVol;
               const float mixedL = osc1SumL * vk.osc1Gain * vk.osc1Vol + centreMix;
               const float mixedR = osc1SumR * vk.osc1Gain * vk.osc1Vol + centreMix;

               // Filter with KeyTracking
               const float keyTrackOctaves = ((currentPitch - 60.0f) / 12.0f) * vk.keyTrack;
               const float driftOctaves = analog ? v.driftCutoffOffset : 0.0f;
               const float effectiveCutoff = std::clamp(vk.cutoff * powf(2.0f, keyTrackOctaves + driftOctaves),
                                                        20.0f, (float)mSampleRate * 0.45f);
               if (wantStereo && !v.stereoPath)
               {
                  v.ladderR.Reset();
                  for (int st = 0; st < SynthModes::kMaxFilterStages; ++st)
                     v.svfR[st].Reset();
                  v.driveR.Reset();
                  v.acR = v.acL; // same coupling state, so R starts without a DC step
                  v.os.ResetChannel(1);
                  mLanes[1][vi / 4].ResetLane(vi % 4);
                  mLanes[1][vi / 4].ac.CopyLane(mLanes[0][vi / 4].ac, vi % 4);
               }
               v.stereoPath = wantStereo;

               // Drive (one noise draw per side) and filter, oversampled.
               const float hissL = analog ? v.noise.Next() * 0.0001f : 0.0f;
               const float hissR = analog ? v.noise.Next() * 0.0001f : 0.0f;
               const FilterCoeffs fc = MakeFilterCoeffs(filterType, effectiveCutoff, vk.resonance,
                                                        mSampleRate * AnalogSynthCore::kFilterOversample);
               float vgL = 1.0f, vgR = 1.0f;
               if (stereoAmt > 0.0001f)
                  TolerancePan(AnalogSynthCore::kVoicePanSeed[vi] * stereoAmt, vgL, vgR);
               const float envGain = ampEnvVal * velGain * fc.makeup;

               if (mUseLanes)
               {
                  // Staged; the drive and filter run four voices at a time
                  // after this loop, and the voices are summed there in order.
                  LaneStage& ls = mLaneStage;
                  ls.mixed[0][vi] = mixedL;
                  ls.mixed[1][vi] = mixedR;
                  ls.hiss[0][vi] = hissL;
                  ls.hiss[1][vi] = hissR;
                  ls.driveGain[vi] = vk.driveGain;
                  ls.g[vi] = fc.g;
                  ls.k[vi] = fc.k;
                  ls.envGain[vi] = envGain;
                  ls.vg[0][vi] = vgL;
                  ls.vg[1][vi] = vgR;
                  ls.staged[vi] = true;
                  ls.fc = fc;
                  active++;
                  continue;
               }

               const float filteredL = DriveAndFilter(v.os, 0, v.acL, v.driveL, v.ladder, v.svf, mixedL, vk.driveGain, hissL, fc);
               const float filteredR = wantStereo
                  ? DriveAndFilter(v.os, 1, v.acR, v.driveR, v.ladderR, v.svfR, mixedR, vk.driveGain, hissR, fc)
                  : filteredL;

               const float voiceOutL = filteredL * envGain * vgL;
               const float voiceOutR = filteredR * envGain * vgR;
               v.lastOut = (voiceOutL + voiceOutR) * 0.5f;
               voiceSumL += voiceOutL;
               voiceSumR += voiceOutR;
               active++;
            }

            if (mUseLanes)
               RunVoiceLanes(wantStereo, voiceSumL, voiceSumR);

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

   // Runs the staged voices' drive and filter four at a time, then sums them
   // in voice order - the scalar loop's order, so the two paths agree.
   void RunVoiceLanes(bool wantStereo, float& sumL, float& sumR)
   {
      using VoiceSimd::F4;
      LaneStage& ls = mLaneStage;
      alignas(16) float filtered[2][AnalogSynthCore::kMaxVoices];
      for (int grpIdx = 0; grpIdx < kLaneGroups; ++grpIdx)
      {
         const int v0 = grpIdx * 4;
         if (!(ls.staged[v0] || ls.staged[v0 + 1] || ls.staged[v0 + 2] || ls.staged[v0 + 3]))
            continue;
         for (int side = 0; side < (wantStereo ? 2 : 1); ++side)
         {
            DspMath::FastTanhAdaa* drive[4];
            for (int l = 0; l < 4; l++)
               drive[l] = ls.staged[v0 + l] ? (side == 0 ? &mVoices[v0 + l].driveL : &mVoices[v0 + l].driveR) : nullptr;
            const F4 out = DriveAndFilter4(mLanes[side][grpIdx], F4::Load(&ls.mixed[side][v0]), F4::Load(&ls.driveGain[v0]),
                                           drive, &ls.hiss[side][v0], F4::Load(&ls.g[v0]), F4::Load(&ls.k[v0]), ls.fc);
            out.Store(&filtered[side][v0]);
         }
         if (!wantStereo)
            for (int l = 0; l < 4; l++)
               filtered[1][v0 + l] = filtered[0][v0 + l];
      }
      for (int vi = 0; vi < AnalogSynthCore::kMaxVoices; ++vi)
      {
         if (!ls.staged[vi])
            continue;
         ls.staged[vi] = false;
         const float voiceOutL = filtered[0][vi] * ls.envGain[vi] * ls.vg[0][vi];
         const float voiceOutR = filtered[1][vi] * ls.envGain[vi] * ls.vg[1][vi];
         mVoices[vi].lastOut = (voiceOutL + voiceOutR) * 0.5f;
         sumL += voiceOutL;
         sumR += voiceOutR;
      }
   }

   void NoteOn(int note, float velocity, int voiceId, float bendSemitones, float pressure = 0.0f, float slide = 0.0f)
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
         ResetVoiceLanes(slot);
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
      mVoiceMod.NoteOn(slot, pressure, slide);
   }

   void NoteOff(int voiceId)
   {
      for (int i = 0; i < AnalogSynthCore::kMaxVoices; ++i)
      {
         Voice& v = mVoices[i];
         if (!v.active || v.voiceId != voiceId || !v.held)
            continue;
         v.held = false;
         v.ampEnv.NoteOff();
         mVoiceMod.NoteOff(i);
      }
   }

   void BendUpdate(const NoteEvent& e)
   {
      for (int i = 0; i < (int)(sizeof(mVoices) / sizeof(mVoices[0])); i++)
      {
         Voice& v = mVoices[i];
         if (!v.active || v.voiceId != e.voiceId)
            continue;
         v.bend = e.bendSemitones;
         if (e.hasExpression) // MPE pressure / slide for Note Expression (2.15)
            mVoiceMod.Express(i, e.pressure, e.slide);
      }
   }

   int ActiveVoices() const { return mActiveVoices.load(std::memory_order_relaxed); }
   MeterRing& ScopeRing() { return mScopeRing; }
   double DebugMailboxSampleRate() const { return mMailbox.SampleRate(); }
   void PublishVoicePlan(const VoiceMod::Plan& plan) { mVoicePlan.Publish(plan); }
   // Tests: what voice `voice`'s per-voice source `source` reads now.
   float DebugVoiceSource(int voice, int source) const { return mVoiceMod.Peek(voice, source); }

private:
   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;
   MeterRing mScopeRing;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;
   VoiceMod::PlanBox mVoicePlan;
   VoiceMod::Runtime<AnalogSynthCore::kMaxVoices> mVoiceMod;

   Voice mVoices[AnalogSynthCore::kMaxVoices];
   uint64_t mNextAge = 1;
   float mLastNotePitch = 60.0f;
   bool mHasLastNote = false;

   // Free-running fallback state
   DspMath::OnePole mFreeGlide;
   BlepOsc::NarrowOscillator mFreeOsc1[AnalogSynthCore::kMaxUnison];
   BlepOsc::NarrowOscillator mFreeOsc2;
   BlepOsc::NarrowOscillator mFreeSub;
   DspMath::WhiteNoise mFreeNoise;
   FilterModels::Bank<float> mFreeLadder;
   FilterModels::Bank<float> mFreeLadderR;
   DspMath::FastTanhAdaa mFreeDriveL, mFreeDriveR;
   DspMath::DcBlocker mFreeAcL, mFreeAcR;
   Oversampler mFreeOs;

   // 2.16 lane path (off when VoiceSimd::ForceScalar was set at prepare).
   bool mUseLanes = false;
   LaneGroup mLanes[2][kLaneGroups];
   struct LaneStage
   {
      alignas(16) float mixed[2][AnalogSynthCore::kMaxVoices] = {};
      alignas(16) float hiss[2][AnalogSynthCore::kMaxVoices] = {};
      alignas(16) float driveGain[AnalogSynthCore::kMaxVoices] = {};
      alignas(16) float g[AnalogSynthCore::kMaxVoices] = {};
      alignas(16) float k[AnalogSynthCore::kMaxVoices] = {};
      float envGain[AnalogSynthCore::kMaxVoices] = {};
      float vg[2][AnalogSynthCore::kMaxVoices] = {};
      bool staged[AnalogSynthCore::kMaxVoices] = {};
      FilterCoeffs fc;
   } mLaneStage;
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
