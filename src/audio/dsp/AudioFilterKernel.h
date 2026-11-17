#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"
#include "audio/dsp/FilterModels.h"
#include "audio/dsp/MinPhaseOversampler.h"
#include "core/Transport.h"

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
      kCombPos, kCombNeg,
      kLadder, kMs20, kDiode, // analogue models, FilterModels.h (v0.6 2.25)
      kNumFilterTypes
   };

   inline const char* const* TypeNames()
   {
      static const char* const kNames[kNumFilterTypes] = {
         "lp 12", "lp 24", "lp 36",
         "hp 12", "hp 24", "hp 36",
         "bp", "notch", "low shelf", "high shelf", "peak", "all-pass",
         "comb +", "comb -",
         "ladder", "ms-20", "diode"
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

   // Damping k (= 1/Q) of stage `stage` of a `stages`-deep SVF cascade. The
   // stages are the Butterworth pole pairs of the whole 12/24/36 dB slope,
   // so at q 0.707 every slope is -3 dB at the cutoff (the same Q on every
   // stage put LP24 at -6 dB, LP36 at -9 dB). Only the last, highest-Q pair
   // takes the resonance, scaled so the cascade's gain at the cutoff is q
   // for every slope (the product of the Butterworth Qs is 0.707) - the same
   // peak a 12 dB filter has, where q on each stage squared or cubed it.
   inline float SvfStageK(int stages, int stage, float q)
   {
      static constexpr float kButterworthQ[3][3] = {
         { 0.70710678f, 0.0f, 0.0f },
         { 0.54119610f, 1.30656296f, 0.0f },
         { 0.51763809f, 0.70710678f, 1.93185165f },
      };
      stages = std::clamp(stages, 1, 3);
      stage = std::clamp(stage, 0, stages - 1);
      float stageQ = kButterworthQ[stages - 1][stage];
      if (stage == stages - 1)
         stageQ *= std::max(0.01f, q) / 0.70710678f;
      return 1.0f / std::max(0.01f, stageQ);
   }
   inline bool IsHighpass(int type) { return type == kHP12 || type == kHP24 || type == kHP36; }
   inline bool UsesGain(int type) { return type == kLowShelf || type == kHighShelf || type == kPeak; }
   inline bool IsComb(int type) { return type == kCombPos || type == kCombNeg; }
   inline bool CombIsNegative(int type) { return type == kCombNeg; }

   // The analogue models: FilterModels::Model for one of them, else -1.
   inline int ModelOf(int type)
   {
      switch (type)
      {
         case kLadder: return FilterModels::kModelLadder;
         case kMs20: return FilterModels::kModelMs20;
         case kDiode: return FilterModels::kModelDiode;
         default: return -1;
      }
   }
   inline bool IsModel(int type) { return ModelOf(type) >= 0; }

   // q onto a model's resonance: the default 0.707 is none, q 10 is the
   // self-oscillation threshold, and the top of the knob sings (1.1, the
   // same ceiling Analog's resonance knob reaches).
   inline float ModelResonance(float q) { return std::clamp((q - 0.70710678f) / (10.0f - 0.70710678f), 0.0f, 1.1f); }

   // The models run at twice the device rate (MinPhaseOversampler), so their
   // tanh stages and self-oscillation near the top keep clear of Nyquist.
   constexpr int kModelOversample = 2;
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

   // Magnitude response, in dB, of one band at `evalHz`, evaluated from the
   // transfer function of a *scratch* instance of the same primitive the
   // kernel uses, never the live AudioNode (per audio-node-ui-system.md
   // §3f/§3). Used by both the response-curve visualizer (main.cpp) and the
   // DSP test fixture, so there is exactly one definition of "the analytic
   // response" to compare the running kernel against.
   //
   // Both primitives are linear and time-invariant, so this is the settled
   // steady-state gain a sine at evalHz sees - what this function used to
   // measure by running up to 8000 samples of a sine through the scratch
   // instance per point. That simulation cost up to ~15 ms per 160-point
   // curve and, with modulated filters on the canvas, was the whole of B3's
   // slow-frame tail (docs/plans/perf/README.md, Block 2).
   //   TptSvf: the bilinear transform of the analog SVF with prewarped g, so
   //   on the unit circle s = j*tan(pi*f/fs)/g and one stage is
   //   LP 1/(1 - W^2 + j*k*W), HP W^2/(same), W = tan(pi*evalHz/fs)/g.
   //   Biquad: H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2) at
   //   z = e^(j*w), from the scratch instance's own coefficients.
   inline float MagnitudeDb(int type, float freqHz, float q, float gainDb, float evalHz, double sampleRate)
   {
      if (evalHz <= 0.0f || sampleRate <= 0.0)
         return 0.0f;

      if (IsComb(type))
      {
         return DspMath::CombMagnitudeDb(freqHz, DspMath::CombFeedbackFromQ(q), CombIsNegative(type),
                                          evalHz, sampleRate);
      }

      double magSq = 1.0;
      if (IsModel(type))
      {
         // The small-signal response; the oversampler is flat below 20 kHz.
         const int model = ModelOf(type);
         const double osRate = sampleRate * kModelOversample;
         const float g = FilterModels::CutoffToG(model, freqHz, osRate, 0.45f * (float)sampleRate);
         const float res = ModelResonance(q);
         const double W = tan(M_PI * std::min((double)evalHz, 0.4999 * osRate) / osRate) /
                          std::max(1e-12, (double)g);
         const double makeup = FilterModels::MakeupGain(model, res);
         magSq = std::norm(FilterModels::LinearResponse(model, W, FilterModels::KFromResonance(model, res))) *
                 makeup * makeup;
      }
      else if (IsSvf(type))
      {
         DspMath::TptSvf svf;
         svf.SetSampleRate(sampleRate);
         svf.SetCutoff(freqHz, q);
         const double w = tan(M_PI * std::min((double)evalHz, 0.4999 * sampleRate) / (double)svf.sampleRate) /
                          std::max(1e-12, (double)svf.g);
         const int stages = SvfStageCount(type);
         for (int s = 0; s < stages; s++)
         {
            const double re = 1.0 - w * w, im = (double)SvfStageK(stages, s, q) * w;
            double stageSq = 1.0 / std::max(1e-300, re * re + im * im);
            if (IsHighpass(type))
               stageSq *= w * w * w * w;
            magSq *= stageSq;
         }
      }
      else
      {
         DspMath::Biquad bq;
         ConfigureBiquad(bq, type, freqHz, q, gainDb, sampleRate);
         const double w = 2.0 * M_PI * (double)evalHz / sampleRate;
         const double c1 = cos(w), s1 = sin(w), c2 = cos(2.0 * w), s2 = sin(2.0 * w);
         const double nr = bq.b0 + bq.b1 * c1 + bq.b2 * c2, ni = -(bq.b1 * s1 + bq.b2 * s2);
         const double dr = 1.0 + bq.a1 * c1 + bq.a2 * c2, di = -(bq.a1 * s1 + bq.a2 * s2);
         magSq = (nr * nr + ni * ni) / std::max(1e-300, dr * dr + di * di);
      }
      return (float)(10.0 * log10(std::max(1e-18, magSq)));
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
   // Raw freq/Q/gain are pushed too (not just the precomputed coeffs above) so
   // ProcessBlock can recompute coefficients per-sample when the envelope
   // knob is in use - the fast path (envAmount == 0) still just applies the
   // precomputed coeffs and never touches these.
   static constexpr int kFreqSlot = kOutputGainSlot + 1;
   static constexpr int kQSlot = kFreqSlot + 1;
   static constexpr int kGainSlot = kQSlot + 1;
   static constexpr int kEnvAmountSlot = kGainSlot + 1;
   // Free-Hz rate for the internal LFO (§"envAmount" above) - only read while
   // `sync` is off, same free-Hz-vs-tempo-synced split as Chorus/Flanger/
   // Phaser/Tremolo's own `rate` slot.
   static constexpr int kRateSlot = kEnvAmountSlot + 1;
   // Input gain into the analogue models (1 + 15 drive), undone after them
   // so the small-signal level stays where the response curve draws it.
   static constexpr int kDriveSlot = kRateSlot + 1;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      for (auto& stage : mSvf)
         for (auto& svf : stage)
            svf.SetSampleRate(sampleRate);
      mOversampler.Prepare(AudioFilterDsp::kModelOversample, sampleRate);
      Reset();
   }

   void Reset() override
   {
      for (auto& stage : mSvf)
         for (auto& svf : stage)
            svf.Reset();
      for (auto& bq : mBiquad)
         bq.Reset();
      for (auto& comb : mComb)
         comb.Reset();
      for (auto& m : mModels)
         m.Reset();
      mOversampler.Reset();
      mLfoPhase = 0.0;
      mBiquadFreqSlew = 1000.0f;
      mEnvPrimed = false;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out) override
   {
      const int numChannels = std::min({ in.numChannels, out.numChannels, kMaxChannels });
      const int type = mType.load(std::memory_order_relaxed);
      const bool sync = mSync.load(std::memory_order_relaxed) != 0;
      const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
      float lastLfoOut = 0.0f;
      const float envCoef = expf(-1.0f / (float)(mSampleRate * 0.02));

      for (int i = 0; i < out.numFrames; i++)
      {
         // envAmount is octaves of sweep (x4), so the mailbox's 5 ms ramp
         // moved the cutoff up to 3.2 octaves in 5 ms on a 30% -> 70% knob
         // step: a +17 dB transient (F13). A 20 ms glide on the depth alone,
         // primed to the knob on the first sample so a loaded patch does not
         // sweep in.
         const float envTarget = mMailbox.SmoothedValue(kEnvAmountSlot);
         if (!mEnvPrimed)
         {
            mEnvSmooth = envTarget;
            mEnvPrimed = true;
         }
         mEnvSmooth = envTarget + envCoef * (mEnvSmooth - envTarget);
         const float envAmount = mEnvSmooth;
         const bool envActive = std::fabs(envAmount) > 1.0e-4f;

         // The internal sweep LFO always advances, active or not, so
         // re-engaging `env` later resumes from a live phase instead of a
         // phase frozen at whatever it last was - same reasoning
         // ProcessBlock already applies to mBiquadFreqSlew below. Rate mode
         // mirrors Chorus/Flanger/Phaser/Tremolo's own sync/rateDiv/rate
         // split exactly.
         float rateHz;
         if (sync)
         {
            const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
            rateHz = (float)MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
         }
         else
         {
            rateHz = std::max(0.0f, mMailbox.SmoothedValue(kRateSlot));
         }
         mLfoPhase += (double)rateHz / mSampleRate;
         if (mLfoPhase >= 1.0)
            mLfoPhase -= floor(mLfoPhase);
         const float lfoOut = sinf(2.0f * (float)M_PI * (float)mLfoPhase);
         lastLfoOut = lfoOut;

         // Comb has no biquad/SVF coefficients to precompute - it's a delay
         // line with feedback, so it always reads its raw freq/Q slots
         // itself and skips the coeffs block below entirely.
         if (AudioFilterDsp::IsComb(type))
         {
            const float freq = mMailbox.SmoothedValue(kFreqSlot);
            const float q = mMailbox.SmoothedValue(kQSlot);
            float freqMod = freq;
            if (envActive)
            {
               const float totalOctaves = envAmount * lfoOut * 4.0f;
               freqMod = std::clamp(freq * powf(2.0f, totalOctaves), 20.0f, (float)mSampleRate * 0.45f);
            }
            const bool negative = AudioFilterDsp::CombIsNegative(type);
            const float fbAmount = DspMath::CombFeedbackFromQ(q);
            const float outputGain = mOutGainCache.Get(mMailbox.SmoothedValue(kOutputGainSlot));
            for (int ch = 0; ch < numChannels; ch++)
            {
               mComb[ch].SetParams(freqMod, fbAmount, negative, mSampleRate);
               const float s = in.channels[ch][i] + 1.0e-20f;
               out.channels[ch][i] = mComb[ch].Process(s) * outputGain;
            }
            continue;
         }

         // The analogue models solve their own coefficients from g and k each
         // sample, at the oversampled rate.
         if (AudioFilterDsp::IsModel(type))
         {
            const int model = AudioFilterDsp::ModelOf(type);
            const float freq = mMailbox.SmoothedValue(kFreqSlot);
            const float res = AudioFilterDsp::ModelResonance(mMailbox.SmoothedValue(kQSlot));
            float freqMod = freq;
            if (envActive)
               freqMod = std::clamp(freq * powf(2.0f, envAmount * lfoOut * 4.0f), 20.0f, (float)mSampleRate * 0.45f);
            const float g = FilterModels::CutoffToG(model, freqMod, mSampleRate * AudioFilterDsp::kModelOversample,
                                                    (float)mSampleRate * 0.45f);
            const float k = FilterModels::KFromResonance(model, res);
            const float inGain = 1.0f + 15.0f * std::clamp(mMailbox.SmoothedValue(kDriveSlot), 0.0f, 1.0f);
            const float outGain = mOutGainCache.Get(mMailbox.SmoothedValue(kOutputGainSlot)) *
                                  FilterModels::MakeupGain(model, res) / inGain;
            for (int ch = 0; ch < numChannels; ch++)
            {
               float os[AudioFilterDsp::kModelOversample];
               mOversampler.Up(ch, (in.channels[ch][i] + 1.0e-20f) * inGain, os);
               for (float& v : os)
                  v = mModels[ch].Process(model, v, g, k);
               out.channels[ch][i] = mOversampler.Down(ch, os) * outGain;
            }
            continue;
         }

         float coeffs[kStages][kCoeffsPerStage];
         if (!envActive)
         {
            // Fast path: precomputed, smoothed coefficients from PushParams -
            // unchanged behaviour for every patch that leaves env at 0.
            for (int s = 0; s < kStages; s++)
               for (int c = 0; c < kCoeffsPerStage; c++)
                  coeffs[s][c] = mMailbox.SmoothedValue(s * kCoeffsPerStage + c);
            // Keep the biquad slew-limiter's state tracking the current
            // cutoff even while it's unused, so re-engaging modulation later
            // glides from "here", not from a stale value left over from a
            // previous session of modulation.
            mBiquadFreqSlew = mMailbox.SmoothedValue(kFreqSlot);
         }
         else
         {
            // Recompute this sample's coefficients from the LFO-modulated
            // cutoff - envAmount is in octaves of shift, scaled by the
            // free-running sine LFO's bipolar -1..1 output (a negative
            // envAmount inverts the sweep direction).
            const float freq = mMailbox.SmoothedValue(kFreqSlot);
            const float q = mMailbox.SmoothedValue(kQSlot);
            const float gainDb = mMailbox.SmoothedValue(kGainSlot);
            const float totalOctaves = envAmount * lfoOut * 4.0f;
            // Clamped to a hard ceiling below Nyquist rather than a fixed
            // 20kHz - correct for sample rates above 44.1kHz, where the old
            // fixed bound left an env/mod sweep stopping well short of what
            // the device can actually represent. ConfigureBiquad's shelf/peak
            // math stays well-behaved this close to Nyquist since it's still
            // bounded away from exactly Nyquist by the 0.45 factor.
            const float freqMod =
               std::clamp(freq * powf(2.0f, totalOctaves), 20.0f, (float)mSampleRate * 0.45f);

            if (AudioFilterDsp::IsSvf(type))
            {
               const float g = tanf((float)M_PI * freqMod / (float)mSampleRate);
               const int stages = AudioFilterDsp::SvfStageCount(type);
               for (int s = 0; s < stages; s++)
               {
                  coeffs[s][0] = g;
                  coeffs[s][1] = AudioFilterDsp::SvfStageK(stages, s, q);
               }
            }
            else
            {
               // The biquad types keep coefficient-dependent state (unlike
               // the SVF's topology-preserving transform), so recomputing
               // coefficients every sample straight from an audio-rate
               // modulator is the classic route to level jumps or blow-up at
               // high Q. A few-ms one-pole slew on the cutoff feeding this
               // branch only keeps the feature on every filter type while
               // costing one multiply-add.
               const float slewCoef = expf(-1.0f / (float)(mSampleRate * 0.005));
               mBiquadFreqSlew = freqMod + slewCoef * (mBiquadFreqSlew - freqMod);

               DspMath::Biquad bq;
               AudioFilterDsp::ConfigureBiquad(bq, type, mBiquadFreqSlew, q, gainDb, mSampleRate);
               coeffs[0][0] = bq.b0;
               coeffs[0][1] = bq.b1;
               coeffs[0][2] = bq.b2;
               coeffs[0][3] = bq.a1;
               coeffs[0][4] = bq.a2;
            }
         }

         const float outputGain = mOutGainCache.Get(mMailbox.SmoothedValue(kOutputGainSlot));

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

      // Publish once per block, not once per sample: mLfoMeter previously
      // wrote every sample (44.1k+/sec), which fills the 4096-entry
      // MeterRing in well under a second while DrawAudioFilterVisualizer's
      // ExtraMeterValue readback only drains up to kMaxExtraMeterValues (4)
      // entries per UI frame (~240/sec at 60fps). Once full, MeterRing::Write
      // silently drops samples until the reader makes room, so the initial
      // ~4096-sample backlog - itself only ~93ms of real audio time - took
      // the UI ~17 seconds of frames to page through, during which
      // consecutive entries were microseconds apart and the yellow sweep
      // overlay read as frozen. That read as "doesn't animate until I nudge
      // a knob," but a knob nudge did nothing to it directly; the backlog
      // was just still draining. Writing once per block (same discipline
      // DynamicsKernel's mGrMeter already uses for its GR-bar dot) keeps the
      // ring shallow so the UI reads a near-current value every frame.
      mLfoMeter.Write(&lastLfoOut, 1);
   }

   // The models' oversampler round trip; every other type has none.
   int LatencySamples() const override
   {
      return AudioFilterDsp::IsModel(mType.load(std::memory_order_relaxed)) ? mOversampler.Latency() : 0;
   }
   int MaxLatencySamples() const override { return mOversampler.Latency(); }

   // Kernel-published LFO output (-1..1), read by DrawAudioFilterVisualizer
   // via AudioEffectNode::ExtraMeterValue(0) so the response curve can
   // animate the live sweep in sync with the audio-thread LFO - same
   // MeterRing discipline DynamicsKernel uses for its GR-bar readout, not a
   // new cross-thread mechanism.
   MeterRing* ExtraMeter() override { return &mLfoMeter; }

private:
   ParamMailbox mMailbox;
   DspMath::DbLinCache mOutGainCache; // output gain dB -> linear: powf only when the knob moves
   double mSampleRate = 44100.0;

   std::atomic<int> mType {};
   std::atomic<int> mSync {};
   std::atomic<int> mRateDiv { MusicTime::kQuarter };

   DspMath::TptSvf mSvf[kStages][kMaxChannels];
   DspMath::Biquad mBiquad[kMaxChannels];
   DspMath::CombFilter mComb[kMaxChannels];
   FilterModels::Bank<float> mModels[kMaxChannels];
   MinPhaseOversampler mOversampler;
   // Free-running sweep LFO phase, 0..1 - see the `envAmount`/kRateSlot
   // comments above.
   double mLfoPhase = 0.0;
   MeterRing mLfoMeter;
   // Slew-limits the cutoff feeding the biquad branch under modulation - see
   // ProcessBlock's comment at the biquad recompute for why.
   float mBiquadFreqSlew = 1000.0f;
   float mEnvSmooth = 0.0f; // envAmount after its 20 ms glide (ProcessBlock)
   bool mEnvPrimed = false;
};
