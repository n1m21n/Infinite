#pragma once

#include <algorithm>
#include <cmath>
#include "audio/DspMath.h"

namespace AnalogDsp
{
   using DspMath::FlushDenormal;

   // Asymmetric tanh waveshaping that generates musical 2nd harmonic warmth
   // without leaving a DC offset when signal is zero.
   inline float AsymTanh(float x, float bias = 0.15f)
   {
      const float shaped = DspMath::FastTanh(x + bias);
      const float zeroRef = DspMath::FastTanh(bias);
      return shaped - zeroRef;
   }

   // 1-pole Lowpass Filter for analog damping & tone softening
   struct OnePoleLP
   {
      float z1 = 0.0f;
      float a = 1.0f;

      static inline float CalcCoeff(float cutoffHz, double sampleRate)
      {
         if (sampleRate <= 0.0) return 1.0f;
         const float fc = std::clamp(cutoffHz, 10.0f, (float)(sampleRate * 0.49));
         return 1.0f - expf(-2.0f * (float)M_PI * fc / (float)sampleRate);
      }

      void SetCoeff(float coeff) { a = coeff; }

      void SetCutoff(float cutoffHz, double sampleRate)
      {
         a = CalcCoeff(cutoffHz, sampleRate);
      }

      void Reset() { z1 = 0.0f; }

      inline float Process(float in)
      {
         z1 += a * (in - z1);
         z1 = DspMath::FlushDenormal(z1);
         return z1;
      }

      inline float Process(float in, float coeff)
      {
         z1 += coeff * (in - z1);
         z1 = DspMath::FlushDenormal(z1);
         return z1;
      }
   };

   // 1-pole Highpass Filter
   struct OnePoleHP
   {
      float z1 = 0.0f;
      float a = 0.0f;

      void SetCutoff(float cutoffHz, double sampleRate)
      {
         if (sampleRate <= 0.0) return;
         const float fc = std::clamp(cutoffHz, 10.0f, (float)(sampleRate * 0.49));
         a = expf(-2.0f * (float)M_PI * fc / (float)sampleRate);
      }

      void Reset() { z1 = 0.0f; }

      inline float Process(float in)
      {
         const float hp = in - z1;
         z1 = in - a * hp;
         z1 = DspMath::FlushDenormal(z1);
         return hp;
      }
   };

   // Diode bridge forward-voltage crossover dead-zone non-linearity
   inline float DiodeDeadZone(float x, float vf = 0.12f)
   {
      const float absX = std::fabs(x);
      if (absX <= vf)
      {
         // Smooth cubic transition through the dead-zone
         const float norm = x / (vf > 1.0e-5f ? vf : 1.0e-5f);
         return norm * norm * norm * vf * (1.0f / 3.0f);
      }
      return x > 0.0f ? (x - 2.0f / 3.0f * vf) : (x + 2.0f / 3.0f * vf);
   }

   // mu-Law non-uniform companding quantization for vintage DAC/sampler emulation
   inline float MuLawQuantize(float x, float bits, float mu = 255.0f)
   {
      const float clamped = std::clamp(x, -1.0f, 1.0f);
      const float sgn = clamped < 0.0f ? -1.0f : 1.0f;
      const float absX = std::fabs(clamped);

      // Compress: y = sgn * ln(1 + mu*|x|) / ln(1 + mu)
      const float logMu = logf(1.0f + mu);
      const float y = (logf(1.0f + mu * absX) / logMu);

      // Uniform quantization in compressed space
      const float levels = powf(2.0f, std::clamp(bits, 1.0f, 16.0f));
      const float steps = levels * 0.5f;
      const float yQuant = std::round(y * steps) / steps;

      // Expand: x_recon = sgn * ((1 + mu)^|yQuant| - 1) / mu
      const float xRecon = sgn * (powf(1.0f + mu, yQuant) - 1.0f) / mu;
      return xRecon;
   }

   // Slow LFO with secondary rate drift for wow & flutter / BBD clock drift
   struct DriftLfo
   {
      float phase = 0.0f;
      float driftPhase = 0.0f;

      void Reset(float initPhase = 0.0f, float initDrift = 0.0f)
      {
         phase = initPhase;
         driftPhase = initDrift;
      }

      inline float Advance(float baseRateHz, float driftDepth, float driftRateHz, double sampleRate)
      {
         if (sampleRate <= 0.0) return sinf(phase * 2.0f * (float)M_PI);
         const float drift = sinf(driftPhase * 2.0f * (float)M_PI);
         const float actualRate = std::max(0.001f, baseRateHz * (1.0f + drift * driftDepth));
         phase += actualRate / (float)sampleRate;
         if (phase >= 1.0f) phase -= floorf(phase);
         driftPhase += driftRateHz / (float)sampleRate;
         if (driftPhase >= 1.0f) driftPhase -= floorf(driftPhase);
         return sinf(phase * 2.0f * (float)M_PI);
      }
   };
}
