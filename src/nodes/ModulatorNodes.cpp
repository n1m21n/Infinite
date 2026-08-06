#include "ModulatorNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "Transport.h"

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
   return Hash01(step);
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

void PatternNode::Reparse()
{
   mSteps.clear();
   std::string cleaned = text;
   for (char& c : cleaned)
   {
      if (c == ',' || c == ';' || c == '\n' || c == '\t')
         c = ' ';
   }

   std::istringstream stream(cleaned);
   std::string token;
   while (stream >> token)
   {
      try
      {
         mSteps.push_back(std::stof(token));
      }
      catch (...)
      {
         // ignore anything that isn't a number rather than clearing the pattern
      }
   }
}

float PatternNode::Value01()
{
   if (mSteps.empty())
      Reparse();
   if (mSteps.empty())
      return low;

   const double beats = Transport::Instance().Beats();
   const float rate = std::max(0.01f, stepBeats);
   const double pos = beats / rate;
   const long long stepIndex = (long long)std::floor(pos);
   const int count = (int)mSteps.size();

   int i = (int)(((stepIndex % count) + count) % count);
   mCurrentStep = i;

   float raw = mSteps[i];
   if (smoothSteps)
   {
      int nextI = (i + 1) % count;
      float t = Frac(pos);
      float eased = t * t * (3.0f - 2.0f * t);
      raw = mSteps[i] + (mSteps[nextI] - mSteps[i]) * eased;
   }

   return Remap(raw, low, high);
}
