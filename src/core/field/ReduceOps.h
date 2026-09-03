#pragma once

#include <cstddef>
#include <vector>
#include <string>

namespace Field
{
   enum class ReduceOpKind
   {
      None,
      Sum,
      Rms,
      RmsBandLimited,
      Min,
      Max,
      Mean
   };

   ReduceOpKind ParseReduceOpKind(const std::string& opName);
   const char* ReduceOpKindToString(ReduceOpKind op);

   // Shared reduction kernels across all backends.
   // Real-time safe: no dynamic heap allocation, pure arithmetic over contiguous arrays.
   double ReduceSum(const float* data, size_t count);
   double ReduceMean(const float* data, size_t count);
   double ReduceRms(const float* data, size_t count);
   double ReduceRmsBandLimited(const float* data, size_t count, double loHz, double hiHz, double sampleRate);
   double ReduceMin(const float* data, size_t count);
   double ReduceMax(const float* data, size_t count);

   // Helper for multi-lane reduction
   void ReduceLanes(ReduceOpKind op,
                   const float* const* lanePointers,
                   int numLanes,
                   size_t count,
                   double* outValues,
                   double loHz = 0.0,
                   double hiHz = 0.0,
                   double sampleRate = 48000.0);
}
