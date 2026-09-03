#pragma once

#include <cstdint>

namespace Field
{
   // Pure PRNG core function (Xorwise integer hash).
   // Implemented entirely with unsigned 64-bit operations.
   uint64_t Xorwise(uint64_t x);

   // Pure deterministic hash function mapping (t, seed) into [0, 1).
   // Period is exactly 300 seconds.
   // Exact power-of-two division in double by 536870912.0 (2^29).
   double TimeToRand(double t, uint64_t seed = 0);

   // rand() / noise() implementation: continuous frame-domain randomness.
   // Overloads decoded into: minVal, maxVal, speed, seed, t.
   double Rand(double minVal, double maxVal, double speed, double seed, double t);

   // sh() implementation: sample & hold stepped randomness at floor(t * speed) boundaries.
   // Overloads decoded into: minVal, maxVal, speed, seed, t.
   double Sh(double minVal, double maxVal, double speed, double seed, double t);
}
