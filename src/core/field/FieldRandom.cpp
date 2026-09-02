#include "FieldRandom.h"

#include <cmath>

namespace Field
{
   // Xorwise integer hash.
   // Implemented entirely in uint64_t unsigned wrapping arithmetic.
   // Unsigned shifts avoid C++17 undefined behavior on negative signed shifts.
   uint64_t Xorwise(uint64_t x)
   {
      uint64_t a = (x << 13) ^ x;
      uint64_t b = (a >> 17) ^ a;
      return (b << 5) ^ b;
   }

   // Deterministic (t, seed) hash.
   // Period: 300 seconds (fmod wraps at 300.0s).
   // Range: [0, 1), never negative and never >= 1.0.
   // Exact double power-of-two division by 536870912.0 (2^29).
   double TimeToRand(double t, uint64_t seed)
   {
      // Handle negative t smoothly by positive wrapping
      double frac = std::fmod(t / 300.0, 1.0);
      if (frac < 0.0)
         frac += 1.0;

      uint64_t timeWord = (uint64_t)(frac * 536870912.0);

      // Two-round mix: Xorwise(Xorwise(timeWord) ^ Xorwise(seed))
      uint64_t hTime = Xorwise(timeWord);
      uint64_t hSeed = Xorwise(seed);
      uint64_t mixed = Xorwise(hTime ^ hSeed);

      // Mask with (2^29 - 1) = 0x1FFFFFFF (equivalent to non-negative modulo 536870912)
      uint64_t m = mixed & 0x1FFFFFFF;

      return (double)m / 536870912.0;
   }

   double Rand(double minVal, double maxVal, double speed, double seed, double t)
   {
      uint64_t seedWord = (uint64_t)(int64_t)seed;
      double n = TimeToRand(t * speed, seedWord);
      return minVal + (maxVal - minVal) * n;
   }

   double Sh(double minVal, double maxVal, double speed, double seed, double t)
   {
      uint64_t seedWord = (uint64_t)(int64_t)seed;
      double stepTime = std::floor(t * speed);
      double frac = TimeToRand(stepTime, seedWord);
      return minVal + (maxVal - minVal) * frac;
   }
}
