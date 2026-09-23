#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// EQ's kernel: 5 fixed bands in series, each supporting 12 filter types:
// low shelf, peak, high shelf, hp 12/24/36, lp 12/24/36, bp, notch, all-pass.
// Multi-slope (24/36 dB) filters cascade up to 3 single-stage RBJ biquads.
//
// Every type here is one surgical move at one frequency, which is what an EQ
// band is for. Comb + / Comb - were briefly in this list and are not any
// more: a comb is a bank of teeth spread across the entire spectrum, so a
// "band" of it has no frequency in the sense the other eleven types mean,
// and the band's freq handle and response curve read as claiming otherwise.
// It still exists on the Filter node, where a whole-spectrum filter belongs.
// See SanitizeType() for what happens to a saved patch that used one.
namespace EqDsp
{
   enum BandType
   {
      kLowShelf = 0, kPeak, kHighShelf,
      kHp12, kHp24, kHp36,
      kLp12, kLp24, kLp36,
      kBP, kNotch, kAllpass,
      kNumBandTypes
   };

   // Indices 12 and 13 were "comb +" and "comb -" before they were removed.
   // Saved patches still carry them, so they are folded to `notch` rather
   // than left to the generic clamp, which would have landed them on
   // `all-pass` - a band that alters phase and nothing else, i.e. silently
   // no filtering at all where the patch asked for a deep one. A notch at
   // the band's own frequency is the nearest thing the EQ still offers: it
   // is one tooth of the comb that was there.
   constexpr int kLegacyCombPos = 12;
   constexpr int kLegacyCombNeg = 13;

   // The only way a band type should be read from a saved param. Both the
   // render path and the visualizer go through it, so a legacy patch cannot
   // sound like one type and draw as another.
   inline int SanitizeType(float storedValue)
   {
      const int t = (int)(storedValue + 0.5f);
      if (t == kLegacyCombPos || t == kLegacyCombNeg)
         return kNotch;
      return std::clamp(t, 0, kNumBandTypes - 1);
   }

   inline const char* const* TypeNames()
   {
      static const char* const kNames[kNumBandTypes] = {
         "low shelf", "peak", "high shelf",
         "hp 12", "hp 24", "hp 36",
         "lp 12", "lp 24", "lp 36",
         "bp", "notch", "all-pass"
      };
      return kNames;
   }

   inline const char* TypeName(int type)
   {
      return (type >= 0 && type < kNumBandTypes) ? TypeNames()[type] : TypeNames()[kPeak];
   }

   inline const std::vector<std::string>& TypeList()
   {
      static std::vector<std::string> list;
      if (list.empty())
         for (int i = 0; i < kNumBandTypes; i++)
            list.push_back(TypeNames()[i]);
      return list;
   }

   inline int StageCount(int type)
   {
      switch (type)
      {
         case kHp24: case kLp24: return 2;
         case kHp36: case kLp36: return 3;
         default: return 1;
      }
   }

   inline bool UsesGain(int type) { return type == kLowShelf || type == kPeak || type == kHighShelf; }

   // Configures a scratch Biquad for one band type. Shared by PushParams and
   // the visualizer/tests, so the picture, the running filter and the DSP
   // fixture can never disagree about what a given band computes.
   inline void ConfigureBiquad(DspMath::Biquad& bq, int type, double freq, double q, double gainDb,
                                double sampleRate)
   {
      switch (type)
      {
         case kLowShelf: bq.SetLowShelf(freq, q, gainDb, sampleRate); break;
         case kHighShelf: bq.SetHighShelf(freq, q, gainDb, sampleRate); break;
         case kHp12: case kHp24: case kHp36: bq.SetHighpass(freq, q, sampleRate); break;
         case kLp12: case kLp24: case kLp36: bq.SetLowpass(freq, q, sampleRate); break;
         case kBP: bq.SetBandpass(freq, q, sampleRate); break;
         case kNotch: bq.SetNotch(freq, q, sampleRate); break;
         case kAllpass: bq.SetAllpass(freq, q, sampleRate); break;
         case kPeak: default: bq.SetPeaking(freq, q, gainDb, sampleRate); break;
      }
   }

   // Bypass coefficients - a true identity filter (y[n] = x[n]), pushed for
   // a disabled band instead of branching on an enable flag per sample. That
   // keeps the mailbox's smoothing crossfading the band out instead of
   // clicking it out, and makes "all bands off" an exact bit-identity bypass.
   inline void ConfigureBypass(DspMath::Biquad& bq)
   {
      bq.b0 = 1.0f; bq.b1 = 0.0f; bq.b2 = 0.0f; bq.a1 = 0.0f; bq.a2 = 0.0f;
   }

   // |H(e^-jw)| of one RBJ biquad, in dB, evaluated at evalHz. Computed
   // directly from the (already-settled, no-transient) transfer function
   // instead of rendering a settled sine through a scratch filter
   // (AudioFilterDsp::MagnitudeDb's approach) - affordable at 5 bands x 160
   // points recomputed on every frame of a handle drag, where the
   // render-a-sine approach (~8000 samples/point) would not be.
   //
   // Split in two so a curve sweep can pay for the trig once per point
   // (EvalTrigFor) and the band's coefficients once per band (BandEval),
   // instead of once per band per point - BiquadMagnitudeDb/BandMagnitudeDb
   // below are the same arithmetic, so both paths give identical floats.
   struct EvalTrig
   {
      bool valid = false; // false where BiquadMagnitudeDb returns 0
      double c1 = 0.0, s1 = 0.0, c2 = 0.0, s2 = 0.0;
   };

   inline EvalTrig EvalTrigFor(float evalHz, double sampleRate)
   {
      EvalTrig t;
      if (evalHz <= 0.0f || sampleRate <= 0.0)
         return t;
      const double w = 2.0 * M_PI * (double)evalHz / sampleRate;
      t.valid = true;
      t.c1 = cos(w);
      t.s1 = sin(w);
      t.c2 = cos(2.0 * w);
      t.s2 = sin(2.0 * w);
      return t;
   }

   inline float BiquadMagnitudeDbAt(const DspMath::Biquad& bq, const EvalTrig& t)
   {
      if (!t.valid)
         return 0.0f;
      const double nRe = bq.b0 + bq.b1 * t.c1 + bq.b2 * t.c2;
      const double nIm = -(bq.b1 * t.s1 + bq.b2 * t.s2);
      const double dRe = 1.0 + bq.a1 * t.c1 + bq.a2 * t.c2;
      const double dIm = -(bq.a1 * t.s1 + bq.a2 * t.s2);
      const double mag2 = (nRe * nRe + nIm * nIm) / std::max(1e-20, dRe * dRe + dIm * dIm);
      return (float)(10.0 * log10(std::max(1e-20, mag2)));
   }

   inline float BiquadMagnitudeDb(const DspMath::Biquad& bq, float evalHz, double sampleRate)
   {
      return BiquadMagnitudeDbAt(bq, EvalTrigFor(evalHz, sampleRate));
   }

   // One band's coefficients, configured once for a whole sweep.
   struct BandEval
   {
      DspMath::Biquad bq;
      float stages = 1.0f;
      bool enabled = false;
   };

   inline BandEval PrepareBand(int type, float freq, float q, float gainDb, bool enabled, double sampleRate)
   {
      BandEval b;
      b.enabled = enabled;
      if (!enabled)
         return b;
      ConfigureBiquad(b.bq, type, freq, q, gainDb, sampleRate);
      b.stages = (float)StageCount(type);
      return b;
   }

   inline float BandMagnitudeDbAt(const BandEval& b, const EvalTrig& t)
   {
      if (!b.enabled)
         return 0.0f;
      return BiquadMagnitudeDbAt(b.bq, t) * b.stages;
   }

   // Convenience: magnitude of one band directly from type/freq/q/gain,
   // without the caller having to build a scratch Biquad itself.
   inline float BandMagnitudeDb(int type, float freq, float q, float gainDb, bool enabled,
                                 float evalHz, double sampleRate)
   {
      return BandMagnitudeDbAt(PrepareBand(type, freq, q, gainDb, enabled, sampleRate),
                               EvalTrigFor(evalHz, sampleRate));
   }
}

class AudioEffectNode;

// AudioEffectNode's kernel for the EQ node: 5 bands in series, each cascading
// up to 3 RBJ biquads (for 12/24/36 dB/oct slopes). See IEffectKernel.h for
// the PushParams/ProcessBlock split this obeys - PushParams (main thread)
// computes coefficients, ProcessBlock (audio thread) only ever applies them.
class EqKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kNumBands = 5;
   static constexpr int kMaxStagesPerBand = 3;
   static constexpr int kCoeffsPerStage = 5; // b0,b1,b2,a1,a2
   static constexpr int kCoeffsPerBand = kMaxStagesPerBand * kCoeffsPerStage;
   static constexpr int kOutputGainSlot = kNumBands * kCoeffsPerBand;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      Reset();
   }

   void Reset() override
   {
      for (auto& band : mBiquad)
         for (auto& stage : band)
            for (auto& bq : stage)
               bq.Reset();
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out) override
   {
      const int numChannels = std::min({ in.numChannels, out.numChannels, kMaxChannels });

      for (int i = 0; i < out.numFrames; i++)
      {
         float coeffs[kNumBands][kMaxStagesPerBand][kCoeffsPerStage];
         for (int b = 0; b < kNumBands; b++)
            for (int s = 0; s < kMaxStagesPerBand; s++)
               for (int c = 0; c < kCoeffsPerStage; c++)
                  coeffs[b][s][c] = mMailbox.SmoothedValue((b * kMaxStagesPerBand + s) * kCoeffsPerStage + c);

         // Output gain hardcoded to unity (0 dB) - the "output" section
         // (output gain + mix) was removed from DrawEqBody entirely, so this
         // no longer reads outputGainDb/kOutputGainSlot at all. Keeping the
         // slot/constant declared (PushParams simply never writes it, so it
         // stays at the mailbox's zero-initialized default) rather than
         // deleting them is what makes this a pure DSP hardcode with no
         // save/load risk - see EqKernel.cpp's PushParams comment.
         const float outputGain = 1.0f;

         for (int ch = 0; ch < numChannels; ch++)
         {
            // Tiny bias away from exact zero before the recursive stages -
            // the same denormal guard AudioFilterKernel uses for its
            // resonant ring-down.
            float s = in.channels[ch][i] + 1.0e-20f;

            for (int b = 0; b < kNumBands; b++)
            {
               for (int st = 0; st < kMaxStagesPerBand; st++)
               {
                  DspMath::Biquad& bq = mBiquad[b][st][ch];
                  bq.b0 = coeffs[b][st][0];
                  bq.b1 = coeffs[b][st][1];
                  bq.b2 = coeffs[b][st][2];
                  bq.a1 = coeffs[b][st][3];
                  bq.a2 = coeffs[b][st][4];
                  s = bq.Process(s);
               }
            }

            out.channels[ch][i] = s * outputGain;
         }
      }
   }

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;
   DspMath::Biquad mBiquad[kNumBands][kMaxStagesPerBand][kMaxChannels];
};
