#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "IEffectKernel.h"
#include "MinPhaseOversampler.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Saturator (v0.6 2.17): three saturation stages, each with its own curve
// and amount, wired one of five ways. Drive stays the one-knob unit; this is
// the node for building a distortion out of parts.
//
//   stage curves  tube, tape, diode, fold, rectify, crush (or off)
//   routing       serial      s3(s2(s1(x)))
//                 parallel    the average of the stages that are on
//                 mid-side    s1 on mid, s2 on side, then s3 on left/right
//                 3-band      s1 low, s2 mid, s3 high (crossovers 250 Hz, 2.5 kHz)
//                 feedback    serial, with the output fed back through a
//                             3 kHz low-pass and a 1 ms delay; the knob is
//                             the loop gain at small signals (divided by
//                             the stages' drive), so it colours, never howls
//
// Every curve has unit slope at zero, so amount sets a drive gain of 0..36 dB
// into a curve whose small-signal gain is 1 (crush instead maps amount to its
// bit depth, 16 down to 2). Each curve has a closed-form antiderivative, and
// every stage runs first-order ADAA (Parker, Zavalishin & Le Bivic, DAFx-16)
// at 8x through MinPhaseOversampler - the same treatment, in the same order,
// as Drive (DriveKernel.h). The curves:
//
//   tube     (T(u + b) - T(b)) / T'(b), b = 0.2: tanh pushed off centre, so
//            it adds even harmonics. T is DspMath::FastTanh.
//   tape     u / sqrt(1 + u^2): a softer knee than tanh, never fully flat.
//   diode    1 - e^-u above zero, -k(1 - e^(u/k)) below (k = 0.5): the two
//            sides of a diode pair with unequal thresholds.
//   fold     sin u: the signal folds back on itself past the peak.
//   rectify  |T(u)|: full wave; adds an octave.
//   crush    a quantiser, q round(x / q), clipped at full scale.
//
// The 3-band split is two Linkwitz-Riley 4th-order crossovers (two
// Butterworth TPT SVFs per side); the low band also passes the upper
// crossover's allpass, so the three bands sum back to an allpass and the
// split is flat with every stage off. The dry half of the mix (ProcessDry)
// goes through the same oversampler and, in 3-band, the same two allpasses,
// so wet and dry stay in phase at any mix. Mid-side encodes and decodes
// inside the oversampled domain, so a mono input stays exactly mono. A
// one-channel input in mid-side gets s1 then s3 (its side is silent);
// channels past the first pair run the serial chain.
//
// Model and routing are discrete (atomics, read once per block); amounts,
// feedback and output are smoothed through the mailbox. A changed model or
// amount re-evaluates the previous antiderivative on the new curve (Drive's
// ADAA note), and a stage that is off forgets its history, so turning it on
// does not average across the gap.
class AudioEffectNode;

namespace SaturatorDsp
{
   enum Model
   {
      kOff = 0,
      kTube,
      kTape,
      kDiode,
      kFold,
      kRectify,
      kCrush,
      kNumModels
   };

   enum Routing
   {
      kSerial = 0,
      kParallel,
      kMidSide,
      kBands,
      kFeedback,
      kNumRoutings
   };

   inline const std::vector<std::string>& ModelList()
   {
      static const std::vector<std::string> kList = { "off", "tube", "tape", "diode", "fold", "rectify", "crush" };
      return kList;
   }

   inline const std::vector<std::string>& RoutingList()
   {
      static const std::vector<std::string> kList = { "serial", "parallel", "mid-side", "3-band", "feedback" };
      return kList;
   }

   constexpr double kMaxDriveDb = 36.0;
   constexpr double kTubeBias = 0.2;
   constexpr double kDiodeKnee = 0.5;
   constexpr float kLowCrossoverHz = 250.0f;
   constexpr float kHighCrossoverHz = 2500.0f;
   constexpr float kFeedbackLowpassHz = 3000.0f;
   constexpr double kFeedbackDelaySec = 0.001;
   constexpr double kRoutingFadeSec = 0.005; // out and back in, on a routing change

   // DspMath::FastTanh in double, so a curve's value and its antiderivative
   // (DspMath::FastTanhAntiderivative) describe the same function.
   inline double FastTanhD(double x)
   {
      if (x < -3.0)
         return -1.0;
      if (x > 3.0)
         return 1.0;
      const double x2 = x * x;
      return x * (27.0 + x2) / (27.0 + 9.0 * x2);
   }

   inline double FastTanhSlope(double x)
   {
      const double x2 = x * x;
      const double den = 27.0 + 9.0 * x2;
      return ((27.0 + 3.0 * x2) * den - x * (27.0 + x2) * 18.0 * x) / (den * den);
   }

   using DspMath::FastTanhAntiderivative;

   // Tube's offset constants, set once at static initialisation (a
   // function-local static would put an init guard on the audio thread).
   inline const double kTubeTanhAtBias = FastTanhD(kTubeBias);
   inline const double kTubeSlopeAtBias = FastTanhSlope(kTubeBias);
   inline const double kTubeAntiAtBias = FastTanhAntiderivative(kTubeBias);
   inline const double kTanhAntiAtZero = FastTanhAntiderivative(0.0);

   // One stage's curve at one amount, resolved once per base-rate sample.
   struct Curve
   {
      int model = kOff;
      double gain = 1.0; // drive into the curve (shaping models)
      double q = 0.0;    // quantiser step (crush)

      Curve() = default;
      Curve(int model_, float amount)
         : model(model_)
      {
         const double a = std::clamp((double)amount, 0.0, 1.0);
         if (model == kCrush)
            q = std::pow(2.0, 1.0 - (16.0 - 14.0 * a));
         else
            gain = std::pow(10.0, kMaxDriveDb * a / 20.0);
      }

      bool operator!=(const Curve& o) const { return model != o.model || gain != o.gain || q != o.q; }

      // f(u) with f(0) = 0 and f'(0) = 1, for the shaping models.
      static double Shape(int model, double u)
      {
         switch (model)
         {
         case kTube:
            return (FastTanhD(u + kTubeBias) - kTubeTanhAtBias) / kTubeSlopeAtBias;
         case kTape:
            return u / std::sqrt(1.0 + u * u);
         case kDiode:
            return u >= 0.0 ? 1.0 - std::exp(-u) : -kDiodeKnee * (1.0 - std::exp(u / kDiodeKnee));
         case kFold:
            return std::sin(u);
         case kRectify:
            return std::fabs(FastTanhD(u));
         default:
            return u;
         }
      }

      // An antiderivative of Shape(), zero at zero.
      static double ShapeAntiderivative(int model, double u)
      {
         switch (model)
         {
         case kTube:
            return (FastTanhAntiderivative(u + kTubeBias) - kTubeAntiAtBias - kTubeTanhAtBias * u) /
                   kTubeSlopeAtBias;
         case kTape:
            return std::sqrt(1.0 + u * u) - 1.0;
         case kDiode:
            return u >= 0.0 ? u + std::exp(-u) - 1.0
                            : -kDiodeKnee * u + kDiodeKnee * kDiodeKnee * (std::exp(u / kDiodeKnee) - 1.0);
         case kFold:
            return 1.0 - std::cos(u);
         case kRectify:
         {
            const double f = FastTanhAntiderivative(u) - kTanhAntiAtZero;
            return u >= 0.0 ? f : -f;
         }
         default:
            return 0.5 * u * u;
         }
      }

      double Value(double x) const
      {
         if (model == kCrush)
         {
            const double c = std::clamp(x, -1.0, 1.0);
            return q * std::round(c / q);
         }
         return Shape(model, gain * x);
      }

      // Antiderivative of Value() in x. Crush: on the step holding x
      // (n = round(x / q)) the integral is n q x - n^2 q^2 / 2, which joins
      // up continuously at every step edge; past full scale it continues
      // with the last step's slope.
      double Antiderivative(double x) const
      {
         if (model == kCrush)
         {
            const double c = std::clamp(x, -1.0, 1.0);
            const double n = std::round(c / q);
            return n * q * c - 0.5 * n * n * q * q + n * q * (x - c);
         }
         return ShapeAntiderivative(model, gain * x) / gain;
      }
   };

   // The curve as drawn: main-thread, from params alone.
   inline float Transfer(int model, float amount, float x)
   {
      if (model == kOff)
         return x;
      return (float)Curve(model, amount).Value((double)x);
   }

   // First-order ADAA of one stage on one signal (DriveDsp::Adaa's shape,
   // keyed on the whole Curve).
   struct Adaa
   {
      double prevX = 0.0, prevF = 0.0;
      Curve prevCurve;
      bool primed = false;

      void Reset() { primed = false; }

      float Process(float x, const Curve& c)
      {
         if (primed && c != prevCurve)
            prevF = c.Antiderivative(prevX);
         prevCurve = c;
         const double F = c.Antiderivative((double)x);
         const double dx = (double)x - prevX;
         double y;
         if (!primed)
            y = c.Value((double)x);
         else if (std::fabs(dx) < 1.0e-5)
            y = c.Value(0.5 * ((double)x + prevX));
         else
            y = (F - prevF) / dx;
         prevX = x;
         prevF = F;
         primed = true;
         return (float)y;
      }
   };

   // Butterworth TPT SVF (Q = 1/sqrt 2), the building block of the
   // Linkwitz-Riley crossovers and their allpass.
   struct Butter
   {
      DspMath::TptSvf svf;

      void Prepare(double sampleRate, float hz)
      {
         svf.SetSampleRate(sampleRate);
         svf.SetCutoff(hz, 0.70710678f);
         svf.Reset();
      }
      void Reset() { svf.Reset(); }
   };

   // LR4: low = LP(LP(x)), high = HP(HP(x)), sharing the first section.
   struct Crossover
   {
      Butter first, low, high;

      void Prepare(double sampleRate, float hz)
      {
         first.Prepare(sampleRate, hz);
         low.Prepare(sampleRate, hz);
         high.Prepare(sampleRate, hz);
      }
      void Reset()
      {
         first.Reset();
         low.Reset();
         high.Reset();
      }
      void Split(float x, float& lo, float& hi)
      {
         const DspMath::TptSvf::Outputs a = first.svf.Process(x);
         lo = low.svf.Process(a.low).low;
         hi = high.svf.Process(a.high).high;
      }
   };

   // LP^2 + HP^2 of the LR4 above: x - 2k band, k = sqrt 2.
   struct Allpass
   {
      Butter b;

      void Prepare(double sampleRate, float hz) { b.Prepare(sampleRate, hz); }
      void Reset() { b.Reset(); }
      float Process(float x)
      {
         const DspMath::TptSvf::Outputs o = b.svf.Process(x);
         return x - 2.0f * b.svf.k * o.band;
      }
   };
}

class SaturatorKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kStages = 3;
   static constexpr int kOversample = 8;
   static constexpr int kMaxLatency = 8; // MinPhaseOversampler at 8x, any rate

   enum ParamSlot
   {
      kAmount1 = 0,
      kAmount2,
      kAmount3,
      // Not kFeedback: in members that would hide SaturatorDsp::kFeedback,
      // the routing, and run the feedback code in 3-band.
      kFeedbackAmount,
      kOutputDb,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int maxBlockSize) override;
   void Reset() override;
   void PushParams(const AudioEffectNode& node, double sampleRate) override;
   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;
   bool ProcessDry(const AudioBuffer& in, AudioBuffer& dry) override;

   int LatencySamples() const override { return mOversampler.Latency(); }
   int MaxLatencySamples() const override { return kMaxLatency; }

private:
   float Serial(int ch, float x, const SaturatorDsp::Curve* c);
   float RunStage(int stage, int slot, float x, const SaturatorDsp::Curve& c);
   void ClearRoutingState();

   ParamMailbox mMailbox;
   std::atomic<int> mModel[kStages] {};
   std::atomic<int> mRouting { SaturatorDsp::kSerial };
   int mActiveRouting = SaturatorDsp::kSerial; // the graph running now; trails mRouting by a fade
   float mSwitchGain = 1.0f;                   // wet gain through a routing change
   float mSwitchStep = 1.0f / 240.0f;
   double mSampleRate = 44100.0;

   MinPhaseOversampler mOversampler;
   MinPhaseOversampler mDryOversampler;
   SaturatorDsp::Adaa mAdaa[kStages][kMaxChannels];
   SaturatorDsp::Crossover mLowSplit[kMaxChannels], mHighSplit[kMaxChannels];
   SaturatorDsp::Allpass mLowBandAllpass[kMaxChannels];
   SaturatorDsp::Allpass mDryAllpassLow[kMaxChannels], mDryAllpassHigh[kMaxChannels];
   DspMath::DcBlocker mDcBlockers[kMaxChannels];

   // Feedback routing: a one-pole low-pass on the delayed output.
   std::vector<float> mFeedbackLine[kMaxChannels];
   int mFeedbackPos = 0;
   int mFeedbackDelay = 44;
   float mFeedbackLpState[kMaxChannels] = {};
   float mFeedbackLpCoef = 0.0f;
};
