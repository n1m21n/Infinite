#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Audio Filter's kernel: up to 4 bands in series, each one of 12 types.
// docs/plans/audio/P3c-P3a2-design.md §1.1:
//   - LP/HP slopes (12/24/36 dB) are cascaded 12 dB DspMath::TptSvf stages,
//     per Vadim Zavalishin's "The Art of VA Filter Design" (already the
//     primitive DspMath.h implements it from).
//   - BP/Notch/Low Shelf/High Shelf/Peak/All-pass are single-stage
//     DspMath::Biquad, per Robert Bristow-Johnson's Audio EQ Cookbook (again,
//     already what DspMath::Biquad implements).
// Both primitives already live in this tree's DspMath.h from an earlier
// session; this kernel is the first thing to drive them from a real node.
namespace AudioFilterDsp
{
   enum FilterType
   {
      kLP12 = 0, kLP24, kLP36,
      kHP12, kHP24, kHP36,
      kBP, kNotch, kLowShelf, kHighShelf, kPeak, kAllpass,
      kNumFilterTypes
   };

   inline const char* const* TypeNames()
   {
      static const char* const kNames[kNumFilterTypes] = {
         "lp 12", "lp 24", "lp 36",
         "hp 12", "hp 24", "hp 36",
         "bp", "notch", "low shelf", "high shelf", "peak", "all-pass"
      };
      return kNames;
   }

   inline const char* TypeName(int type)
   {
      return (type >= 0 && type < kNumFilterTypes) ? TypeNames()[type] : TypeNames()[kLP24];
   }

   inline const std::vector<std::string>& TypeList()
   {
      static std::vector<std::string> list;
      if (list.empty())
         for (int i = 0; i < kNumFilterTypes; i++)
            list.push_back(TypeNames()[i]);
      return list;
   }

   // Cascaded 12 dB/octave SVF stages: 1, 2 or 3. 0 for the biquad types,
   // which are always a single stage - the render path branches on this, not
   // on the type directly, matching SynthModes::FilterStages' reasoning.
   inline int SvfStageCount(int type)
   {
      switch (type)
      {
         case kLP12: case kHP12: return 1;
         case kLP24: case kHP24: return 2;
         case kLP36: case kHP36: return 3;
         default: return 0;
      }
   }

   inline bool IsSvf(int type) { return SvfStageCount(type) > 0; }
   inline bool IsHighpass(int type) { return type == kHP12 || type == kHP24 || type == kHP36; }
   inline bool UsesGain(int type) { return type == kLowShelf || type == kHighShelf || type == kPeak; }

   // Configures a scratch Biquad for one of the non-SVF types. Shared by the
   // kernel's main-thread coefficient push and by MagnitudeDb below, so the
   // two can never compute a different filter for the same params.
   inline void ConfigureBiquad(DspMath::Biquad& bq, int type, double freq, double q, double gainDb,
                               double sampleRate)
   {
      switch (type)
      {
         case kBP: bq.SetBandpass(freq, q, sampleRate); break;
         case kNotch: bq.SetNotch(freq, q, sampleRate); break;
         case kLowShelf: bq.SetLowShelf(freq, q, gainDb, sampleRate); break;
         case kHighShelf: bq.SetHighShelf(freq, q, gainDb, sampleRate); break;
         case kPeak: bq.SetPeaking(freq, q, gainDb, sampleRate); break;
         case kAllpass: bq.SetAllpass(freq, q, sampleRate); break;
         default: bq.SetAllpass(freq, q, sampleRate); break;
      }
   }

   // Closed-form-by-measurement magnitude response, in dB, of one band at
   // `evalHz` - feed a settled sine through a *scratch* instance of the same
   // primitive the kernel uses, never the live AudioNode (per
   // audio-node-ui-system.md §3f/§3). Used by both the response-curve
   // visualizer (main.cpp) and the DSP test fixture, so there is exactly one
   // definition of "the analytic response" to compare the running kernel
   // against.
   inline float MagnitudeDb(int type, float freqHz, float q, float gainDb, float evalHz, double sampleRate)
   {
      if (evalHz <= 0.0f || sampleRate <= 0.0)
         return 0.0f;

      const double periodSamples = std::max(4.0, sampleRate / (double)evalHz);
      // Settling time depends on the *filter's* cutoff/Q, not on evalHz - a
      // budget sized only off the eval tone's own period (the original
      // version here) starves the settle phase whenever evalHz is far above
      // freqHz, since a fast tone's "8 cycles" is a handful of samples long
      // even though the filter's own state needs many more to stop ringing.
      // Measured separately from settling, so evalHz still gets enough
      // cycles of its own for an accurate RMS regardless of how short the
      // settle phase is.
      const double freqPeriodSamples = std::max(4.0, sampleRate / (double)std::max(1.0f, freqHz));
      const int settleSamples =
         (int)std::clamp(freqPeriodSamples * 6.0 * (double)std::max(1.0f, q), 200.0, 4000.0);
      const int measureSamples = (int)std::clamp(periodSamples * 8.0, 64.0, 4000.0);
      const int totalSamples = std::min(8000, settleSamples + measureSamples);
      const int skip = std::min(settleSamples, totalSamples - 16);
      const double phaseInc = 2.0 * M_PI * (double)evalHz / sampleRate;

      double sumInSq = 0.0, sumOutSq = 0.0;
      double phase = 0.0;

      if (IsSvf(type))
      {
         const int stages = SvfStageCount(type);
         DspMath::TptSvf svf[3];
         for (int s = 0; s < stages; s++)
         {
            svf[s].SetSampleRate(sampleRate);
            svf[s].SetCutoff(freqHz, q);
         }
         for (int i = 0; i < totalSamples; i++)
         {
            const float x = (float)sin(phase);
            float y = x;
            for (int s = 0; s < stages; s++)
            {
               DspMath::TptSvf::Outputs o = svf[s].Process(y);
               y = IsHighpass(type) ? o.high : o.low;
            }
            if (i >= skip)
            {
               sumInSq += (double)x * (double)x;
               sumOutSq += (double)y * (double)y;
            }
            phase += phaseInc;
         }
      }
      else
      {
         DspMath::Biquad bq;
         ConfigureBiquad(bq, type, freqHz, q, gainDb, sampleRate);
         for (int i = 0; i < totalSamples; i++)
         {
            const float x = (float)sin(phase);
            const float y = bq.Process(x);
            if (i >= skip)
            {
               sumInSq += (double)x * (double)x;
               sumOutSq += (double)y * (double)y;
            }
            phase += phaseInc;
         }
      }

      const int n = std::max(1, totalSamples - skip);
      const double inRms = sqrt(sumInSq / n);
      const double outRms = sqrt(sumOutSq / n);
      const double ratio = outRms / std::max(1e-9, inRms);
      return (float)(20.0 * log10(std::max(1e-9, ratio)));
   }
}

// AudioEffectNode's kernel for the Audio Filter node: one filter, one type.
// See IEffectKernel.h for the PushParams/ProcessBlock split this obeys.
class AudioFilterKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kStages = 3;
   static constexpr int kCoeffsPerStage = 5; // b0,b1,b2,a1,a2 (SVF only uses slots 0,1 as g,k)
   static constexpr int kOutputGainSlot = kStages * kCoeffsPerStage;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      for (auto& stage : mSvf)
         for (auto& svf : stage)
            svf.SetSampleRate(sampleRate);
      Reset();
   }

   void Reset() override
   {
      for (auto& stage : mSvf)
         for (auto& svf : stage)
            svf.Reset();
      for (auto& bq : mBiquad)
         bq.Reset();
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out) override
   {
      const int numChannels = std::min({ in.numChannels, out.numChannels, kMaxChannels });
      const int type = mType.load(std::memory_order_relaxed);

      for (int i = 0; i < out.numFrames; i++)
      {
         // Per-sample smoothed coefficients, read once per sample and shared
         // across channels (the coefficients don't vary per channel; only the
         // filter *state* below does).
         float coeffs[kStages][kCoeffsPerStage];
         for (int s = 0; s < kStages; s++)
            for (int c = 0; c < kCoeffsPerStage; c++)
               coeffs[s][c] = mMailbox.SmoothedValue(s * kCoeffsPerStage + c);
         const float outputGain =
            DspMath::DbToLinear(mMailbox.SmoothedValue(kOutputGainSlot));

         for (int ch = 0; ch < numChannels; ch++)
         {
            // Tiny bias away from exact zero before the recursive stages -
            // the resonant ring-down that would otherwise decay toward a true
            // zero and hit denormal territory (§0.5's per-kernel guard,
            // alongside the engine's own FTZ/DAZ).
            float s = in.channels[ch][i] + 1.0e-20f;

            if (AudioFilterDsp::IsSvf(type))
            {
               const int stages = AudioFilterDsp::SvfStageCount(type);
               const bool hp = AudioFilterDsp::IsHighpass(type);
               for (int st = 0; st < stages; st++)
               {
                  DspMath::TptSvf& svf = mSvf[st][ch];
                  svf.g = coeffs[st][0];
                  svf.k = coeffs[st][1];
                  DspMath::TptSvf::Outputs o = svf.Process(s);
                  s = hp ? o.high : o.low;
               }
            }
            else
            {
               DspMath::Biquad& bq = mBiquad[ch];
               bq.b0 = coeffs[0][0];
               bq.b1 = coeffs[0][1];
               bq.b2 = coeffs[0][2];
               bq.a1 = coeffs[0][3];
               bq.a2 = coeffs[0][4];
               s = bq.Process(s);
            }

            out.channels[ch][i] = s * outputGain;
         }
      }
   }

   int LatencySamples() const override { return 0; } // no lookahead/oversampling/FFT window

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mType {};

   DspMath::TptSvf mSvf[kStages][kMaxChannels];
   DspMath::Biquad mBiquad[kMaxChannels];
};
