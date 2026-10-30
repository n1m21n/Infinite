#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "SimdF4.h"

// An instrument body or soundboard as a dense bank of decaying modes. A real
// body has hundreds of modes; past a few hundred Hz they overlap, and the body
// reads as a short, dense reverb with the instrument's colour, not as a
// filter (Woodhouse 2014; Bank et al. 2010 for the piano soundboard). A
// handful of resonators reads as a filter, which is why ten- and twelve-mode
// banks made every body type sound alike.
//
// The bank, per body type:
//   - signature modes: the low modes an instrument is known by (a guitar's
//     air mode at ~100 Hz, a violin's A0 and B1 pair), each at its own Q;
//   - a dense field of `modes` modes from loHz to hiHz, drawn once from a
//     seed, density rising as f^density (0 for a plate, more for a box),
//     each ringing for T60 = 2.2 / (f eta), eta the loss factor;
//   - every mode's level follows the type's spectral envelope (the fitted
//     curve, a bridge hill, a top roll-off, a low cut) times a random sign
//     and size, which is what a measured body response looks like above its
//     first few modes;
//   - left and right read the modes with partly different gains (a second
//     pickup point), so the body is wide while the bass and the signature
//     modes stay centred.
// Each mode is a complex one-pole (a rotating phasor): unit power gain per
// mode with b = 2 sqrt(1 - r) (Shape Resonator's normalisation), so a mode's
// level does not depend on how long it rings, and with the output gains
// normalised to sum(g^2) = 1 the bank's white-noise power gain is 1.
namespace DenseBody
{
   struct Mode
   {
      float hz, q, gain;
   };

   struct Spec
   {
      const Mode* signature = nullptr;  // explicit low modes
      int signatureCount = 0;
      const Mode* envelope = nullptr;   // fitted curve: sum of bandpass peaks (hz, q, gain)
      int envelopeCount = 0;
      float loHz = 80.0f, hiHz = 6000.0f;
      int modes = 128;
      float density = 0.5f;   // modal density grows as f^density
      float eta = 0.025f;     // loss factor
      float hillHz = 0.0f, hillDb = 0.0f, hillOct = 1.0f; // bridge hill (Gaussian in octaves)
      float rollHz = 4000.0f, rollDbPerOct = 6.0f;        // top roll-off
      float lowCutHz = 0.0f;  // second-order high-pass on the envelope
      float stereo = 0.5f;    // 0 the same gains both sides .. 1 independent
      uint32_t seed = 1;
   };

   class Bank
   {
   public:
      static constexpr int kMaxModes = 256;

      void Design(const Spec& spec, double sampleRate)
      {
         uint32_t rng = spec.seed * 2654435761u + 12345u;
         auto uniform = [&]() {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return (float)((rng >> 8) * (1.0 / 16777216.0));
         };
         auto gauss = [&]() {
            const float u1 = std::max(uniform(), 1e-7f), u2 = uniform();
            return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
         };
         auto envelope = [&](float f) {
            double e = 0.12; // floor between fitted peaks
            for (int m = 0; m < spec.envelopeCount; m++)
            {
               const double x = spec.envelope[m].q * (f / spec.envelope[m].hz - spec.envelope[m].hz / f);
               e += spec.envelope[m].gain / std::sqrt(1.0 + x * x);
            }
            double db = 20.0 * std::log10(e);
            if (spec.hillHz > 0.0f)
            {
               const double o = std::log2(f / spec.hillHz) / spec.hillOct;
               db += spec.hillDb * std::exp(-0.5 * o * o);
            }
            if (f > spec.rollHz)
               db -= spec.rollDbPerOct * std::log2(f / spec.rollHz);
            if (spec.lowCutHz > 0.0f)
            {
               const double r = (double)f / spec.lowCutHz;
               db += 10.0 * std::log10(r * r * r * r / (1.0 + r * r * r * r));
            }
            return (float)std::pow(10.0, db / 20.0);
         };

         const double nyq = 0.45 * sampleRate;
         mCount = 0;
         auto add = [&](float hz, float r, float gl, float gr) {
            if (mCount >= kMaxModes || hz >= nyq || hz <= 0.0f)
               return;
            const double w = 2.0 * M_PI * hz / sampleRate;
            mC[mCount] = (float)(r * std::cos(w));
            mS[mCount] = (float)(r * std::sin(w));
            mB[mCount] = 2.0f * std::sqrt(std::max(1.0f - r, 1e-9f));
            mGL[mCount] = gl;
            mGR[mCount] = gr;
            mCount++;
         };
         auto poleOf = [&](float hz, float t60) {
            return (float)std::exp(-6.9078 / (std::clamp(t60, 0.008f, 2.0f) * sampleRate));
         };

         // Dense field: stratified draws from the f^density distribution.
         const double p = spec.density + 1.0;
         const double a = std::pow((double)spec.loHz, p), b = std::pow((double)spec.hiHz, p);
         double fieldPower = 0.0;
         const int first = mCount;
         for (int k = 0; k < spec.modes; k++)
         {
            const double u = (k + uniform()) / spec.modes;
            const float f = (float)std::pow(a + u * (b - a), 1.0 / p);
            const float e = envelope(f);
            const float g1 = gauss(), g2 = gauss();
            const float gl = e * g1;
            // Below ~500 Hz the sides converge: low modes radiate as one, and a mix
            // has to survive mono.
            const float rho = 1.0f - spec.stereo * std::min(1.0f, f / 500.0f);
            const float gr = e * std::copysign(rho * g1 + std::sqrt(std::max(0.0f, 1.0f - rho * rho)) * g2, g1); // same sign: never anti-phase
            add(f, poleOf(f, 2.2f / (f * spec.eta)), gl, gr);
         }
         for (int k = first; k < mCount; k++)
            fieldPower += 0.5 * (mGL[k] * mGL[k] + mGR[k] * mGR[k]);
         // Signature modes: as loud as a field mode at the envelope's peak level
         // times their gain, the same both sides.
         const float sigScale = (float)std::sqrt(fieldPower / std::max(1, mCount - first)) * 3.0f;
         for (int m = 0; m < spec.signatureCount; m++)
         {
            const Mode& s = spec.signature[m];
            const float g = s.gain * sigScale * (s.hz > 0.0f ? 1.0f : 0.0f);
            add(s.hz, (float)std::exp(-M_PI * s.hz / (s.q * sampleRate)), g, g);
         }
         double pl = 0.0, pr = 0.0;
         for (int k = 0; k < mCount; k++)
         {
            pl += mGL[k] * mGL[k];
            pr += mGR[k] * mGR[k];
         }
         const float nl = pl > 0.0 ? (float)(1.0 / std::sqrt(pl)) : 0.0f;
         const float nr = pr > 0.0 ? (float)(1.0 / std::sqrt(pr)) : 0.0f;
         for (int k = 0; k < mCount; k++)
         {
            mGL[k] *= nl;
            mGR[k] *= nr;
         }
         // Pad to a multiple of 4 with silent modes.
         while (mCount % 4 != 0)
         {
            mC[mCount] = mS[mCount] = mB[mCount] = mGL[mCount] = mGR[mCount] = 0.0f;
            mCount++;
         }
         // State is kept: a redesign while ringing (a body switch) does not
         // click to silence. PrepareToPlay callers Reset() themselves.
      }

      void Reset()
      {
         std::fill_n(mRe, kMaxModes, 0.0f);
         std::fill_n(mIm, kMaxModes, 0.0f);
      }

      // One sample in, the body's left and right out.
      void Process(float in, float& outL, float& outR)
      {
         using VoiceSimd::F4;
         const F4 x(in);
         F4 accL(0.0f), accR(0.0f);
         for (int k = 0; k < mCount; k += 4)
         {
            const F4 c = F4::Load(mC + k), s = F4::Load(mS + k);
            const F4 re = F4::Load(mRe + k), im = F4::Load(mIm + k);
            const F4 nre = VoiceSimd::FlushDenormal(c * re - s * im + F4::Load(mB + k) * x);
            const F4 nim = VoiceSimd::FlushDenormal(s * re + c * im);
            nre.Store(mRe + k);
            nim.Store(mIm + k);
            accL = accL + nim * F4::Load(mGL + k);
            accR = accR + nim * F4::Load(mGR + k);
         }
         alignas(16) float l[4], r[4];
         accL.Store(l);
         accR.Store(r);
         outL = (l[0] + l[1]) + (l[2] + l[3]);
         outR = (r[0] + r[1]) + (r[2] + r[3]);
      }

      int Count() const { return mCount; }

   private:
      int mCount = 0;
      alignas(16) float mC[kMaxModes] = {}, mS[kMaxModes] = {}, mB[kMaxModes] = {};
      alignas(16) float mGL[kMaxModes] = {}, mGR[kMaxModes] = {};
      alignas(16) float mRe[kMaxModes] = {}, mIm[kMaxModes] = {};
   };
}
