// Frozen copy of ReverbKernel::ProcessBlockSimd as it was before the Block 1
// perf work (docs/plans/perf/README.md, main 2324f75). Test-only: DSPTEST
// renders the live kernel and this one side by side, so every optimisation
// of the live kernel is checked against the sound it replaced - bit-exact
// for the exact changes, within -80 dBFS for the approximate ones. Do not
// edit or optimise this file; it is the reference, not a second kernel.
#include "ReverbKernel.h"

#include "nodes/AudioEffectNode.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define REVERB_LEGACY_SIMD_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define REVERB_LEGACY_SIMD_SSE 1
#endif

namespace
{
#if defined(REVERB_LEGACY_SIMD_NEON)
inline float32x4_t FlushDenormal_Neon(float32x4_t v)
{
   const float32x4_t thresh = vdupq_n_f32(1.0e-15f);
   const float32x4_t zero = vdupq_n_f32(0.0f);
   uint32x4_t mask = vcageq_f32(v, thresh);
   return vbslq_f32(mask, v, zero);
}

inline float32x4_t Hermite4_Neon(float32x4_t xm1, float32x4_t x0, float32x4_t x1, float32x4_t x2, float32x4_t frac)
{
   const float32x4_t half = vdupq_n_f32(0.5f);
   const float32x4_t one_half = vdupq_n_f32(1.5f);
   const float32x4_t two = vdupq_n_f32(2.0f);
   const float32x4_t two_half = vdupq_n_f32(2.5f);

   const float32x4_t c0 = x0;
   const float32x4_t c1 = vmulq_f32(half, vsubq_f32(x1, xm1));
   float32x4_t c2 = vsubq_f32(xm1, vmulq_f32(two_half, x0));
   c2 = vmlaq_f32(c2, two, x1);
   c2 = vmlsq_f32(c2, half, x2);
   const float32x4_t c3 = vaddq_f32(vmulq_f32(half, vsubq_f32(x2, xm1)), vmulq_f32(one_half, vsubq_f32(x0, x1)));
   float32x4_t res = vmlaq_f32(c2, c3, frac);
   res = vmlaq_f32(c1, res, frac);
   res = vmlaq_f32(c0, res, frac);
   return res;
}

inline void Transpose4x4_Neon(float32x4_t& r0, float32x4_t& r1, float32x4_t& r2, float32x4_t& r3)
{
   float32x4x2_t t0 = vzipq_f32(r0, r2);
   float32x4x2_t t1 = vzipq_f32(r1, r3);
   float32x4x2_t o0 = vzipq_f32(t0.val[0], t1.val[0]);
   float32x4x2_t o1 = vzipq_f32(t0.val[1], t1.val[1]);
   r0 = o0.val[0];
   r1 = o0.val[1];
   r2 = o1.val[0];
   r3 = o1.val[1];
}

inline float32x4_t Hadamard4_Neon(float32x4_t v)
{
   float32x2_t lo = vget_low_f32(v);
   float32x2_t hi = vget_high_f32(v);
   float32x2_t s1 = vadd_f32(lo, hi);
   float32x2_t s2 = vsub_f32(lo, hi);

   float32x2_t s1_rev = vrev64_f32(s1);
   float32x2_t s2_rev = vrev64_f32(s2);

   float32x2_t r1_add = vadd_f32(s1, s1_rev);
   float32x2_t r1_sub = vsub_f32(s1, s1_rev);
   float32x2_t r1 = vzip1_f32(r1_add, r1_sub);

   float32x2_t r2_add = vadd_f32(s2, s2_rev);
   float32x2_t r2_sub = vsub_f32(s2, s2_rev);
   float32x2_t r2 = vzip1_f32(r2_add, r2_sub);

   return vcombine_f32(r1, r2);
}

inline void Hadamard16_Neon(float32x4_t& v0, float32x4_t& v1, float32x4_t& v2, float32x4_t& v3)
{
   v0 = Hadamard4_Neon(v0);
   v1 = Hadamard4_Neon(v1);
   v2 = Hadamard4_Neon(v2);
   v3 = Hadamard4_Neon(v3);

   float32x4_t t0 = vaddq_f32(v0, v1);
   float32x4_t t1 = vsubq_f32(v0, v1);
   float32x4_t t2 = vaddq_f32(v2, v3);
   float32x4_t t3 = vsubq_f32(v2, v3);

   float32x4_t r0 = vaddq_f32(t0, t2);
   float32x4_t r2 = vsubq_f32(t0, t2);
   float32x4_t r1 = vaddq_f32(t1, t3);
   float32x4_t r3 = vsubq_f32(t1, t3);

   const float32x4_t norm = vdupq_n_f32(0.25f);
   v0 = vmulq_f32(r0, norm);
   v1 = vmulq_f32(r1, norm);
   v2 = vmulq_f32(r2, norm);
   v3 = vmulq_f32(r3, norm);
}
#endif

#if defined(REVERB_LEGACY_SIMD_SSE)
inline __m128 FlushDenormal_Sse(__m128 v)
{
   const __m128 thresh = _mm_set1_ps(1.0e-15f);
   const __m128 neg_thresh = _mm_set1_ps(-1.0e-15f);
   __m128 mask_gt = _mm_cmpgt_ps(v, thresh);
   __m128 mask_lt = _mm_cmplt_ps(v, neg_thresh);
   __m128 mask = _mm_or_ps(mask_gt, mask_lt);
   return _mm_and_ps(v, mask);
}

inline __m128 Hermite4_Sse(__m128 xm1, __m128 x0, __m128 x1, __m128 x2, __m128 frac)
{
   const __m128 half = _mm_set1_ps(0.5f);
   const __m128 one_half = _mm_set1_ps(1.5f);
   const __m128 two = _mm_set1_ps(2.0f);
   const __m128 two_half = _mm_set1_ps(2.5f);

   const __m128 c0 = x0;
   const __m128 c1 = _mm_mul_ps(half, _mm_sub_ps(x1, xm1));
   __m128 c2 = _mm_sub_ps(xm1, _mm_mul_ps(two_half, x0));
   c2 = _mm_add_ps(c2, _mm_mul_ps(two, x1));
   c2 = _mm_sub_ps(c2, _mm_mul_ps(half, x2));
   const __m128 c3 = _mm_add_ps(_mm_mul_ps(half, _mm_sub_ps(x2, xm1)), _mm_mul_ps(one_half, _mm_sub_ps(x0, x1)));
   __m128 res = _mm_add_ps(_mm_mul_ps(c3, frac), c2);
   res = _mm_add_ps(_mm_mul_ps(res, frac), c1);
   res = _mm_add_ps(_mm_mul_ps(res, frac), c0);
   return res;
}

inline __m128 Hadamard4_Sse(__m128 v)
{
   __m128 swapped = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 3, 0, 1));
   __m128 add = _mm_add_ps(v, swapped);
   __m128 sub = _mm_sub_ps(v, swapped);
   __m128 lo = _mm_unpacklo_ps(add, sub);
   __m128 hi = _mm_unpackhi_ps(add, sub);
   __m128 s = _mm_movelh_ps(lo, hi);

   __m128 s_hi = _mm_movehl_ps(s, s);
   __m128 res_add = _mm_add_ps(s, s_hi);
   __m128 res_sub = _mm_sub_ps(s, s_hi);
   return _mm_movelh_ps(res_add, res_sub);
}

inline void Hadamard16_Sse(__m128& v0, __m128& v1, __m128& v2, __m128& v3)
{
   v0 = Hadamard4_Sse(v0);
   v1 = Hadamard4_Sse(v1);
   v2 = Hadamard4_Sse(v2);
   v3 = Hadamard4_Sse(v3);

   __m128 t0 = _mm_add_ps(v0, v1);
   __m128 t1 = _mm_sub_ps(v0, v1);
   __m128 t2 = _mm_add_ps(v2, v3);
   __m128 t3 = _mm_sub_ps(v2, v3);

   __m128 r0 = _mm_add_ps(t0, t2);
   __m128 r2 = _mm_sub_ps(t0, t2);
   __m128 r1 = _mm_add_ps(t1, t3);
   __m128 r3 = _mm_sub_ps(t1, t3);

   const __m128 norm = _mm_set1_ps(0.25f);
   v0 = _mm_mul_ps(r0, norm);
   v1 = _mm_mul_ps(r1, norm);
   v2 = _mm_mul_ps(r2, norm);
   v3 = _mm_mul_ps(r3, norm);
}
#endif
} // namespace

void ReverbKernel::ProcessBlockLegacy(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   using namespace ReverbDsp;

   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const float rateScale = (float)(mSampleRate / 44100.0);
   const float outScale = 1.0f / std::sqrt((float)kNumLines);
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   float blockDryPeak = 0.0f, blockWetPeak = 0.0f;

   float* lineBufs[kNumLines];
   int lineCapacities[kNumLines];
   int lineWritePos[kNumLines];
   float lineDampStates[kNumLines];

   for (int l = 0; l < kNumLines; l++)
   {
      lineBufs[l] = mLines[l].buf.data();
      lineCapacities[l] = mLines[l].capacity;
      lineWritePos[l] = mLines[l].writePos;
      lineDampStates[l] = mLines[l].dampState;
   }

   float prevSize = -1.0f;
   float prevDecaySeconds = -1.0f;
   int cachedActiveLen[kNumLines];
   float cachedDecayGain[kNumLines];

   for (int i = 0; i < out.numFrames; i++)
   {
      const float size = std::clamp(mMailbox.SmoothedValue(kSize), 0.0f, 1.0f);
      const float decaySeconds = std::max(0.05f, mMailbox.SmoothedValue(kDecaySeconds));
      const float damping = std::clamp(mMailbox.SmoothedValue(kDamping), 0.0f, 1.0f);
      const float predelayMs = std::max(0.0f, mMailbox.SmoothedValue(kPredelayMs));
      const float width = std::clamp(mMailbox.SmoothedValue(kWidth), 0.0f, 1.0f);

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;
      const float inMono = numChannels >= 2 ? 0.5f * (inL + inR) : inL;

      const int predelaySamples = std::clamp((int)std::lround(predelayMs * 0.001f * (float)mSampleRate), 0,
                                              mPredelayCapacity - 1);
      mPredelayL[(size_t)mPredelayWrite] = inL;
      mPredelayR[(size_t)mPredelayWrite] = inR;
      int readPos = mPredelayWrite - predelaySamples;
      readPos %= mPredelayCapacity;
      if (readPos < 0)
         readPos += mPredelayCapacity;
      const float predelayedL = mPredelayL[(size_t)readPos];
      const float predelayedR = mPredelayR[(size_t)readPos];
      mPredelayWrite++;
      if (mPredelayWrite >= mPredelayCapacity)
         mPredelayWrite = 0;

      float diffusedL = predelayedL;
      for (int s = 0; s < kNumDiffusionStages; s++)
         diffusedL = mDiffuserL[s].Process(diffusedL);
      float diffusedR = predelayedR;
      for (int s = 0; s < kNumDiffusionStages; s++)
         diffusedR = mDiffuserR[s].Process(diffusedR);

      diffusedL = mInputLpfL.Process(diffusedL);
      diffusedR = mInputLpfR.Process(diffusedR);

      if (analog)
      {
         diffusedL = AnalogDsp::AsymTanh(diffusedL, 0.12f);
         diffusedR = AnalogDsp::AsymTanh(diffusedR, 0.12f);
         mInputEnv += (std::fabs(inMono) - mInputEnv) * 0.001f;
         mInputEnv = DspMath::FlushDenormal(mInputEnv);
      }

      const float scaleFactor = 0.15f + 0.85f * size;
      const float dynamicAir = analog ? std::clamp(mInputEnv * 4.0f, 0.0f, 1.0f) : 1.0f;
      const float baseCutoff = 18000.0f * std::pow(800.0f / 18000.0f, damping);
      const float cutoffHz = analog ? std::max(600.0f, baseCutoff * (1.0f - 0.20f * (1.0f - dynamicAir))) : baseCutoff;
      const float dampCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * cutoffHz / (float)mSampleRate);
      const float modDepthSamples = analog ? 10.0f : 4.0f;

      if (size != prevSize || decaySeconds != prevDecaySeconds)
      {
         prevSize = size;
         prevDecaySeconds = decaySeconds;
         const float decayExpConst = -3.0f * 2.302585092994046f / ((float)mSampleRate * decaySeconds);
         for (int l = 0; l < kNumLines; l++)
         {
            cachedActiveLen[l] = std::clamp((int)std::lround(kBaseLengths44k[l] * rateScale * scaleFactor), 8, lineCapacities[l] - 32);
            cachedDecayGain[l] = std::exp(decayExpConst * (float)cachedActiveLen[l]);
         }
      }

#if defined(REVERB_LEGACY_SIMD_NEON)
      float32x4_t delayedOut_v[4];
      float32x4_t decayGain_v[4];
      float32x4_t dampState_v[4];

      for (int g = 0; g < 4; g++)
      {
         float32x4_t s_lines[4];
         alignas(16) float frac_arr[4];
         alignas(16) float decay_arr[4];
         alignas(16) float damp_arr[4];

         for (int k = 0; k < 4; k++)
         {
            const int line = (g << 2) + k;
            const int cap = lineCapacities[line];
            const int wp = lineWritePos[line];
            const float* buf = lineBufs[line];
            const int activeLen = cachedActiveLen[line];

            const float lfoVal = mLfo[line].Advance(kLfoRates[line], 0.25f, 0.08f, mSampleRate);
            const float modDelay = std::clamp((float)activeLen + lfoVal * modDepthSamples, 4.0f, (float)(cap - 4));
            const int iDelay = (int)modDelay;
            frac_arr[k] = modDelay - (float)iDelay;

            int p = wp - iDelay;
            if (p >= 3)
            {
               float32x4_t raw = vld1q_f32(&buf[p - 3]);
               s_lines[k] = vcombine_f32(vrev64_f32(vget_high_f32(raw)), vrev64_f32(vget_low_f32(raw)));
            }
            else
            {
               int pm1 = p; if (pm1 < 0) pm1 += cap;
               int p0  = p - 1; if (p0 < 0) p0 += cap;
               int p1  = p - 2; if (p1 < 0) p1 += cap;
               int p2  = p - 3; if (p2 < 0) p2 += cap;
               alignas(16) float s_arr[4] = { buf[pm1], buf[p0], buf[p1], buf[p2] };
               s_lines[k] = vld1q_f32(s_arr);
            }

            decay_arr[k] = cachedDecayGain[line];
            damp_arr[k] = lineDampStates[line];
         }

         Transpose4x4_Neon(s_lines[0], s_lines[1], s_lines[2], s_lines[3]);
         float32x4_t xm1  = s_lines[0];
         float32x4_t x0   = s_lines[1];
         float32x4_t x1   = s_lines[2];
         float32x4_t x2   = s_lines[3];
         float32x4_t frac = vld1q_f32(frac_arr);

         delayedOut_v[g] = Hermite4_Neon(xm1, x0, x1, x2, frac);
         decayGain_v[g] = vld1q_f32(decay_arr);
         dampState_v[g] = vld1q_f32(damp_arr);
      }

      float32x4_t mixed_v[4] = { delayedOut_v[0], delayedOut_v[1], delayedOut_v[2], delayedOut_v[3] };
      Hadamard16_Neon(mixed_v[0], mixed_v[1], mixed_v[2], mixed_v[3]);

      alignas(16) const float side_arr[4] = { diffusedL, diffusedR, diffusedL, diffusedR };
      const float32x4_t side_v = vld1q_f32(side_arr);

      for (int g = 0; g < 4; g++)
      {
         float32x4_t fb = vmulq_f32(mixed_v[g], decayGain_v[g]);
         float32x4_t diff = vsubq_f32(fb, dampState_v[g]);
         dampState_v[g] = vmlaq_n_f32(dampState_v[g], diff, dampCoeff);
         dampState_v[g] = FlushDenormal_Neon(dampState_v[g]);

         float32x4_t inj;
         if (g < 2)
            inj = vmlaq_n_f32(dampState_v[g], side_v, 0.5f);
         else
            inj = vmlsq_n_f32(dampState_v[g], side_v, 0.5f);

         inj = FlushDenormal_Neon(inj);

         alignas(16) float inj_arr[4];
         alignas(16) float damp_arr[4];
         vst1q_f32(inj_arr, inj);
         vst1q_f32(damp_arr, dampState_v[g]);

         for (int k = 0; k < 4; k++)
         {
            const int line = (g << 2) + k;
            float* buf = lineBufs[line];
            int wp = lineWritePos[line];
            buf[wp] = inj_arr[k];
            wp++;
            if (wp >= lineCapacities[line])
               wp = 0;
            lineWritePos[line] = wp;
            lineDampStates[line] = damp_arr[k];
         }
      }

      float32x4_t sum_v = vaddq_f32(vaddq_f32(delayedOut_v[0], delayedOut_v[1]),
                                    vaddq_f32(delayedOut_v[2], delayedOut_v[3]));
      const float sumEven = vgetq_lane_f32(sum_v, 0) + vgetq_lane_f32(sum_v, 2);
      const float sumOdd  = vgetq_lane_f32(sum_v, 1) + vgetq_lane_f32(sum_v, 3);
#elif defined(REVERB_LEGACY_SIMD_SSE)
      __m128 delayedOut_v[4];
      __m128 decayGain_v[4];
      __m128 dampState_v[4];

      for (int g = 0; g < 4; g++)
      {
         __m128 s_lines[4];
         alignas(16) float frac_arr[4];
         alignas(16) float decay_arr[4];
         alignas(16) float damp_arr[4];

         for (int k = 0; k < 4; k++)
         {
            const int line = (g << 2) + k;
            const int cap = lineCapacities[line];
            const int wp = lineWritePos[line];
            const float* buf = lineBufs[line];
            const int activeLen = cachedActiveLen[line];

            const float lfoVal = mLfo[line].Advance(kLfoRates[line], 0.25f, 0.08f, mSampleRate);
            const float modDelay = std::clamp((float)activeLen + lfoVal * modDepthSamples, 4.0f, (float)(cap - 4));
            const int iDelay = (int)modDelay;
            frac_arr[k] = modDelay - (float)iDelay;

            int p = wp - iDelay;
            if (p >= 3)
            {
               __m128 raw = _mm_loadu_ps(&buf[p - 3]);
               s_lines[k] = _mm_shuffle_ps(raw, raw, _MM_SHUFFLE(0, 1, 2, 3));
            }
            else
            {
               int pm1 = p; if (pm1 < 0) pm1 += cap;
               int p0  = p - 1; if (p0 < 0) p0 += cap;
               int p1  = p - 2; if (p1 < 0) p1 += cap;
               int p2  = p - 3; if (p2 < 0) p2 += cap;
               alignas(16) float s_arr[4] = { buf[pm1], buf[p0], buf[p1], buf[p2] };
               s_lines[k] = _mm_load_ps(s_arr);
            }

            decay_arr[k] = cachedDecayGain[line];
            damp_arr[k] = lineDampStates[line];
         }

         _MM_TRANSPOSE4_PS(s_lines[0], s_lines[1], s_lines[2], s_lines[3]);
         __m128 xm1  = s_lines[0];
         __m128 x0   = s_lines[1];
         __m128 x1   = s_lines[2];
         __m128 x2   = s_lines[3];
         __m128 frac = _mm_load_ps(frac_arr);

         delayedOut_v[g] = Hermite4_Sse(xm1, x0, x1, x2, frac);
         decayGain_v[g] = _mm_load_ps(decay_arr);
         dampState_v[g] = _mm_load_ps(damp_arr);
      }

      __m128 mixed_v[4] = { delayedOut_v[0], delayedOut_v[1], delayedOut_v[2], delayedOut_v[3] };
      Hadamard16_Sse(mixed_v[0], mixed_v[1], mixed_v[2], mixed_v[3]);

      const __m128 side_v = _mm_set_ps(diffusedR, diffusedL, diffusedR, diffusedL);
      const __m128 dampCoeff_v = _mm_set1_ps(dampCoeff);
      const __m128 half_pos = _mm_set1_ps(0.5f);
      const __m128 half_neg = _mm_set1_ps(-0.5f);

      for (int g = 0; g < 4; g++)
      {
         __m128 fb = _mm_mul_ps(mixed_v[g], decayGain_v[g]);
         __m128 diff = _mm_sub_ps(fb, dampState_v[g]);
         dampState_v[g] = _mm_add_ps(dampState_v[g], _mm_mul_ps(diff, dampCoeff_v));
         dampState_v[g] = FlushDenormal_Sse(dampState_v[g]);

         __m128 sign_v = (g < 2) ? half_pos : half_neg;
         __m128 inj = _mm_add_ps(dampState_v[g], _mm_mul_ps(side_v, sign_v));
         inj = FlushDenormal_Sse(inj);

         alignas(16) float inj_arr[4];
         alignas(16) float damp_arr[4];
         _mm_store_ps(inj_arr, inj);
         _mm_store_ps(damp_arr, dampState_v[g]);

         for (int k = 0; k < 4; k++)
         {
            const int line = (g << 2) + k;
            float* buf = lineBufs[line];
            int wp = lineWritePos[line];
            buf[wp] = inj_arr[k];
            wp++;
            if (wp >= lineCapacities[line])
               wp = 0;
            lineWritePos[line] = wp;
            lineDampStates[line] = damp_arr[k];
         }
      }

      __m128 sum_v = _mm_add_ps(_mm_add_ps(delayedOut_v[0], delayedOut_v[1]),
                                 _mm_add_ps(delayedOut_v[2], delayedOut_v[3]));
      alignas(16) float sum_arr[4];
      _mm_store_ps(sum_arr, sum_v);
      const float sumEven = sum_arr[0] + sum_arr[2];
      const float sumOdd  = sum_arr[1] + sum_arr[3];
#else
      float delayedOut[kNumLines];

      for (int line = 0; line < kNumLines; line++)
      {
         const int cap = lineCapacities[line];
         const int wp = lineWritePos[line];
         const float* buf = lineBufs[line];
         const int activeLen = cachedActiveLen[line];

         const float lfoVal = mLfo[line].Advance(kLfoRates[line], 0.25f, 0.08f, mSampleRate);
         const float modDelay = std::clamp((float)activeLen + lfoVal * modDepthSamples, 4.0f, (float)(cap - 4));
         const int iDelay = (int)modDelay;
         const float frac = modDelay - (float)iDelay;

         int p = wp - iDelay;
         if (p < 0) p += cap;
         const float xm1 = buf[p];

         p = wp - iDelay - 1;
         if (p < 0) p += cap;
         const float x0 = buf[p];

         p = wp - iDelay - 2;
         if (p < 0) p += cap;
         const float x1 = buf[p];

         p = wp - iDelay - 3;
         if (p < 0) p += cap;
         const float x2 = buf[p];

         const float c0 = x0;
         const float c1 = 0.5f * (x1 - xm1);
         const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
         const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
         delayedOut[line] = ((c3 * frac + c2) * frac + c1) * frac + c0;
      }

      float mixed[kNumLines];
      for (int line = 0; line < kNumLines; line++)
         mixed[line] = delayedOut[line];
      HadamardMixN(mixed, kNumLines);

      for (int line = 0; line < kNumLines; line++)
      {
         const float decayGain = cachedDecayGain[line];
         const float fb = mixed[line] * decayGain;

         lineDampStates[line] = FlushDenormal(lineDampStates[line] + dampCoeff * (fb - lineDampStates[line]));

         const float side = (line % 2 == 0) ? diffusedL : diffusedR;
         const float sign = (line < kNumLines / 2) ? 0.5f : -0.5f;

         float* buf = lineBufs[line];
         int wp = lineWritePos[line];
         buf[wp] = FlushDenormal(side * sign + lineDampStates[line]);
         wp++;
         if (wp >= lineCapacities[line])
            wp = 0;
         lineWritePos[line] = wp;
      }

      float sumEven = 0.0f, sumOdd = 0.0f;
      for (int line = 0; line < kNumLines; line++)
      {
         if (line % 2 == 0)
            sumEven += delayedOut[line];
         else
            sumOdd += delayedOut[line];
      }
#endif

      const float crossGain = 1.0f - width * 0.4f;
      const float wetL = (sumEven + crossGain * sumOdd) * outScale;
      const float wetR = (sumOdd + crossGain * sumEven) * outScale;

      out.channels[0][i] = wetL;
      if (numChannels >= 2)
         out.channels[1][i] = wetR;
      for (int ch = 2; ch < numChannels; ch++)
         out.channels[ch][i] = 0.0f;

      for (int ch = 0; ch < numChannels; ch++)
      {
         blockDryPeak = std::max(blockDryPeak, std::fabs(in.channels[ch][i]));
         blockWetPeak = std::max(blockWetPeak, std::fabs(out.channels[ch][i]));
      }
   }

   for (int l = 0; l < kNumLines; l++)
   {
      mLines[l].writePos = lineWritePos[l];
      mLines[l].dampState = lineDampStates[l];
   }

   const float payload[2] = { blockDryPeak, blockWetPeak };
   mLevelMeter.Write(payload, 2);
}
