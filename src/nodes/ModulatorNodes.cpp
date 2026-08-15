#include "ModulatorNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "Transport.h"

constexpr int PatternNode::kSteps;

namespace
{
   const std::vector<std::string> kLfoShapes = {
      "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold"
   };

   float Frac(double v) { return (float)(v - std::floor(v)); }

   // Deterministic hash so a given step always yields the same value; the pattern
   // repeats identically after a rewind instead of drifting.
   float Hash01(long long n)
   {
      unsigned long long x = (unsigned long long)(n * 2654435761u + 1013904223u);
      x ^= x >> 33;
      x *= 0xff51afd7ed558ccdULL;
      x ^= x >> 33;
      return (float)((x >> 11) & 0xFFFFFF) / (float)0xFFFFFF;
   }

   float Remap(float v01, float low, float high)
   {
      return low + (high - low) * std::min(1.0f, std::max(0.0f, v01));
   }
}

// ---------------------------------------------------------------- LFO

const std::vector<std::string>& LFONode::ShapeNames()
{
   return kLfoShapes;
}

float LFONode::Value01()
{
   const double beats = Transport::Instance().Beats();
   const float rate = std::max(0.01f, rateBeats);
   const double cycles = beats / rate;
   const float t = Frac(cycles + phase);

   float raw = 0.0f;
   switch (shape)
   {
      case 0: raw = 0.5f + 0.5f * std::sin(t * 6.28318530718f); break;      // Sine
      case 1: raw = t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f; break;           // Triangle
      case 2: raw = t; break;                                               // Saw Up
      case 3: raw = 1.0f - t; break;                                        // Saw Down
      case 4: raw = t < 0.5f ? 0.0f : 1.0f; break;                          // Square
      default: raw = Hash01((long long)std::floor(cycles + phase)); break;  // S&H
   }

   return Remap(raw, low, high);
}

// ---------------------------------------------------------------- Random

float RandomNode::ValueForStep(long long step) const
{
   // The seed is mixed into the hash input rather than added to the result, so
   // two seeds give genuinely different sequences instead of the same sequence
   // offset by a constant.
   const long long salt = (long long)(seed * 1013.0f);
   return Hash01(step * 6364136223LL + salt * 1442695040888963407LL);
}

float RandomNode::Value01()
{
   const double beats = Transport::Instance().Beats();
   const float rate = std::max(0.01f, rateBeats);
   const double pos = beats / rate;
   const long long step = (long long)std::floor(pos);
   const float t = Frac(pos);

   float current = ValueForStep(step);
   float next = ValueForStep(step + 1);

   float raw = current;
   if (smooth > 0.0f)
   {
      // ease only across the tail of the step, scaled by the smooth amount
      float k = std::min(1.0f, t / std::max(0.0001f, smooth));
      float eased = k * k * (3.0f - 2.0f * k);
      raw = current + (next - current) * eased;
   }

   return Remap(raw, low, high);
}

// ---------------------------------------------------------------- Pattern

float PatternNode::Value01()
{
   const int count = std::max(1, std::min(length, kSteps));
   const double beats = Transport::Instance().Beats();
   const float rate = std::max(0.01f, stepBeats);
   const double pos = beats / rate;
   const long long stepIndex = (long long)std::floor(pos);

   const int i = (int)(((stepIndex % count) + count) % count);
   mCurrentStep = i;

   float raw = steps[i];
   if (smoothSteps)
   {
      const int nextI = (i + 1) % count;
      const float t = Frac(pos);
      const float eased = t * t * (3.0f - 2.0f * t);
      raw = steps[i] + (steps[nextI] - steps[i]) * eased;
   }

   return Remap(raw, low, high);
}

// ---------------------------------------------------------------- Math

namespace
{
   const std::vector<std::string> kMathOps = {
      "A + B", "A - B", "A * B", "A / B", "min(A,B)", "max(A,B)",
      "average", "difference", "A only", "B only"
   };
}

const std::vector<std::string>& MathNode::OpNames()
{
   return kMathOps;
}

float MathNode::Value01()
{
   const float a = inputA ? inputA->Value01() : constantA;
   const float b = inputB ? inputB->Value01() : constantB;

   float r;
   switch (op)
   {
      case 0: r = a + b; break;
      case 1: r = a - b; break;
      case 2: r = a * b; break;
      case 3: r = a / std::max(b, 1e-4f); break;
      case 4: r = std::min(a, b); break;
      case 5: r = std::max(a, b); break;
      case 6: r = (a + b) * 0.5f; break;
      case 7: r = std::fabs(a - b); break;
      case 8: r = a; break;
      default: r = b; break;
   }

   r = r * gain + offset;
   return clampOutput ? std::min(1.0f, std::max(0.0f, r)) : r;
}

// ---------------------------------------------------------------- Compare

namespace
{
   const std::vector<std::string> kCompareOps = {
      "A > B", "A >= B", "A < B", "A <= B", "A == B", "A != B"
   };
}

const std::vector<std::string>& CompareNode::OpNames()
{
   return kCompareOps;
}

float CompareNode::Value01()
{
   const float a = inputA ? inputA->Value01() : constantA;
   const float b = inputB ? inputB->Value01() : constantB;
   const float tol = std::max(0.0f, tolerance);

   bool result;
   switch (op)
   {
      case 0: result = a > b; break;
      case 1: result = a >= b - tol; break;
      case 2: result = a < b; break;
      case 3: result = a <= b + tol; break;
      case 4: result = std::fabs(a - b) <= tol; break;
      default: result = std::fabs(a - b) > tol; break; // A != B
   }

   return result ? 1.0f : 0.0f;
}

// ---------------------------------------------------------------- Range to Range

float RangeToRangeNode::Value01()
{
   const float v = input ? input->Value01() : constantIn;
   const float span = inHigh - inLow;
   const float t = std::fabs(span) > 1e-6f ? (v - inLow) / span : 0.0f;
   const float r = outLow + (outHigh - outLow) * t;
   if (!clampOutput) return r;
   return std::min(std::max(outLow, outHigh), std::max(std::min(outLow, outHigh), r));
}

// ---------------------------------------------------------------- Smooth

float SmoothNode::Value01()
{
   const float target = input ? input->Value01() : constantIn;
   const double beats = Transport::Instance().Beats();
   if (mLast >= 0.0f && beats == mLastBeats)
      return mLast; // already advanced this tick - don't double-apply the filter

   const float k = std::min(1.0f, std::max(0.0f, amount));
   mLast = (mLast < 0.0f) ? target : mLast + (target - mLast) * (1.0f - k);
   mLastBeats = beats;
   return mLast;
}

// ---------------------------------------------------------------- Mod Depth

float ModDepthNode::Value01()
{
   const float v = input ? input->Value01() : constantIn;
   return std::clamp(0.5f + (v - 0.5f) * depth, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- Invert

float InvertNode::Value01()
{
   const float v = input ? input->Value01() : constantIn;
   return low + high - v;
}
