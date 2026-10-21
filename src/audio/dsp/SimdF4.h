#pragma once

#include <atomic>
#include <cmath>
#include <cstring>

// Four floats in one register, for running four synth voices at once (2.16).
// NEON on arm64 (macOS, Linux arm), SSE2 on x86-64 (Windows, Linux), plain
// arrays anywhere else. Every operation is the lane-wise IEEE single op the
// scalar code does - no fused multiply-add, no approximate reciprocal - so a
// lane computes what the scalar voice would have, and the lane and scalar
// paths agree far below audibility (SYNTHSIMDTEST holds them to -120 dB).
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#define INFINITE_F4_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define INFINITE_F4_SSE 1
#endif

namespace VoiceSimd
{
   // Tests only: run the scalar voice path even where lanes are available.
   inline std::atomic<bool>& ForceScalar()
   {
      static std::atomic<bool> sForce { false };
      return sForce;
   }

   struct F4
   {
#if defined(INFINITE_F4_NEON)
      float32x4_t v;
      F4() : v(vdupq_n_f32(0.0f)) {}
      F4(float32x4_t x) : v(x) {}
      explicit F4(float x) : v(vdupq_n_f32(x)) {}
      static F4 Load(const float* p) { return F4(vld1q_f32(p)); }
      void Store(float* p) const { vst1q_f32(p, v); }
      friend F4 operator+(F4 a, F4 b) { return F4(vaddq_f32(a.v, b.v)); }
      friend F4 operator-(F4 a, F4 b) { return F4(vsubq_f32(a.v, b.v)); }
      friend F4 operator*(F4 a, F4 b) { return F4(vmulq_f32(a.v, b.v)); }
      friend F4 operator/(F4 a, F4 b) { return F4(vdivq_f32(a.v, b.v)); }
      friend F4 Min(F4 a, F4 b) { return F4(vminq_f32(a.v, b.v)); }
      friend F4 Max(F4 a, F4 b) { return F4(vmaxq_f32(a.v, b.v)); }
      friend F4 Abs(F4 a) { return F4(vabsq_f32(a.v)); }
      // Lane-wise a < b ? x : y
      friend F4 SelectLess(F4 a, F4 b, F4 x, F4 y) { return F4(vbslq_f32(vcltq_f32(a.v, b.v), x.v, y.v)); }
#elif defined(INFINITE_F4_SSE)
      __m128 v;
      F4() : v(_mm_setzero_ps()) {}
      F4(__m128 x) : v(x) {}
      explicit F4(float x) : v(_mm_set1_ps(x)) {}
      static F4 Load(const float* p) { return F4(_mm_loadu_ps(p)); }
      void Store(float* p) const { _mm_storeu_ps(p, v); }
      friend F4 operator+(F4 a, F4 b) { return F4(_mm_add_ps(a.v, b.v)); }
      friend F4 operator-(F4 a, F4 b) { return F4(_mm_sub_ps(a.v, b.v)); }
      friend F4 operator*(F4 a, F4 b) { return F4(_mm_mul_ps(a.v, b.v)); }
      friend F4 operator/(F4 a, F4 b) { return F4(_mm_div_ps(a.v, b.v)); }
      // Scalar std::min(a, b) is b < a ? b : a and std::max(a, b) is a < b ? b : a;
      // _mm_min_ps(b, a) / _mm_max_ps(b, a) return the second operand on a tie
      // or NaN the same way.
      friend F4 Min(F4 a, F4 b) { return F4(_mm_min_ps(b.v, a.v)); }
      friend F4 Max(F4 a, F4 b) { return F4(_mm_max_ps(b.v, a.v)); }
      friend F4 Abs(F4 a) { return F4(_mm_andnot_ps(_mm_set1_ps(-0.0f), a.v)); }
      friend F4 SelectLess(F4 a, F4 b, F4 x, F4 y)
      {
         const __m128 m = _mm_cmplt_ps(a.v, b.v);
         return F4(_mm_or_ps(_mm_and_ps(m, x.v), _mm_andnot_ps(m, y.v)));
      }
#else
      float v[4];
      F4() : v { 0.0f, 0.0f, 0.0f, 0.0f } {}
      explicit F4(float x) : v { x, x, x, x } {}
      static F4 Load(const float* p)
      {
         F4 r;
         std::memcpy(r.v, p, sizeof r.v);
         return r;
      }
      void Store(float* p) const { std::memcpy(p, v, sizeof v); }
      template <class Op>
      static F4 Map(F4 a, F4 b, Op op)
      {
         F4 r;
         for (int i = 0; i < 4; i++)
            r.v[i] = op(a.v[i], b.v[i]);
         return r;
      }
      friend F4 operator+(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x + y; }); }
      friend F4 operator-(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x - y; }); }
      friend F4 operator*(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x * y; }); }
      friend F4 operator/(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x / y; }); }
      friend F4 Min(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return y < x ? y : x; }); }
      friend F4 Max(F4 a, F4 b) { return Map(a, b, [](float x, float y) { return x < y ? y : x; }); }
      friend F4 Abs(F4 a)
      {
         F4 r;
         for (int i = 0; i < 4; i++)
            r.v[i] = std::fabs(a.v[i]);
         return r;
      }
      friend F4 SelectLess(F4 a, F4 b, F4 x, F4 y)
      {
         F4 r;
         for (int i = 0; i < 4; i++)
            r.v[i] = a.v[i] < b.v[i] ? x.v[i] : y.v[i];
         return r;
      }
#endif
      F4& operator+=(F4 b) { return *this = *this + b; }
      F4& operator-=(F4 b) { return *this = *this - b; }
      F4& operator*=(F4 b) { return *this = *this * b; }
      friend F4 Clamp(F4 x, F4 lo, F4 hi) { return Min(Max(x, lo), hi); } // std::clamp(x, lo, hi) for lo <= hi
   };

   // DspMath::FastTanh and DspMath::FlushDenormal, lane-wise.
   inline F4 FastTanh(F4 x)
   {
      // DspMath::FastTanh's +-3 clamp: the rational form is exactly +-1 at 3.
      const F4 c = Clamp(x, F4(-3.0f), F4(3.0f));
      const F4 x2 = c * c;
      return c * (F4(27.0f) + x2) / (F4(27.0f) + F4(9.0f) * x2);
   }

   inline F4 FlushDenormal(F4 x) { return SelectLess(Abs(x), F4(1.0e-20f), F4(0.0f), x); }
}
