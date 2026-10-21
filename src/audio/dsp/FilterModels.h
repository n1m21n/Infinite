#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

#include "audio/DspMath.h"
#include "audio/FpContract.h"
#include "audio/dsp/SimdF4.h"

// The analogue filter models behind Audio Filter's and Analog's model
// choices (v0.6 2.25): the Moog ladder, the MS-20 style Sallen-Key and the
// diode ladder. Every model is zero-delay feedback. Each stage's
// nonlinearity is linearised at its previous sample's value (tanh(v) ~=
// v * T(v'), T(v) = tanh(v) / v), so the stages are a linear system solved
// outright; the one loop nonlinearity left is solved by Newton's method
// (SolveLimiter).
//
// Integrators are trapezoidal (TPT, Zavalishin, "The Art of VA Filter
// Design", ch. 3). Each stage is an OTA-style one-pole that saturates on the
// difference it integrates, y' = wc * h * tanh((in - y) / h): DC gain stays
// exactly 1 at any level, small signals see the linear filter, and drive
// pushes the stages into compression. The loop that makes the filter ring
// has its own limiter, fb = L * tanh(fb_in / L), which is what bounds
// self-oscillation. A limiter changes gain only, not phase, so a filter
// rings at its cutoff however hard it oscillates; the stage headroom h is set
// above the oscillation's level for the same reason (a compressed stage is a
// slower stage, and it would pull the pitch flat).
//
// Cutoff: a model whose analogue prototype oscillates at m * wc (m = 1 for
// the ladder and MS-20, 0.7265 for the diode ladder) takes
// g = tan(pi fc / fs) / m, which puts the bilinear-transformed oscillation
// exactly on fc. The knob means "the pitch it sings at" for every model.
//
// Templated on the sample type: float for one voice, VoiceSimd::F4 for four
// (Analog's voice lanes, 2.16). The two run the same operations in the same
// order, so a lane computes what the scalar voice would.
namespace FilterModels
{
   using VoiceSimd::F4;

   enum Model
   {
      kModelLadder = 0, // Moog 4-pole transistor ladder
      kModelMs20,       // MS-20 style 2-pole Sallen-Key, positive feedback through a high-pass
      kModelDiode,      // TB-303 style 4-pole diode ladder, coupled stages
      kNumModels
   };

   // The analogue prototype's oscillation frequency over wc, and the loop
   // gain k at which it starts to oscillate. Diode: the linear system below
   // solved for the frequency where the open loop's phase is -180 degrees,
   // k = -1 / Re H(j m) there (bisection, 60 steps).
   constexpr float kOscRatio[kNumModels] = { 1.0f, 1.0f, 0.726483157f };
   constexpr float kCriticalK[kNumModels] = { 4.0f, 2.0f, 22.1049383f };

   // Loop gain for a 0..~1.05 resonance (1 = the self-oscillation threshold).
   inline float KFromResonance(int model, float res)
   {
      return kCriticalK[std::clamp(model, 0, kNumModels - 1)] * std::max(0.0f, res);
   }

   // Passband make-up gain. The diode ladder's own DC gain is 1 / (1 + 22
   // res), -27 dB at full resonance; this brings it to the ladder's
   // 1 / (1 + 4 res), so switching between the two keeps the level. The
   // MS-20's DC gain is 1 at any resonance (its feedback is high-passed).
   inline float MakeupGain(int model, float res)
   {
      res = std::max(0.0f, res);
      if (model == kModelDiode)
         return (1.0f + kCriticalK[kModelDiode] * res) / (1.0f + 4.0f * res);
      return 1.0f;
   }

   // g for a cutoff in Hz at the rate the filter runs at. `limitHz` is the
   // highest cutoff allowed (below the base rate's Nyquist when oversampled).
   inline float CutoffToG(int model, float cutoffHz, double rate, float limitHz)
   {
      if (rate <= 0.0)
         return 0.0f;
      const float fc = std::clamp(cutoffHz, 10.0f, std::min(limitHz, (float)rate * 0.45f));
      const float m = kOscRatio[std::clamp(model, 0, kNumModels - 1)];
      return std::clamp(tanf((float)M_PI * fc / (float)rate) / m, 0.0001f, 8.0f);
   }

   // The linear (small-signal) response at normalised frequency W = w / wc,
   // with loop gain k: what the filter does below saturation, for a response
   // curve. Ladder 1 / ((1 + s)^4 + k); MS-20 1 / (s^2 + (2 - k) s + 1);
   // diode G / (1 + k G), G = y4 / u solved up the chain from y4 = 1.
   inline std::complex<double> LinearResponse(int model, double W, double k)
   {
      const std::complex<double> s(0.0, W);
      if (model == kModelMs20)
         return 1.0 / (s * s + (2.0 - k) * s + 1.0);
      if (model == kModelDiode)
      {
         const std::complex<double> y4 = 1.0;
         const std::complex<double> y3 = (2.0 * s + 1.0) * y4;
         const std::complex<double> y2 = (2.0 * s + 2.0) * y3 - y4;
         const std::complex<double> y1 = (2.0 * s + 2.0) * y2 - y3;
         const std::complex<double> G = 1.0 / ((s + 2.0) * y1 - y2);
         return G / (1.0 + k * G);
      }
      const std::complex<double> p = (1.0 + s) * (1.0 + s);
      return 1.0 / (p * p + k);
   }

   // --------------------------------------------------------- lane helpers
   inline float Lit(float, float c) { return c; }
   inline F4 Lit(F4, float c) { return F4(c); }

   inline float Slope(float v)
   {
      INFINITE_NO_FMA
      return std::fabs(v) < 1.0e-4f ? 1.0f : DspMath::FastTanh(v) / v;
   }
   inline F4 Slope(F4 v) { return SelectLess(Abs(v), F4(1.0e-4f), F4(1.0f), VoiceSimd::FastTanh(v) / v); }

   inline float Tanh(float v) { return DspMath::FastTanh(v); }
   inline F4 Tanh(F4 v) { return VoiceSimd::FastTanh(v); }
   inline float ClampTanhArg(float v) { return std::clamp(v, -3.0f, 3.0f); }
   inline F4 ClampTanhArg(F4 v) { return Clamp(v, F4(-3.0f), F4(3.0f)); }

   inline float Flush(float x) { return DspMath::FlushDenormal(x); }
   inline F4 Flush(F4 x) { return VoiceSimd::FlushDenormal(x); }

   // Zeroes one lane of a four-voice state (a voice starting a note).
   inline void ZeroLane(F4& v, int l)
   {
      alignas(16) float t[4];
      v.Store(t);
      t[l] = 0.0f;
      v = F4::Load(t);
   }

   // d/dv FastTanh(v) = ((9 - c^2) / (3 (3 + c^2)))^2, c = v clamped to +-3
   // (zero outside, where FastTanh is flat).
   template <class T>
   inline T TanhDeriv(T v)
   {
      INFINITE_NO_FMA
      const T c = ClampTanhArg(v);
      const T c2 = c * c;
      const T r = (Lit(v, 9.0f) - c2) / (Lit(v, 3.0f) * (Lit(v, 3.0f) + c2));
      return r * r;
   }

   // Solves z = C + E tanh(z) by Newton's method from `guess`. This is the
   // loop's limiter, solved instead of linearised: a slope taken from the
   // previous sample is a one-sample-late gain, and in a ringing loop that
   // reads as phase - it pulled the oscillation up to 34 c sharp at 10 kHz
   // and 96 kHz. f(z) = z - C - E tanh(z) has f' = 1 - E tanh'(z) >= 1 - E,
   // positive for every E < 1 (negative feedback has E < 0), so it is
   // monotone and three steps from the previous sample's root land within
   // float rounding.
   template <class T>
   inline T SolveLimiter(T C, T E, T guess)
   {
      INFINITE_NO_FMA
      T z = guess;
      for (int it = 0; it < 3; it++)
      {
         const T f = z - C - E * Tanh(z);
         const T fp = Lit(z, 1.0f) - E * TanhDeriv(z);
         z = z - f / fp;
      }
      return z;
   }

   // ------------------------------------------------------------ ladder
   // Four OTA one-poles in series, the fourth's output fed back negatively
   // to the input through the limiter:
   //   y_i' = wc D(y_{i-1} - y_i),  y_{-1} = u = x - k tanh(y_3),
   // linear H(s) = 1 / ((1 + s)^4 + k), oscillating at k = 4, s = j. With
   // the stage slopes held, y_3 = A u + B; with u = x - k tanh(y_3) that is
   // y_3 = (A x + B) - A k tanh(y_3), solved by SolveLimiter.
   template <class T>
   struct Ladder
   {
      static constexpr float kStageHeadroom = 16.0f;

      T s[4] {};
      T d[4] {};
      T y3 {};

      void Reset()
      {
         for (int i = 0; i < 4; i++)
            s[i] = d[i] = T {};
         y3 = T {};
      }
      void ResetLane(int l)
      {
         for (int i = 0; i < 4; i++)
         {
            ZeroLane(s[i], l);
            ZeroLane(d[i], l);
         }
         ZeroLane(y3, l);
      }

      T Process(T x, T g, T k)
      {
         INFINITE_NO_FMA
         const T one = Lit(x, 1.0f);
         const T invH = Lit(x, 1.0f / kStageHeadroom);
         T a[4], b[4];
         for (int i = 0; i < 4; i++)
         {
            const T gt = g * Slope(d[i] * invH);
            const T den = one / (one + gt);
            a[i] = gt * den;
            b[i] = s[i] * den;
         }
         T A = a[0], B = b[0];
         for (int i = 1; i < 4; i++)
         {
            B = a[i] * B + b[i];
            A = a[i] * A;
         }
         const T out = SolveLimiter(A * x + B, Lit(x, 0.0f) - A * k, y3);
         const T u = x - k * Tanh(out);
         T prev = u;
         for (int i = 0; i < 4; i++)
         {
            const T y = a[i] * prev + b[i];
            d[i] = prev - y;
            s[i] = Flush(Lit(x, 2.0f) * y - s[i]);
            prev = y;
         }
         y3 = prev;
         return prev;
      }
   };

   // ------------------------------------------------------------ MS-20
   // Two OTA one-poles in series; the output, high-passed by a third
   // (linear) one-pole, is fed back positively into the second stage's input
   // through the limiter (the MS-20's diode clipper):
   //   y = L(L(x) + k tanh(HP(y)))  =>  linear H(s) = 1 / (s^2 + (2 - k) s + 1),
   // oscillating at k = 2, s = j. With the stage slopes held, hp = C + D k
   // tanh(hp), and D = (stage gain) (high-pass gain) <= g / (1 + g)^2 <= 1/4,
   // so for k below 4 the limiter's equation stays monotone.
   template <class T>
   struct Ms20
   {
      static constexpr float kStageHeadroom = 16.0f;

      T s1 {}, s2 {}, s3 {};
      T d1 {}, d2 {}, hp {};

      void Reset() { s1 = s2 = s3 = d1 = d2 = hp = T {}; }
      void ResetLane(int l)
      {
         for (T* v : { &s1, &s2, &s3, &d1, &d2, &hp })
            ZeroLane(*v, l);
      }

      T Process(T x, T g, T k)
      {
         INFINITE_NO_FMA
         const T one = Lit(x, 1.0f);
         const T invH = Lit(x, 1.0f / kStageHeadroom);
         const T gt1 = g * Slope(d1 * invH);
         const T gt2 = g * Slope(d2 * invH);

         const T y1 = (s1 + gt1 * x) / (one + gt1);
         // hp = y2 - lp3, lp3 = (s3 + g y2) / (1 + g)  =>  hp = gamma y2 - gamma s3.
         const T gamma = one / (one + g);
         // y2 = a2 v + b2, v = y1 + k tanh(hp).
         const T den2 = one / (one + gt2);
         const T a2 = gt2 * den2;
         const T b2 = s2 * den2;
         const T C = gamma * (a2 * y1 + b2) - gamma * s3;
         const T D = gamma * a2;
         const T h = SolveLimiter(C, D * k, hp);

         const T v = y1 + k * Tanh(h);
         const T y2 = a2 * v + b2;
         const T lp3 = y2 - h;
         s1 = Flush(Lit(x, 2.0f) * y1 - s1);
         s2 = Flush(Lit(x, 2.0f) * y2 - s2);
         s3 = Flush(Lit(x, 2.0f) * lp3 - s3);
         d1 = x - y1;
         d2 = v - y2;
         hp = h;
         return y2;
      }
   };

   // ------------------------------------------------------- diode ladder
   // Four capacitors coupled through diode pairs, so each stage loads its
   // neighbours (Zavalishin, "The Art of VA Filter Design", 2nd ed., 5.10):
   //   y1' = wc (D(u - y1) - D(y1 - y2))
   //   y2' = wc (D(y1 - y2) - D(y2 - y3)) / 2
   //   y3' = wc (D(y2 - y3) - D(y3 - y4)) / 2
   //   y4' = wc  D(y3 - y4) / 2,          u = x - k tanh(y4)
   // With the slopes held, one sample is a tridiagonal system in y1..y4 plus
   // the loop through u. The Thomas algorithm runs with two right-hand sides
   // (the states, and the column u multiplies), so y4 comes out as P + Q u;
   // the loop is then y4 = (P + Q x) - Q k tanh(y4), solved by SolveLimiter,
   // and the back substitution uses the real u.
   template <class T>
   struct Diode
   {
      static constexpr float kStageHeadroom = 64.0f;

      T s[4] {};
      T d[4] {};
      T y4 {};

      void Reset()
      {
         for (int i = 0; i < 4; i++)
            s[i] = d[i] = T {};
         y4 = T {};
      }
      void ResetLane(int l)
      {
         for (int i = 0; i < 4; i++)
         {
            ZeroLane(s[i], l);
            ZeroLane(d[i], l);
         }
         ZeroLane(y4, l);
      }

      T Process(T x, T g, T k)
      {
         INFINITE_NO_FMA
         const T one = Lit(x, 1.0f);
         const T zero = Lit(x, 0.0f);
         const T invH = Lit(x, 1.0f / kStageHeadroom);
         T a[4];
         for (int i = 0; i < 4; i++)
            a[i] = Slope(d[i] * invH);
         const T hg = g * Lit(x, 0.5f);

         // Row i: lower[i] y_{i-1} + diag[i] y_i + upper[i] y_{i+1} = s_i (+ g a0 u on row 0).
         const T diag[4] = { one + g * (a[0] + a[1]), one + hg * (a[1] + a[2]), one + hg * (a[2] + a[3]),
                             one + hg * a[3] };
         const T lower[4] = { zero, zero - hg * a[1], zero - hg * a[2], zero - hg * a[3] };
         const T upper[4] = { zero - g * a[1], zero - hg * a[2], zero - hg * a[3], zero };

         T cp[4], ds[4], du[4];
         {
            const T inv = one / diag[0];
            cp[0] = upper[0] * inv;
            ds[0] = s[0] * inv;
            du[0] = g * a[0] * inv;
         }
         for (int i = 1; i < 4; i++)
         {
            const T inv = one / (diag[i] - lower[i] * cp[i - 1]);
            cp[i] = upper[i] * inv;
            ds[i] = (s[i] - lower[i] * ds[i - 1]) * inv;
            du[i] = (zero - lower[i] * du[i - 1]) * inv;
         }

         const T out = SolveLimiter(ds[3] + du[3] * x, zero - du[3] * k, y4);
         const T u = x - k * Tanh(out);

         T y[4];
         y[3] = ds[3] + u * du[3];
         for (int i = 2; i >= 0; i--)
            y[i] = ds[i] + u * du[i] - cp[i] * y[i + 1];

         for (int i = 0; i < 4; i++)
            s[i] = Flush(Lit(x, 2.0f) * y[i] - s[i]);
         d[0] = u - y[0];
         d[1] = y[0] - y[1];
         d[2] = y[1] - y[2];
         d[3] = y[2] - y[3];
         y4 = y[3];
         return y[3];
      }
   };

   // One of each model's state, for a filter whose model is a setting: the
   // unused ones stay at rest, so switching model starts that one from
   // silence.
   template <class T>
   struct Bank
   {
      Ladder<T> ladder;
      Ms20<T> ms20;
      Diode<T> diode;

      void Reset()
      {
         ladder.Reset();
         ms20.Reset();
         diode.Reset();
      }
      void ResetLane(int l)
      {
         ladder.ResetLane(l);
         ms20.ResetLane(l);
         diode.ResetLane(l);
      }

      T Process(int model, T x, T g, T k)
      {
         switch (model)
         {
            case kModelMs20: return ms20.Process(x, g, k);
            case kModelDiode: return diode.Process(x, g, k);
            default: return ladder.Process(x, g, k);
         }
      }
   };
}
