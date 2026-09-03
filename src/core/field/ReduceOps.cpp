#include "ReduceOps.h"
#include <cmath>
#include <algorithm>
#include <cfloat>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Field
{
   ReduceOpKind ParseReduceOpKind(const std::string& opName)
   {
      if (opName == "sum") return ReduceOpKind::Sum;
      if (opName == "rms") return ReduceOpKind::Rms;
      if (opName == "min") return ReduceOpKind::Min;
      if (opName == "max") return ReduceOpKind::Max;
      if (opName == "mean") return ReduceOpKind::Mean;
      return ReduceOpKind::None;
   }

   const char* ReduceOpKindToString(ReduceOpKind op)
   {
      switch (op)
      {
         case ReduceOpKind::Sum: return "sum";
         case ReduceOpKind::Rms: return "rms";
         case ReduceOpKind::RmsBandLimited: return "rms";
         case ReduceOpKind::Min: return "min";
         case ReduceOpKind::Max: return "max";
         case ReduceOpKind::Mean: return "mean";
         case ReduceOpKind::None: return "none";
      }
      return "none";
   }

   double ReduceSum(const float* data, size_t count)
   {
      if (!data || count == 0) return 0.0;
      double sum = 0.0;
      for (size_t i = 0; i < count; ++i)
      {
         sum += (double)data[i];
      }
      return sum;
   }

   double ReduceMean(const float* data, size_t count)
   {
      if (!data || count == 0) return 0.0;
      return ReduceSum(data, count) / (double)count;
   }

   double ReduceRms(const float* data, size_t count)
   {
      if (!data || count == 0) return 0.0;
      double sumSq = 0.0;
      for (size_t i = 0; i < count; ++i)
      {
         double v = (double)data[i];
         sumSq += v * v;
      }
      return std::sqrt(sumSq / (double)count);
   }

   double ReduceRmsBandLimited(const float* data, size_t count, double loHz, double hiHz, double sampleRate)
   {
      if (!data || count == 0) return 0.0;
      if (sampleRate <= 0.0) sampleRate = 48000.0;

      // Bandpass filter via cascade of high-pass (at loHz) and low-pass (at hiHz)
      // Clamp frequencies to Nyquist
      double nyquist = sampleRate * 0.499;
      loHz = std::max(1.0, std::min(loHz, nyquist));
      hiHz = std::max(loHz, std::min(hiHz, nyquist));

      // 1-pole high-pass coefficient: alphaHp = 1 / (1 + 2*pi*fc/sr)
      double wHp = 2.0 * M_PI * loHz / sampleRate;
      double aHp = 1.0 / (1.0 + wHp);

      // 1-pole low-pass coefficient: alphaLp = 2*pi*fc / (sr + 2*pi*fc)
      double wLp = 2.0 * M_PI * hiHz / sampleRate;
      double aLp = wLp / (1.0 + wLp);

      double hpPrevIn = 0.0;
      double hpPrevOut = 0.0;
      double lpPrevOut = 0.0;
      double sumSq = 0.0;

      for (size_t i = 0; i < count; ++i)
      {
         double inVal = (double)data[i];

         // High-pass filter: y[n] = a * (y[n-1] + x[n] - x[n-1])
         double hpOut = aHp * (hpPrevOut + inVal - hpPrevIn);
         hpPrevIn = inVal;
         hpPrevOut = hpOut;

         // Low-pass filter: y[n] = y[n-1] + a * (x[n] - y[n-1])
         double lpOut = lpPrevOut + aLp * (hpOut - lpPrevOut);
         lpPrevOut = lpOut;

         sumSq += lpOut * lpOut;
      }

      return std::sqrt(sumSq / (double)count);
   }

   double ReduceMin(const float* data, size_t count)
   {
      if (!data || count == 0) return 0.0;
      double minVal = (double)data[0];
      for (size_t i = 1; i < count; ++i)
      {
         double v = (double)data[i];
         if (v < minVal) minVal = v;
      }
      return minVal;
   }

   double ReduceMax(const float* data, size_t count)
   {
      if (!data || count == 0) return 0.0;
      double maxVal = (double)data[0];
      for (size_t i = 1; i < count; ++i)
      {
         double v = (double)data[i];
         if (v > maxVal) maxVal = v;
      }
      return maxVal;
   }

   void ReduceLanes(ReduceOpKind op,
                   const float* const* lanePointers,
                   int numLanes,
                   size_t count,
                   double* outValues,
                   double loHz,
                   double hiHz,
                   double sampleRate)
   {
      if (!lanePointers || !outValues || numLanes <= 0) return;

      for (int l = 0; l < numLanes; ++l)
      {
         const float* data = lanePointers[l];
         switch (op)
         {
            case ReduceOpKind::Sum:
               outValues[l] = ReduceSum(data, count);
               break;
            case ReduceOpKind::Mean:
               outValues[l] = ReduceMean(data, count);
               break;
            case ReduceOpKind::Rms:
               outValues[l] = ReduceRms(data, count);
               break;
            case ReduceOpKind::RmsBandLimited:
               outValues[l] = ReduceRmsBandLimited(data, count, loHz, hiHz, sampleRate);
               break;
            case ReduceOpKind::Min:
               outValues[l] = ReduceMin(data, count);
               break;
            case ReduceOpKind::Max:
               outValues[l] = ReduceMax(data, count);
               break;
            case ReduceOpKind::None:
               outValues[l] = 0.0;
               break;
         }
      }
   }
}
