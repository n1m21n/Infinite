#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "WavetableNode.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/PhaseShape.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/SynthModes.h"
#include "audio/VoiceMod.h"
#include "audio/Wavetable.h"
#include "audio/dsp/BlepOscillator.h"
#include "audio/dsp/SimdF4.h"
#include "core/Transport.h"

namespace WavetableSynthCore
{
   constexpr int kEngines = WavetableNode::kEngines;
   constexpr int kMaxUnison = WavetableNode::kMaxUnison;
   constexpr int kMaxVoices = WavetableNode::kMaxVoices;
   constexpr int kMaxStages = SynthModes::kMaxFilterStages;

   // Smoothed params, carried through ParamMailbox. Everything that is an
   // integer, a mode, or an envelope time is a plain atomic instead (read once
   // per block, below) - smoothing an attack time per sample would be
   // meaningless and the mailbox only has kMaxParams (64) slots.
   //
   // Worst case here is 4 + 2*12 = 28 slots for one node.
   enum GlobalParam
   {
      kFrequency = 0,
      kVolume,
      kMix,
      kGlide,
      kPitchBend,
      kFmDepth,
      kNumGlobalParams
   };

   enum EngineParam
   {
      kEngPosition = 0,
      kEngVolume,
      kEngPan,
      kEngDetune,
      kEngStereoWidth,
      kEngFine,
      kEngPhase,
      kEngPhaseRand,
      kEngWarpAmount,
      kEngWarpRatio,
      kEngCutoff,
      kEngResonance,
      kNumEngineParams
   };

   inline int EngineParamId(int engine, int param)
   {
      return kNumGlobalParams + engine * kNumEngineParams + param;
   }

   static_assert(kNumGlobalParams + kEngines * kNumEngineParams <= ParamMailbox::kMaxParams,
                 "Wavetable's smoothed params no longer fit one ParamMailbox");

   // Arrangement Timeline per-clip pitch (AudioNode::SetClipPitchOverride,
   // pushed from main.cpp's RunTopology lookahead pass): a dedicated slot one
   // past PushParams's own range, so PushParams's per-UI-frame push (main
   // thread) never stomps the per-block clip override (audio thread) back to
   // 0 - unlike kPitchBend, which PushParams re-sends every frame and which
   // already carries real pitch-bend-wheel semantics that a clip transpose
   // has no business fighting with. Added to the same cents sum as
   // kPitchBend in both the free-running and note-driven render paths below.
   constexpr int kClipPitchParam = kNumGlobalParams + kEngines * kNumEngineParams;
   static_assert(kClipPitchParam < ParamMailbox::kMaxParams,
                 "Wavetable's clip-pitch slot no longer fits one ParamMailbox");

   // Golden-ratio phase seeds: deterministic per-unison-voice start offsets,
   // so `phaseRandomize` spreads the stack without needing PRNG state.
   constexpr float kVoicePhaseSeed[kMaxUnison] = {
      0.000000f, 0.618034f, 0.236068f, 0.854102f,
      0.472136f, 0.090170f, 0.708204f, 0.326238f
   };

   // Time constant of the polyphony-normalisation ramp. Deliberately fixed
   // and not tied to the glide knob: this is a click fix, not a musical
   // control. Short enough that a chord still lands as one event, long enough
   // that the gain step it replaces is inaudible.
   constexpr float kPolyNormSmoothSec = 0.015f;

   inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

   // Asymmetric phase bend. w > 0 compresses the first half of the cycle into
   // less time and stretches the second, which moves the waveform's spectral
   // centroid the way a formant shift does; the p*(1-p) term keeps the result
   // inside [0, 1] for any |w| <= 1, so the read can never leave the frame.
   inline double BendPhase(double p, double w)
   {
      p -= floor(p);
      if (w == 0.0)
         return p;
      return p + w * p * (1.0 - p);
   }

   // Sync's and formant's ratio, swept in from 1 by the warp amount.
   inline double WarpBlendedRatio(float amount, float ratio)
   {
      return 1.0 + (double)amount * ((double)ratio - 1.0);
   }

   // Quantize's step count: 64 at the bottom of the amount, 2 at the top.
   inline int QuantizeSteps(float amount)
   {
      const double inv = 1.0 - (double)amount;
      return 2 + (int)(inv * inv * 62.0);
   }

   // ------------------------------------------------------- warp, phase side
   // The part of a warp that acts before the table read. `mod` is the other
   // engine's previous sample (or the internal operator standing in for it -
   // see SynthModes.h), already in [-1, 1].
   //
   // amount <= 0 is an early-out for every mode, not just an optimisation: it
   // is what makes the warp amount a genuine dry/wet control, so a mode with
   // its depth at zero is bit-identical to "off" and switching modes at zero
   // depth is silent.
   inline double WarpReadPhase(int mode, float amount, float ratio, double p, float mod)
   {
      p -= floor(p);
      if (amount <= 0.0f)
         return p;

      switch (mode)
      {
         case SynthModes::kWarpFM:
            // Phase modulation, not true frequency modulation: adding the
            // modulator to the phase instead of to the increment is what keeps
            // the carrier's pitch stable as depth rises. Half a cycle at full
            // depth is already deep enough to reach noise.
            return p + 0.5 * (double)(amount * mod);

         case SynthModes::kWarpPD:
            // Phase distortion: the same bend as "bend +", but with its amount
            // driven by the modulator rather than fixed - which is the
            // difference between a static formant shift and a moving one.
            return BendPhase(p, (double)(amount * mod));

         case SynthModes::kWarpSync:
            // Hard sync: the read phase runs at `ratio` times the voice's own
            // and wraps early, so the cycle restarts mid-waveform. `amount`
            // blends the ratio in from 1, so the knob sweeps sync in from
            // nothing rather than jumping to the full interval.
            return PhaseShape::Apply(PhaseShape::Kind::Sync, p, WarpBlendedRatio(amount, ratio));

         case SynthModes::kWarpFormant:
            // The cycle plays `ratio` times faster and holds its end for the
            // rest of the period: the table's spectrum moves up by the ratio
            // while the pitch stays (Vital/Serum "formant"). Blended like sync.
            return PhaseShape::Apply(PhaseShape::Kind::Formant, p, WarpBlendedRatio(amount, ratio));

         case SynthModes::kWarpBendPlus:  return BendPhase(p, (double)amount);
         case SynthModes::kWarpBendMinus: return BendPhase(p, -(double)amount);

         case SynthModes::kWarpAsymPlus:
            // Power-law skew, PhaseShape's bend: p^(4^-a), a 4:1 stretch of
            // one half of the cycle against the other at full depth.
            return PhaseShape::Apply(PhaseShape::Kind::Bend, p, (double)amount);
         case SynthModes::kWarpAsymMinus:
            return PhaseShape::Apply(PhaseShape::Kind::Bend, p, -(double)amount);

         case SynthModes::kWarpMirror:
            // Fold the cycle back on itself: the second half replays the first
            // in reverse, which halves the period of the fundamental and
            // leaves a waveform symmetric about its midpoint.
            return PhaseShape::Apply(PhaseShape::Kind::Mirror, p, (double)amount);

         case SynthModes::kWarpPinch:
            return PhaseShape::Apply(PhaseShape::Kind::Pinch, p, (double)amount);
         case SynthModes::kWarpSkew:
            return PhaseShape::Apply(PhaseShape::Kind::Skew, p, (double)amount);

         case SynthModes::kWarpQuantize:
         {
            // Step the phase, so the cycle reads as a staircase - the coarser
            // the steps, the more high harmonics the edges add. 64 steps at
            // the bottom of the range down to 2 at the top, blended by the
            // same amount so the effect fades in continuously.
            const double q = PhaseShape::Apply(PhaseShape::Kind::Quantize, p, (double)QuantizeSteps(amount));
            return p + (q - p) * (double)amount;
         }

         default:
            return p; // amplitude-domain or off
      }
   }

   // --------------------------------------------------- warp, amplitude side
   // The part of a warp that acts on the sample the table read returned.
   // `half` is the same read half a cycle later, which only the harmonic
   // selectors need - computing it unconditionally would double every read.
   inline float WarpSample(int mode, float amount, float s, float half, float mod)
   {
      if (amount <= 0.0f)
         return s;

      switch (mode)
      {
         case SynthModes::kWarpAM:
            // Unipolar: the modulator scales between silence and unity, so the
            // carrier is never inverted and the original pitch stays audible.
            return s * ((1.0f - amount) + amount * (0.5f + 0.5f * mod));

         case SynthModes::kWarpRM:
            // Bipolar: the carrier *is* inverted every time the modulator
            // crosses zero, which is what removes the fundamental and leaves
            // the sum-and-difference pair ring modulation is wanted for.
            return s * ((1.0f - amount) + amount * mod);

         case SynthModes::kWarpFlip:
            return s * (1.0f - 2.0f * amount);

         case SynthModes::kWarpRectify:
            return Lerp(s, fabsf(s), amount);

         case SynthModes::kWarpOddOnly:
            // f(p) decomposes into an odd part (f(p) - f(p+1/2))/2 and an even
            // part (f(p) + f(p+1/2))/2, whose spectra are exactly the odd and
            // even harmonics of the original. So this is harmonic selection
            // proper, not a filter that approximates it.
            return Lerp(s, (s - half) * 0.5f, amount);

         case SynthModes::kWarpEvenOnly:
            return Lerp(s, (s + half) * 0.5f, amount);

         default:
            return s;
      }
   }

   inline bool WarpNeedsHalfCycle(int mode)
   {
      return mode == SynthModes::kWarpOddOnly || mode == SynthModes::kWarpEvenOnly;
   }

   // ------------------------------------------------- warp anti-aliasing
   // A phase warp changes how fast the table is read (sync up to 16x, the
   // bends and skews locally), so the mip the voice's pitch alone would pick
   // can hold harmonics that land above Nyquist. These warps choose the mip
   // from the read's local speed instead (WarpReadSpeed), crossfaded between
   // two levels.
   inline bool WarpIsPhaseDomain(int mode)
   {
      switch (mode)
      {
         case SynthModes::kWarpFM:
         case SynthModes::kWarpPD:
         case SynthModes::kWarpSync:
         case SynthModes::kWarpBendPlus:
         case SynthModes::kWarpBendMinus:
         case SynthModes::kWarpAsymPlus:
         case SynthModes::kWarpAsymMinus:
         case SynthModes::kWarpMirror:
         case SynthModes::kWarpQuantize:
         case SynthModes::kWarpFormant:
         case SynthModes::kWarpPinch:
         case SynthModes::kWarpSkew:
            return true;
         default:
            return false;
      }
   }

   // Read-phase cycles per sample at base phase p: |d(read)/dp| * inc, from a
   // one-sided difference so a jump (sync wrap, quantize step) does not count
   // as speed - those are band-limited separately, as discontinuities.
   inline double WarpReadSpeed(int mode, float amount, float ratio, double p, float mod, double inc)
   {
      constexpr double kDelta = 1.0e-4;
      const double a = WarpReadPhase(mode, amount, ratio, p, mod);
      double b = WarpReadPhase(mode, amount, ratio, p + kDelta, mod);
      double d = b - a;
      d -= std::floor(d + 0.5); // wrap: a read wrap is not a jump
      if (std::fabs(d) > 0.25)  // a real jump inside the probe: read the other side
      {
         b = WarpReadPhase(mode, amount, ratio, p - kDelta, mod);
         d = a - b;
         d -= std::floor(d + 0.5);
      }
      return std::fabs(d / kDelta) * std::fabs(inc);
   }

   // The base phases in (0, 1] at which a warp has a value or slope
   // discontinuity, in increasing order; returns how many.
   inline int WarpBreakpoints(int mode, float amount, float ratio, double* out, int maxOut)
   {
      // FM's read is continuous across the wrap. The power-law skews are
      // continuous in value too, but asym+'s p^e (e < 1) has an unbounded
      // slope there - a cusp, not a corner - so a slope-change correction has
      // no finite size; and regularising the curve (tried) only turns the cusp
      // into a sub-sample sweep through most of a cycle, which is worse. Both
      // skews rely on the read-speed mip alone; asym+ at high depth (which is
      // meant to approach a pulse) still aliases and would need the voice
      // oversampled to fix. Every other phase warp has a corner or a jump at
      // the wrap.
      if (amount <= 0.0f || !WarpIsPhaseDomain(mode) || mode == SynthModes::kWarpFM ||
          mode == SynthModes::kWarpAsymPlus || mode == SynthModes::kWarpAsymMinus)
         return 0;
      int n = 0;
      if (mode == SynthModes::kWarpMirror)
         out[n++] = 0.5;
      if (mode == SynthModes::kWarpFormant)
      {
         // The read reaches the cycle's end at 1/r and holds: a corner.
         const double r = WarpBlendedRatio(amount, ratio);
         if (r > 1.0 + 1.0e-6)
            out[n++] = 1.0 / std::min(r, 64.0);
      }
      if (mode == SynthModes::kWarpSkew)
         out[n++] = 0.5 + 0.49 * (double)std::min(amount, 1.0f); // the moved half-way point
      if (mode == SynthModes::kWarpQuantize)
      {
         const int steps = QuantizeSteps(amount);
         for (int k = 1; k < steps && n < maxOut - 1; k++)
            out[n++] = (double)k / (double)steps;
      }
      out[n++] = 1.0;
      return n;
   }

   // TPT SVF coefficients, computed once per engine per sample and copied into
   // every stage of the cascade. TptSvf::SetCutoff would recompute the same
   // tanf per stage per channel - six times over for a 36 dB stereo filter -
   // for a value that is identical in all of them.
   inline void SvfCoeffs(float hz, float q, float sampleRate, float& g, float& k)
   {
      const float freq = std::clamp(hz, 20.0f, sampleRate * 0.45f);
      g = tanf((float)M_PI * freq / sampleRate);
      k = 1.0f / (q < 0.01f ? 0.01f : q);
   }

   inline float NoteToHz(float note)
   {
      return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
   }

   // Linear through-zero FM (fmMode == 1): the modulator adds a frequency
   // offset in Hz proportional to the carrier, the DX7/Serum/Massive-X/
   // Operator convention - not a pitch-multiplying exponential FM, which
   // scales the wrong way (a modulator that should double the carrier's
   // *frequency offset* instead barely moves it in cents at low carrier
   // frequencies and blows past Nyquist at high ones). Tuned so a full-scale
   // modulator at depth 1.0 drives roughly a 4x frequency excursion.
   constexpr float kFmLinScale = 4.0f;

   // 2.16: a voice's pitch, pan and filter inputs sit still for most of a
   // note, but each costs a powf / sincosf / tanf per sample. Memo keeps the
   // last input and its result and recomputes only when the input moves, so
   // the output is bit-identical to computing every sample. `on` false (the
   // reference path, VoiceSimd::ForceScalar) always recomputes.
   struct Memo
   {
      float x = NAN, y = 0.0f;
      template <class F>
      float Get(bool on, float in, F f)
      {
         if (!on || !(in == x))
         {
            x = in;
            y = f(in);
         }
         return y;
      }
   };
}

// ------------------------------------------------------------- audio thread
class AudioWavetableNode : public AudioNode
{
public:
   AudioWavetableNode() { BlepOsc::EnsureTables(); } // main thread, before any audio-thread use

   // The warp anti-aliasing's linear-phase correction kernel: every voice
   // passes through it, so the latency is fixed whatever the warp.
   int LatencySamples() const override { return BlepOsc::kLatency; }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      using namespace WavetableSynthCore;
      mSampleRate = sampleRate;
      mFast = !VoiceSimd::ForceScalar().load(std::memory_order_relaxed);
      mMailbox.PrepareToPlay(sampleRate);
      mVoiceMod.Prepare(sampleRate);
      for (int i = 0; i < kNumGlobalParams + kEngines * kNumEngineParams; i++)
         mMailbox.SetImmediate(i, mFloatAtomics[i].load(std::memory_order_relaxed));
      mMailbox.SetImmediate(kClipPitchParam, 0.0f);

      mFreeGlide.SetImmediate(mFloatAtomics[kFrequency].load(std::memory_order_relaxed));
      // Unity, not zero: at 0 the very first note would fade in over the
      // smoother's time constant instead of starting at full level.
      mPolyNormSmooth.SetImmediate(1.0f);
      mPolyNormSmooth.SetTimeConstant(kPolyNormSmoothSec, sampleRate);
      for (int e = 0; e < kEngines; e++)
         mFreeEngine[e].Reset(sampleRate);
      for (Voice& v : mVoices)
         v.Reset(sampleRate);
      mLastNotePitch = 60.0f;
      mHasLastNote = false;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   // See WavetableNode::DebugMailboxSampleRate's comment - test-only.
   double DebugMailboxSampleRate() const { return mMailbox.SampleRate(); }
   void PublishVoicePlan(const VoiceMod::Plan& plan) { mVoicePlan.Publish(plan); }
   float DebugVoiceSource(int voice, int source) const { return mVoiceMod.Peek(voice, source); }

   // See WavetableNode::DebugFmMode's comment - test-only. mFmMode is a
   // plain atomic (mode switches skip the smoothed ParamMailbox), so this
   // reads it back directly rather than through the mailbox.
   int DebugFmMode() const { return mFmMode.load(std::memory_order_relaxed); }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      using namespace WavetableSynthCore;
      // Per-block snapshot of everything that isn't smoothed. Reading these
      // once here rather than per sample is both cheaper and more correct: an
      // integer that changed mid-block would otherwise split the block's
      // unison stack or table across two settings.
      EngineBlock eng[kEngines];
      for (int e = 0; e < kEngines; e++)
      {
         eng[e].on = mEngOn[e].load(std::memory_order_relaxed) != 0;
         eng[e].table = mEngTable[e].load(std::memory_order_relaxed);
         eng[e].unison = std::clamp(mEngUnison[e].load(std::memory_order_relaxed), 1, kMaxUnison);
         eng[e].octave = mEngOctave[e].load(std::memory_order_relaxed);
         eng[e].semi = mEngSemi[e].load(std::memory_order_relaxed);
         eng[e].warpMode = std::clamp(mEngWarpMode[e].load(std::memory_order_relaxed), 0,
                                      SynthModes::kNumWarpModes - 1);
         const int filterType = std::clamp(mEngFilterType[e].load(std::memory_order_relaxed), 0,
                                           SynthModes::kNumFilterTypes - 1);
         eng[e].filterStages = SynthModes::FilterStages(filterType);
         eng[e].filterShape = SynthModes::FilterShapeOf(filterType);
         eng[e].filterIsComb = SynthModes::IsCombFilter(filterType);
         eng[e].filterCombNegative = SynthModes::CombIsNegative(filterType);
         eng[e].filterAmount = mEngFilterAmount[e].load(std::memory_order_relaxed);
         eng[e].pitchAmount = mEngPitchAmount[e].load(std::memory_order_relaxed);
         eng[e].ampA = mEngAmpAdsr[e][0].load(std::memory_order_relaxed);
         eng[e].ampD = mEngAmpAdsr[e][1].load(std::memory_order_relaxed);
         eng[e].ampS = mEngAmpAdsr[e][2].load(std::memory_order_relaxed);
         eng[e].ampR = mEngAmpAdsr[e][3].load(std::memory_order_relaxed);
         eng[e].pitA = mEngPitchAdsr[e][0].load(std::memory_order_relaxed);
         eng[e].pitD = mEngPitchAdsr[e][1].load(std::memory_order_relaxed);
         eng[e].pitS = mEngPitchAdsr[e][2].load(std::memory_order_relaxed);
         eng[e].pitR = mEngPitchAdsr[e][3].load(std::memory_order_relaxed);
         eng[e].fltA = mEngFilterAdsr[e][0].load(std::memory_order_relaxed);
         eng[e].fltD = mEngFilterAdsr[e][1].load(std::memory_order_relaxed);
         eng[e].fltS = mEngFilterAdsr[e][2].load(std::memory_order_relaxed);
         eng[e].fltR = mEngFilterAdsr[e][3].load(std::memory_order_relaxed);
      }

      const bool noteDriven = mNoteInbox != nullptr;
      // Summed to mono rather than reading channels[0] alone - a hard-panned
      // or stereo-wide modulator should still modulate at full strength.
      const AudioBuffer* fmBuf = (inputs && numInputs > 1 && inputs[1] && inputs[1]->numChannels > 0)
                                    ? inputs[1]
                                    : nullptr;
      const int fmMode = mFmMode.load(std::memory_order_relaxed);

      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (noteDriven)
         numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);

      mVoiceMod.BeginBlock(mVoicePlan, Transport::Instance().Tempo());
      const bool perVoice = mVoiceMod.Active();

      int activeCount = 0;
      // Fixed rate, so hoisted out of the sample loop (unlike glide, which
      // tracks a smoothed param and has to be re-set per sample).
      mPolyNormSmooth.SetTimeConstant(WavetableSynthCore::kPolyNormSmoothSec, mSampleRate);

      for (int i = 0; i < buffer.numFrames; i++)
      {
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            if (evts[evtIdx].isNoteOn)
               NoteOn(evts[evtIdx].note, evts[evtIdx].velocity, evts[evtIdx].voiceId, evts[evtIdx].bendSemitones, eng, evts[evtIdx].pressure, evts[evtIdx].slide);
            else if (evts[evtIdx].bendUpdate)
               BendUpdate(evts[evtIdx]);
            else
               NoteOff(evts[evtIdx].voiceId);
            evtIdx++;
         }

         SmoothedBlock sm;
         sm.frequency = mMailbox.SmoothedValue(kFrequency);
         sm.volume = mMailbox.SmoothedValue(kVolume);
         sm.mix = std::clamp(mMailbox.SmoothedValue(kMix), 0.0f, 1.0f);
         sm.glide = mMailbox.SmoothedValue(kGlide);
         sm.pitchBend = mMailbox.SmoothedValue(kPitchBend);
         sm.fmDepth = mMailbox.SmoothedValue(kFmDepth);
         sm.clipPitch = mMailbox.SmoothedValue(kClipPitchParam);
         for (int e = 0; e < kEngines; e++)
         {
            sm.eng[e].position = std::clamp(mMailbox.SmoothedValue(EngineParamId(e, kEngPosition)), 0.0f, 1.0f);
            sm.eng[e].volume = mMailbox.SmoothedValue(EngineParamId(e, kEngVolume));
            sm.eng[e].pan = mMailbox.SmoothedValue(EngineParamId(e, kEngPan));
            sm.eng[e].detune = mMailbox.SmoothedValue(EngineParamId(e, kEngDetune));
            sm.eng[e].stereoWidth = mMailbox.SmoothedValue(EngineParamId(e, kEngStereoWidth));
            sm.eng[e].fine = mMailbox.SmoothedValue(EngineParamId(e, kEngFine));
            sm.eng[e].phase = mMailbox.SmoothedValue(EngineParamId(e, kEngPhase));
            sm.eng[e].phaseRand = mMailbox.SmoothedValue(EngineParamId(e, kEngPhaseRand));
            sm.eng[e].warpAmount = std::clamp(mMailbox.SmoothedValue(EngineParamId(e, kEngWarpAmount)), 0.0f, 1.0f);
            sm.eng[e].warpRatio = mMailbox.SmoothedValue(EngineParamId(e, kEngWarpRatio));
            sm.eng[e].cutoff = mMailbox.SmoothedValue(EngineParamId(e, kEngCutoff));
            sm.eng[e].resonance = std::clamp(mMailbox.SmoothedValue(EngineParamId(e, kEngResonance)), 0.0f, 1.0f);
         }
         // Equal-power A/B crossfade: a linear one dips ~3 dB in the middle,
         // which reads as the mix knob having a hole in it.
         const float mixA = cosf(sm.mix * (float)M_PI * 0.5f);
         const float mixB = sinf(sm.mix * (float)M_PI * 0.5f);

         float extFm = 0.0f;
         if (fmBuf != nullptr)
         {
            const int fmChans = std::min(fmBuf->numChannels, 2);
            for (int c = 0; c < fmChans; c++)
               extFm += fmBuf->channels[c][i];
            extFm /= (float)fmChans;
         }
         const float fmPhaseOffset = (fmMode == 0) ? (extFm * sm.fmDepth) : 0.0f;
         // Linear through-zero FM: a ratio on the carrier's frequency, applied
         // AFTER pitch-bend/vibrato's exponential math below, not folded into
         // the semitone sum - see kFmLinScale's comment.
         const float fmLinRatio = (fmMode == 1) ? (1.0f + extFm * sm.fmDepth * kFmLinScale) : 1.0f;

         float outL = 0.0f, outR = 0.0f;

         if (!noteDriven)
         {
            // Free-running: one permanently-open voice at `frequency`, no
            // envelopes. Same rule the Oscillator had, so a Wavetable patched
            // straight to an output makes sound with nothing else connected.
            // With no envelopes running, the filter envelope contributes
            // nothing and the cutoff knob is the cutoff.
            mFreeGlide.SetTimeConstant(sm.glide, mSampleRate);
            const float base = mFreeGlide.Process(sm.frequency);
            const float prevOut[kEngines] = { mFreeEngine[0].lastOut, mFreeEngine[1].lastOut };
            for (int e = 0; e < kEngines; e++)
            {
               if (!eng[e].on)
               {
                  mFreeEngine[e].lastOut = 0.0f;
                  continue;
               }
               const float semitones = (float)(eng[e].octave * 12 + eng[e].semi) * 100.0f + sm.eng[e].fine +
                                       sm.pitchBend * 100.0f + sm.clipPitch * 100.0f;
               // Carrier pitch (pre-FM) clamped to its own sane range first...
               const float carrierFreq = std::clamp(base * powf(2.0f, semitones / 1200.0f), 20.0f,
                                                    (float)mSampleRate * 0.45f);
               // ...then linear FM is applied and clamped separately, since
               // it can legitimately drive the instantaneous frequency
               // negative (through zero) rather than just toward silence.
               const float freq = std::clamp(carrierFreq * fmLinRatio, -(float)mSampleRate * 0.45f,
                                             (float)mSampleRate * 0.45f);
               float l = 0.0f, r = 0.0f;
               RenderEngine(eng[e], sm.eng[e], freq, carrierFreq, mFreeEngine[e], eng[1 - e].on ? prevOut[1 - e] : 0.0f,
                            eng[1 - e].on, 0.0f, fmPhaseOffset, l, r);
               const float g = (e == 0 ? mixA : mixB) * sm.eng[e].volume;
               outL += l * g;
               outR += r * g;
            }
            activeCount = 1;
         }
         else
         {
            int active = 0;
            for (int vi = 0; vi < kMaxVoices; vi++)
            {
               Voice& v = mVoices[vi];
               if (!v.active)
                  continue;

               // Per-voice modulation (audio/VoiceMod.h): this voice's own
               // copy of the smoothed params, with its routes written over it.
               SmoothedBlock vsm;
               float vMixA = mixA, vMixB = mixB, vFmPhase = fmPhaseOffset, vFmLin = fmLinRatio;
               if (perVoice)
               {
                  vsm = sm;
                  mVoiceMod.Apply(vi, [&vsm](int id, float x) { SetVoiceParam(vsm, id, x); });
                  vMixA = cosf(vsm.mix * (float)M_PI * 0.5f);
                  vMixB = sinf(vsm.mix * (float)M_PI * 0.5f);
                  vFmPhase = (fmMode == 0) ? (extFm * vsm.fmDepth) : 0.0f;
                  vFmLin = (fmMode == 1) ? (1.0f + extFm * vsm.fmDepth * kFmLinScale) : 1.0f;
               }
               const SmoothedBlock& vs = perVoice ? vsm : sm;

               if (!mFast || !(vs.glide == v.glideSec))
               {
                  v.glideSec = vs.glide;
                  v.glide.SetTimeConstant(vs.glide, mSampleRate);
               }
               const float currentPitch = v.glide.Process((float)v.note);
               const float base = v.baseHz.Get(mFast, currentPitch, NoteToHz);
               // Velocity always shapes level. It used to be a 0..1 "amount"
               // param, which is a control nobody reaches for: a synth that
               // ignores how hard you played is broken, and one that responds
               // to it needs no permission. Squared, because perceived
               // loudness tracks roughly the square of MIDI velocity.
               const float velGain = v.velocity * v.velocity;

               // Both engines read the *previous* sample of the other, latched
               // before either renders - see SynthModes.h on why the cycle is
               // broken this way rather than by ordering the engines.
               const float prevOut[kEngines] = { v.eng[0].lastOut, v.eng[1].lastOut };

               bool stillActive = false;
               for (int e = 0; e < kEngines; e++)
               {
                  // All three envelopes advance whether or not the engine
                  // sounds, so switching an engine on mid-note doesn't
                  // resurrect a stale envelope stage from whenever it was last
                  // on.
                  const float ampEnv = v.amp[e].Process();
                  const float pitchEnv = v.pitch[e].Process();
                  const float filtEnv = v.filt[e].Process();
                  if (v.amp[e].IsActive())
                     stillActive = true;
                  if (!eng[e].on || ampEnv <= 0.0f)
                  {
                     v.eng[e].lastOut = 0.0f;
                     continue;
                  }

                  const float semitones = (float)(eng[e].octave * 12 + eng[e].semi) * 100.0f +
                                          vs.eng[e].fine + eng[e].pitchAmount * pitchEnv * 100.0f +
                                          vs.pitchBend * 100.0f + v.bend * 100.0f + vs.clipPitch * 100.0f;
                  // Carrier pitch (pre-FM) clamped to its own sane range
                  // first, then linear FM applied and clamped separately -
                  // see the free-running path's identical comment above.
                  const float carrierFreq =
                     std::clamp(base * v.eng[e].carrier.Get(mFast, semitones, [](float c) { return powf(2.0f, c / 1200.0f); }),
                                20.0f, (float)mSampleRate * 0.45f);
                  const float freq = std::clamp(carrierFreq * vFmLin, -(float)mSampleRate * 0.45f,
                                                (float)mSampleRate * 0.45f);
                  float l = 0.0f, r = 0.0f;
                  RenderEngine(eng[e], vs.eng[e], freq, carrierFreq, v.eng[e], eng[1 - e].on ? prevOut[1 - e] : 0.0f,
                               eng[1 - e].on, filtEnv, vFmPhase, l, r);
                  const float g = (e == 0 ? vMixA : vMixB) * vs.eng[e].volume * ampEnv * velGain;
                  outL += l * g;
                  outR += r * g;
               }

               if (!stillActive)
               {
                  v.active = false;
                  v.note = -1;
               }
               else
               {
                  active++;
               }
            }
            activeCount = active;
            // Polyphony shouldn't make the patch louder as notes pile up, but
            // it shouldn't duck an existing note either - sqrt is the usual
            // compromise between "sums to clipping" and "held chord pumps".
            //
            // Ramped, not applied as a step: `active` is an integer recounted
            // every sample, so a note-on that overlaps a sounding voice (a
            // chord, or just the previous note's release tail) used to snap
            // this gain 1.0 -> 0.7071 within one sample and click. The new
            // note's attack can't mask that, because the discontinuity lands
            // on the voice that was already sounding. Steady state is
            // unchanged - the smoother settles on the same 1/sqrt(N).
            const float normTarget = active > 1 ? 1.0f / sqrtf((float)active) : 1.0f;
            const float norm = mPolyNormSmooth.Process(normTarget);
            outL *= norm;
            outR *= norm;
         }

         outL *= sm.volume;
         outR *= sm.volume;

         if (buffer.numChannels >= 2)
         {
            buffer.channels[0][i] = outL;
            buffer.channels[1][i] = outR;
            // Zeroed rather than duplicating L into every extra channel: a
            // >2-channel device path should get silence on channels this
            // node has no opinion about, not a phantom copy of the stereo
            // mix nobody asked it to route there.
            for (int ch = 2; ch < buffer.numChannels; ch++)
               buffer.channels[ch][i] = 0.0f;
         }
         else if (buffer.numChannels == 1)
         {
            buffer.channels[0][i] = (outL + outR) * 0.5f;
         }

         if ((i & 3) == 0)
         {
            const float s = (outL + outR) * 0.5f;
            mScopeRing.Write(&s, 1);
         }
      }

      mActiveVoices.store(activeCount, std::memory_order_relaxed);
   }

   // Main thread only.
   void PushParams(const WavetableSynthParams& n)
   {
      using namespace WavetableSynthCore;
      float values[kNumGlobalParams + kEngines * kNumEngineParams];
      values[kFrequency] = n.frequency;
      values[kVolume] = n.volume;
      values[kMix] = n.mix;
      values[kGlide] = n.glide;
      values[kPitchBend] = n.pitchBend;
      values[kFmDepth] = n.fmDepth;

      mFmMode.store(n.fmMode, std::memory_order_relaxed);

      for (int e = 0; e < kEngines; e++)
      {
         const WavetableEngine& s = n.engines[e];
         values[EngineParamId(e, kEngPosition)] = s.position;
         values[EngineParamId(e, kEngVolume)] = s.volume;
         values[EngineParamId(e, kEngPan)] = s.pan;
         values[EngineParamId(e, kEngDetune)] = s.detune;
         values[EngineParamId(e, kEngStereoWidth)] = s.stereoWidth;
         values[EngineParamId(e, kEngFine)] = s.fine;
         values[EngineParamId(e, kEngPhase)] = s.phase;
         values[EngineParamId(e, kEngPhaseRand)] = s.phaseRandomize;
         values[EngineParamId(e, kEngWarpAmount)] = s.warpAmount;
         values[EngineParamId(e, kEngWarpRatio)] = s.warpRatio;
         values[EngineParamId(e, kEngCutoff)] = s.cutoff;
         values[EngineParamId(e, kEngResonance)] = s.resonance;

         mEngOn[e].store(s.on ? 1 : 0, std::memory_order_relaxed);
         mEngTable[e].store(s.table, std::memory_order_relaxed);
         mEngUnison[e].store(s.unison, std::memory_order_relaxed);
         mEngOctave[e].store(s.octave, std::memory_order_relaxed);
         mEngSemi[e].store(s.semi, std::memory_order_relaxed);
         mEngWarpMode[e].store(s.warpMode, std::memory_order_relaxed);
         mEngFilterType[e].store(s.filterType, std::memory_order_relaxed);
         mEngFilterAmount[e].store(s.filterAmount, std::memory_order_relaxed);
         mEngPitchAmount[e].store(s.pitchAmount, std::memory_order_relaxed);
         mEngAmpAdsr[e][0].store(s.ampAttack, std::memory_order_relaxed);
         mEngAmpAdsr[e][1].store(s.ampDecay, std::memory_order_relaxed);
         mEngAmpAdsr[e][2].store(s.ampSustain, std::memory_order_relaxed);
         mEngAmpAdsr[e][3].store(s.ampRelease, std::memory_order_relaxed);
         mEngPitchAdsr[e][0].store(s.pitchAttack, std::memory_order_relaxed);
         mEngPitchAdsr[e][1].store(s.pitchDecay, std::memory_order_relaxed);
         mEngPitchAdsr[e][2].store(s.pitchSustain, std::memory_order_relaxed);
         mEngPitchAdsr[e][3].store(s.pitchRelease, std::memory_order_relaxed);
         mEngFilterAdsr[e][0].store(s.filterAttack, std::memory_order_relaxed);
         mEngFilterAdsr[e][1].store(s.filterDecay, std::memory_order_relaxed);
         mEngFilterAdsr[e][2].store(s.filterSustain, std::memory_order_relaxed);
         mEngFilterAdsr[e][3].store(s.filterRelease, std::memory_order_relaxed);
      }

      for (int i = 0; i < kNumGlobalParams + kEngines * kNumEngineParams; i++)
      {
         mFloatAtomics[i].store(values[i], std::memory_order_relaxed);
         mMailbox.Push(i, values[i]);
      }
   }

   // Arrangement Timeline per-clip pitch: pushed to the dedicated
   // kClipPitchParam slot (see its own comment above) rather than kPitchBend,
   // so this never fights the node's real pitch-bend-wheel value or gets
   // stomped back to 0 by PushParams's per-UI-frame push. ParamMailbox::Push
   // is documented main-thread-only, but this callsite is audio-thread, same
   // as AudioNode::RequestRetrigger()'s established dual-caller contract - a
   // plain atomic store, at worst racing itself for one block.
   void SetClipPitchOverride(float semitones) override
   {
      mMailbox.Push(WavetableSynthCore::kClipPitchParam, semitones);
   }

   MeterRing& ScopeRing() { return mScopeRing; }
   int ActiveVoices() const { return mActiveVoices.load(std::memory_order_relaxed); }

private:
   std::atomic<int> mFmMode{0};

   struct EngineBlock
   {
      bool on;
      int table, unison, octave, semi;
      int warpMode;
      int filterStages, filterShape;
      bool filterIsComb, filterCombNegative;
      float filterAmount;
      float pitchAmount;
      float ampA, ampD, ampS, ampR;
      float pitA, pitD, pitS, pitR;
      float fltA, fltD, fltS, fltR;
   };

   struct SmoothedEngine
   {
      float position, volume, pan, detune, stereoWidth, fine, phase, phaseRand;
      float warpAmount, warpRatio, cutoff, resonance;
   };

   struct SmoothedBlock
   {
      float frequency, volume, mix, glide, pitchBend, fmDepth, clipPitch;
      SmoothedEngine eng[WavetableSynthCore::kEngines];
   };

   // A per-voice route's value onto one voice's copy, clamped as the shared
   // read is. Only the params VoiceParamFor hands out arrive here.
   static void SetVoiceParam(SmoothedBlock& b, int id, float x)
   {
      using namespace WavetableSynthCore;
      if (id == kMix)
         b.mix = std::clamp(x, 0.0f, 1.0f);
      else if (id == kFmDepth)
         b.fmDepth = x;
      if (id < kNumGlobalParams || id >= kNumGlobalParams + kEngines * kNumEngineParams)
         return;
      SmoothedEngine& se = b.eng[(id - kNumGlobalParams) / kNumEngineParams];
      switch ((id - kNumGlobalParams) % kNumEngineParams)
      {
         case kEngPosition: se.position = std::clamp(x, 0.0f, 1.0f); break;
         case kEngVolume: se.volume = x; break;
         case kEngPan: se.pan = x; break;
         case kEngDetune: se.detune = x; break;
         case kEngStereoWidth: se.stereoWidth = x; break;
         case kEngFine: se.fine = x; break;
         case kEngWarpAmount: se.warpAmount = std::clamp(x, 0.0f, 1.0f); break;
         case kEngWarpRatio: se.warpRatio = x; break;
         case kEngCutoff: se.cutoff = x; break;
         case kEngResonance: se.resonance = std::clamp(x, 0.0f, 1.0f); break;
         default: break;
      }
   }

   // Everything one engine of one voice carries between samples. Free-running
   // mode uses the same struct, so the two paths cannot drift apart on what a
   // warp or a filter is allowed to remember.
   struct EngineState
   {
      double phase[WavetableSynthCore::kMaxUnison] = {};
      // Band-limits the warps' discontinuities, one per unison voice; every
      // voice output passes through it (BlepOsc::kLatency samples late).
      BlepOsc::Corrector blep[WavetableSynthCore::kMaxUnison];
      float prevRaw[WavetableSynthCore::kMaxUnison] = {}; // pre-rectify sample, for its zero-crossing kinks
      double opPhase = 0.0; // internal cross-mod operator, used when the other engine is off
      float lastOut = 0.0f; // previous sample, read by the other engine's cross-mod
      DspMath::TptSvf filter[2][WavetableSynthCore::kMaxStages];
      DspMath::CombFilter comb[2];

      // Input-keyed caches (WavetableSynthCore::Memo); never stale, so Reset
      // leaves them alone.
      WavetableSynthCore::Memo carrier;                          // 2^(cents/1200)
      WavetableSynthCore::Memo detune[WavetableSynthCore::kMaxUnison]; // 2^(spread/1200)
      WavetableSynthCore::Memo filterOctaves;                    // 2^(amount * env)
      struct PanMemo
      {
         float pan = NAN, l = 0.0f, r = 0.0f;
      } pan[WavetableSynthCore::kMaxUnison];
      struct SvfMemo
      {
         float hz = NAN, q = NAN, g = 0.0f, k = 0.0f;
      } svf;
      struct MipMemo
      {
         double inc = NAN;
         int level = 0;
      } mip[WavetableSynthCore::kMaxUnison];
      struct FrameMemo
      {
         int table = -1, frame = -1, mip = -1;
         const float* lo = nullptr;
         const float* hi = nullptr;
      } frames[WavetableSynthCore::kMaxUnison];

      void Reset(double sampleRate)
      {
         for (int u = 0; u < WavetableSynthCore::kMaxUnison; u++)
         {
            phase[u] = 0.0;
            blep[u].Reset();
            prevRaw[u] = 0.0f;
         }
         opPhase = 0.0;
         lastOut = 0.0f;
         svf = SvfMemo {}; // its result depends on the sample rate
         for (int ch = 0; ch < 2; ch++)
         {
            for (int s = 0; s < WavetableSynthCore::kMaxStages; s++)
            {
               filter[ch][s].SetSampleRate(sampleRate);
               filter[ch][s].Reset();
            }
            comb[ch].Reset();
         }
      }
   };

   struct Voice
   {
      bool active = false;
      bool held = false;
      int note = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      float bend = 0.0f; // live bendSemitones from the note chain (Pitch Bend), updated in place on bendUpdate
      uint64_t age = 0;
      Envelope amp[WavetableSynthCore::kEngines];
      Envelope pitch[WavetableSynthCore::kEngines];
      Envelope filt[WavetableSynthCore::kEngines];
      EngineState eng[WavetableSynthCore::kEngines];
      DspMath::OnePole glide;
      WavetableSynthCore::Memo baseHz; // NoteToHz of the glided pitch
      float glideSec = NAN;            // time constant `glide.coeff` was last set for

      void Reset(double sampleRate)
      {
         active = false;
         held = false;
         note = -1;
         voiceId = 0;
         velocity = 0.0f;
         bend = 0.0f;
         glideSec = NAN; // the coefficient depends on the sample rate
         for (int e = 0; e < WavetableSynthCore::kEngines; e++)
         {
            amp[e].SetSampleRate(sampleRate);
            pitch[e].SetSampleRate(sampleRate);
            filt[e].SetSampleRate(sampleRate);
            eng[e].Reset(sampleRate);
         }
      }
   };

   // One unison stack of one engine, for one voice, for one sample. `st` is
   // the caller's persistent state - this advances its phases and publishes
   // its `lastOut` for the other engine to read next sample.
   // `freq` is the (possibly through-zero, FM-modulated) playback frequency;
   // `carrierFreq` is the same voice's pre-FM pitch, always positive - the
   // cross-mod/sync operator below tracks carrierFreq so a hard-synced
   // oscillator's sync rate doesn't itself warp with FM depth.
   void RenderEngine(const EngineBlock& eb, const SmoothedEngine& se, float freq, float carrierFreq,
                     EngineState& st, float otherOut, bool otherOn, float filtEnv,
                     float fmPhaseOffset, float& outL, float& outR)
   {
      using namespace WavetableSynthCore;
      outL = 0.0f;
      outR = 0.0f;
      if (freq == 0.0f)
      {
         st.lastOut = 0.0f;
         return;
      }

      // The modulator the four cross-modulated warps read: the other engine
      // when it is running, an internal sine operator at freq * ratio when it
      // is not. The operator's phase advances unconditionally so switching the
      // other engine off mid-note doesn't restart it from zero with a click.
      const bool crossMod = SynthModes::WarpIsCrossModulated(eb.warpMode);
      float mod = 0.0f;
      {
         const double opInc = (double)(carrierFreq * std::max(0.01f, se.warpRatio)) / mSampleRate;
         // Only read when a cross-mod warp has no other engine to read.
         const bool needOp = !mFast || (crossMod && !otherOn);
         const float opOut = needOp ? sinf(2.0f * (float)M_PI * (float)st.opPhase) : 0.0f;
         st.opPhase += opInc;
         st.opPhase -= floor(st.opPhase);
         if (crossMod)
            mod = otherOn ? otherOut : opOut;
      }

      const float framePos = se.position * (float)(Wavetable::kFrames - 1);
      const int frameLo = std::clamp((int)framePos, 0, Wavetable::kFrames - 1);
      const int frameHi = std::min(frameLo + 1, Wavetable::kFrames - 1);
      const float frameFrac = framePos - (float)frameLo;

      const bool needHalf = WarpNeedsHalfCycle(eb.warpMode) && se.warpAmount > 0.0f;
      const bool phaseWarp = WarpIsPhaseDomain(eb.warpMode) && se.warpAmount > 0.0f;
      double breaks[72];
      const int numBreaks = WarpBreakpoints(eb.warpMode, se.warpAmount, se.warpRatio, breaks, 72);

      float sumL = 0.0f, sumR = 0.0f, sumMono = 0.0f;
      for (int u = 0; u < eb.unison; u++)
      {
         // `detune` is the total spread across the stack, so the outermost
         // voices sit at +/- detune/2 - a "12 cent" setting is 12 cents wide,
         // not 24, which is how every unison control is labelled in practice.
         const float spread = (eb.unison > 1)
            ? se.detune * 0.5f * (2.0f * (float)u / (float)(eb.unison - 1) - 1.0f)
            : 0.0f;
         const float voiceFreq = freq * st.detune[u].Get(mFast, spread, [](float c) { return powf(2.0f, c / 1200.0f); });
         const double inc = (double)voiceFreq / mSampleRate;

         const double randOffset = (double)se.phaseRand * (double)kVoicePhaseSeed[u];
         const double basePhase = st.phase[u] + (double)se.phase + randOffset + (double)fmPhaseOffset;
         const double readPhase = WarpReadPhase(eb.warpMode, se.warpAmount, se.warpRatio, basePhase, mod);

         // Mip: from the voice's pitch (abs(): through-zero FM can make inc
         // negative; aliasing risk is about magnitude) - exactly as before
         // for plain playback - or, under a phase warp, from the read's own
         // speed, crossfaded continuously one level on the safe side.
         EngineState::MipMemo& mm = st.mip[u];
         const double absInc = std::abs(inc);
         if (!mFast || !(absInc == mm.inc))
         {
            mm.inc = absInc;
            mm.level = Wavetable::MipForPhaseInc(absInc);
         }
         int mipLo = mm.level;
         int mipHi = mipLo;
         float mipFrac = 0.0f;
         if (phaseWarp)
         {
            const double speed = WarpReadSpeed(eb.warpMode, se.warpAmount, se.warpRatio, basePhase, mod, inc);
            const float pos = std::clamp(
               1.0f + (float)std::log2(std::max(1.0e-9, 2.0 * Wavetable::kMaxHarmonic * speed)), 0.0f,
               (float)(Wavetable::kMipLevels - 1));
            mipLo = (int)pos;
            mipHi = std::min(mipLo + 1, Wavetable::kMipLevels - 1);
            mipFrac = pos - (float)mipLo;
         }
         // The bank never moves once built, so a frame pointer is a pure
         // function of (table, frame, mip).
         EngineState::FrameMemo& fm = st.frames[u];
         if (!mFast || fm.table != eb.table || fm.frame != frameLo || fm.mip != mipLo)
         {
            fm.table = eb.table;
            fm.frame = frameLo;
            fm.mip = mipLo;
            fm.lo = Wavetable::Frame(eb.table, frameLo, mipLo);
            fm.hi = Wavetable::Frame(eb.table, frameHi, mipLo);
         }
         const float* lo0 = fm.lo;
         const float* hi0 = fm.hi;
         const bool oneMip = mFast && mipHi == mipLo;
         const float* lo1 = oneMip ? lo0 : Wavetable::Frame(eb.table, frameLo, mipHi);
         const float* hi1 = oneMip ? hi0 : Wavetable::Frame(eb.table, frameHi, mipHi);
         auto read = [&](double rp) {
            const float a = Wavetable::Sample(lo0, hi0, frameFrac, rp);
            return mipFrac > 0.0f ? a + (Wavetable::Sample(lo1, hi1, frameFrac, rp) - a) * mipFrac : a;
         };
         // The full warped value at base phase p (same mip, same modulator).
         auto valueAt = [&](double p) {
            const double rp = WarpReadPhase(eb.warpMode, se.warpAmount, se.warpRatio, p, mod);
            const float h = needHalf ? read(rp + 0.5) : 0.0f;
            return (double)WarpSample(eb.warpMode, se.warpAmount, read(rp), h, mod);
         };

         const float raw = read(readPhase);
         const float half = needHalf ? read(readPhase + 0.5) : 0.0f;
         float s = WarpSample(eb.warpMode, se.warpAmount, raw, half, mod);

         // Discontinuities the warp put between the previous sample and this
         // one, band-limited at their exact time: each breakpoint's value jump
         // and slope change, measured from the warp itself (BlepOscillator.h).
         BlepOsc::Corrector& corr = st.blep[u];
         if (numBreaks > 0 && inc > 0.0)
         {
            constexpr double kD = 1.0e-5;
            const double pNow = basePhase - std::floor(basePhase);
            const double pPrev = pNow - inc; // may be negative: the wrap is in this step
            for (int b = 0; b < numBreaks; b++)
            {
               for (double bp : { breaks[b] - 1.0, breaks[b] })
               {
                  if (bp <= pPrev || bp > pNow)
                     continue;
                  const double tau = (bp - pPrev) / inc;
                  const double vMinus = valueAt(bp - kD), vPlus = valueAt(bp + kD);
                  const double sMinus = (vMinus - valueAt(bp - 2.0 * kD)) / kD;
                  const double sPlus = (valueAt(bp + 2.0 * kD) - vPlus) / kD;
                  corr.AddEvent(tau, vPlus - vMinus, (sPlus - sMinus) * inc);
               }
            }
         }
         if (eb.warpMode == SynthModes::kWarpRectify && se.warpAmount > 0.0f)
         {
            // |s| folds at each zero crossing of the raw read: a slope change
            // of twice the slope there, scaled by the blend.
            const float prev = st.prevRaw[u];
            if ((prev < 0.0f) != (raw < 0.0f) && prev != raw)
            {
               const double tau = (double)prev / ((double)prev - (double)raw);
               const double slope = (double)raw - (double)prev;
               corr.AddEvent(std::clamp(tau, 1.0e-6, 1.0), 0.0, 2.0 * std::fabs(slope) * se.warpAmount);
            }
         }
         st.prevRaw[u] = raw;
         const float naive = s;
         s = corr.Push(s);

         st.phase[u] += inc;
         st.phase[u] -= floor(st.phase[u]);

         const float spreadPan = (eb.unison > 1)
            ? se.stereoWidth * (2.0f * (float)u / (float)(eb.unison - 1) - 1.0f)
            : 0.0f;
         EngineState::PanMemo& pm = st.pan[u];
         const float panAt = std::clamp(se.pan + spreadPan, -1.0f, 1.0f);
         if (!mFast || !(panAt == pm.pan))
         {
            pm.pan = panAt;
            DspMath::EqualPowerPan(panAt, pm.l, pm.r);
         }
         const float lg = pm.l, rg = pm.r;
         sumL += s * lg;
         sumR += s * rg;
         sumMono += naive; // the other engine's cross-mod reads this sample now, uncorrected
      }

      const float norm = 1.0f / sqrtf((float)eb.unison); // more voices != louder
      sumL *= norm;
      sumR *= norm;

      // Published before the filter, not after: a cross-modulated pair whose
      // modulator is being lowpassed off loses its warp entirely as the filter
      // closes, which reads as the warp knob having stopped working.
      st.lastOut = std::clamp(sumMono * norm, -4.0f, 4.0f);

      if (eb.filterIsComb)
      {
         // Cutoff in octaves off the knob, matching the SVF path below, so
         // switching between comb and LP/HP/etc mid-patch doesn't jump the
         // envelope's perceived depth.
         const float hz = se.cutoff * st.filterOctaves.Get(mFast, eb.filterAmount * filtEnv, [](float o) { return exp2f(o); });
         float* chans[2] = { &sumL, &sumR };
         for (int ch = 0; ch < 2; ch++)
         {
            st.comb[ch].SetParams(hz, DspMath::CombFeedbackFromResonance(se.resonance),
                                  eb.filterCombNegative, mSampleRate);
            *chans[ch] = st.comb[ch].Process(*chans[ch]);
         }
      }
      else if (eb.filterStages > 0)
      {
         // Cutoff in octaves off the knob, so the envelope moves the same
         // musical distance wherever the knob is parked.
         const float hz = se.cutoff * st.filterOctaves.Get(mFast, eb.filterAmount * filtEnv, [](float o) { return exp2f(o); });
         // Resonance stops just short of self-oscillation: Q beyond ~10 on a
         // cascade of three stages rings loudly enough to swamp the signal it
         // is filtering, and there is no drive stage here to tame it.
         const float q = 0.707f + se.resonance * se.resonance * 9.3f;
         EngineState::SvfMemo& cm = st.svf;
         if (!mFast || !(hz == cm.hz) || !(q == cm.q))
         {
            cm.hz = hz;
            cm.q = q;
            SvfCoeffs(hz, q, (float)mSampleRate, cm.g, cm.k);
         }
         const float g = cm.g, k = cm.k;

         float* chans[2] = { &sumL, &sumR };
         for (int ch = 0; ch < 2; ch++)
         {
            float x = *chans[ch];
            for (int stage = 0; stage < eb.filterStages; stage++)
            {
               DspMath::TptSvf& f = st.filter[ch][stage];
               f.g = g;
               f.k = k;
               const DspMath::TptSvf::Outputs o = f.Process(x);
               switch (eb.filterShape)
               {
                  case SynthModes::kShapeHigh:  x = o.high; break;
                  case SynthModes::kShapeBand:  x = o.band; break;
                  case SynthModes::kShapeNotch: x = o.notch; break;
                  default:                      x = o.low; break;
               }
            }
            *chans[ch] = x;
         }
      }

      outL = sumL;
      outR = sumR;
   }

   void NoteOn(int note, float velocity, int voiceId, float bendSemitones, const EngineBlock eng[WavetableSynthCore::kEngines], float pressure = 0.0f, float slide = 0.0f)
   {
      using namespace WavetableSynthCore;
      // Retrigger an existing voice on the same note before allocating a new
      // one: without this, a fast repeated note stacks two voices and doubles
      // in level.
      int slot = -1;
      for (int i = 0; i < kMaxVoices; i++)
      {
         if (mVoices[i].active && mVoices[i].note == note)
         {
            slot = i;
            break;
         }
      }
      if (slot < 0)
      {
         for (int i = 0; i < kMaxVoices; i++)
         {
            if (!mVoices[i].active)
            {
               slot = i;
               break;
            }
         }
      }
      if (slot < 0) // all busy: steal the oldest
      {
         uint64_t oldest = UINT64_MAX;
         slot = 0;
         for (int i = 0; i < kMaxVoices; i++)
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
      // Stolen: this slot was sounding a *different* note, so the phase and
      // filter reset below happens under an amp envelope that is still at
      // whatever level the old note had reached. Zeroing it first is what
      // makes the new attack start from real silence instead of splicing a
      // fresh waveform in at the old note's amplitude.
      const bool stolen = v.active && v.note != note;
      const bool wasInactive = !v.active;
      v.active = true;
      v.held = true;
      v.note = note;
      v.voiceId = voiceId;
      v.velocity = velocity;
      v.bend = bendSemitones;
      v.age = mNextAge++;
      const float targetHz = NoteToHz((float)note);
      if (fresh)
      {
         // A new note starts its phase where the engine's `phase` param says,
         // not wherever the last note happened to leave off - otherwise the
         // attack transient is different every time. The filter state is reset
         // with it: a stolen voice carrying the previous note's resonant
         // ringing into a new attack is an audible thump.
         for (int e = 0; e < kEngines; e++)
            v.eng[e].Reset(mSampleRate);
         const float glide = mFloatAtomics[kGlide].load(std::memory_order_relaxed);
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
      for (int e = 0; e < kEngines; e++)
      {
         v.amp[e].SetSampleRate(mSampleRate);
         v.pitch[e].SetSampleRate(mSampleRate);
         v.filt[e].SetSampleRate(mSampleRate);
         v.amp[e].SetADSR(eng[e].ampA, eng[e].ampD, eng[e].ampS, eng[e].ampR);
         v.pitch[e].SetADSR(eng[e].pitA, eng[e].pitD, eng[e].pitS, eng[e].pitR);
         v.filt[e].SetADSR(eng[e].fltA, eng[e].fltD, eng[e].fltS, eng[e].fltR);
         if (stolen)
            v.amp[e].ResetLevel();
         v.amp[e].NoteOn();
         v.pitch[e].NoteOn();
         v.filt[e].NoteOn();
      }
      mVoiceMod.NoteOn(slot, pressure, slide);
   }

   void NoteOff(int voiceId)
   {
      for (int i = 0; i < WavetableSynthCore::kMaxVoices; i++)
      {
         Voice& v = mVoices[i];
         if (!v.active || v.voiceId != voiceId || !v.held)
            continue;
         v.held = false;
         for (int e = 0; e < WavetableSynthCore::kEngines; e++)
         {
            v.amp[e].NoteOff();
            v.pitch[e].NoteOff();
            v.filt[e].NoteOff();
         }
         mVoiceMod.NoteOff(i);
      }
   }

   // Slides a voice already sounding, without retriggering it - this is the
   // whole point of Pitch Bend being a note-chain node now (see NoteEvent.h's
   // bendUpdate field): a real note-on would restart envelopes and the
   // wavetable read phase, both audible artifacts a bend wheel must not
   // cause.
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

   double mSampleRate = 44100.0;
   bool mFast = true; // input-keyed caches on (off: the 2.16 reference path)
   ParamMailbox mMailbox;
   MeterRing mScopeRing;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;
   VoiceMod::PlanBox mVoicePlan;
   VoiceMod::Runtime<WavetableSynthCore::kMaxVoices> mVoiceMod;

   Voice mVoices[WavetableSynthCore::kMaxVoices];
   uint64_t mNextAge = 1;
   float mLastNotePitch = 60.0f;
   bool mHasLastNote = false;

   // Free-running state (no note cable) - see ProcessBlock.
   DspMath::OnePole mFreeGlide;
   EngineState mFreeEngine[WavetableSynthCore::kEngines];

   // Polyphony normalisation gain, smoothed across blocks - see ProcessBlock's
   // note-driven branch. Persistent because the voice count changes mid-block
   // and the ramp has to survive the block boundary.
   DspMath::OnePole mPolyNormSmooth;

   std::atomic<int> mActiveVoices { 0 };
   std::atomic<int> mEngOn[WavetableSynthCore::kEngines] { { 1 }, { 0 } };
   std::atomic<int> mEngTable[WavetableSynthCore::kEngines] {};
   std::atomic<int> mEngUnison[WavetableSynthCore::kEngines] { { 1 }, { 1 } };
   std::atomic<int> mEngOctave[WavetableSynthCore::kEngines] {};
   std::atomic<int> mEngSemi[WavetableSynthCore::kEngines] {};
   std::atomic<int> mEngWarpMode[WavetableSynthCore::kEngines] {};
   std::atomic<int> mEngFilterType[WavetableSynthCore::kEngines] {};
   std::atomic<float> mEngFilterAmount[WavetableSynthCore::kEngines] {};
   std::atomic<float> mEngPitchAmount[WavetableSynthCore::kEngines] {};
   std::atomic<float> mEngAmpAdsr[WavetableSynthCore::kEngines][4] {};
   std::atomic<float> mEngPitchAdsr[WavetableSynthCore::kEngines][4] {};
   std::atomic<float> mEngFilterAdsr[WavetableSynthCore::kEngines][4] {};
   std::atomic<float> mFloatAtomics[WavetableSynthCore::kNumGlobalParams + WavetableSynthCore::kEngines * WavetableSynthCore::kNumEngineParams] {};
};
