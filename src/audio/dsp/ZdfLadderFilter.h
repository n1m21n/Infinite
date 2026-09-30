#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "audio/DspMath.h"

// Non-linear Zero-Delay-Feedback (ZDF) Moog ladder filter.
//
// Reference:
// Antti Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder
// Filter", DAFx 2004.
//
// Four cascaded one-pole stages with tanh saturating nonlinearity per stage,
// wrapped in a zero-delay feedback loop. Solved with predictor-corrector
// iterations per sample. High resonance produces bounded limit-cycle
// self-oscillation at the cutoff frequency.
struct ZdfLadderFilter
{
   struct State
   {
      float stage[4] = {};
      float stageTanh[4] = {};

      void Reset()
      {
         for (int i = 0; i < 4; ++i)
         {
            stage[i] = 0.0f;
            stageTanh[i] = 0.0f;
         }
      }
   };

   // Computes the cutoff coefficient g from cutoff frequency in Hz and sample rate.
   // Uses standard bilinear/tangent prewarping clamped safely below Nyquist.
   static inline float CutoffToG(float cutoffHz, double sampleRate)
   {
      if (sampleRate <= 0.0)
         return 0.0f;
      const float nyquistLimit = (float)sampleRate * 0.45f;
      const float fc = std::clamp(cutoffHz, 10.0f, nyquistLimit);
      const float w = (float)M_PI * fc / (float)sampleRate;
      // Prewarped tangent mapping, clamped so g stays well-conditioned
      return std::clamp(tanf(w), 0.0001f, 3.0f);
   }

   // Processes a single sample through the 4-stage ZDF ladder filter.
   // input: audio sample (typically in [-1, 1], optionally saturated by pre-filter drive)
   // g: cutoff coefficient from CutoffToG
   // k: feedback resonance amount in [0, 4.0]
   // iterations: predictor-corrector iterations (default 2)
   static inline float Process(State& s, float input, float g, float k, int iterations = 2)
   {
      // Clamp resonance feedback to prevent unconstrained numerical blow-up
      // while allowing rich self-oscillation up to k ~ 3.98
      const float clampedK = std::clamp(k, 0.0f, 3.98f);
      const float h = g / (1.0f + g);

      // Predictor guess for y3 based on previous stage state
      float y3 = s.stage[3];
      float y0 = 0.0f, y1 = 0.0f, y2 = 0.0f;

      const int iters = std::max(1, iterations);
      for (int iter = 0; iter < iters; ++iter)
      {
         // Loop feedback saturation
         const float u = DspMath::FastTanh(input - clampedK * y3);

         // Stage 0
         y0 = h * (u - s.stageTanh[0]) + s.stage[0];
         const float tanh0 = DspMath::FastTanh(y0);

         // Stage 1
         y1 = h * (tanh0 - s.stageTanh[1]) + s.stage[1];
         const float tanh1 = DspMath::FastTanh(y1);

         // Stage 2
         y2 = h * (tanh1 - s.stageTanh[2]) + s.stage[2];
         const float tanh2 = DspMath::FastTanh(y2);

         // Stage 3 (output)
         y3 = h * (tanh2 - s.stageTanh[3]) + s.stage[3];
      }

      // Update states using trapezoidal integration
      const float uFinal = DspMath::FastTanh(input - clampedK * y3);
      const float tanh0 = DspMath::FastTanh(y0);
      const float tanh1 = DspMath::FastTanh(y1);
      const float tanh2 = DspMath::FastTanh(y2);
      const float tanh3 = DspMath::FastTanh(y3);

      s.stage[0] = DspMath::FlushDenormal(2.0f * y0 - s.stage[0]);
      s.stage[1] = DspMath::FlushDenormal(2.0f * y1 - s.stage[1]);
      s.stage[2] = DspMath::FlushDenormal(2.0f * y2 - s.stage[2]);
      s.stage[3] = DspMath::FlushDenormal(2.0f * y3 - s.stage[3]);

      s.stageTanh[0] = tanh0;
      s.stageTanh[1] = tanh1;
      s.stageTanh[2] = tanh2;
      s.stageTanh[3] = tanh3;

      return y3;
   }
};
