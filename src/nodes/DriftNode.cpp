#include "DriftNode.h"

#include <algorithm>
#include <cmath>

#include "audio/MusicTime.h"
#include "core/Transport.h"

float DriftNode::Gauss()
{
   // xorshift64* -> Box-Muller
   auto next = [this]() {
      mRng ^= mRng >> 12;
      mRng ^= mRng << 25;
      mRng ^= mRng >> 27;
      return (double)((mRng * 2685821657736338717ull) >> 11) * (1.0 / 9007199254740992.0);
   };
   const double u1 = std::max(1e-12, next());
   const double u2 = next();
   return (float)(std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2));
}

void DriftNode::Tick(double dt)
{
   if (mSeededWith != seed)
   {
      mSeededWith = seed;
      mRng = 0x9E3779B97F4A7C15ull ^ ((uint64_t)(uint32_t)seed * 0xD1B54A32D192ED03ull);
      if (mRng == 0)
         mRng = 1;
   }
   dt = std::clamp(dt, 0.0, 0.1); // a stalled frame must not fling the walk
   const float fdt = (float)dt;

   float lo = std::clamp(rangeLo, 0.0f, 1.0f), hi = std::clamp(rangeHi, 0.0f, 1.0f);
   if (lo > hi)
      std::swap(lo, hi);
   if (hi - lo < 1e-4f)
   {
      mX = lo;
      mV = 0.0f;
   }
   else
   {
      const float h = std::clamp(home, lo, hi);
      const float m = std::clamp(momentum, 0.0f, 4.0f);
      mV = m > 1e-4f ? mV * std::exp(-fdt / m) : 0.0f;
      const float theta = 0.5f;
      const float k = 0.02f; // stationary variance of the well at stray 1 (sd ~0.14)
      const float spd = std::clamp(speed, 0.05f, 8.0f);
      const float T = std::clamp(stray, 0.1f, 8.0f);
      const float pull = std::clamp(-spd * theta * (mX - h) * fdt, -0.05f, 0.05f);
      const float noise = std::sqrt(2.0f * spd * theta * k * T * fdt) * Gauss();
      // momentum: part of each random kick becomes velocity that carries on
      if (m > 1e-4f && fdt > 0.0f)
         mV += 0.5f * noise / std::max(fdt, 1e-3f) * (1.0f - std::exp(-fdt / m));
      mX += mV * fdt + pull + noise;
      for (int i = 0; i < 4 && (mX < lo || mX > hi); i++)
      {
         if (mX < lo) { mX = 2.0f * lo - mX; mV = -mV; }
         if (mX > hi) { mX = 2.0f * hi - mX; mV = -mV; }
      }
      mX = std::clamp(mX, lo, hi);
   }

   float target = mX;
   if (quantizeRate > 0 && quantizeRate <= MusicTime::kNumRateDivisions)
   {
      const double grid = MusicTime::BeatsFor((MusicTime::RateDivision)(quantizeRate - 1));
      if (grid > 1e-6)
      {
         const long long idx = (long long)std::floor(Transport::Instance().Beats() / grid);
         if (idx != mHeldIdx)
         {
            mHeld = mX;
            mHeldIdx = idx;
         }
         target = mHeld;
      }
   }
   const float s = std::clamp(smoothness, 0.0f, 0.95f);
   mSmoothed = mSmoothed * s + target * (1.0f - s);
}

void DriftNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   const auto now = std::chrono::steady_clock::now();
   const double dt = mHaveTime ? std::chrono::duration<double>(now - mLastTime).count() : 0.0;
   mLastTime = now;
   mHaveTime = true;
   Tick(dt);
}

float DriftNode::Value01()
{
   // Advance by wall time even when nothing cooks this node (a modulator is
   // pulled by the apply loop, not by the render graph).
   const auto now = std::chrono::steady_clock::now();
   const double dt = mHaveTime ? std::chrono::duration<double>(now - mLastTime).count() : 0.0;
   if (!mHaveTime || dt >= 0.004)
   {
      mLastTime = now;
      mHaveTime = true;
      Tick(dt);
   }
   const float d = std::clamp(depth, 0.0f, 1.0f);
   return std::clamp(0.5f + (mSmoothed - 0.5f) * d, 0.0f, 1.0f);
}
