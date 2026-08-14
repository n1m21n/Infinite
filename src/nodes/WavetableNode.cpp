#include "WavetableNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/SynthModes.h"
#include "audio/Wavetable.h"

namespace
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

   int EngineParamId(int engine, int param)
   {
      return kNumGlobalParams + engine * kNumEngineParams + param;
   }

   static_assert(kNumGlobalParams + kEngines * kNumEngineParams <= ParamMailbox::kMaxParams,
                 "Wavetable's smoothed params no longer fit one ParamMailbox");

   // Golden-ratio phase seeds: deterministic per-unison-voice start offsets,
   // so `phaseRandomize` spreads the stack without needing PRNG state.
   constexpr float kVoicePhaseSeed[kMaxUnison] = {
      0.000000f, 0.618034f, 0.236068f, 0.854102f,
      0.472136f, 0.090170f, 0.708204f, 0.326238f
   };

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
         {
            // Hard sync: the read phase runs at `ratio` times the voice's own
            // and wraps early, so the cycle restarts mid-waveform. `amount`
            // blends the ratio in from 1, so the knob sweeps sync in from
            // nothing rather than jumping to the full interval.
            const double r = 1.0 + (double)amount * ((double)ratio - 1.0);
            const double sp = p * (r > 0.01 ? r : 0.01);
            return sp - floor(sp);
         }

         case SynthModes::kWarpBendPlus:  return BendPhase(p, (double)amount);
         case SynthModes::kWarpBendMinus: return BendPhase(p, -(double)amount);

         case SynthModes::kWarpAsymPlus:
            // Power-law skew. exp(+/-1.6) at full depth is roughly a 5:1
            // stretch of one half of the cycle against the other - past that
            // the read spends so long on one sample that the result is a pulse
            // rather than a warped table.
            return pow(p, exp(-1.6 * (double)amount));
         case SynthModes::kWarpAsymMinus:
            return pow(p, exp(1.6 * (double)amount));

         case SynthModes::kWarpMirror:
         {
            // Fold the cycle back on itself: the second half replays the first
            // in reverse, which halves the period of the fundamental and
            // leaves a waveform symmetric about its midpoint.
            const double folded = 1.0 - fabs(2.0 * p - 1.0);
            return p + (folded - p) * (double)amount;
         }

         case SynthModes::kWarpQuantize:
         {
            // Step the phase, so the cycle reads as a staircase - the coarser
            // the steps, the more high harmonics the edges add. 64 steps at
            // the bottom of the range down to 2 at the top, blended by the
            // same amount so the effect fades in continuously.
            const double inv = 1.0 - (double)amount;
            const int steps = 2 + (int)(inv * inv * 62.0);
            const double q = floor(p * (double)steps) / (double)steps;
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
}

// ------------------------------------------------------------- audio thread
class AudioWavetableNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      for (int i = 0; i < kNumGlobalParams + kEngines * kNumEngineParams; i++)
         mMailbox.SetImmediate(i, mFloatAtomics[i].load(std::memory_order_relaxed));

      mFreeGlide.SetImmediate(mFloatAtomics[kFrequency].load(std::memory_order_relaxed));
      for (int e = 0; e < kEngines; e++)
         mFreeEngine[e].Reset(sampleRate);
      for (Voice& v : mVoices)
         v.Reset(sampleRate);
   }

   void SetNoteInbox(NoteEventQueue* inbox) override { mNoteInbox = inbox; }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
   {
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

      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (noteDriven)
         numEvts = mNoteInbox->Pop(evts, 64);

      int activeCount = 0;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            if (evts[evtIdx].isNoteOn)
               NoteOn(evts[evtIdx].note, evts[evtIdx].velocity, eng);
            else
               NoteOff(evts[evtIdx].note);
            evtIdx++;
         }

         SmoothedBlock sm;
         sm.frequency = mMailbox.SmoothedValue(kFrequency);
         sm.volume = mMailbox.SmoothedValue(kVolume);
         sm.mix = std::clamp(mMailbox.SmoothedValue(kMix), 0.0f, 1.0f);
         sm.glide = mMailbox.SmoothedValue(kGlide);
         sm.pitchBend = mMailbox.SmoothedValue(kPitchBend);
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
                                       sm.pitchBend * 100.0f;
               const float freq = base * powf(2.0f, semitones / 1200.0f);
               float l = 0.0f, r = 0.0f;
               RenderEngine(eng[e], sm.eng[e], freq, mFreeEngine[e], eng[1 - e].on ? prevOut[1 - e] : 0.0f,
                            eng[1 - e].on, 0.0f, l, r);
               const float g = (e == 0 ? mixA : mixB) * sm.eng[e].volume;
               outL += l * g;
               outR += r * g;
            }
            activeCount = 1;
         }
         else
         {
            int active = 0;
            for (Voice& v : mVoices)
            {
               if (!v.active)
                  continue;

               v.glide.SetTimeConstant(sm.glide, mSampleRate);
               const float targetHz = NoteToHz((float)v.note);
               const float base = v.glide.Process(targetHz);
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
                                          sm.eng[e].fine + eng[e].pitchAmount * pitchEnv * 100.0f +
                                          sm.pitchBend * 100.0f;
                  const float freq = base * powf(2.0f, semitones / 1200.0f);
                  float l = 0.0f, r = 0.0f;
                  RenderEngine(eng[e], sm.eng[e], freq, v.eng[e], eng[1 - e].on ? prevOut[1 - e] : 0.0f,
                               eng[1 - e].on, filtEnv, l, r);
                  const float g = (e == 0 ? mixA : mixB) * sm.eng[e].volume * ampEnv * velGain;
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
            if (active > 1)
            {
               const float norm = 1.0f / sqrtf((float)active);
               outL *= norm;
               outR *= norm;
            }
         }

         outL *= sm.volume;
         outR *= sm.volume;

         if (buffer.numChannels >= 2)
         {
            buffer.channels[0][i] = outL;
            buffer.channels[1][i] = outR;
            for (int ch = 2; ch < buffer.numChannels; ch++)
               buffer.channels[ch][i] = outL;
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
   void PushParams(const WavetableNode& n)
   {
      float values[kNumGlobalParams + kEngines * kNumEngineParams];
      values[kFrequency] = n.frequency;
      values[kVolume] = n.volume;
      values[kMix] = n.mix;
      values[kGlide] = n.glide;
      values[kPitchBend] = n.pitchBend;

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

   MeterRing& ScopeRing() { return mScopeRing; }
   int ActiveVoices() const { return mActiveVoices.load(std::memory_order_relaxed); }

private:
   struct EngineBlock
   {
      bool on;
      int table, unison, octave, semi;
      int warpMode;
      int filterStages, filterShape;
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
      float frequency, volume, mix, glide, pitchBend;
      SmoothedEngine eng[kEngines];
   };

   // Everything one engine of one voice carries between samples. Free-running
   // mode uses the same struct, so the two paths cannot drift apart on what a
   // warp or a filter is allowed to remember.
   struct EngineState
   {
      double phase[kMaxUnison] = {};
      double opPhase = 0.0; // internal cross-mod operator, used when the other engine is off
      float lastOut = 0.0f; // previous sample, read by the other engine's cross-mod
      DspMath::TptSvf filter[2][kMaxStages];

      void Reset(double sampleRate)
      {
         for (int u = 0; u < kMaxUnison; u++)
            phase[u] = 0.0;
         opPhase = 0.0;
         lastOut = 0.0f;
         for (int ch = 0; ch < 2; ch++)
         {
            for (int s = 0; s < kMaxStages; s++)
            {
               filter[ch][s].SetSampleRate(sampleRate);
               filter[ch][s].Reset();
            }
         }
      }
   };

   struct Voice
   {
      bool active = false;
      bool held = false;
      int note = -1;
      float velocity = 0.0f;
      uint64_t age = 0;
      Envelope amp[kEngines];
      Envelope pitch[kEngines];
      Envelope filt[kEngines];
      EngineState eng[kEngines];
      DspMath::OnePole glide;

      void Reset(double sampleRate)
      {
         active = false;
         held = false;
         note = -1;
         velocity = 0.0f;
         for (int e = 0; e < kEngines; e++)
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
   void RenderEngine(const EngineBlock& eb, const SmoothedEngine& se, float freq,
                     EngineState& st, float otherOut, bool otherOn, float filtEnv,
                     float& outL, float& outR)
   {
      outL = 0.0f;
      outR = 0.0f;
      if (freq <= 0.0f)
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
         const double opInc = (double)(freq * std::max(0.01f, se.warpRatio)) / mSampleRate;
         const float opOut = sinf(2.0f * (float)M_PI * (float)st.opPhase);
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

      float sumL = 0.0f, sumR = 0.0f, sumMono = 0.0f;
      for (int u = 0; u < eb.unison; u++)
      {
         // `detune` is the total spread across the stack, so the outermost
         // voices sit at +/- detune/2 - a "12 cent" setting is 12 cents wide,
         // not 24, which is how every unison control is labelled in practice.
         const float spread = (eb.unison > 1)
            ? se.detune * 0.5f * (2.0f * (float)u / (float)(eb.unison - 1) - 1.0f)
            : 0.0f;
         const float voiceFreq = freq * powf(2.0f, spread / 1200.0f);
         const double inc = (double)voiceFreq / mSampleRate;

         const int mip = Wavetable::MipForPhaseInc(inc);
         const float* lo = Wavetable::Frame(eb.table, frameLo, mip);
         const float* hi = Wavetable::Frame(eb.table, frameHi, mip);

         const double randOffset = (double)se.phaseRand * (double)kVoicePhaseSeed[u];
         const double basePhase = st.phase[u] + (double)se.phase + randOffset;
         const double readPhase = WarpReadPhase(eb.warpMode, se.warpAmount, se.warpRatio, basePhase, mod);

         float s = Wavetable::Sample(lo, hi, frameFrac, readPhase);
         const float half = needHalf ? Wavetable::Sample(lo, hi, frameFrac, readPhase + 0.5) : 0.0f;
         s = WarpSample(eb.warpMode, se.warpAmount, s, half, mod);

         st.phase[u] += inc;
         st.phase[u] -= floor(st.phase[u]);

         const float spreadPan = (eb.unison > 1)
            ? se.stereoWidth * (2.0f * (float)u / (float)(eb.unison - 1) - 1.0f)
            : 0.0f;
         float lg, rg;
         DspMath::EqualPowerPan(std::clamp(se.pan + spreadPan, -1.0f, 1.0f), lg, rg);
         sumL += s * lg;
         sumR += s * rg;
         sumMono += s;
      }

      const float norm = 1.0f / sqrtf((float)eb.unison); // more voices != louder
      sumL *= norm;
      sumR *= norm;

      // Published before the filter, not after: a cross-modulated pair whose
      // modulator is being lowpassed off loses its warp entirely as the filter
      // closes, which reads as the warp knob having stopped working.
      st.lastOut = std::clamp(sumMono * norm, -4.0f, 4.0f);

      if (eb.filterStages > 0)
      {
         // Cutoff in octaves off the knob, so the envelope moves the same
         // musical distance wherever the knob is parked.
         const float hz = se.cutoff * exp2f(eb.filterAmount * filtEnv);
         // Resonance stops just short of self-oscillation: Q beyond ~10 on a
         // cascade of three stages rings loudly enough to swamp the signal it
         // is filtering, and there is no drive stage here to tame it.
         const float q = 0.707f + se.resonance * se.resonance * 9.3f;
         float g, k;
         SvfCoeffs(hz, q, (float)mSampleRate, g, k);

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

   void NoteOn(int note, float velocity, const EngineBlock eng[kEngines])
   {
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
      v.active = true;
      v.held = true;
      v.note = note;
      v.velocity = velocity;
      v.age = mNextAge++;
      if (fresh)
      {
         // A new note starts its phase where the engine's `phase` param says,
         // not wherever the last note happened to leave off - otherwise the
         // attack transient is different every time. The filter state is reset
         // with it: a stolen voice carrying the previous note's resonant
         // ringing into a new attack is an audible thump.
         for (int e = 0; e < kEngines; e++)
            v.eng[e].Reset(mSampleRate);
         v.glide.SetImmediate(NoteToHz((float)note));
      }
      for (int e = 0; e < kEngines; e++)
      {
         v.amp[e].SetSampleRate(mSampleRate);
         v.pitch[e].SetSampleRate(mSampleRate);
         v.filt[e].SetSampleRate(mSampleRate);
         v.amp[e].SetADSR(eng[e].ampA, eng[e].ampD, eng[e].ampS, eng[e].ampR);
         v.pitch[e].SetADSR(eng[e].pitA, eng[e].pitD, eng[e].pitS, eng[e].pitR);
         v.filt[e].SetADSR(eng[e].fltA, eng[e].fltD, eng[e].fltS, eng[e].fltR);
         v.amp[e].NoteOn();
         v.pitch[e].NoteOn();
         v.filt[e].NoteOn();
      }
   }

   void NoteOff(int note)
   {
      for (Voice& v : mVoices)
      {
         if (!v.active || v.note != note || !v.held)
            continue;
         v.held = false;
         for (int e = 0; e < kEngines; e++)
         {
            v.amp[e].NoteOff();
            v.pitch[e].NoteOff();
            v.filt[e].NoteOff();
         }
      }
   }

   double mSampleRate = 44100.0;
   ParamMailbox mMailbox;
   MeterRing mScopeRing;
   NoteEventQueue* mNoteInbox = nullptr;

   Voice mVoices[kMaxVoices];
   uint64_t mNextAge = 1;

   // Free-running state (no note cable) - see ProcessBlock.
   DspMath::OnePole mFreeGlide;
   EngineState mFreeEngine[kEngines];

   std::atomic<int> mActiveVoices { 0 };
   std::atomic<int> mEngOn[kEngines] { { 1 }, { 0 } };
   std::atomic<int> mEngTable[kEngines] {};
   std::atomic<int> mEngUnison[kEngines] { { 1 }, { 1 } };
   std::atomic<int> mEngOctave[kEngines] {};
   std::atomic<int> mEngSemi[kEngines] {};
   std::atomic<int> mEngWarpMode[kEngines] {};
   std::atomic<int> mEngFilterType[kEngines] {};
   std::atomic<float> mEngFilterAmount[kEngines] {};
   std::atomic<float> mEngPitchAmount[kEngines] {};
   std::atomic<float> mEngAmpAdsr[kEngines][4] {};
   std::atomic<float> mEngPitchAdsr[kEngines][4] {};
   std::atomic<float> mEngFilterAdsr[kEngines][4] {};
   std::atomic<float> mFloatAtomics[kNumGlobalParams + kEngines * kNumEngineParams] {};
};

// -------------------------------------------------------------- main thread
WavetableNode::WavetableNode()
{
   // Main-thread build of the shared bank, before any AudioNode of ours can
   // exist - Wavetable::Frame allocates nothing and must never see an unbuilt
   // bank from the audio thread.
   Wavetable::EnsureBuilt();

   // Engine B is off by default: a two-engine node that starts with both
   // engines summing is 6 dB hotter than the single-oscillator node it
   // replaces, and every patch would open needing the mix pulled down.
   engines[1].on = false;
   engines[1].table = 3;      // Formant - audibly different from A's Basic Shapes
   engines[1].octave = -1;
   engines[1].position = 0.4f;
}

WavetableNode::~WavetableNode() = default;

void WavetableNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWavetableNode>();
   mAudioNode->PushParams(*this);
}

AudioNode* WavetableNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWavetableNode>();
   return mAudioNode.get();
}

int WavetableNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int WavetableNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void WavetableNode::VisitParams(ParamVisitor& v)
{
   v.Float("mix", mix);
   v.Float("volume", volume);
   v.Float("frequency", frequency);
   v.Float("glide", glide);
   v.Float("pitchBend", pitchBend);

   // Per-engine names are prefixed rather than indexed generically so a saved
   // patch stays readable and a future third engine can't silently renumber
   // the existing two.
   static const char* const kPrefix[kEngines] = { "a", "b" };
   for (int e = 0; e < kEngines; e++)
   {
      WavetableEngine& s = engines[e];
      const std::string p = std::string(kPrefix[e]) + ".";
      v.Bool((p + "on").c_str(), s.on);
      v.Int((p + "table").c_str(), s.table);
      v.Float((p + "position").c_str(), s.position);
      v.Float((p + "volume").c_str(), s.volume);
      v.Float((p + "pan").c_str(), s.pan);
      v.Int((p + "unison").c_str(), s.unison);
      v.Float((p + "detune").c_str(), s.detune);
      v.Float((p + "stereoWidth").c_str(), s.stereoWidth);
      v.Int((p + "octave").c_str(), s.octave);
      v.Int((p + "semi").c_str(), s.semi);
      v.Float((p + "fine").c_str(), s.fine);
      v.Float((p + "phase").c_str(), s.phase);
      v.Float((p + "phaseRandomize").c_str(), s.phaseRandomize);
      v.Int((p + "warpMode").c_str(), s.warpMode);
      v.Float((p + "warpAmount").c_str(), s.warpAmount);
      v.Float((p + "warpRatio").c_str(), s.warpRatio);
      v.Int((p + "filterType").c_str(), s.filterType);
      v.Float((p + "cutoff").c_str(), s.cutoff);
      v.Float((p + "resonance").c_str(), s.resonance);
      v.Float((p + "ampAttack").c_str(), s.ampAttack);
      v.Float((p + "ampDecay").c_str(), s.ampDecay);
      v.Float((p + "ampSustain").c_str(), s.ampSustain);
      v.Float((p + "ampRelease").c_str(), s.ampRelease);
      v.Float((p + "pitchAmount").c_str(), s.pitchAmount);
      v.Float((p + "pitchAttack").c_str(), s.pitchAttack);
      v.Float((p + "pitchDecay").c_str(), s.pitchDecay);
      v.Float((p + "pitchSustain").c_str(), s.pitchSustain);
      v.Float((p + "pitchRelease").c_str(), s.pitchRelease);
      v.Float((p + "filterAmount").c_str(), s.filterAmount);
      v.Float((p + "filterAttack").c_str(), s.filterAttack);
      v.Float((p + "filterDecay").c_str(), s.filterDecay);
      v.Float((p + "filterSustain").c_str(), s.filterSustain);
      v.Float((p + "filterRelease").c_str(), s.filterRelease);
   }
}
